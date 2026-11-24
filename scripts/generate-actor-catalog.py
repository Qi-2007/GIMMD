"""Generate a version-specific actor template catalog from completed read-only runtime evidence."""
import json
import re
import sys
from pathlib import Path
source=Path(sys.argv[1]);data=json.loads(source.read_text(encoding='utf-8-sig'))
links=data['assetLinks']
if data.get('engineException')!=0 or data.get('settersCalled') is not False or data.get('entryBytesRestored') is not True or links['status']!='asset-links-observed' or links['scanned']!=links['bundles']:
    raise SystemExit('Incomplete or unclean discovery evidence')
entries=[]
for item in links['cacheSamples']:
    name=item['name'];bundle=item['bundle'];key=item['hash']
    if not re.fullmatch(r'Avatar_(?:Boy|Girl|Lady|Loli|Male)_[A-Za-z0-9_]+_Manekin',name):continue
    if not re.fullmatch(r'[0-9]+/[0-9a-f]{16}\.asb',bundle) or type(key) is not int or not 0<key<=0xffffffff:
        raise SystemExit('Invalid catalog entry')
    entries.append((name,bundle,key))
destination=Path(sys.argv[2])
if destination.exists():
    previous=destination.read_text(encoding='utf-8')
    if data['executableSha256'] not in previous: raise SystemExit('Existing catalog belongs to another executable')
    entries.extend((name,bundle,int(key,16)) for name,bundle,key in re.findall(r'\{"([A-Za-z0-9_]+)", "([0-9]+/[0-9a-f]{16}\.asb)", (0x[0-9a-f]+)\}',previous))
entries=sorted(set(entries))
lines=['#pragma once','#include <cstdint>','// Generated from completed, version-pinned runtime template discovery.',
       'namespace gimmd::actor_catalog {','struct Entry { const char* name; const char* bundle; uint32_t hash; };',
       'inline constexpr char BuildSha256[] = "'+data['executableSha256']+'";', 'inline constexpr Entry Entries[] = {']
for name,bundle,key in entries:lines.append(f'    {{"{name}", "{bundle}", {hex(key)}}},')
lines+=['};','}']
Path(sys.argv[2]).write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(f'{len(entries)} actor template entries')
