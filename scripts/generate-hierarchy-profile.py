"""Pin manually reviewed interface entries to the exact local EXE; no process access."""
import hashlib
import runpy
import sys
from pathlib import Path

Image = runpy.run_path(str(Path(__file__).with_name('inspect-gi-functions.py')))['Image']
image = Image(sys.argv[1])
fingerprint = hashlib.sha256(image.data).hexdigest().upper()
expected = '7F89938DA606C1281659607D464702CDDB9A09A7D4320A196630F60A811EC38E'
if fingerprint != expected:
    raise SystemExit('Unreviewed executable: refuse to generate hierarchy profile')
# These are reviewed native wrappers, not offsets inferred solely from adjacency.
entries = [('NameA', 0x14389f0), ('NameB', 0x1438b70),
           ('ChildCount', 0x1437d10), ('GetChild', 0x1438070),
           ('GetParent', 0x14374b0), ('GameObjectTransform', 0x1452440),
           ('ComponentGameObject', 0x1453c60), ('GetActive', 0x1452eb0),
           ('RotationWorldCandidate', 0x19cb66a0), ('RotationLocalCandidate', 0x19cb6840),
           ('SetLocalRotation', 0x14371d0), ('SetLocalRotationNative', 0x15c57e0),
           ('LocalRotationStore', 0x11f9830),
           ('TimeScaleGet', 0x1436bd0), ('TimeScaleSet', 0x1436bf0),
           ('TimeScaleSetNative', 0x16f3270), ('TimeManagerGet', 0x16f2660),
           ('ComponentGetByName', 0x1453dd0), ('ComponentGetByNameNative', 0x14c8660),
           ('AnimatorClassRegistration', 0x1b0a401), ('AnimatorClassCacheStore', 0x1b0a435),
           ('BehaviourClassRegistration', 0x1c0e937), ('BehaviourClassCacheStore', 0x1c0e97b),
           ('ClassParentGet', 0x4b2330), ('MethodNameGet', 0x4b2be0),
           ('MethodNameInitialize', 0x5291b0), ('ClassMethodSearch', 0x511b50),
           ('ClassMethodSearchCompare', 0x511c32), ('ClassMethodSearchParent', 0x511ca0),
           ('BehaviourEnabledGet', 0x19c8cca0), ('BehaviourEnabledGetNative', 0x10dee70),
           ('BehaviourEnabledSet', 0x19c8ccb0), ('BehaviourEnabledSetNative', 0x10deeb0),
           ('AnimatorUpdateModeGet', 0x19c86940), ('AnimatorUpdateModeGetNative', 0x11a18f0),
           ('AnimatorIsHumanGet', 0x19c86230), ('AnimatorIsHumanGetNative', 0x11a13f0),
           ('AnimatorAvatarGet', 0x19c888b0), ('AnimatorAvatarGetNative', 0x11a3ff0),
           ('RendererBonesGet', 0x19c9a4e0), ('RendererBonesGetNative', 0x144c1c0),
           ('RendererMeshGet', 0x19c9a560), ('RendererMeshGetNative', 0x982130),
           ('MeshBindPosesGet', 0x19ca0260), ('MeshBindPosesGetNative', 0x97c2f0),
           ('MeshBindPosesArrayCopy', 0x1724230),
           ('AvatarBoneIndexGet', 0x19c89470), ('AvatarBoneIndexGetNative', 0x11a5b60),
           ('AvatarBoneIndexLookup', 0x11f9f50), ('AvatarDefaultPoseStorage', 0x11f9fd0),
           ('AvatarDefaultPositionWalk', 0x11fa040)]
lines = ['#pragma once', '#include <cstdint>', '// Generated from locally reviewed, build-pinned interface entries.',
         '// See docs/HIERARCHY_OBSERVER_RESULTS.md for semantic evidence and limitations.',
         'namespace gimmd::hierarchy_profile {', 'struct Entry { const char* name; uint32_t rva; uint8_t bytes[32]; };',
         f'inline constexpr char BuildSha256[] = "{fingerprint}";', 'inline constexpr Entry Entries[] = {']
for name, rva in entries:
    code = image.read(rva, 32)
    if len(code) != 32 or not image.executable(rva):
        raise SystemExit(f'Invalid entry: {name}')
    lines.append('    {"' + name + '", ' + hex(rva) + ', {' + ','.join(map(str, code)) + '}},')
lines += ['};', '}']
Path(sys.argv[2]).write_text('\n'.join(lines) + '\n', encoding='utf-8')
