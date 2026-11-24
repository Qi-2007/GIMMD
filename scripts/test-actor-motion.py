"""Bounded live actor playback, pause, resume, hold and restoration check."""
import argparse
import json
import sys
import time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/gui'))
from session import Controller,stage_session,start_loader

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('pid',type=int);p.add_argument('--source',default='Nahida')
p.add_argument('--build',default='build-studio-7.2-sdk');p.add_argument('--motion',type=Path);p.add_argument('--expect-root',action='store_true');p.add_argument('--expect-helper',action='store_true')
p.add_argument('--scene-source',action='store_true');p.add_argument('--expect-face',action='store_true')
a=p.parse_args();root=Path(__file__).resolve().parents[1]
d=stage_session(root/a.build,None,None,root/'sessions',dynamic_session=True)
print('Session:',d,flush=True);loader=start_loader(d,a.pid);controller=Controller(d)
reports={}
def wait(predicate,seconds=20):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        controller.poll()
        result=predicate()
        if result:return result
        if loader.poll() is not None:raise RuntimeError('Loader exited')
        time.sleep(.05)
    raise TimeoutError('Operation timeout: '+str(controller.actor_status()))
def operation(op,source=''):
    seq=controller.actors(op,source)
    return wait(lambda:s if (s:=controller.actor_status()) is not None and s['acknowledgedSequence']>=seq else None)
try:
    wait(lambda:controller.fresh)
    reports['created']=operation('spawn' if a.scene_source else 'spawn-template',a.source)
    assert reports['created']['status']=='actor-created',reports['created']
    controller.prepare_actor_motion(a.motion or root/a.build/'actor-calibration.vmd')
    operation('motion-play','1')
    wait(lambda:(controller.actor_status() or {}).get('actorMotion',{}).get('writes',0)>0)
    time.sleep(.3)
    reports['paused']=operation('motion-pause')
    assert reports['paused']['actorMotion']['status']=='paused',reports['paused']
    saved=reports['paused']['actorMotion']['seconds'];time.sleep(.3)
    assert abs(wait(controller.actor_status)['actorMotion']['seconds']-saved)<.05
    reports['resumed']=operation('motion-resume')
    reports['held']=wait(lambda:s if (s:=controller.actor_status()) and s.get('actorMotion',{}).get('status')=='held' else None,max(8,min(300,(controller.actor_status() or {}).get('actorMotion',{}).get('duration',0)+3)))
    if a.expect_root:
        assert reports['held']['actorMotion']['rootWrites']>0
        assert reports['held']['actorMotion']['maximumRootOffset']>.00001
    if a.expect_helper:
        assert reports['held']['actorMotion']['pelvisHelpers']>0
        assert reports['held']['actorMotion']['pelvisHelperWrites']>0
    reports['restored']=operation('motion-restore')
    assert reports['restored']['actorMotion']['status']=='restored',reports['restored']
    reports['health']=operation('check')
    assert reports['health']['status']=='actor-models-valid',reports['health']
    operation('motion-play','1')
    wait(lambda:(controller.actor_status() or {}).get('actorMotion',{}).get('writes',0)>0)
    reports['clearDuringPlayback']=operation('clear')
    assert not any(s['created'] for s in reports['clearDuringPlayback']['slots'])
    assert reports['clearDuringPlayback']['actorMotion']['status'] in ('restored','idle')
finally:
    controller.poll()
    if controller.fresh:
        reports['cleanup']=operation('clear')
        controller.enqueue('exit');controller.pump()
    loader.wait(timeout=15)
    reports['final']=controller.actor_status()
    (d/'actor-motion-test.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    final=reports['final']
    assert loader.returncode==0,loader.returncode
    assert final and not final['exception'] and not final['orphanedObject'],final
    handles=reports.get('created',{}).get('sdkResourceHandles',0)
    if handles:
        assert final.get('sdkResourceHandles')==0 and final.get('sdkHandlesReleased',0)>=handles,final
if a.expect_face:
    report=json.loads((d/f'gimmd-{a.pid}.probe.json').read_text(encoding='utf-8'))
    face=report['facialPlayback'][1]
    assert face['mappedTracks']>0 and face['writes']>0 and face['status']=='restored' and not face['cleanupPending'],face
print('Actor motion lifecycle checks passed.',flush=True)
