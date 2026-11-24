"""Pin reviewed CN 7.1 clone/model cleanup wrappers; no process access."""
import hashlib
import runpy
import sys
from pathlib import Path

Image = runpy.run_path(str(Path(__file__).with_name('inspect-gi-functions.py')))['Image']
image = Image(sys.argv[1])
sha = hashlib.sha256(image.data).hexdigest().upper()
if sha != '7F89938DA606C1281659607D464702CDDB9A09A7D4320A196630F60A811EC38E':
    raise SystemExit('Unreviewed executable')
entries = [('MaterialShaderGet',0x143d9b0), ('MaterialShaderGetNative',0x163dcc0), ('GamePackageRetain',0x6e36b60), ('GamePackageRelease',0x6e372b0), ('GameDependencyList',0x6f36810), ('GameDependencyListReturn',0x6f368e0), ('GameDependencyExpand',0x7011930), ('GameDependencyRecords',0x710e250), ('GameEnsurePackage',0x7015f30), ('GameBundleLookup',0x6e34e30),
           ('GameResourceRoot',0x6de1ae0), ('GameProxyForward',0x7021590),
           ('GameResourceHashSplit',0x6e37aa0), ('BundleLoadFile', 0x19cc6630), ('BundleLoadBridge', 0x145b450),
           ('BundleLoadNative', 0x19d8ba0), ('BundleLoadMHY', 0x19f3dd0), ('LoadInstanceCache', 0x19cc6800), ('LoadInstanceCacheBridge', 0x145b7c0),
           ('LoadInstanceCacheNative', 0x19d95d0), ('LoadTemplate', 0x19ca1ef0),
           ('LoadTemplateBridge', 0x145b320), ('LoadTemplateNative', 0x19d9660), ('SharedMaterialsGet', 0x19cd29e0), ('SharedMaterialsGetBridge', 0x982be0),
           ('SharedMaterialsSet', 0x19cd29f0), ('SharedMaterialsSetBridge', 0x982c70), ('CloneWithParent', 0x19cb2d00), ('CloneBridge', 0x1438690),
           ('CloneNative', 0x18238a0), ('DestroyImmediate', 0x19cb2dd0),
           ('DestroyBridge', 0x14388a0), ('DestroyNative', 0x14c8470),
           ('SetActive', 0x19c9b800), ('SetActiveBridge', 0x1452ee0),
           ('SetParent', 0x19cb70a0), ('SetParentBridge', 0x14376c0),
           ('SetName', 0x1438af0), ('ComponentVectorRead', 0x19c7f10),
           ('NativeToManaged', 0x14c8d20), ('Unwrap', 0x144adc0),
           ('RawTemplateClone', 0x19e2510), ('FinishTemplateClone', 0x19e2d10),
           ('NativeVectorFree', 0x1a1a410), ('TemplateInactiveWrite', 0x1823967),
           ('TemplateCloneCaller', 0x19e0c70), ('TemplateVectorLayout', 0x19e0ca0),
           ('TemplateVectorCleanup', 0x19e0d22), ('RendererRootBoneGet', 0x19c9a540), ('RendererRootBoneBridge', 0x982050)]
lines = ['#pragma once', '#include <cstdint>',
         '// Reviewed against the 2026-10-15 actor survey and offline disassembly.',
         'namespace gimmd::actor_profile {',
         'struct Entry { const char* name; uint32_t rva; uint8_t bytes[32]; };',
         f'inline constexpr char BuildSha256[] = "{sha}";',
         'inline constexpr Entry Entries[] = {']
for name, rva in entries:
    data = image.read(rva, 32)
    if len(data) != 32 or not image.executable(rva):
        raise SystemExit('Invalid entry: ' + name)
    lines.append('    {"' + name + '", ' + hex(rva) + ', {' + ','.join(map(str, data)) + '}},')
lines += ['};', '}']
Path(sys.argv[2]).write_text('\n'.join(lines) + '\n', encoding='utf-8')
