"""Data-only Better-Endfield/EIEM-style set.ini import; never executes files."""
from pathlib import Path
import math


def import_work(path: Path):
    if not 0 < path.stat().st_size <= 16 * 1024:
        raise ValueError('set.ini 大小无效')
    folder = path.resolve().parent
    fields = {}
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        line = line.strip()
        if not line or line.startswith(('#', ';', '[')) or '=' not in line:
            continue
        key, value = [part.strip() for part in line.split('=', 1)]
        key = key.lower()
        if key in fields:
            raise ValueError('set.ini 字段重复：' + key)
        fields[key] = value
    files = {}
    for key in ['motion', 'camera', 'face', 'music', *['motion' + str(i) for i in range(2, 5)], *['face' + str(i) for i in range(2, 5)]]:
        value = fields.get(key, '')
        if not value:
            continue
        if len(value) > 200 or value in ('.', '..') or any(c in value for c in '/\\:') or any(ord(c) < 32 for c in value):
            raise ValueError(key + ' 必须为作品目录内的文件名')
        target = (folder / value).resolve()
        if target.parent != folder or not target.is_file():
            raise ValueError(key + ' 缺失或指向作品目录外：' + value)
        files[key] = str(target)
    if 'motion' not in files:
        raise ValueError('当前版本导入作品需要单人动作 motion')
    if files.get('music') and Path(files['music']).suffix.lower() != '.wav':
        raise ValueError('当前音频仅支持 PCM WAV，请先转换 music 文件')
    offset = float(fields.get('audio_offset', '0'))
    if not math.isfinite(offset) or abs(offset) > 600:
        raise ValueError('audio_offset 应为 ±600 秒内的有限数值')
    return {'name': fields.get('name', folder.name), 'motion': files['motion'],
            'camera': files.get('camera', ''), 'audio': files.get('music', ''), 'audioOffset': offset,
            'unsupported': {key: value for key, value in files.items() if key.startswith('face') or key.startswith('motion') and key != 'motion'},
            'source': str(path.resolve())}
