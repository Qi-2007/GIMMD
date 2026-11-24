"""Generate a two-second synthetic FK, center-yaw and root-translation fixture."""
import argparse
import math
import re
import struct
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
text=(root/'src/generated/motion_profile.h').read_text(encoding='utf-8')
names=re.findall(r'\{"((?:\\x[0-9a-fA-F]{2})+)", L"',text)
keys=[]
def key(name,frame,position,rotation):
    return name.ljust(15,b'\0')+struct.pack('<I3f4f',frame,*position,*rotation)+bytes([20,20,107,107])*16
for i,name in enumerate(names):
    raw=bytes(int(x,16) for x in re.findall(r'\\x([0-9a-fA-F]{2})',name))
    for frame,angle in [(0,0),(30,.2 if i%2 else -.2),(60,0)]:
        keys.append(key(raw,frame,(0,0,0),(math.sin(angle/2),0,0,math.cos(angle/2))))
center='センター'.encode('cp932')
for frame,position,yaw in [(0,(0,0,0),0),(30,(3,1,2),.5),(60,(1,.2,0),.2)]:
    keys.append(key(center,frame,position,(0,math.sin(yaw/2),0,math.cos(yaw/2))))
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_bytes(b'Vocaloid Motion Data 0002'.ljust(30,b'\0')+b'GIMMD calibration'.ljust(20,b'\0')+
    struct.pack('<I',len(keys))+b''.join(keys)+struct.pack('<5I',0,0,0,0,0))
print(f'Generated {len(names)+1} synthetic tracks: {a.output}')
