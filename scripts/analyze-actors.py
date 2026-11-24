"""Summarize a bounded AvatarRoot snapshot without treating inactive models as actors."""
import argparse
import json
from pathlib import Path


def summarize(report):
    hierarchy = report.get('hierarchy', {})
    nodes = hierarchy.get('nodes', [])
    by_id = {node['id']: node for node in nodes}
    if len(by_id) != len(nodes):
        raise ValueError('duplicate node IDs')
    children = {}
    for node in nodes:
        children.setdefault(node['parent'], []).append(node)
    complete = (report.get('buildMatch') is True and hierarchy.get('published') is True
                and hierarchy.get('status') == 'hierarchy-read-observed'
                and not hierarchy.get('truncated') and not hierarchy.get('timedOut'))
    actors = []
    for root in children.get(0, []):
        if not root.get('name', '').startswith('Avatar_'):
            continue
        subtree, pending, seen = [], [root], set()
        while pending:
            node = pending.pop()
            if node['id'] in seen:
                raise ValueError('hierarchy cycle')
            seen.add(node['id'])
            subtree.append(node)
            pending.extend(children.get(node['id'], []))
        models = [node for node in subtree if node.get('name', '').startswith('Avatar_')]
        hips = [node['id'] for node in subtree if node.get('name') == 'Bip001 Pelvis']
        actors.append({'snapshotNode': root['id'], 'name': root['name'],
                       'upstreamActive': root.get('upstreamActive', False),
                       'nodeCount': len(subtree), 'pelvisNodes': hips,
                       'modelNodes': [node['id'] for node in models],
                       'skinCandidates': [{'node': node['id'], 'name': node['name']}
                                          for node in subtree if node.get('name') in ('Body', 'Face', 'Hair')],
                       'renderingIdentityVerified': False, 'spawnSourceApproved': False})
    return {'schemaVersion': 1, 'snapshotComplete': complete,
            'executableSha256': report.get('executableSha256'),
            'activeRootCount': sum(actor['upstreamActive'] for actor in actors),
            'actors': actors, 'creationCandidates': hierarchy.get('actorSurvey', {}),
            'spawnEnabled': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = summarize(json.loads(args.snapshot.read_text(encoding='utf-8-sig')))
    text = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(text + '\n', encoding='utf-8')
    else:
        print(text)
