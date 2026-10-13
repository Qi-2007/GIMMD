"""Self-authored ten-bone VMD calibration; uses exact CP932 Japanese names."""
import argparse
import math
import runpy
import struct
from pathlib import Path


def fixture():
    definitions = runpy.run_path(str(Path(__file__).with_name('build-bind-preview.py')))['definitions']()
    targets = ['Bip001 L UpperArm', 'Bip001 R UpperArm', 'Bip001 L Forearm', 'Bip001 R Forearm',
               'Bip001 L Hand', 'Bip001 R Hand', 'Bip001 Neck', 'Bip001 Head', 'Bip001 L Finger1', 'Bip001 R Finger1']
    names = [next(name for name, target in definitions if target == wanted) for wanted in targets]
    data = bytearray(b'Vocaloid Motion Data 0002'.ljust(30, b'\0')) + b'gimmd calibration'.ljust(20, b'\0')
    data += struct.pack('<I', len(names) * 3)
    interpolation = bytearray(64)
    for axis in range(4):
        for offset, value in [(0, 20), (4, 20), (8, 107), (12, 107)]: interpolation[axis + offset] = value
    for index, name in enumerate(names):
        for frame, angle in [(0, 0), (30, 5), (60, 0)]:
            half = math.radians(angle * (-1 if index % 2 else 1)) / 2
            q = [0., 0., 0., math.cos(half)]; q[[2, 2, 0, 0, 1, 1, 1, 1, 0, 0][index]] = math.sin(half)
            data += name.encode('cp932').ljust(15, b'\0') + struct.pack('<I7f', frame, 0, 0, 0, *q) + interpolation
    return bytes(data + struct.pack('<5I', 0, 0, 0, 0, 0))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('output', type=Path)
    args = parser.parse_args(); args.output.write_bytes(fixture())
    print(f'{len(fixture())} bytes -> {args.output}')
