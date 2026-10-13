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
parser.add_argument('--build',type=Path,default=ROOT/'build-studio'); parser.add_argument('--camera',type=Path)
args=parser.parse_args()
build=args.build.resolve()
directory=stage_session(build,ROOT/'build-studio/multi-track.vmd',ROOT/'build-studio/multi-track.wav',ROOT/'sessions',camera=args.camera)
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
    wait(lambda s:s['state']==1 and s['seconds']>.6)
    seq=send('pause'); paused=wait(lambda s:s['state']==2 and s['acknowledgedSequence']==seq)
    time.sleep(.25); controller.poll()
    assert controller.status['seconds']==paused['seconds']
    seq=send('seek',1.5); wait(lambda s:s['state']==2 and s['seconds']==1.5 and s['acknowledgedSequence']==seq)
    seq=send('loop-on'); wait(lambda s:s['loop'] and s['acknowledgedSequence']==seq)
    seq=send('play'); wait(lambda s:s['state']==1 and s['acknowledgedSequence']==seq)
    seq=send('stop')
    if loader.wait(timeout=10)!=0: raise RuntimeError('Loader failed')
    controller.poll(); assert controller.status['closed'] and not controller.fresh
    report=json.loads((directory/f'gimmd-{args.pid}.probe.json').read_text(encoding='utf-8-sig'))
    p=report['singleTrackPlayback']; m=p['multiTrackPreview']
    assert p['localRestored'] and not p['cleanupPending'] and not p['exception']
    assert len(m['tracks'])==52 and all(t['restored'] and not t['cleanupPending'] for t in m['tracks'])
    assert m['rootMotion']['restored'] and p['animatorLease']['restored']
    assert report['hookDisabled'] and report['entryBytesRestored'] and report['messageHookRemoved'] and report['playerSession']['audioClosed']
    if args.camera:
        camera=report['cameraPlayback']
        assert camera['loaded'] and camera['acquired'] and camera['restored'] and not camera['cleanupPending']
        assert not camera['aborted'] and not camera['exception'] and camera['poseWrites']>20
        assert camera['maxWriteError']<.0001 and camera['restoreError']<.0001
        assert any(t['state']==2 and t['seconds']==1.5 for t in camera['timelineSamples'])
    result={'cameraPlayback':report.get('cameraPlayback'),'session':str(directory),'passed':True,'events':events,'finalStatus':controller.status,
            'probeReport':str(directory/f'gimmd-{args.pid}.probe.json'),
            'cameraSamples':[s['cameras'] for s in report['samples']]}
    atomic_json(ROOT/('docs/evidence/camera-playback-live-validation.json' if args.camera else 'docs/evidence/studio-live-validation.json'),result)
    print(json.dumps(result,ensure_ascii=True,indent=2))
finally:
    if loader.poll() is None:
        controller.poll()
        if controller.fresh:
            controller.enqueue('stop'); controller.pump()
