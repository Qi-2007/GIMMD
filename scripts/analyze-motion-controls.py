"""Inspect sampled root, leg FK and foot IK activity without writing game state."""
import argparse
import json
import math
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('inspector',type=Path); parser.add_argument('motion',type=Path); parser.add_argument('output',type=Path)
args = parser.parse_args()
def sample(seconds):
    value = json.loads(subprocess.check_output([str(args.inspector.resolve()),str(args.motion.resolve()),str(seconds)]))
    return value, {bytes.fromhex(b['nameCp932Hex']).decode('cp932'):b for b in value['bones']}
summary, first = sample(0)
names = ['全ての親','センター','グルーブ','下半身','左足','左ひざ','左足首','右足','右ひざ','右足首',
         '左足ＩＫ','右足ＩＫ','左つま先ＩＫ','右つま先ＩＫ','左足IK','右足IK','左足D','右足D','左ひざD','右ひざD']
tracks = {name:{'name':name,'samples':[]} for name in names if name in first}
for i in range(17):
    seconds = summary['durationSeconds']*i/16
    _, current = sample(seconds)
    for name,track in tracks.items():
        b = current[name]; track['samples'].append({'seconds':seconds,'position':b['position'],'rotation':b['rotation']})
for track in tracks.values():
    items = track['samples']; base = items[0]['position']
    track['maxPositionDeltaFromFirst'] = max(math.dist(base,b['position']) for b in items)
    track['maxRotationDegreesFromIdentity'] = max(2*math.acos(min(1,abs(b['rotation'][3])))*180/math.pi for b in items)
args.output.write_text(json.dumps({'mode':'offline-sampled-control-activity','sampleCount':17,
    'samplesAreNotExhaustive':True,'sourceUnits':'unscaled MMD units','tracks':list(tracks.values())},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps([{key:t[key] for key in ('name','maxPositionDeltaFromFirst','maxRotationDegreesFromIdentity')} for t in tracks.values()],ensure_ascii=True))
