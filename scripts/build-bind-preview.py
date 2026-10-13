"""Build provisional MMD bindings from a validated observation; never writes to the game."""
import argparse
import json
from pathlib import Path


def definitions():
    entries = [('下半身', 'Bip001 Pelvis'), ('上半身', 'Bip001 Spine'),
               ('上半身2', 'Bip001 Spine1'), ('上半身3', 'Bip001 Spine2'),
               ('首', 'Bip001 Neck'), ('頭', 'Bip001 Head')]
    for side, letter in [('左', 'L'), ('右', 'R')]:
        for source, target in [('肩', 'Clavicle'), ('腕', 'UpperArm'), ('ひじ', 'Forearm'),
                               ('手首', 'Hand'), ('足', 'Thigh'), ('ひざ', 'Calf'),
                               ('足首', 'Foot'), ('つま先', 'Toe0')]:
            entries.append((side + source, f'Bip001 {letter} {target}'))
        for finger, label in enumerate(['親指', '人指', '中指', '薬指', '小指']):
            for joint in range(3):
                number = joint if finger == 0 else joint + 1
                source = side + label + chr(ord('０') + number)
                suffix = str(finger) + (str(joint) if joint else '')
                entries.append((source, f'Bip001 {letter} Finger{suffix}'))
    return entries


def preview(report):
    hierarchy = report.get('hierarchy', {})
    pose = report.get('poseRoundTrip', {})
    if report.get('settersCalled') and not all(pose.get(flag) for flag in ('published', 'localRestored', 'worldRestored')):
        raise ValueError('Pose writes were made without verified local/world restoration')
    if (report.get('engineException') != 0 or not report.get('hookDisabled') or
            not report.get('entryBytesRestored') or not hierarchy.get('profileVerified') or
            not hierarchy.get('published') or hierarchy.get('status') != 'hierarchy-read-observed'):
        raise ValueError('Require a complete, restored, exception-free hierarchy observation')
    nodes = hierarchy['nodes']
    by_id = {node['id']: node for node in nodes}
    if len(by_id) != len(nodes):
        raise ValueError('Duplicate node IDs')
    for node in nodes:
        if not all(node.get(flag) for flag in ('processed', 'nameValid', 'nameAliasesAgree', 'parentValidated', 'ownerValidated')):
            raise ValueError('Unvalidated hierarchy node')
        visited = set()
        current = node
        while current['parent'] != -1:
            if current['id'] in visited or current['parent'] not in by_id:
                raise ValueError('Cycle or missing parent')
            visited.add(current['id'])
            current = by_id[current['parent']]
    actors = [node for node in nodes if node['parent'] == 0 and node['name'].startswith('Avatar_')]
    candidates = []
    for actor in actors:
        descendants = []
        for node in nodes:
            current = node
            while current['parent'] != -1 and current['id'] != actor['id']:
                current = by_id[current['parent']]
            if current['id'] == actor['id']:
                descendants.append(node)
        bindings = []
        for mmd, target in definitions():
            matches = [node for node in descendants if node['name'] == target]
            bindings.append({'mmdName': mmd, 'mmdCp932Hex': mmd.encode('cp932').hex(), 'targetName': target,
                             'status': 'unique' if len(matches) == 1 else 'missing' if not matches else 'ambiguous',
                             'nodeId': matches[0]['id'] if len(matches) == 1 else None,
                             'rotationRelationVerified': len(matches) == 1 and matches[0].get('parentWorldTimesLocalMatchesWorld', False)})
        candidates.append({'actorNodeId': actor['id'], 'name': actor['name'], 'upstreamActive': actor['upstreamActive'],
                           'descendantCount': len(descendants), 'uniqueBindingCount': sum(entry['status'] == 'unique' for entry in bindings),
                           'bindings': bindings})
    active = [actor['actorNodeId'] for actor in candidates if actor['upstreamActive']]
    return {'schemaVersion': 1, 'mode': 'offline-bind-preview', 'executableSha256': report['executableSha256'],
            'snapshotPid': report['pid'], 'suggestedActorNodeId': active[0] if len(active) == 1 else None,
            'selectionBasis': 'unique upstream-active AvatarRoot child in this snapshot; rendering identity not proven',
            'writeEnabled': False, 'bindPoseCalibrated': False,
            'note': 'Node IDs expire with this snapshot. Observed idle rotations are not a bind pose. Spine allocation remains provisional.',
            'virtualChannels': ['全ての親', 'センター', 'グルーブ', '腰'],
            'unsupportedChannels': ['足IK', 'つま先IK', '腕捩', '手捩', '表情'], 'actors': candidates}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report')
    parser.add_argument('output')
    args = parser.parse_args()
    result = preview(json.loads(Path(args.report).read_text(encoding='utf-8-sig')))
    Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
