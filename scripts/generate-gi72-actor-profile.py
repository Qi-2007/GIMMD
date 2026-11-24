"""CN 7.2 native loaded-model cloning profile; template resources remain separate."""
import hashlib
import runpy
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHA = '8B014F64B5DC12FBC476E0C362B053ABFDD7A1378CD282E4AD81057621DBD6B5'

def generate(executable):
    image = runpy.run_path(str(ROOT/'scripts/inspect-gi-functions.py'))['Image'](executable)
    if hashlib.sha256(image.data).hexdigest().upper() != SHA:
        raise ValueError('Unreviewed executable')
    entries = dict(EmoManagerTickGet=0x6f34ff0,EmoManagerTickSet=0x6f35000,
        EmoBindingBanApply=0x716e5a0,EmoBindingApply=0x716e750,
        EmoUserTickGet=0x6db0bb0,EmoUserTickSet=0x6db0bc0,
        MeshBlendShapeCount=0x1a213e10,MeshBlendShapeCountBridge=0x9851d0,
        MeshBlendShapeName=0x1a213e20,MeshBlendShapeNameBridge=0x985240,
        RendererBlendShapeGet=0x1a20e1c0,RendererBlendShapeGetBridge=0x98b380,
        RendererBlendShapeSet=0x1a20e1d0,RendererBlendShapeSetBridge=0x98b3e0,
        RendererEnabledGet=0x1a246290,RendererEnabledSet=0x1a2462a0,
        RendererEnabledGetBridge=0x98c0b0,RendererEnabledSetBridge=0x98c100,
        MaterialShaderGet=0x144e7a0, MaterialShaderGetNative=0x164fb10,
        SharedMaterialsGet=0x1a2465b0, SharedMaterialsGetBridge=0x98bd50,
        SharedMaterialsSet=0x1a2465c0, SharedMaterialsSetBridge=0x98bde0,
        CloneWithParent=0x1a2268f0, CloneBridge=0x14494c0, CloneNative=0x18376e0,
        DestroyImmediate=0x1a2269c0, DestroyBridge=0x14496d0, DestroyNative=0x14d96e0,
        SetActive=0x1a20f440, SetActiveBridge=0x1463fc0,
        SetParent=0x1a22ac90, SetParentBridge=0x1448450, SetName=0x1449920,
        ComponentVectorRead=0x19dc060, NativeToManaged=0x14d9f90, Unwrap=0x145be60,
        RendererRootBoneGet=0x1a20e180, RendererRootBoneBridge=0x98b1c0,
        ClassLookup=0x14e0c80, ClassLookupNative=0x14d79d0, DomainGet=0x14d5b30,
        ReflectionType=0x538230, FindObjectsAll=0x1a23e950, FindObjectsAllNative=0x1452ab0,
        ReflectionClassTypeCall=0x156ab1,
        SdkLoadIndependentObject=0x6ebb750,SdkLoadIndependentGeneric=0x147afdb0,
        SdkDismissExternal=0x6eb81e0,SdkTryGetAssetByHandle=0x6ebb080)
    lines=['#pragma once','#include <cstdint>',
        '// CN 7.2 named Unity wrappers and reviewed native clone/cleanup chain.',
        '// Scene and cached model sources. No 7.1 resource IDs are accepted.',
        'namespace gimmd::actor_profile {',
        'struct Entry { const char* name; uint32_t rva; uint8_t bytes[32]; };',
        f'inline constexpr char BuildSha256[] = "{SHA}";',
        'inline constexpr bool ResourceLookupSupported = true;',
        'inline constexpr uint32_t ClassType = 0x10;',
        'inline constexpr Entry Entries[] = {']
    for name,rva in entries.items():
        code=image.read(rva,32)
        if len(code)!=32 or not image.executable(rva):raise ValueError(name)
        lines.append('    {"'+name+'", '+hex(rva)+', {'+','.join(map(str,code))+'}},')
    lines+=['};','}']
    (ROOT/'src/generated/gi_actor_profile.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')

if __name__=='__main__':generate(*sys.argv[1:])
