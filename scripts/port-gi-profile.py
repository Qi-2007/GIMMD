"""Offline relocation audit. Ambiguous signatures are never promoted automatically."""
import argparse
import hashlib
import json
import re
import runpy
from pathlib import Path

import capstone
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

Image = runpy.run_path(str(Path(__file__).with_name('inspect-gi-functions.py')))['Image']


def signature(code):
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    decoder.detail = True
    mask = [True] * len(code)
    end = 0
    for ins in decoder.disasm(code, 0):
        end = ins.address + ins.size
        ranges = []
        if ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP):
            ranges.append((ins.imm_offset, ins.imm_size))
        if any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in ins.operands):
            ranges.append((ins.disp_offset, ins.disp_size))
        for offset, size in ranges:
            for i in range(ins.address + offset, ins.address + offset + size):
                mask[i] = False
    if sum(mask[:end]) < 8:
        return None
    return re.compile(b''.join(re.escape(bytes([b])) if fixed else b'.'
                               for b, fixed in zip(code[:end], mask[:end])), re.S)


def audit(executable, dump, profile):
    image = Image(executable)
    sections = [(base, image.data[raw:raw + size]) for base, raw, size, flags in image.sections if flags & 0x20000000]
    methods = {}
    namespace = owner = ''
    rva = None
    for line in Path(dump).read_text(encoding='utf-8-sig').splitlines():
        if line.startswith('// Namespace:'):
            namespace = line.partition(':')[2].strip()
        elif re.search(r'\b(class|struct)\s+', line) and not line.startswith('\t'):
            owner = line.strip()
        elif '// RVA: 0x' in line:
            rva = int(re.search(r'RVA: (0x[\da-fA-F]+)', line)[1], 16)
        elif rva is not None and '(' in line and not line.lstrip().startswith('//'):
            methods.setdefault(rva, []).append(f'{namespace} | {owner} | {line.strip()}')
            rva = None
    entries = []
    source = Path(profile).read_text(encoding='utf-8')
    for name, old, values in re.findall(r'\{"([^"]+)", (0x[\da-fA-F]+), \{([\d,]+)\}\}', source):
        code = bytes(map(int, values.split(',')))
        pattern = signature(code)
        hits = []
        if pattern:
            for base, data in sections:
                for match in pattern.finditer(data):
                    hits.append(base + match.start())
                    if len(hits) > 64:
                        break
                if len(hits) > 64:
                    break
        entries.append(dict(name=name, oldRva=old, candidates=[dict(rva=hex(hit), methods=methods.get(hit, [])) for hit in hits]))
    return dict(executableSha256=hashlib.sha256(image.data).hexdigest().upper(),
                dumpSha256=hashlib.sha256(Path(dump).read_bytes()).hexdigest().upper(),
                status='offline-candidates-require-review', entries=entries)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('dump')
    parser.add_argument('profile')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = audit(args.executable, args.dump, args.profile)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    for entry in result['entries']:
        print(entry['name'], len(entry['candidates']), ', '.join(c['rva'] for c in entry['candidates'][:4]))
