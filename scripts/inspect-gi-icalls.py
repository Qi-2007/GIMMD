"""Find named Unity internal-call registration evidence in a local PE; read-only."""
import argparse
import json
import re
import runpy
import struct
from pathlib import Path

import capstone
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

Image = runpy.run_path(str(Path(__file__).with_name('inspect-gi-functions.py')))['Image']
NAMES = [
    'UnityEngine.Time::get_timeScale', 'UnityEngine.Time::set_timeScale',
    'UnityEngine.Time::get_unscaledTime', 'UnityEngine.Time::get_frameCount',
    'UnityEngine.Behaviour::get_enabled', 'UnityEngine.Behaviour::set_enabled',
    'UnityEngine.Animator::get_speed', 'UnityEngine.Animator::set_speed',
    'UnityEngine.Animator::GetBoneTransformInternal',
    'UnityEngine.GameObject::GetComponentByName',
    'UnityEngine.GameObject::GetComponentsInternal',
    'UnityEngine.Component::GetComponentFastPath',
]


def inspect(executable, extra_names=()):
    image = Image(executable)
    strings = {}
    for name in NAMES + list(extra_names):
        locations = []
        offset = image.data.find(name.encode('ascii') + b'\0')
        while offset >= 0:
            for rva, raw, length, _ in image.sections:
                if raw <= offset < raw + length:
                    locations.append(rva + offset - raw)
            offset = image.data.find(name.encode('ascii') + b'\0', offset + 1)
        for rva in locations:
            strings[rva] = name
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    decoder.detail = True
    references = []
    for base, raw, length, flags in image.sections:
        if not flags & 0x20000000:
            continue
        section = image.data[raw:raw + length]
        # RIP-relative LEA candidates; each match is independently decoded.
        for match in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d].{4}', section, re.S):
            offset = match.start()
            address = base + offset
            target = address + 7 + struct.unpack_from('<i', section, offset + 3)[0]
            if target not in strings:
                continue
            instruction = next(decoder.disasm(section[offset:offset + 7], address), None)
            if not instruction or not any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in instruction.operands):
                continue
            after = []
            for insn in decoder.disasm(section[offset:offset + 55], address):
                entry = {'rva': hex(insn.address), 'text': f'{insn.mnemonic} {insn.op_str}'}
                targets = [insn.address + insn.size + op.mem.disp for op in insn.operands
                           if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP]
                if targets:
                    entry['ripTargets'] = [hex(value) for value in targets]
                after.append(entry)
            references.append({'name': strings[target], 'stringRva': hex(target), 'codeRva': hex(address),
                               'followingInstructions': after})
    return {'mode': 'offline-named-icall-research',
            'note': 'Registration and instruction boundaries require review; candidates are not callable profiles.',
            'strings': [{'name': name, 'rva': hex(rva)} for rva, name in strings.items()], 'references': references}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('output', type=Path)
    parser.add_argument('--string', action='append', default=[])
    args = parser.parse_args()
    result = inspect(args.executable, args.string)
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(f'{len(result["strings"])} names, {len(result["references"])} RIP-relative references')
