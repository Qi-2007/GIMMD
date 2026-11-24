"""Exercise the GUI's transport against the game without visual feedback."""
import argparse
import json
from pathlib import Path
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'src/gui'))
from session import Controller,atomic_json,stage_session,start_loader

parser=argparse.ArgumentParser(); parser.add_argument('--pid',type=int,required=True)
parser.add_argument('--build',type=Path,default=ROOT/'build-studio'); parser.add_argument('--camera',type=Path); parser.add_argument('--manual-camera',action='store_true')
parser.add_argument('--motion',type=Path,default=ROOT/'build-studio/multi-track.vmd')
parser.add_argument('--audio',type=Path,default=ROOT/'build-studio/multi-track.wav')
parser.add_argument('--evidence',type=Path)
parser.add_argument('--dynamic-session',action='store_true')
parser.add_argument('--expect-aux-drivers',action='store_true')
parser.add_argument('--visual-check',action='store_true')
parser.add_argument('--front-camera',action='store_true')
parser.add_argument('--expect-face',action='store_true')
parser.add_argument('--expect-face-isolated',action='store_true')
parser.add_argument('--visual-seconds',type=float,default=300)
parser.add_argument('--face-closeup',action='store_true')
parser.add_argument('--expect-reference-pose',action='store_true')
args=parser.parse_args()
if args.front_camera: args.manual_camera=True
build=args.build.resolve()
directory=stage_session(build,args.motion,args.audio,ROOT/'sessions',camera=args.camera,manual_camera=args.manual_camera,dynamic_session=args.dynamic_session)
print('Session: '+str(directory),flush=True)
loader=start_loader(directory,args.pid); controller=Controller(directory); events=[]
deadline=time.monotonic()+65
def wait(predicate):
    while time.monotonic()<deadline:
        status=controller.poll()
        if status and predicate(status): events.append(dict(status)); return status
        if loader.poll() is not None: raise RuntimeError('Loader ended before expected state; '+str(directory))
        time.sleep(.04)
    raise TimeoutError('Session transport timeout; '+str(directory))
def send(op,seconds=0):
    controller.enqueue(op,seconds); text=controller.pump()
    if not text: raise RuntimeError('Unacknowledged command blocked test')
    return controller.sent

try:
    if args.dynamic_session:
        wait(lambda s:s.get('dynamicSession') and not s['closed'])
        if args.front_camera: controller.prepare_front_camera(True)
        elif args.camera or args.manual_camera:
            controller.prepare_camera(args.camera if not args.manual_camera else None)
            seq=send('camera-on')
            wait(lambda s:s['acknowledgedSequence']==seq and s.get('cameraActive'))
        send('play')
    wait(lambda s:s['state']==1 and s['seconds']>.6)
    seq=send('pause'); paused=wait(lambda s:s['state']==2 and s['acknowledgedSequence']==seq)
    time.sleep(.25); controller.poll()
    assert controller.status['seconds']==paused['seconds']
    seq=send('seek',1.5); wait(lambda s:s['state']==2 and s['seconds']==1.5 and s['acknowledgedSequence']==seq)
    seq=send('loop-on'); wait(lambda s:s['loop'] and s['acknowledgedSequence']==seq)
    if args.camera or args.manual_camera:
        camera_seq=controller.camera([.2,.1,0,5,0,0,60])
        wait(lambda s:s.get('cameraAcknowledgedSequence')==camera_seq)
        time.sleep(.15)
        camera_seq=controller.camera([0,.3,.8,0,0,0,35] if args.face_closeup else [0]*7)
        wait(lambda s:s.get('cameraAcknowledgedSequence')==camera_seq)
    seq=send('play'); wait(lambda s:s['state']==1 and s['acknowledgedSequence']==seq)
    if args.visual_check:
        print(f'Visual check: looping until visual-check.done (maximum {min(300,max(1,args.visual_seconds)):g} seconds).',flush=True)
        visual_deadline=time.monotonic()+min(300,max(1,args.visual_seconds))
        while not (directory/'visual-check.done').exists() and time.monotonic()<visual_deadline:
            controller.poll()
            if loader.poll() is not None: raise RuntimeError('Loader ended during visual check')
            time.sleep(.1)
        deadline=time.monotonic()+20
    seq=send('stop')
    if args.dynamic_session:
        wait(lambda s:s.get('mmdPhase')==0 and s['acknowledgedSequence']==seq)
        send('exit')
    if loader.wait(timeout=10)!=0: raise RuntimeError('Loader failed')
    controller.poll(); assert controller.status['closed'] and not controller.fresh
    report=json.loads((directory/f'gimmd-{args.pid}.probe.json').read_text(encoding='utf-8-sig'))
    p=report['singleTrackPlayback']; m=p['multiTrackPreview']
    assert p['localRestored'] and not p['cleanupPending'] and not p['exception']
    assert len(m['tracks'])==52 and all(t['restored'] and not t['cleanupPending'] for t in m['tracks'])
    assert m['rootMotion']['restored'] and p['animatorLease']['restored']
    if args.expect_face:
        face=report['facialPlayback'][0]
        assert face['mappedTracks']>0 and face['writes']>0 and face['status']=='restored' and not face['cleanupPending'],face
    if args.expect_face_isolated:
        face=report['facialPlayback'][0]; lease=report['facialBindingLease'][0]
        assert face['writes']>face['channels'] and face['overwrites']==0,face
        assert lease['bindings']>0 and lease['bindings']==lease['restores'] and lease['pending']==0,lease
    if args.expect_reference_pose:
        reference=m['referencePose']
        assert reference['initialized'] and reference['bones']>=52,reference
        assert reference['writes']==reference['bones']==reference['restores'],reference
        assert reference['status']=='reference-pose-restored' and not reference['cleanupPending'],reference
    if args.expect_aux_drivers:
        drivers=report['auxiliaryDrivers']
        assert drivers['count']>0 and drivers['checks']>0,drivers
        assert drivers['status']=='drivers-restored' and drivers['restored']==drivers['count'],drivers
        assert not any(component['cleanupPending'] for component in drivers['components']),drivers
    assert report['hookDisabled'] and report['entryBytesRestored'] and report['messageHookRemoved'] and report['playerSession']['audioClosed']
    if args.camera or args.manual_camera:
        camera=report['cameraPlayback']
        assert camera['loaded'] and camera['restored'] and not camera['cleanupPending']
        # Dynamic sessions release acquisition after restoring, permitting reuse.
        assert not camera['acquired'] if args.dynamic_session else camera['acquired']
        assert not camera['aborted'] and not camera['exception'] and camera['poseWrites']>20
        assert camera['maxWriteError']<.0001 and camera['restoreError']<.0001
        assert camera['fovWritten'] and camera['maxFovWriteError']<.0001 and camera['fovRestoreError']<.0001
        assert camera['controlSequence']==(3 if args.front_camera else 2) and any(t['controlSequence']==(2 if args.front_camera else 1) and t['fov']==60 for t in camera['timelineSamples'])
        if args.front_camera: assert camera['frontState']==2
        assert camera['manualOnly']==args.manual_camera
        assert any(t['state']==2 and t['seconds']==1.5 for t in camera['timelineSamples'])
    result={'cameraPlayback':report.get('cameraPlayback'),'session':str(directory),'passed':True,'events':events,'finalStatus':controller.status,
            'probeReport':str(directory/f'gimmd-{args.pid}.probe.json'),
            'cameraSamples':[s['cameras'] for s in report['samples']]}
    atomic_json(args.evidence or ROOT/('docs/evidence/manual-camera-live-validation.json' if args.manual_camera else 'docs/evidence/camera-fov-live-validation.json' if args.camera else 'docs/evidence/studio-live-validation.json'),result)
    print(json.dumps(result,ensure_ascii=True,indent=2))
finally:
    if loader.poll() is None:
        controller.poll()
        if controller.fresh:
            controller.enqueue('exit' if args.dynamic_session else 'stop'); controller.pump()
