"""Generate an original six-second neutral FK/foot IK clip with alternating toe lifts."""
import argparse
import json
import runpy
import struct
import wave
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory',type=Path)
args=parser.parse_args()
args.directory.mkdir(parents=True,exist_ok=True)
definitions=runpy.run_path(str(Path(__file__).with_name('build-bind-preview.py')))['definitions']()
records=[]
def key(name,frame,y=0):
    records.append(name.encode('cp932').ljust(15,b'\0')+struct.pack('<I3f4f',frame,0,y,0,0,0,0,1)+bytes(64))
for name,_ in definitions:
    key(name,0); key(name,180)
for name in ('全ての親','センター','グルーブ','左足ＩＫ','右足ＩＫ'):
    key(name,0); key(name,180)
for side,peak in [('左',45),('右',120)]:
    for frame,y in [(0,0),(peak-15,0),(peak,0.5),(peak+15,0.5),(peak+30,0),(180,0)]:
        key(side+'つま先ＩＫ',frame,y)
header=b'Vocaloid Motion Data 0002'.ljust(30,b'\0')+b'gimmd toe test'.ljust(20,b'\0')
clip=header+struct.pack('<I',len(records))+b''.join(records)+struct.pack('<5I',0,0,0,0,0)
target=args.directory/'multi-track.vmd'
if target.exists(): raise SystemExit('Use a fresh directory; calibration never replaces an existing staged motion')
target.write_bytes(clip)
with wave.open(str(args.directory/'multi-track.wav'),'wb') as audio:
    audio.setnchannels(1); audio.setsampwidth(2); audio.setframerate(48000); audio.writeframes(bytes(6*48000*2))
manifest={'mode':'original-toe-calibration','source':'self-generated; no third-party motion edited','durationSeconds':6,
          'leftToeLiftFrames':[45,60],'rightToeLiftFrames':[120,135],'maximumSourceToeLift':0.5,'audio':'six-second silence',
          'otherChannels':'neutral reference FK and foot IK; Center zero'}
(args.directory/'toe-calibration.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(manifest,ensure_ascii=True))
