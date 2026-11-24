"""Bounded cached-model creation, optional menu-return check, and guaranteed session cleanup."""
import argparse
import json
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src' / 'gui'))
from session import Controller, stage_session, start_loader

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('pid',type=int)
parser.add_argument('--source',default='Odette')
parser.add_argument('--build',default='build-studio-7.2-sdk')
parser.add_argument('--load-only',action='store_true')
parser.add_argument('--template',action='store_true')
parser.add_argument('--wait-for-return',action='store_true')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
directory=stage_session(root/args.build,None,None,root/'sessions',dynamic_session=True)
print('Session: '+str(directory),flush=True)
loader=start_loader(directory,args.pid)
controller=Controller(directory)
retained_handles=0
def wait_for(predicate,seconds=20):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        controller.poll()
        result=predicate()
        if result:return result
        if loader.poll() is not None:raise RuntimeError('Loader exited: '+str(loader.returncode))
        time.sleep(.1)
    raise TimeoutError('Session did not acknowledge operation')
def actor_operation(operation,source=''):
    sequence=controller.actors(operation,source)
    return wait_for(lambda:snapshot if (snapshot:=controller.actor_status()) is not None and snapshot['acknowledgedSequence']>=sequence else None)

def save(name,value):
    (directory/name).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(name+': '+json.dumps(value,ensure_ascii=True),flush=True)

try:
    wait_for(lambda:controller.fresh)
    created=actor_operation('load-template' if args.load_only else ('spawn-template' if args.template else 'spawn'),args.source)
    save('cached-actor-result.json',created)
    assert created['status']==('actor-template-ready' if args.load_only else 'actor-created'),created
    retained_handles=created.get('sdkResourceHandles',0)
    if created['status']=='actor-created':
        save('cached-actor-health-before.json',actor_operation('check'))
        if args.wait_for_return:
            print('Waiting for return-to-scene.flag (maximum 300 seconds).',flush=True)
            wait_for(lambda:(directory/'return-to-scene.flag').exists(),300)
        health=actor_operation('check')
        save('cached-actor-health-after.json',health)
        assert health['status']=='actor-models-valid',health
    save('cached-actor-cleanup.json',actor_operation('clear'))
finally:
    controller.poll()
    if controller.fresh:
        controller.enqueue('exit');controller.pump()
    try:loader.wait(timeout=15)
    except Exception:print('Loader still active; retain session directory for cleanup.',flush=True)
    final=controller.actor_status()
    save('cached-actor-final.json',final)
    assert final and not final['exception'] and not final['orphanedObject'],final
    if retained_handles:
        assert final.get('sdkResourceHandles')==0 and final.get('sdkHandlesReleased',0)>=retained_handles,final
    assert loader.poll()==0,loader.returncode
