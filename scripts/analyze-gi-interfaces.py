"""Read-only PE/call-site analysis. Requires capstone; never executes game code."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
import capstone


def inspect(executable, report, output):
    image = Path(executable).read_bytes()
    pe = struct.unpack_from('<I', image, 0x3c)[0]
    count, optional = struct.unpack_from('<H', image, pe + 6)[0], struct.unpack_from('<H', image, pe + 20)[0]
    sections = []
    for i in range(count):
        pos = pe + 24 + optional + i * 40
        vsize, rva, size, offset = struct.unpack_from('<IIII', image, pos + 8)
        sections.append((rva, vsize, offset, size))

    def data_at(rva, size=96):
        for base, _, offset, raw_size in sections:
            if base <= rva < base + raw_size:
                available = min(size, base + raw_size - rva)
                return image[offset + rva - base:offset + rva - base + available]
        return b''

    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    findings = []
    source = json.loads(Path(report).read_text(encoding='utf-8-sig'))
    for entry in source['patterns']:
        for candidate in entry['candidates']:
            rva = int(candidate['rva'], 16)
            code = data_at(rva, 192)
            instructions = []
            for instruction in decoder.disasm(code, rva):
                instructions.append(f'{instruction.address:#x}: {instruction.mnemonic} {instruction.op_str}')
                if len(instructions) == 24 or instruction.mnemonic == 'ret':
                    break
            jump_chain = []
            address = rva
            visited = set()
            for _ in range(8):
                if address in visited:
                    break
                visited.add(address)
                chunk = data_at(address, 5)
                if len(chunk) < 5 or chunk[0] != 0xe9:
                    break
                target = address + 5 + struct.unpack_from('<i', chunk, 1)[0]
                jump_chain.append({'from': hex(address), 'to': hex(target)})
                address = target
            leaf = []
            if address != rva:
                for instruction in decoder.disasm(data_at(address), address):
                    leaf.append(f'{instruction.address:#x}: {instruction.mnemonic} {instruction.op_str}')
                    if len(leaf) == 16 or instruction.mnemonic == 'ret':
                        break
            findings.append({'name': entry['name'], 'rva': hex(rva),
                             'instructions': instructions, 'jumpChain': jump_chain, 'leafInstructions': leaf})
    result = {'mode': 'offline-disassembly-only', 'sha256': hashlib.sha256(image).hexdigest().upper(),
              'note': 'Function identity and ABI remain hypotheses until runtime observation.', 'interfaces': findings}
    Path(output).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('report')
    parser.add_argument('output')
    args = parser.parse_args()
    inspect(args.executable, args.report, args.output)
