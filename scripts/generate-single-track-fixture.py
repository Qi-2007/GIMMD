"""Generate the only VMD accepted by the bounded native playback experiment."""
import argparse
import math
import struct
from pathlib import Path


def fixture():
    data = bytearray(b'Vocaloid Motion Data 0002'.ljust(30, b'\0'))
    data += b'gimmd synthetic'.ljust(20, b'\0')
    data += struct.pack('<I', 3)
    interpolation = bytearray(64)
    for axis in range(4):
        for offset, value in [(0, 20), (4, 20), (8, 107), (12, 107)]:
            interpolation[axis + offset] = value
    for frame, angle in [(0, 0), (30, 5), (60, 0)]:
        half = math.radians(angle) / 2
        data += b'left_arm_test'.ljust(15, b'\0')
        data += struct.pack('<I7f', frame, 0, 0, 0, 0, 0, math.sin(half), math.cos(half))
        data += interpolation
    data += struct.pack('<5I', 0, 0, 0, 0, 0)
    return bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_bytes(fixture())
    print(f'{len(fixture())} bytes -> {args.output}')
