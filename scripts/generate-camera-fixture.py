"""Original two-second camera track for offline interpolation/coordinate checks."""
import argparse
import math
from pathlib import Path
import struct

parser=argparse.ArgumentParser(); parser.add_argument('output',type=Path); args=parser.parse_args()
clip=bytearray(b'Vocaloid Motion Data 0002'.ljust(30,b'\0')+b'gimmd camera'.ljust(20,b'\0'))
clip+=struct.pack('<3I',0,0,3)
for frame,distance,interest,euler,fov in [(0,-20,(0,10,0),(0,0,0),45),
        (30,-25,(5,12,2),(0,math.pi/2,0),60),(60,-20,(0,10,0),(0,0,0),45)]:
    clip+=struct.pack('<I7f',frame,distance,*interest,*euler)+bytes(24)+struct.pack('<IB',fov,0)
clip+=struct.pack('<3I',0,0,0)
if args.output.exists(): raise SystemExit('Refuse to replace an existing camera file')
args.output.write_bytes(clip)
print(str(args.output))
