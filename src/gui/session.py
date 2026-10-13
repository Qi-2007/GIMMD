"""File transport and isolated session staging for the desktop controller."""
from __future__ import annotations
from collections import deque
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import time
import uuid


def atomic_json(path: Path, value: dict):
    atomic_text(path, json.dumps(value, ensure_ascii=False, indent=2))


def atomic_text(path: Path, value: str):
    temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    try:
        temporary.write_text(value, encoding='utf-8')
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def run_tool(arguments):
    result = subprocess.run([str(x) for x in arguments], capture_output=True, encoding='utf-8',
                            errors='replace', creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if result.returncode:
        raise ValueError(result.stderr.strip() or result.stdout.strip() or '工具运行失败')
    return json.loads(result.stdout)


def stage_session(build: Path, motion: Path, audio: Path | None, sessions: Path, camera: Path | None = None):
    build, motion = build.resolve(), motion.resolve()
    manifest = json.loads((build / 'build-manifest.json').read_text(encoding='utf-8-sig'))
    if manifest.get('schemaVersion') != 1 or not manifest.get('playerSession') or not manifest.get('fullBodyFK'):
        raise ValueError('请选择支持完整播放会话的构建目录')
    for filename, field in [('gimmd-probe.dll', 'dllSha256'), ('gimmd-inject-probe.exe', 'loaderSha256')]:
        if hashlib.sha256((build / filename).read_bytes()).hexdigest().upper() != manifest[field]:
            raise ValueError('构建文件与清单不匹配，请重新构建')
    summary = run_tool([build / 'gimmd-vmd.exe', motion, '0'])
    if not summary['boneTracks'] or not 0 < summary['durationSeconds'] <= 86400:
        raise ValueError('动作需要非空骨骼轨道和有效时长')
    if camera:
        if not manifest.get("cameraPlayback"):
            raise ValueError("该构建未启用相机播放")
        camera = camera.resolve()
        camera_summary = run_tool([build / "gimmd-camera.exe", camera, "0"])
        if camera_summary.get("hasOrthographicKeys", True):
            raise ValueError("暂不支持包含正交投影的相机轨道")
    if audio:
        import wave
        if audio.stat().st_size > 64 << 20:
            raise ValueError('音频文件超过 64 MiB')
        with wave.open(str(audio), 'rb') as stream:
            if stream.getsampwidth() != 2 or stream.getnchannels() not in (1, 2) or not 8000 <= stream.getframerate() <= 192000:
                raise ValueError('音频需要 16 位单/双声道 PCM WAV')
    sessions.mkdir(parents=True, exist_ok=True)
    target = sessions / (time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8])
    target.mkdir()  # Never reuse a loaded DLL or another session's mailbox.
    try:
        for filename in ['gimmd-probe.dll', 'gimmd-inject-probe.exe', 'gimmd-vmd.exe', 'build-manifest.json']:
            shutil.copy2(build / filename, target / filename)
        shutil.copy2(motion, target / 'multi-track.vmd')
        if audio:
            shutil.copy2(audio.resolve(), target / 'multi-track.wav')
        if camera:
            shutil.copy2(camera, target / 'camera.vmd')
        atomic_json(target / 'session.json', {'schemaVersion': 1, 'motion': str(motion),
            'audio': str(audio.resolve()) if audio else None, 'camera': str(camera) if camera else None, 'build': str(build),
            'durationSeconds': summary['durationSeconds'], 'createdAt': time.time()})
    except Exception:
        # Preserve the failed directory for diagnosis; it has never been loaded.
        raise
    return target


def game_processes():
    result = subprocess.run(['tasklist.exe', '/FO', 'CSV', '/NH'], capture_output=True,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    rows = csv.reader(result.stdout.decode(errors='replace').splitlines())
    return [(int(row[1]), row[0]) for row in rows if len(row) >= 2 and
            row[0].lower() in ('yuanshen.exe', 'genshinimpact.exe') and row[1].isdigit()]


def start_loader(directory: Path, pid: int):
    if type(pid) is not int or pid <= 0:
        raise ValueError('请选择有效游戏进程')
    # Paths are environment data, never interpolated into PowerShell code.
    script = """$ErrorActionPreference='Stop'
$p=Start-Process -FilePath $env:GIMMD_STUDIO_LOADER -ArgumentList @('--pid',$env:GIMMD_STUDIO_PID,('"'+$env:GIMMD_STUDIO_DLL+'"')) -Verb RunAs -WindowStyle Hidden -PassThru
$p.WaitForExit(); exit $p.ExitCode
"""
    import base64
    env = dict(os.environ, GIMMD_STUDIO_LOADER=str(directory / 'gimmd-inject-probe.exe'),
               GIMMD_STUDIO_DLL=str(directory / 'gimmd-probe.dll'), GIMMD_STUDIO_PID=str(pid))
    with (directory / 'loader.log').open('w', encoding='utf-8') as log:
        return subprocess.Popen(['powershell.exe', '-NoProfile', '-EncodedCommand',
            base64.b64encode(script.encode('utf-16le')).decode('ascii')], env=env,
            stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)


class Controller:
    def __init__(self, directory: Path):
        self.directory = directory.resolve()
        self.pending = deque()
        self.token = None
        self.sent = 0
        self.sequence = 0
        self.status = None
        self.fresh = False

    def poll(self):
        path = self.directory / 'player.status.json'
        self.fresh = False
        try:
            stamp = path.stat().st_mtime
            current = json.loads(path.read_text(encoding='utf-8-sig'))
            if current.get('schemaVersion') != 2:
                return None
            if (type(current.get('sessionToken')) is not int or type(current.get('closed')) is not bool or
                type(current.get('state')) is not int or current['state'] not in range(4)):
                return None
            for key in ('seconds', 'durationSeconds'):
                if not isinstance(current.get(key), (int, float)) or not math.isfinite(current[key]) or current[key] < 0:
                    return None
            for key in ('commandSequence', 'acknowledgedSequence'):
                if type(current.get(key)) is not int or not 0 <= current[key] < 1 << 28:
                    return None
            if current['sessionToken'] != self.token:
                self.token = current['sessionToken']; self.sent = 0; self.pending.clear()
                self.sequence = 0
            self.sequence = max(self.sequence, current['commandSequence'], current['acknowledgedSequence'])
            try:
                prior = int((self.directory / 'player.command').read_text(encoding='utf-8').split(' ', 1)[0])
                if 0 < prior < 1 << 28:
                    self.sequence = max(self.sequence, prior); self.sent = max(self.sent, prior)
            except (OSError, ValueError):
                pass
            self.status = current
            self.fresh = 0 <= time.time() - stamp < 3 and not current['closed']
            if current['closed']:
                self.pending.clear()
            return current
        except (OSError, ValueError, TypeError):
            return None

    def enqueue(self, operation: str, seconds=0):
        if not self.fresh:
            raise ValueError('当前没有可控制的活动会话')
        if operation not in ('play', 'pause', 'seek', 'loop-on', 'loop-off', 'stop'):
            raise ValueError('无效播放命令')
        if operation == 'seek' and (not math.isfinite(seconds) or seconds < 0 or seconds > min(86400, self.status['durationSeconds'])):
            raise ValueError('跳转时间超出动作时长')
        if operation == 'stop':
            self.pending.clear(); self.sent = 0
        if operation == 'seek' and self.pending and self.pending[-1][0] == 'seek':
            self.pending.pop()
        self.pending.append((operation, seconds))

    def pump(self):
        if not self.fresh or not self.pending or self.status['acknowledgedSequence'] < self.sent:
            return None
        if self.sequence + 1 >= 1 << 28:
            raise ValueError('命令序号已耗尽，请开始新会话')
        operation, seconds = self.pending[0]
        sequence = self.sequence + 1
        text = f'{sequence} {operation}' + (f' {seconds:.3f}' if operation == 'seek' else '')
        atomic_text(self.directory / 'player.command', text)
        self.pending.popleft(); self.sent = self.sequence = sequence
        return text
