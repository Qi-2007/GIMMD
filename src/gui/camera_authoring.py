"""Original camera-key authoring; exported VMD uses the pinned runtime convention."""
from __future__ import annotations
import json
import math
from pathlib import Path
import struct
import uuid

UNITS = 0.0748706888


def validate_keys(keys):
    if not isinstance(keys, list) or not 1 <= len(keys) <= 10000:
        raise ValueError('需要 1–10000 个关键帧')
    result = []
    seen = set()
    for key in keys:
        if not isinstance(key, dict) or set(key) != {'seconds', 'values'}:
            raise ValueError('关键帧格式无效')
        seconds, values = key['seconds'], key['values']
        if type(seconds) not in (int, float) or not math.isfinite(seconds) or not 0 <= seconds <= 86400:
            raise ValueError('关键帧时间应为 0–86400 秒')
        if not isinstance(values, list) or len(values) != 7 or any(type(v) not in (int, float) or not math.isfinite(v) for v in values):
            raise ValueError('关键帧需要七个有限数值')
        if any(abs(v) > 100 for v in values[:3]) or any(abs(v) > 180 for v in values[3:6]) or not 1 <= values[6] <= 179:
            raise ValueError('关键帧位移范围 ±100、角度 ±180°，FOV 必须为 1–179°')
        frame = int(math.floor(seconds * 30 + .5))
        if frame in seen:
            raise ValueError('两个关键帧落在同一 VMD 帧；请间隔至少 1/30 秒')
        seen.add(frame)
        result.append({'seconds': frame / 30, 'values': list(values)})
    result.sort(key=lambda k: k['seconds'])
    if result[0]['seconds'] != 0:
        raise ValueError('请先添加第 0 秒的初始机位')
    return result


def encode_vmd(keys):
    keys = validate_keys(keys)
    data = bytearray(b'Vocaloid Motion Data 0002'.ljust(30, b'\0'))
    data += b'GIMMD camera'.ljust(20, b'\0')
    data += struct.pack('<III', 0, 0, len(keys))  # bone, morph, camera counts
    for key in keys:
        right, up, forward, yaw, pitch, roll, fov = key['values']
        # Runtime aligns the source Y-180 first pose to the initial game view.
        data += struct.pack('<If3f3f', round(key['seconds'] * 30), 0,
                            right / UNITS, up / UNITS, forward / UNITS,
                            -math.radians(pitch), -math.radians(yaw), -math.radians(roll))
        data += bytes([20, 107, 20, 107] * 6)  # linear Bezier curves
        data += struct.pack('<IB', int(math.floor(fov + .5)), 0)  # perspective
    data += struct.pack('<III', 0, 0, 0)  # light, shadow, IK counts
    return bytes(data)


def atomic_bytes(path: Path, data: bytes):
    temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    try:
        temporary.write_bytes(data)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def load_keys(path: Path):
    if path.stat().st_size > 4 << 20:
        raise ValueError('关键帧工程超过 4 MiB')
    value = json.loads(path.read_text(encoding='utf-8-sig'))
    if value.get('schemaVersion') != 1 or value.get('type') != 'gimmd-camera-keys':
        raise ValueError('不支持的运镜工程')
    return validate_keys(value.get('keys'))
