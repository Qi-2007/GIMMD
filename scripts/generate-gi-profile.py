"""Emit build-pinned candidate masks, not a promise of callable interfaces."""
import json
import re
import sys
from pathlib import Path

report = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
source = Path(sys.argv[2]).read_text(encoding='utf-8-sig')
definitions = {}
for name, literals in re.findall(r'inline\s+constexpr\s+const\s+char\*\s+(\w+)\s*=\s*((?:"[^"]*"\s*)+);', source):
    definitions[name] = ' '.join(re.findall(r'"([^"]*)"', literals)).split()
lines = ['#pragma once', '// Generated from FufuLauncher.UnlockerIsland Patterns.h, AGPL-3.0.',
         '// Commit: ' + report['patternsCommit'], '#include <cstddef>', '#include <cstdint>',
         'namespace gimmd::gi {', 'struct Candidate { const char* name; uint32_t rva; const int* mask; size_t size; };',
         'inline constexpr char BuildSha256[] = "' + report['sha256'] + '";']
entries = []
for index, interface in enumerate(report['patterns']):
    mask = [-1 if token in ('?', '??') else int(token, 16) for token in definitions[interface['name']]]
    lines.append(f'inline constexpr int Mask{index}[] = {{{",".join(map(str, mask))}}};')
    for candidate in interface['candidates']:
        entries.append(f'    {{"{interface["name"]}", {candidate["rva"]}, Mask{index}, sizeof(Mask{index})/sizeof(int)}}')
lines += ['inline constexpr Candidate Candidates[] = {', ',\n'.join(entries), '};', '}']
dest = Path(sys.argv[3])
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_text('\n'.join(lines) + '\n', encoding='utf-8')
