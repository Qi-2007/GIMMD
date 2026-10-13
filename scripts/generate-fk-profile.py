"""Bake target rest axes and EIEM canonical source stance into a build-specific FK profile."""
import hashlib
import json
import math
import runpy
from pathlib import Path

root = Path(__file__).resolve().parent.parent
rest_path = root / 'docs/evidence/playerboy-target-rest-pose.json'
rest = json.loads(rest_path.read_text(encoding='utf-8'))
snapshot = json.loads(Path(rest['runtimeSnapshot']).read_text(encoding='utf-8'))
if rest['mappedTargetLocalRestCount'] != 52 or rest['avatarReference']['status'] != 'verified-against-mesh-bindposes':
    raise SystemExit('Verified target reference required')
definitions = runpy.run_path(str(root / 'scripts/build-bind-preview.py'))['definitions']()
bones = {b['name']: b for b in rest['bones']}
nodes = {b['nodeId']: b for b in rest['bones']}
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def cross(a,b): return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def unit(a):
    length = math.sqrt(dot(a,a))
    if length < 1e-8: raise ValueError('Degenerate reference direction')
    return [x/length for x in a]
def trans(a): return [list(row) for row in zip(*a)]
def mul(a,b): return [[dot(row,col) for col in zip(*b)] for row in a]
def position(name):
    m = bones[name]['meshSpaceMatrixColumnMajor']; return m[12:15]
def sub(a,b): return [x-y for x,y in zip(a,b)]
def basis(forward,lateral):
    x = unit(forward); y = unit(sub(lateral,[dot(lateral,x)*v for v in x])); z = cross(x,y)
    return trans([x,y,z])
def from_to(a,b):
    a,b = unit(a),unit(b); v = cross(a,b); w = 1+dot(a,b)
    if w < 1e-7:
        v = unit(cross(a,[1,0,0] if abs(a[0]) < 0.8 else [0,1,0])); w = 0
    norm = math.sqrt(dot(v,v)+w*w); x,y,z = [q/norm for q in v]; w /= norm
    return [[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
            [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
            [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
identity = [[1,0,0],[0,1,0],[0,0,1]]
convert = [[-1,0,0],[0,1,0],[0,0,-1]]  # EIEM SourceToGameBasis: Y 180 degrees.
def converted(v): return [dot(r,v) for r in convert]
directions = {
    'Bip001 Spine':([0,.999,.044],'Bip001 Spine1'), 'Bip001 Spine1':([0,.999,.044],'Bip001 Neck'),
    'Bip001 Neck':([0,1,-.0099],'Bip001 Head')}
hand_alignments = {}
for side,sign in [('L',1),('R',-1)]:
    prefix = f'Bip001 {side} '
    directions[prefix+'Clavicle'] = ([sign*.9687,-.248,.0138],prefix+'UpperArm')
    directions[prefix+'UpperArm'] = ([sign*.7941,-.6076,.012],prefix+'Forearm')
    directions[prefix+'Forearm'] = ([sign*.7962,-.6047,-.0182],prefix+'Hand')
    forward = [sign*.721561,-.53866,-.0537414]
    lateral = [sign*.107932,-.03325,-.5565567]
    target_forward = sub(position(prefix+'Finger2'),position(prefix+'Hand'))
    target_lateral = sub(position(prefix+'Finger1'),position(prefix+'Finger4'))
    hand_alignments[side] = mul(basis(target_forward,target_lateral),trans(basis(converted(forward),converted(lateral))))

def number(x):
    text = format(x,'.9g')
    return text + ('.0f' if '.' not in text and 'e' not in text else 'f')
def array(xs): return '{'+','.join(number(x) for x in xs)+'}'
lines = ['#pragma once','#include "core/fk_retarget.h"','namespace gimmd::fk_profile {',
         '// EIEM canonical Tda source preset approximation; no source PMX was supplied.',
         'inline constexpr wchar_t ModelName[] = L"Avatar_Boy_Sword_PlayerBoy";',
         'inline constexpr char ReferenceSha256[] = "'+hashlib.sha256(rest_path.read_bytes()).hexdigest().upper()+'";',
         'inline constexpr fk_retarget::Binding Bindings[] = {']
audit = []
for source,target in definitions:
    bone = bones[target]; parent = nodes[bone['parentNodeId']]
    raw = parent['meshSpaceMatrixColumnMajor']; columns = [raw[i*4:i*4+3] for i in range(3)]
    frame = basis(columns[0],columns[1]); sign = 1 if dot([r[2] for r in frame],columns[2]) > 0 else -1
    for row in frame: row[2] *= sign
    alignment = identity; kind = 'canonical-basis'
    if target in directions:
        direction,child = directions[target]
        alignment = from_to(converted(direction),sub(position(child),position(target))); kind = 'canonical-limb-direction'
    elif ' Hand' in target or ' Finger' in target:
        side = 'L' if ' L ' in target else 'R'; alignment = hand_alignments[side]; kind = 'canonical-wrist-frame'
    axes = mul(trans(frame),mul(alignment,convert))
    axes = [[sign*x for x in row] for row in axes]  # Pseudovector transport through signed parent frame.
    error = max(abs(dot(a,b)-float(i==j)) for i,a in enumerate(axes) for j,b in enumerate(axes))
    if error > 1e-6: raise ValueError('FK axes not orthonormal')
    local = bone['local']
    lines.append('    {'+array(local['rotation'])+','+array([x for row in axes for x in row])+'},')
    audit.append({'source':source,'target':target,'sourcePresetAlignment':kind,'signedParentFrame':sign < 0,'axesOrthogonalityError':error})
lines += ['};','struct Reference { int parent; float position[3], rotation[4], scale[3]; };','inline constexpr Reference AvatarReference[] = {']
for item in snapshot['animatorObserver']['bindPose']['avatarReference']['localTrs']:
    lines.append('    {'+str(item['parentIndex'])+','+array(item['position'])+','+array(item['rotation'])+','+array(item['scale'])+'},')
target_leg = sum(math.dist(position('Bip001 '+side+' '+a),position('Bip001 '+side+' '+b))
                 for side in ('L','R') for a,b in (('Thigh','Calf'),('Calf','Foot')))/2
lines += ['};','inline constexpr float RootMotionScale = '+number(target_leg/10.62420198)+';',
          'inline constexpr const char* RootTrackNames[] = {']
for name in ('全ての親','センター','グルーブ'):
    lines.append('    "'+''.join('\\x%02x'%b for b in name.encode('cp932'))+'",')
links = {item['nodeId']: item['avatarIndex'] for item in snapshot['animatorObserver']['bindPose']['avatarReference']['nodeLinks']}
lines += ['};','inline constexpr unsigned AvatarIndices[] = {'+','.join(str(links[bones[target]['nodeId']]) for _,target in definitions)+'};',
          'inline constexpr const char* FootTrackNames[2][2] = {']
for side in ('左','右'):
    names = [side+'足ＩＫ',side+'足IK']
    lines.append('    {'+','.join('"'+''.join('\\x%02x'%b for b in name.encode('cp932'))+'"' for name in names)+'},')
lines += ['};','}']
(root/'src/generated/fk_profile.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
(root/'docs/evidence/fk-binding-profile.json').write_text(json.dumps({'sourcePreset':'EIEM canonical Tda approximation',
    'sourcePmxCalibrated':False,'targetReferenceSha256':hashlib.sha256(rest_path.read_bytes()).hexdigest().upper(),'bindings':audit},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Generated 52 FK bindings and 139 reference records')
