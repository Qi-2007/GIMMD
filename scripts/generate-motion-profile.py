"""Generate shared CP932-to-Genshin provisional mapping; no process access."""
import runpy
from pathlib import Path
entries = runpy.run_path(str(Path(__file__).with_name('build-bind-preview.py')))['definitions']()
selected = ['Bip001 L UpperArm','Bip001 R UpperArm','Bip001 L Forearm','Bip001 R Forearm','Bip001 L Hand','Bip001 R Hand','Bip001 Neck','Bip001 Head','Bip001 L Finger1','Bip001 R Finger1']
lines = ['#pragma once', '// Generated from the provisional mapping, not a calibrated bind pose.', 'namespace gimmd::motion_profile {', 'struct Mapping { const char* cp932; const wchar_t* target; const char* label; };', 'inline constexpr Mapping Mappings[] = {']
for name,target in entries:
    encoded=''.join('\\x%02x'%b for b in name.encode('cp932'))
    lines.append('    {"'+encoded+'", L"'+target+'", "'+target+'"},')
lines += ['};','inline constexpr unsigned PreviewIndices[] = {'+', '.join(str(next(i for i,(_,t) in enumerate(entries) if t==target)) for target in selected)+'};','}']
(Path(__file__).resolve().parent.parent / 'src/generated/motion_profile.h').write_text('\n'.join(lines)+'\n', encoding='utf-8')
