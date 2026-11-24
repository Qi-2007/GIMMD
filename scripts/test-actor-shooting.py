"""Live visibility, actor camera, and exit restoration checks."""
import argparse
import json
from pathlib import Path
import sys
import time

root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'src/gui'))
from session import Controller,stage_session,start_loader

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('pid',type=int)
a=p.parse_args()
d=stage_session(root/'build-studio-7.2-shooting',None,None,root/'sessions',dynamic_session=True,manual_camera=True)
print('Session:',d,flush=True)
c=Controller(d);loader=start_loader(d,a.pid);reports={}
def wait(predicate):
    end=time.monotonic()+20
    while time.monotonic()<end:
        c.poll();result=predicate()
        if result:return result
        if loader.poll() is not None:raise RuntimeError('Loader exited')
        time.sleep(.1)
    raise TimeoutError(str(c.actor_status()))
def op(name,source=''):
    seq=c.actors(name,source)
    result=wait(lambda:s if (s:=c.actor_status()) and s['acknowledgedSequence']>=seq else None)
    reports[name]=result
    return result
try:
    wait(lambda:c.fresh)
    assert op('spawn-template','Nahida')['status']=='actor-created'
    assert op('camera-follow','1')['status']=='actor-camera-needs-manual'
    assert op('hide-player')['status']=='player-hidden'
    assert reports['hide-player']['hiddenRenderers']>0
    assert op('show-player')['hiddenRenderers']==0
    c.prepare_camera(None);c.enqueue('camera-on');c.pump()
    wait(lambda:c.status.get('cameraActive'))
    time.sleep(.3)
    assert op('camera-move','1')['status']=='actor-camera-moved'
    assert op('camera-follow','1')['status']=='actor-camera-follow'
    assert op('camera-follow','3')['status']=='actor-camera-target-missing'
    assert op('camera-release')['status']=='actor-camera-released'
    wait(lambda:not c.status.get('cameraActive'))
    c.prepare_camera(None);c.enqueue('camera-on');c.pump()
    wait(lambda:c.status.get('cameraActive'));time.sleep(.3)
    assert op('camera-follow','1')['status']=='actor-camera-follow'
    op('clear');wait(lambda:not c.status.get('cameraActive'))
    assert op('hide-player')['hiddenRenderers']>0
finally:
    c.poll()
    if c.fresh:c.enqueue('exit');c.pump()
    loader.wait(timeout=20)
    reports['final']=c.actor_status()
    (d/'actor-shooting-test.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')
    final=reports['final']
    assert loader.returncode==0 and final['hiddenRenderers']==0 and final['sdkResourceHandles']==0 and final['exception']==0,final
    probe=json.loads((d/f'gimmd-{a.pid}.probe.json').read_text(encoding='utf-8'))
    assert probe['cameraPlayback']['restored'] and not probe['cameraPlayback']['cleanupPending']
    assert probe['engineException']==0 and probe['entryBytesRestored'] and probe['messageHookRemoved']
print('Actor shooting and restoration checks passed.',flush=True)
