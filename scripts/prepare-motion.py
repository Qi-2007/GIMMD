"""Inspect local MMD assets and stage a ten-bone preview or canonical full-body FK session."""
import argparse
import hashlib
import json
import runpy
import shutil
import subprocess
import wave
from pathlib import Path


def inspect(folder, inspector, choreography='', motion_author='', author_notified=False):
    folder = Path(folder).resolve()
    motions = sorted(folder.glob('*.vmd'))
    if len(motions) != 1:
        raise ValueError('Select a folder with exactly one VMD for this preview')
    motion = motions[0]
    parsed = json.loads(subprocess.check_output([str(Path(inspector).resolve()), str(motion), '0']))
    definitions = runpy.run_path(str(Path(__file__).with_name('build-bind-preview.py')))['definitions']()
    selected = {'Bip001 L UpperArm', 'Bip001 R UpperArm', 'Bip001 L Forearm', 'Bip001 R Forearm',
                'Bip001 L Hand', 'Bip001 R Hand', 'Bip001 Neck', 'Bip001 Head', 'Bip001 L Finger1', 'Bip001 R Finger1'}
    tracks = {item['nameCp932Hex'] for item in parsed['bones']}
    mappings = [{'mmdName': name, 'cp932Hex': name.encode('cp932').hex(), 'target': target,
                 'sourcePresent': name.encode('cp932').hex() in tracks, 'previewTarget': target in selected}
                for name, target in definitions]
    assets = []
    for path in sorted(folder.iterdir()):
        if path.is_file():
            assets.append({'path': str(path), 'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest().upper()})
    audio = []
    for path in sorted(folder.glob('*.wav')):
        with wave.open(str(path), 'rb') as stream:
            audio.append({'path': str(path), 'seconds': stream.getnframes() / stream.getframerate(),
                          'channels': stream.getnchannels(), 'sampleRate': stream.getframerate(),
                          'sampleBytes': stream.getsampwidth()})
    return {'schemaVersion': 1, 'mode': 'offline-motion-admission', 'motionPath': str(motion),
            'summary': {key: parsed[key] for key in ['durationSeconds', 'boneTracks', 'boneKeys', 'morphTracks', 'cameraKeys', 'ikTracks']},
            'exactMappedTracks': sum(item['sourcePresent'] for item in mappings),
            'previewSourceReady': all(item['sourcePresent'] for item in mappings if item['previewTarget']),
            'preview': {'durationSeconds': 2, 'maxRelativeAngleDegrees': 5, 'targets': 10, 'sourceReferenceFrame': 0,
                        'positions': False, 'morphs': False, 'camera': False, 'audioPlayback': False, 'bindPoseCalibrated': False},
            'credits': {'choreography': choreography, 'motion': motion_author, 'authorNotifiedForGameImport': author_notified},
            'assets': assets, 'audio': audio, 'mappings': mappings}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    parser.add_argument('inspector', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--stage', type=Path)
    parser.add_argument('--choreography', default='')
    parser.add_argument('--motion-author', default='')
    parser.add_argument('--author-notified', action='store_true')
    parser.add_argument('--player-session', action='store_true', help='Stage one WAV for the controlled full-source preview')
    parser.add_argument('--full-body-fk', action='store_true', help='Admit canonical full-body FK with target rest fallback for absent tracks')
    parser.add_argument('--root-motion', action='store_true', help='Enable first-frame-anchored visual root translation')
    parser.add_argument('--leg-ik', action='store_true', help='Enable independent two-bone foot IK and ankle orientation')
    args = parser.parse_args()
    result = inspect(args.folder, args.inspector, args.choreography, args.motion_author, args.author_notified)
    if args.root_motion and not args.full_body_fk:
        raise SystemExit('--root-motion requires --full-body-fk')
    if args.leg_ik and not args.root_motion:
        raise SystemExit('--leg-ik requires --root-motion')
    if args.leg_ik:
        result['preview'].update({'legIk':True,'legIkSolver':'independent two-bone','toeIk':False,
            'footPositionReference':'common root frame zero origin; retain initial foot stance',
            'flatSupportReachCompensation':True,'terrainRaycast':False,'unreachableTargets':'bounded visual root lowering, then clamp without stretching'})
    if args.full_body_fk:
        mappings = result['mappings']
        for item in mappings:
            item['previewTarget'] = True
        result['previewSourceReady'] = result['exactMappedTracks'] >= 16 and all(mappings[i]['sourcePresent'] for i in (0,1,7,8,30,31))
        result['preview'].update({'targets':52, 'maxRelativeAngleDegrees':None, 'sourceReferenceFrame':None,
            'rotationMode':'absolute-canonical-FK', 'sourcePreset':'EIEM canonical Tda approximation',
            'sourcePmxCalibrated':False, 'targetRestProfile':'src/generated/fk_profile.h', 'entryBlendSeconds':0.5})
        if args.root_motion:
            result['preview'].update({'positions':True, 'positionScope':'visual skeleton Bip001 only',
                'rootControls':['全ての親','センター','グルーブ'], 'rootRotationsSupported':False,
                'positionReferenceFrame':0, 'positionUnits':'target/source reference leg-length ratio'})
    if args.player_session:
        if not args.stage or len(result['audio']) != 1:
            raise SystemExit('--player-session requires --stage and exactly one WAV file')
        audio = result['audio'][0]
        if audio['sampleBytes'] != 2 or audio['channels'] not in (1, 2) or not 8000 <= audio['sampleRate'] <= 192000:
            raise SystemExit('Player audio requires mono/stereo 16-bit PCM, 8-192 kHz')
        result['preview']['durationSeconds'] = result['summary']['durationSeconds']
        result['preview']['wallTimeLimitSeconds'] = 90
        result['preview']['audioBackend'] = 'WinMM PCM'
        result['preview']['audioPlayback'] = False  # Admission is not a playback measurement.
    if args.stage:
        if not result['previewSourceReady']:
            raise SystemExit('Missing one or more required preview tracks')
        args.stage.mkdir(parents=True, exist_ok=True)
        target = args.stage / 'multi-track.vmd'
        if target.resolve() == Path(result['motionPath']).resolve():
            raise SystemExit('Refuse to replace the original source asset')
        shutil.copy2(result['motionPath'], target)
        result['stagedPath'] = str(target.resolve())
        if args.player_session:
            audio_target = args.stage / 'multi-track.wav'
            audio_source = Path(result['audio'][0]['path'])
            if audio_target.resolve() == audio_source.resolve():
                raise SystemExit('Refuse to replace the original audio asset')
            shutil.copy2(audio_source, audio_target)
            result['stagedAudioPath'] = str(audio_target.resolve())
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'mappedTracks': result['exactMappedTracks'], 'previewReady': result['previewSourceReady'],
                      'durationSeconds': result['summary']['durationSeconds']}, ensure_ascii=True))
