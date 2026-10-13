"""Recover target rest transforms from a verified, read-only skin binding snapshot."""
import argparse
import hashlib
import json
import math
import runpy
from pathlib import Path


def matrix(values):
    if len(values) != 16 or not all(math.isfinite(x) for x in values):
        raise ValueError('Invalid matrix')
    return [[values[col * 4 + row] for col in range(4)] for row in range(4)]


def multiply(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def inverse(a):
    rows = [list(row) + [float(r == c) for c in range(4)] for r, row in enumerate(a)]
    for col in range(4):
        pivot = max(range(col, 4), key=lambda r: abs(rows[r][col]))
        if abs(rows[pivot][col]) < 1e-10:
            raise ValueError('Singular binding matrix')
        rows[col], rows[pivot] = rows[pivot], rows[col]
        divisor = rows[col][col]
        rows[col] = [x / divisor for x in rows[col]]
        for row in range(4):
            if row != col:
                factor = rows[row][col]
                rows[row] = [x - factor * y for x, y in zip(rows[row], rows[col])]
    return [row[4:] for row in rows]


def residual(a, b):
    return max(abs(a[r][c] - b[r][c]) for r in range(4) for c in range(4))


IDENTITY = [[float(r == c) for c in range(4)] for r in range(4)]


def trs_matrix(record):
    x, y, z, w = record['rotation']; norm = math.sqrt(x*x+y*y+z*z+w*w)
    if not math.isfinite(norm) or abs(norm-1) > 0.001:
        raise ValueError('Invalid Avatar quaternion')
    x, y, z, w = [q/norm for q in (x,y,z,w)]
    a = [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w), record['position'][0]],
         [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w), record['position'][1]],
         [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y), record['position'][2]],
         [0,0,0,1]]
    for row in range(3):
        for col in range(3):
            a[row][col] *= record['scale'][col]
    return a


def avatar_reference(reference, nodes, mesh_matrices):
    if reference.get('status') != 'avatar-local-reference-observed':
        return {}, {}, {'status': reference.get('status', 'unavailable')}
    records = {r['index']: r for r in reference['localTrs']}
    links = {r['nodeId']: r['avatarIndex'] for r in reference['nodeLinks']}
    if len(records) != reference['boneCount'] or len(links.values()) != len(set(links.values())):
        raise ValueError('Avatar reference indices are ambiguous')
    world = {}; visiting = set()
    def compose(index):
        if index in visiting:
            raise ValueError('Avatar reference hierarchy is cyclic')
        if index not in world:
            visiting.add(index); record = records[index]; local = trs_matrix(record)
            world[index] = multiply(compose(record['parentIndex']), local) if record['parentIndex'] >= 0 else local
            visiting.remove(index)
        return world[index]
    for index in records:
        compose(index)
    common = set(mesh_matrices) & set(links)
    if len(common) < 3:
        raise ValueError('Insufficient mesh references for Avatar validation')
    first = min(common); alignment = multiply(mesh_matrices[first], inverse(world[links[first]]))
    error = max(residual(mesh_matrices[n], multiply(alignment,world[links[n]])) for n in common)
    if error > 0.0002:
        raise ValueError(f'Avatar and mesh reference poses disagree: residual={error}')
    locals_by_node = {}
    for node, index in links.items():
        parent = nodes[node]['parent']; record = records[index]
        if parent in links and links[parent] != record['parentIndex']:
            raise ValueError('Avatar/Transform parent mismatch')
        if parent in links or record['parentIndex'] == -1:
            locals_by_node[node] = {key: record[key] for key in ('position','rotation','scale')}
    matrices = {node: multiply(alignment,world[index]) for node,index in links.items()}
    return matrices, locals_by_node, {'status': 'verified-against-mesh-bindposes', 'boneCount': len(records),
        'linkedNodeCount': len(links), 'sharedMeshBoneCount': len(common), 'maxMeshAgreementError': error,
        'negativeScaleRecordCount': sum(any(s < 0 for s in r['scale']) for r in records.values())}


def decompose(a):
    scale = [math.sqrt(sum(a[r][c] ** 2 for r in range(3))) for c in range(3)]
    if min(scale) < 1e-8:
        raise ValueError('Zero scale')
    rot = [[a[r][c] / scale[c] for c in range(3)] for r in range(3)]
    orthogonal_error = max(abs(sum(rot[k][r] * rot[k][c] for k in range(3)) - float(r == c)) for r in range(3) for c in range(3))
    det = (rot[0][0] * (rot[1][1]*rot[2][2]-rot[1][2]*rot[2][1])
           - rot[0][1] * (rot[1][0]*rot[2][2]-rot[1][2]*rot[2][0])
           + rot[0][2] * (rot[1][0]*rot[2][1]-rot[1][1]*rot[2][0]))
    if orthogonal_error > 0.0002 or abs(det - 1) > 0.0002:
        raise ValueError('Sheared or reflected binding transform')
    trace = sum(rot[i][i] for i in range(3))
    if trace > 0:
        s = math.sqrt(trace + 1) * 2
        q = [(rot[2][1]-rot[1][2])/s, (rot[0][2]-rot[2][0])/s, (rot[1][0]-rot[0][1])/s, s/4]
    else:
        i = max(range(3), key=lambda n: rot[n][n]); j = (i+1) % 3; k = (i+2) % 3
        s = math.sqrt(1+rot[i][i]-rot[j][j]-rot[k][k])*2
        q = [0.0] * 4; q[i] = s/4; q[j] = (rot[j][i]+rot[i][j])/s
        q[k] = (rot[k][i]+rot[i][k])/s; q[3] = (rot[k][j]-rot[j][k])/s
    norm = math.sqrt(sum(x*x for x in q)); q = [x/norm for x in q]
    if q[3] < 0:
        q = [-x for x in q]
    return {'position': [a[r][3] for r in range(3)], 'rotation': q, 'scale': scale, 'orthogonalError': orthogonal_error}


def recover(path):
    report = json.loads(path.read_text(encoding='utf-8-sig'))
    renderer = report['animatorObserver']['bindPose']['renderer']
    if (report['settersCalled'] or not report['buildMatch'] or report['engineException']
            or not report['hookDisabled'] or not report['entryBytesRestored'] or report['activeCallbacksAtReport']
            or renderer['status'] != 'bindpose-matrices-observed'):
        raise ValueError('Snapshot did not pass read-only runtime admission')
    ids = renderer['boneNodeIds']; values = renderer['inverseBindMatricesColumnMajor']
    if not ids or len(ids) != len(values) or len(ids) != renderer['bindPoseCount'] or len(ids) != len(set(ids)):
        raise ValueError('Bone/matrix correspondence invalid')
    nodes = {n['id']: n for n in report['hierarchy']['nodes']}
    poses = {}; matrices = {}; max_inverse_error = 0; skin_alignment = []
    for node, raw in zip(ids, values):
        bind = matrix(raw); rest = inverse(bind); matrices[node] = rest
        max_inverse_error = max(max_inverse_error, residual(multiply(bind, rest), IDENTITY))
    for skin in report['animatorObserver']['bindPose'].get('additionalSkins', []):
        audit = {'name': skin['name'], 'status': skin['status'], 'mergedBones': 0}
        skin_alignment.append(audit)
        if skin['status'] != 'bindpose-matrices-observed':
            continue
        extra_ids = skin['boneNodeIds']; raw_extra = skin['inverseBindMatricesColumnMajor']
        if len(extra_ids) != len(set(extra_ids)) or len(extra_ids) != len(raw_extra) or len(extra_ids) != skin['boneCount']:
            raise ValueError('Additional skin correspondence invalid')
        extras = {node: inverse(matrix(raw)) for node, raw in zip(extra_ids, raw_extra)}
        common = set(extras) & set(matrices); audit['sharedBones'] = len(common)
        if len(common) < 3:
            audit['mergeStatus'] = 'insufficient-shared-reference-bones'; continue
        reference = min(common); alignment = multiply(matrices[reference], inverse(extras[reference]))
        error = max(residual(matrices[node], multiply(alignment, extras[node])) for node in common)
        audit['alignmentResidual'] = error
        if error > 0.0002:
            raise ValueError('Skin reference poses disagree: ' + skin['name'])
        for node, rest in extras.items():
            if node not in matrices:
                matrices[node] = multiply(alignment, rest); audit['mergedBones'] += 1
        audit['mergeStatus'] = 'aligned-by-shared-rest-matrices'
    avatar_matrices, avatar_locals, avatar_audit = avatar_reference(
        report['animatorObserver']['bindPose'].get('avatarReference', {}), nodes, matrices)
    for node, rest in avatar_matrices.items():
        matrices.setdefault(node, rest)
    for node, rest in matrices.items():
        try:
            mesh_space = decompose(rest)
        except ValueError:
            if node not in avatar_matrices:
                raise
            mesh_space = None  # Signed scales are preserved by native local TRS and the full matrix.
        poses[node] = {'nodeId': node, 'name': nodes[node]['name'], 'parentNodeId': nodes[node]['parent'], 'meshSpace': mesh_space,
                       'meshSpaceMatrixColumnMajor': [rest[r][c] for c in range(4) for r in range(4)]}
    for node, pose in poses.items():
        parent = pose['parentNodeId']
        if node in avatar_locals:
            pose['local'] = avatar_locals[node]; pose['localStatus'] = 'avatar-reference-verified-against-skin'
        else:
            pose['local'] = decompose(multiply(inverse(matrices[parent]), matrices[node])) if parent in matrices else None
            pose['localStatus'] = 'recovered-from-parent-bind-matrix' if parent in matrices else 'parent-not-in-skin-array'
    definitions = runpy.run_path(str(Path(__file__).with_name('build-bind-preview.py')))['definitions']()
    mappings = []
    for source, target in definitions:
        candidates = [p for p in poses.values() if p['name'] == target]
        pose = candidates[0] if len(candidates) == 1 else None
        mappings.append({'source': source, 'target': target, 'targetRestAvailable': pose is not None,
                         'targetLocalRestAvailable': bool(pose and pose['local']), 'nodeId': pose['nodeId'] if pose else None})
    return {'schemaVersion': 1, 'mode': 'offline-target-rest-recovery', 'executableSha256': report['executableSha256'],
            'runtimeSnapshot': str(path.resolve()), 'snapshotSha256': hashlib.sha256(path.read_bytes()).hexdigest().upper(),
            'space': 'renderer-mesh', 'source': 'Mesh.bindposes inverse; custom Avatar local reference admitted only after mesh agreement',
            'usedLiveAnimationAsRest': False, 'sourceSkeletonCalibrated': False, 'runtimeRetargetingApplied': False,
            'boneCount': len(poses), 'mappedTargetRestCount': sum(m['targetRestAvailable'] for m in mappings),
            'mappedTargetLocalRestCount': sum(m['targetLocalRestAvailable'] for m in mappings),
            'maxInverseProductError': max_inverse_error, 'skinAlignment': skin_alignment, 'avatarReference': avatar_audit,
            'mappings': mappings, 'bones': list(poses.values())}


def self_test():
    a = [[0, -3, 0, 5], [2, 0, 0, -7], [0, 0, 4, 11], [0, 0, 0, 1]]
    assert residual(multiply(a, inverse(a)), IDENTITY) < 1e-12
    result = decompose(a)
    assert result['position'] == [5, -7, 11] and result['scale'] == [2, 3, 4]
    assert abs(result['rotation'][2] - math.sqrt(0.5)) < 1e-12
    child = [[1, 0, 0, 1], [0, 1, 0, 2], [0, 0, 1, 3], [0, 0, 0, 1]]
    assert residual(multiply(inverse(a), multiply(a, child)), child) < 1e-12
    reflected = [row[:] for row in IDENTITY]; reflected[0][0] = -1
    try:
        decompose(reflected)
    except ValueError:
        pass
    else:
        raise AssertionError('Reflected pose accepted')
    signed = trs_matrix({'rotation':[0,0,1,0], 'position':[0,0,0], 'scale':[-1,-1,-1]})
    assert signed[0][0] == 1 and signed[1][1] == 1 and signed[2][2] == -1
    assert residual(multiply(signed,signed),IDENTITY) < 1e-12
    records = [{'index':i,'parentIndex':i-1,'position':[1,0,0], 'rotation':[0,0,0,1], 'scale':[1,1,1]} for i in range(3)]
    reference = {'status':'avatar-local-reference-observed','boneCount':3,'localTrs':records,
                 'nodeLinks':[{'nodeId':i,'avatarIndex':i} for i in range(3)]}
    node_records = {i:{'parent':i-1} for i in range(3)}
    mesh = {i:trs_matrix({'position':[i+1,0,0],'rotation':[0,0,0,1],'scale':[1,1,1]}) for i in range(3)}
    assert avatar_reference(reference,node_records,mesh)[2]['maxMeshAgreementError'] == 0
    mesh[2][0][3] += 0.01
    try:
        avatar_reference(reference,node_records,mesh)
    except ValueError:
        pass
    else:
        raise AssertionError('Mismatched Avatar and mesh poses accepted')
    print('7 rest-pose math and admission checks passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path, nargs='?'); parser.add_argument('output', type=Path, nargs='?')
    parser.add_argument('--self-test', action='store_true'); args = parser.parse_args()
    if args.self_test:
        self_test()
    if args.snapshot and args.output:
        result = recover(args.snapshot)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        print(json.dumps({key: result[key] for key in ['boneCount', 'mappedTargetRestCount', 'mappedTargetLocalRestCount', 'maxInverseProductError']}))
    elif not args.self_test:
        parser.error('Supply snapshot and output')
