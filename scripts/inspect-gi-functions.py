"""Bounded offline x64 disassembly of explicit RVAs; never accesses a process."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import capstone
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP


class Image:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        if self.data[:2] != b'MZ':
            raise ValueError('Not a PE image')
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        if self.data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('Invalid PE header')
        machine, count = struct.unpack_from('<HH', self.data, pe + 4)
        optional = struct.unpack_from('<H', self.data, pe + 20)[0]
        if machine != 0x8664 or struct.unpack_from('<H', self.data, pe + 24)[0] != 0x20b:
            raise ValueError('Expected PE32+ x64')
        self.sections = []
        for index in range(count):
            offset = pe + 24 + optional + index * 40
            _, rva, size, raw = struct.unpack_from('<IIII', self.data, offset + 8)
            flags = struct.unpack_from('<I', self.data, offset + 36)[0]
            if raw + size > len(self.data):
                raise ValueError('Section extends beyond file')
            self.sections.append((rva, raw, size, flags))
        self.functions = []
        exception_rva, exception_size = struct.unpack_from('<II', self.data, pe + 24 + 112 + 3 * 8)
        table = self.read(exception_rva, exception_size)
        for offset in range(0, len(table) - 11, 12):
            begin, end, unwind = struct.unpack_from('<III', table, offset)
            if begin < end:
                self.functions.append((begin, end, unwind))

    def read(self, rva, size):
        for base, raw, length, _ in self.sections:
            if base <= rva < base + length:
                available = min(size, base + length - rva)
                return self.data[raw + rva - base:raw + rva - base + available]
        return b''

    def executable(self, rva):
        return any(base <= rva < base + size and flags & 0x20000000
                   for base, _, size, flags in self.sections)


def inspect(path, rvas, limit, ranges=(), callers=(), linear=False):
    image = Image(path)
    for begin, end in ranges:
        rvas += [address for address, _, _ in image.functions if begin <= address < end]
    rvas = sorted(set(rvas))
    call_sites = []
    for target in callers:
        for base, raw, length, flags in image.sections:
            if not flags & 0x20000000:
                continue
            section = image.data[raw:raw + length]
            for opcode in (b'\xe8', b'\xe9'):
                offset = section.find(opcode)
                while 0 <= offset <= length - 5:
                    address = base + offset
                    destination = address + 5 + struct.unpack_from('<i', section, offset + 1)[0]
                    if destination == target:
                        owner = next((item for item in image.functions if item[0] <= address < item[1]), None)
                        call_sites.append({'rva': hex(address), 'target': hex(target),
                                           'unwindBegin': hex(owner[0]) if owner else None,
                                           'note': 'Byte candidate; instruction alignment not yet proven'})
                    offset = section.find(opcode, offset + 1)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    decoder.detail = True
    output = []
    for rva in rvas:
        if not image.executable(rva):
            raise ValueError(f'RVA {rva:#x} is not executable file data')
        owner = next((item for item in image.functions if item[0] <= rva < item[1]), None)
        # A Windows unwind entry may cover only one fragment of a function.
        # Explicit linear mode is bounded by --bytes, not a function boundary.
        size = min(limit, owner[1] - rva) if owner and not linear else limit
        instructions = []
        for instruction in decoder.disasm(image.read(rva, size), rva):
            entry = {'rva': hex(instruction.address), 'text': f'{instruction.mnemonic} {instruction.op_str}'}
            if instruction.mnemonic in ('call', 'jmp') and instruction.operands[0].type == X86_OP_IMM:
                entry['branchTarget'] = hex(instruction.operands[0].imm)
            references = []
            for operand in instruction.operands:
                if operand.type == X86_OP_MEM and operand.mem.base == X86_REG_RIP:
                    address = instruction.address + instruction.size + operand.mem.disp
                    chunk = image.read(address, 96).split(b'\0', 1)[0]
                    reference = {'rva': hex(address)}
                    if len(chunk) >= 4 and all(32 <= value < 127 for value in chunk):
                        reference['asciiPrefix'] = chunk.decode('ascii')
                    references.append(reference)
            if references:
                entry['ripReferences'] = references
            instructions.append(entry)
        output.append({'rva': hex(rva), 'unwindRange': [hex(owner[0]), hex(owner[1])] if owner else None,
                       'instructions': instructions})
    return {'mode': 'offline-explicit-rva-disassembly', 'sha256': hashlib.sha256(image.data).hexdigest().upper(),
            'linearWindow': linear,
            'note': 'Instruction boundaries and semantics require review; linear windows may include adjacent functions; this output does not authorize engine calls.',
            'functions': output, 'directBranchCandidates': call_sites}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('output')
    parser.add_argument('--rva', type=lambda value: int(value, 0), action='append', default=[])
    parser.add_argument('--range', type=lambda value: tuple(int(part, 0) for part in value.split(':')),
                        action='append', default=[])
    parser.add_argument('--callers', type=lambda value: int(value, 0), action='append', default=[])
    parser.add_argument('--bytes', type=int, default=512)
    parser.add_argument('--linear', action='store_true', help='Decode a bounded window across unwind fragments; may include adjacent functions')
    args = parser.parse_args()
    if not 1 <= args.bytes <= 65536:
        parser.error('--bytes must be 1..65536')
    if not args.rva and not args.range and not args.callers:
        parser.error('Specify --rva, --range or --callers')
    if any(len(span) != 2 or not 0 <= span[0] < span[1] <= span[0] + 65536 for span in args.range):
        parser.error('Each range must be ascending and at most 64 KiB')
    result = inspect(args.executable, args.rva, args.bytes, args.range, args.callers, args.linear)
    Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
