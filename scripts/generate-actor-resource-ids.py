"""Generate only resource IDs verified against a clean runtime LoadedBundle dictionary."""
import json
import re
import sys
from pathlib import Path

report = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
sha = '7F89938DA606C1281659607D464702CDDB9A09A7D4320A196630F60A811EC38E'
if report.get('executableSha256') != sha or report.get('engineException') != 0 or not report.get('entryBytesRestored'):
    raise SystemExit('Unverified source report')
names = {(x['hash'], x['bundle']): x['name'] for x in report['assetLinks']['cacheSamples']
         if x.get('name', '').endswith('_Manekin')}
entries = []
packages={}
for row in report.get('loadedPackages',{}).get('entries',[]):
    bundle=row['bundle'];package=row['lookupKey']
    if not re.fullmatch(r'[0-9]+/[0-9a-f]{16}\.asb',bundle) or type(package) is not int or not 0<package<=0xffffffff:continue
    if bundle in packages and packages[bundle]!=package:raise SystemExit('Ambiguous bundle identity')
    packages[bundle]=package
for (hash_value,bundle),name in names.items():
    if bundle in packages:
        if not re.fullmatch(r'Avatar_(?:Boy|Girl|Lady|Loli|Male)_[A-Za-z0-9_]+_Manekin',name):continue
        entries.append(({'hash':hash_value,'lookupKey':packages[bundle],'bundle':bundle},name))
for row in report['managerDirectory']['entries']:
    if not row.get('matched') or not row.get('lookupKey'):
        continue
    key = (row['hash'], row['bundle'])
    if key not in names or row['resourceId'] != (row['hash'] << 32) | row['lookupKey']:
        raise SystemExit('Inconsistent resource ID')
    entries.append((row, names[key]))
if not entries:
    raise SystemExit('No verified resource IDs')
destination=Path(sys.argv[2])
if destination.exists():
    previous=destination.read_text(encoding='utf-8')
    if sha not in previous:raise SystemExit('Existing resource IDs belong to another executable')
    for hash_text,package,bundle,name in re.findall(r'\{(0x[0-9a-f]+), ([0-9]+), "([0-9]+/[0-9a-f]{16}\.asb)", "([A-Za-z0-9_]+)"\}',previous):
        entries.append(({'hash':int(hash_text,16),'lookupKey':int(package),'bundle':bundle},name))
unique={}
for row,name in entries:
    value=(row['hash'],row['lookupKey'],row['bundle'])
    if name in unique and unique[name]!=value:raise SystemExit('Conflicting role resource identity: '+name)
    unique[name]=value
entries=[({'hash':value[0],'lookupKey':value[1],'bundle':value[2]},name) for name,value in sorted(unique.items())]
lines = ['#pragma once', '#include <cstdint>', 'namespace gimmd::actor_resource_ids {',
         f'inline constexpr char BuildSha256[] = "{sha}";',
         'struct Entry { uint32_t hash, package; const char* bundle; const char* model; };',
         'inline constexpr Entry Entries[] = {']
for row, name in entries:
    lines.append(f'    {{{hex(row["hash"])}, {row["lookupKey"]}, "{row["bundle"]}", "{name}"}},')
lines += ['};', '}']
Path(sys.argv[2]).write_text('\n'.join(lines) + '\n', encoding='utf-8')
destination.with_suffix('.json').write_text(json.dumps({'schemaVersion':1,'executableSha256':sha,'roles':[
    {'selector':re.sub(r'^Avatar_[^_]+_[^_]+_|_Manekin$','',name),'model':name,'assetHash':hex(row['hash']),
     'packageKey':row['lookupKey'],'bundle':row['bundle']} for row,name in entries]},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(f'{len(entries)} verified role resource IDs')
