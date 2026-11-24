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

PHYSICS_DEFAULTS = {'frontAngle':24, 'rearAngle':10, 'frontPaddingCm':1.5, 'rearPaddingCm':.4,
                    'frontResponseMs':60, 'rearResponseMs':160, 'skirtSupportPercent':95}
PHYSICS_RANGES = {'frontAngle':(0,45), 'rearAngle':(0,45), 'frontPaddingCm':(0,5), 'rearPaddingCm':(0,5),
                  'frontResponseMs':(20,1000), 'rearResponseMs':(20,1000), 'skirtSupportPercent':(0,100)}

def validate_physics_controls(value=None):
    if value is None: return dict(PHYSICS_DEFAULTS)
    if not isinstance(value,dict) or set(value)-set(PHYSICS_DEFAULTS): raise ValueError('物理参数格式无效')
    settings={**PHYSICS_DEFAULTS,**value}
    for key,number in settings.items():
        low,high=PHYSICS_RANGES[key]
        if type(number) not in (int,float) or not math.isfinite(number) or not low<=number<=high:
            raise ValueError(f'物理参数 {key} 应在 {low}–{high} 之间')
    return settings


def atomic_json(path: Path, value: dict):
    atomic_text(path, json.dumps(value, ensure_ascii=False, indent=2))


def atomic_text(path: Path, value: str):
    temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    try:
        temporary.write_text(value, encoding='utf-8')
        for attempt in range(12):
            try:
                os.replace(temporary, path)
                break
            except PermissionError:
                if attempt==11: raise
                time.sleep(.01)
    finally:
        temporary.unlink(missing_ok=True)


def run_tool(arguments):
    result = subprocess.run([str(x) for x in arguments], capture_output=True, encoding='utf-8',
                            errors='replace', creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if result.returncode:
        raise ValueError(result.stderr.strip() or result.stdout.strip() or '工具运行失败')
    return json.loads(result.stdout)


def reject_gaze_only_motion(summary):
    """Use parsed track names, not filenames, to identify unsupported eye motion."""
    bones = summary.get('bones', [])
    gaze_names = {name.encode('cp932').hex() for name in ('両目', '左目', '右目')}
    if bones and len(bones) == summary.get('boneTracks') and all(
        bone.get('nameCp932Hex', '').lower() in gaze_names for bone in bones
    ):
        raise ValueError('这是目线 VMD（仅含眼睛骨骼轨道），不是全身动作。当前版本尚不支持目线播放，请选择完整舞蹈动作 VMD。')


def validate_uid_text(value):
    if not isinstance(value,str) or len(value)>64 or len(value.encode('utf-8'))>256 or any(ord(c)<32 or ord(c)==127 for c in value):
        raise ValueError('UID 区域文字限 64 字，不可包含换行或控制字符')
    return value


def validate_camera_adjustments(values, multiplier=1):
    if not isinstance(values,(list,tuple)) or len(values)!=7 or any(type(v) not in (int,float) or not math.isfinite(v) for v in values):
        raise ValueError('请输入七个有限机位数值')
    if any(abs(v)>100 for v in values[:3]) or any(abs(v)>180 for v in values[3:6]) or (values[6]!=0 and not 1<=values[6]<=179):
        raise ValueError('位移范围 ±100，角度 ±180°，FOV 为 0 或 1–179°')
    if type(multiplier) not in (int,float) or not math.isfinite(multiplier) or not .1<=multiplier<=4:
        raise ValueError('FOV 倍率范围 0.1–4')
    return list(values)


def stage_session(build: Path, motion: Path, audio: Path | None, sessions: Path, camera: Path | None = None, manual_camera=False, audio_offset=0, stabilize_cloth=False, stabilize_braid=False, pause_bgm=False, uid_text=None, camera_values=None, fov_multiplier=1, hold_at_end=False, dynamic_session=False):
    build=build.resolve(); motion=motion.resolve() if motion else None
    manifest = json.loads((build / 'build-manifest.json').read_text(encoding='utf-8-sig'))
    if manifest.get('schemaVersion') != 1 or not manifest.get('playerSession') or not manifest.get('fullBodyFK'):
        raise ValueError('请选择支持完整播放会话的构建目录')
    for filename, field in [('gimmd-probe.dll', 'dllSha256'), ('gimmd-inject-probe.exe', 'loaderSha256')]:
        if hashlib.sha256((build / filename).read_bytes()).hexdigest().upper() != manifest[field]:
            raise ValueError('构建文件与清单不匹配，请重新构建')
    if dynamic_session and not manifest.get('dynamicSession'): raise ValueError('该构建不支持动态会话')
    if motion is None and not dynamic_session: raise ValueError('请选择动作 VMD')
    summary={'durationSeconds':0}
    if motion:
        summary = run_tool([build / 'gimmd-vmd.exe', motion, '0'])
        reject_gaze_only_motion(summary)
        if not summary['boneTracks'] or not 0 < summary['durationSeconds'] <= 86400:
            raise ValueError('动作需要非空骨骼轨道和有效时长')
        if manifest.get('rootMotion') and summary.get('rootControlCompatibility','supported')!='supported':
            raise ValueError('动作根控制暂不支持：'+summary['rootControlCompatibility'])
        if summary.get('centerYawRequired') and not manifest.get('centerYaw'):
            raise ValueError('该动作需要中心转向支持，请选择 build-studio-center-yaw')
    if stabilize_cloth and not manifest.get("clothStabilization"):
        raise ValueError("该构建未启用披风静态稳定")
    for enabled, capability, title in [(stabilize_braid,'braidStabilization','辫子静态稳定'),(pause_bgm,'bgmPause','背景音乐暂停'),(uid_text is not None,'uidText','UID 区域文字')]:
        if enabled and not manifest.get(capability): raise ValueError('该构建未启用'+title)
    if type(hold_at_end) is not bool: raise ValueError('末帧保持开关无效')
    if hold_at_end and not manifest.get('holdAtEnd'): raise ValueError('该构建不支持结束后保持末帧')
    if uid_text is not None: validate_uid_text(uid_text)
    camera_values=validate_camera_adjustments([0]*7 if camera_values is None else camera_values,fov_multiplier)
    adjusted=any(camera_values) or fov_multiplier!=1
    if adjusted and (not (camera or manual_camera) or not manifest.get('cameraAdjustments')):
        raise ValueError('初始运镜调整需要启用相机并选择支持运镜调整的构建')
    if manual_camera and (camera or not manifest.get("manualCamera")):
        raise ValueError("手动机位需要支持该功能的构建，且不能同时指定相机轨道")
    if camera:
        if not manifest.get("cameraPlayback"):
            raise ValueError("该构建未启用相机播放")
        camera = camera.resolve()
        camera_summary = run_tool([build / "gimmd-camera.exe", camera, "0"])
        if camera_summary.get("hasOrthographicKeys", True):
            raise ValueError("暂不支持包含正交投影的相机轨道")
    if type(audio_offset) not in (int,float) or not math.isfinite(audio_offset) or abs(audio_offset)>600:
        raise ValueError('音频偏移应为 ±600 秒内的有限数值')
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
        if (build/'gimmd-camera.exe').exists(): shutil.copy2(build/'gimmd-camera.exe',target/'gimmd-camera.exe')
        if motion: shutil.copy2(motion, target / 'multi-track.vmd')
        if dynamic_session: atomic_text(target/'dynamic-session.flag','1')
        if audio:
            if audio_offset:
                with wave.open(str(audio.resolve()), 'rb') as stream:
                    channels,rate=stream.getnchannels(),stream.getframerate()
                    frames=stream.getnframes(); shift=round(audio_offset*rate)
                    output_frames=frames-shift
                    if output_frames<=0 or output_frames*channels*2+44>64<<20:
                        raise ValueError('音频偏移后为空或超过 64 MiB')
                    if shift>0: stream.setpos(shift)
                    data=(b'\0'*(-min(shift,0)*channels*2))+stream.readframes(frames)
                with wave.open(str(target/'multi-track.wav'),'wb') as output:
                    output.setnchannels(channels); output.setsampwidth(2); output.setframerate(rate); output.writeframes(data)
            else: shutil.copy2(audio.resolve(), target / 'multi-track.wav')
        if camera:
            shutil.copy2(camera, target / 'camera.vmd')
        if stabilize_cloth:
            atomic_text(target/'cloth-static.flag','1')
        if stabilize_braid: atomic_text(target/'braid-static.flag','1')
        if pause_bgm: atomic_text(target/'pause-bgm.flag','1')
        if uid_text is not None: atomic_text(target/'uid-text.txt',uid_text)
        if hold_at_end: atomic_text(target/'hold-end.flag','1')
        if adjusted:
            atomic_text(target/'camera-initial.txt','1 '+' '.join(format(v,'.9g') for v in [*camera_values,fov_multiplier]))
        if manual_camera:
            atomic_text(target / 'manual-camera.flag', '1')
        atomic_json(target / 'session.json', {'schemaVersion': 1, 'motion': str(motion) if motion else None,
            'audio': str(audio.resolve()) if audio else None, 'camera': str(camera) if camera else None, 'build': str(build),
            'dynamicSession':dynamic_session,'holdAtEnd':hold_at_end,'cameraValues':camera_values,'fovMultiplier':fov_multiplier,'stabilizeBraid':bool(stabilize_braid),'pauseBgm':bool(pause_bgm),'uidText':uid_text,'stabilizeCloth': bool(stabilize_cloth), 'audioOffset': audio_offset, 'manualCamera': bool(manual_camera), 'durationSeconds': summary['durationSeconds'], 'createdAt': time.time()})
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

    def camera(self, values, fov_multiplier=1, preview_override=False):
        if not self.fresh or not self.status.get('cameraEnabled'):
            raise ValueError('当前会话未启用相机控制')
        values=validate_camera_adjustments(values,fov_multiplier)
        if fov_multiplier!=1 or preview_override:
            try: supported=json.loads((self.directory/'build-manifest.json').read_text(encoding='utf-8-sig')).get('cameraLivePreview' if preview_override else 'cameraAdjustments')
            except (OSError,ValueError): supported=False
            if not supported: raise ValueError('当前会话构建不支持 FOV 倍率，请开始新版会话')
            values=[*values,fov_multiplier]
            if preview_override: values.append(1)
        acknowledged=self.status.get('cameraAcknowledgedSequence',0)
        if type(acknowledged) is not int or not 0<=acknowledged<0xffffffff:
            raise ValueError('相机状态序号无效')
        previous=0
        try: previous=int((self.directory/'camera.command').read_text().split()[0])
        except (OSError,ValueError,IndexError): pass
        if not 0<=previous<0xffffffff: raise ValueError('相机命令序号无效')
        sequence=max(previous,acknowledged)+1
        if sequence>0xffffffff: raise ValueError('相机命令序号已耗尽')
        atomic_text(self.directory/'camera.command',str(sequence)+' '+' '.join(format(v,'.9g') for v in values))
        return sequence

    def prepare_camera(self, camera=None, allow_active=False):
        if not self.fresh or not self.status.get('dynamicSession'): raise ValueError('请先连接动态会话')
        if self.status.get('cameraActive') and not allow_active: raise ValueError('请先关闭相机控制，再切换机位来源')
        path=self.directory/'camera-next.vmd'
        if camera is None: path.unlink(missing_ok=True); return
        summary=run_tool([self.directory/'gimmd-camera.exe',camera,'0'])
        if summary.get('hasOrthographicKeys',True): raise ValueError('不支持正交投影相机轨道')
        temporary=self.directory/'camera-load.tmp'; shutil.copy2(camera,temporary); os.replace(temporary,path)

    def load_motion(self, motion, audio=None, audio_offset=0):
        if not self.fresh or not self.status.get('dynamicSession'): raise ValueError('请先连接动态会话')
        if self.status.get('mmdPhase') in (1,2,4): raise ValueError('请先恢复游戏动画，再载入新动作')
        summary=run_tool([self.directory/'gimmd-vmd.exe',motion,'0'])
        reject_gaze_only_motion(summary)
        if not summary.get('boneTracks') or not 0<summary['durationSeconds']<=86400: raise ValueError('动作时长或轨道无效')
        audio_target=self.directory/'multi-track.wav'
        if audio is None: audio_target.unlink(missing_ok=True)
        else:
            import wave
            if type(audio_offset) not in (int,float) or not math.isfinite(audio_offset) or abs(audio_offset)>600: raise ValueError('音频偏移无效')
            if audio.stat().st_size>64<<20: raise ValueError('音频超过 64 MiB')
            with wave.open(str(audio),'rb') as source:
                channels,rate=source.getnchannels(),source.getframerate(); frames=source.getnframes(); shift=round(audio_offset*rate)
                if source.getsampwidth()!=2 or channels not in (1,2) or not 8000<=rate<=192000: raise ValueError('需要 16 位 PCM WAV')
                if frames-shift<=0 or (frames-shift)*channels*2+44>64<<20: raise ValueError('偏移后音频无效或过大')
                if shift>0: source.setpos(shift)
                data=b'\0'*(-min(shift,0)*channels*2)+source.readframes(frames)
            temporary_audio=self.directory/'audio-load.tmp'
            with wave.open(str(temporary_audio),'wb') as output:
                output.setnchannels(channels); output.setsampwidth(2); output.setframerate(rate); output.writeframes(data)
            os.replace(temporary_audio,audio_target)
        temporary=self.directory/'motion-load.tmp'; shutil.copy2(motion,temporary); os.replace(temporary,self.directory/'multi-track.vmd')

    def prepare_physics(self, enabled, controls=None):
        if not self.fresh: raise ValueError('请先连接正在运行的会话')
        manifest=json.loads((self.directory/'build-manifest.json').read_text(encoding='utf-8-sig'))
        if enabled and not manifest.get('secondaryPhysics'): raise ValueError('当前构建不支持动态物理，请选择物理实验构建')
        settings=validate_physics_controls(controls)
        if enabled and controls is not None and not manifest.get('secondaryPhysicsControls'):
            raise ValueError('当前构建不支持物理参数调整，请使用新版物理构建重新连接会话')
        if enabled and self.status.get('mmdPhase') in (1,2,4): raise ValueError('请先恢复游戏动画，再应用物理参数')
        if enabled and any((self.directory/name).exists() for name in ('cloth-static.flag','braid-static.flag')):
            raise ValueError('动态物理与静态稳定冲突，请取消披风/辫子静态稳定后重新连接会话')
        flag=self.directory/'physics-enabled.flag'
        if enabled:
            if manifest.get('secondaryPhysicsControls'):
                atomic_text(self.directory/'physics-controls.txt','GIMMD_PHYSICS_V1\n'+' '.join(format(settings[k],'.9g') for k in PHYSICS_DEFAULTS)+'\n')
            atomic_text(flag,'1')
        else: flag.unlink(missing_ok=True)

    def prepare_face(self, enabled):
        if not self.fresh: raise ValueError('请先连接正在运行的会话')
        manifest=json.loads((self.directory/'build-manifest.json').read_text(encoding='utf-8-sig'))
        if enabled and not manifest.get('faceMotion'): raise ValueError('当前构建不支持表情，请切换到表情版本')
        flag=self.directory/'face-disabled.flag'
        if enabled: flag.unlink(missing_ok=True)
        else: atomic_text(flag,'1')

    def prepare_front_camera(self, enabled, camera=None):
        if not self.fresh: raise ValueError('请先连接正在运行的会话')
        flag=self.directory/'camera-front.flag'
        if not enabled:
            flag.unlink(missing_ok=True)
            return
        manifest=json.loads((self.directory/'build-manifest.json').read_text(encoding='utf-8-sig'))
        if not manifest.get('cameraFront'): raise ValueError('当前构建不支持播放前相机归位，请选择新版构建')
        self.prepare_camera(camera,allow_active=True)
        self.camera([0]*7,1)
        atomic_text(flag,'1')

    def uid(self, value):
        if not self.fresh: raise ValueError('请先连接正在运行的会话')
        manifest=json.loads((self.directory/'build-manifest.json').read_text(encoding='utf-8-sig'))
        if not manifest.get('uidLiveText'): raise ValueError('请开始支持实时 UID 调整的新版会话')
        if value is not None: validate_uid_text(value)
        atomic_text(self.directory/'uid-command.txt','0' if value is None else '1 '+value)

    def actors(self, operation, source=''):
        if not self.fresh or not self.status.get('dynamicSession'):
            raise ValueError('请先连接动态会话')
        manifest=json.loads((self.directory/'build-manifest.json').read_text(encoding='utf-8-sig'))
        if not manifest.get('localActors'): raise ValueError('请选择支持本地演员的新版构建并开启新会话')
        if operation=='check' and not manifest.get('actorInspection'): raise ValueError('当前会话不支持演员检查，请使用新版构建')
        if operation in ('spawn-template','load-template') and not manifest.get('actorTemplateLoading'): raise ValueError('当前构建不支持直接加载，请选择 build-studio-7.2-sdk')
        if operation in ('spawn-template','load-template') and not source: raise ValueError('直接加载必须指定角色')
        if operation in ('hide-player','show-player','camera-move','camera-follow','camera-release') and not manifest.get('actorShooting'): raise ValueError('请切换到拍摄新版构建，并开启新会话')
        if operation.startswith('motion-') and not manifest.get('actorMotion'): raise ValueError('请使用支持演员动作的新版构建并开启新会话')
        if operation in ('motion-play','camera-move','camera-follow') and source not in ('1','2','3'): raise ValueError('请选择演员槽位 1–3')
        if operation not in ('spawn','spawn-template','load-template','clear','check','motion-play','motion-pause','motion-resume','motion-restore','hide-player','show-player','camera-move','camera-follow','camera-release'): raise ValueError('无效演员操作')
        if len(source)>=64 or any(not ('a'<=ch<='z' or 'A'<=ch<='Z' or '0'<=ch<='9' or ch=='_') for ch in source):
            raise ValueError('模型筛选只支持英文字母、数字和下划线')
        if operation in ('spawn','spawn-template') and self.status.get('mmdPhase') in (1,2,4):
            raise ValueError('请先恢复游戏动画，再生成演员')
        path=self.directory/'actors.command'
        previous=int(path.read_text().split()[0]) if path.exists() else 0
        status=self.actor_status()
        acknowledged=status.get('acknowledgedSequence',0) if status else 0
        if previous>acknowledged: raise ValueError('演员操作尚未完成，请稍候')
        if not 0<=previous<0xffffffff: raise ValueError('演员操作序号无效')
        sequence=previous+1
        atomic_text(path,f'{sequence} {operation}'+(' '+source if operation in ('spawn','spawn-template','load-template','motion-play','camera-move','camera-follow') and source else ''))
        return sequence

    def prepare_actor_motion(self, motion):
        status=self.actor_status() or {}
        if status.get('actorMotion',{}).get('status') in ('playing','paused','held','restore-failed'):
            raise ValueError('请先恢复演员姿态，再载入另一个动作')
        summary=run_tool([self.directory/'gimmd-vmd.exe',motion,'0'])
        reject_gaze_only_motion(summary)
        if not summary.get('boneTracks') or not 0<summary['durationSeconds']<=86400: raise ValueError('动作轨道或时长无效')
        temporary=self.directory/'actor-motion.tmp';shutil.copy2(motion,temporary);os.replace(temporary,self.directory/'actor-motion.vmd')

    def actor_status(self):
        for attempt in range(3):
            try:
                path=self.directory/'actors.status.json'
                stamp=path.stat().st_mtime
                value=json.loads(path.read_text(encoding='utf-8-sig'))
                if not self.fresh or not 0<=time.time()-stamp<3: return None
                return value if value.get('schemaVersion')==1 else None
            except (OSError,ValueError):
                if attempt<2: time.sleep(.01)
        return None

    def enqueue(self, operation: str, seconds=0):
        if not self.fresh:
            raise ValueError('当前没有可控制的活动会话')
        if operation not in ('play', 'pause', 'seek', 'loop-on', 'loop-off', 'stop','restore','exit','camera-on','camera-off'):
            raise ValueError('无效播放命令')
        if operation == 'seek' and (not math.isfinite(seconds) or seconds < 0 or seconds > min(86400, self.status['durationSeconds'])):
            raise ValueError('跳转时间超出动作时长')
        if operation in ('stop','restore','exit'):
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
