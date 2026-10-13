import copy
import runpy
import unittest
from pathlib import Path

preview_module = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/build-bind-preview.py'))
preview = preview_module['preview']


def fixture():
    nodes = []
    for identity, parent, name, active in [(0, -1, 'AvatarRoot', True),
                                           (1, 0, 'Avatar_Player(Clone)', True),
                                           (2, 0, 'Avatar_Player(Clone)', False),
                                           (3, 1, 'Bip001 Pelvis', True),
                                           (4, 2, 'Bip001 Pelvis', False)]:
        node = {'id': identity, 'parent': parent, 'name': name, 'upstreamActive': active}
        node.update({key: True for key in ('processed', 'nameValid', 'nameAliasesAgree', 'parentValidated',
                                          'ownerValidated', 'parentWorldTimesLocalMatchesWorld')})
        nodes.append(node)
    return {'pid': 1, 'executableSha256': 'fixture', 'engineException': 0, 'hookDisabled': True,
            'entryBytesRestored': True, 'hierarchy': {'profileVerified': True, 'published': True,
                                                    'status': 'hierarchy-read-observed', 'nodes': nodes}}


class BindPreviewTests(unittest.TestCase):
    def test_separates_identical_actor_names(self):
        result = preview(fixture())
        self.assertEqual(result['suggestedActorNodeId'], 1)
        self.assertEqual([actor['bindings'][0]['nodeId'] for actor in result['actors']], [3, 4])
        self.assertTrue(all(actor['uniqueBindingCount'] == 1 for actor in result['actors']))

    def test_ambiguous_active_selection(self):
        source = fixture()
        source['hierarchy']['nodes'][2]['upstreamActive'] = True
        self.assertIsNone(preview(source)['suggestedActorNodeId'])

    def test_duplicate_bone_rejected(self):
        source = fixture()
        node = copy.deepcopy(source['hierarchy']['nodes'][3])
        node['id'] = 5
        source['hierarchy']['nodes'].append(node)
        binding = preview(source)['actors'][0]['bindings'][0]
        self.assertEqual(binding['status'], 'ambiguous')
        self.assertIsNone(binding['nodeId'])

    def test_partial_snapshot_rejected(self):
        source = fixture()
        source['hierarchy']['status'] = 'hierarchy-partial'
        with self.assertRaises(ValueError):
            preview(source)

    def test_cycle_and_missing_parent_rejected(self):
        for parent in (3, 999):
            source = fixture()
            source['hierarchy']['nodes'][3]['parent'] = parent
            with self.assertRaises(ValueError):
                preview(source)

    def test_duplicate_id_rejected(self):
        source = fixture()
        source['hierarchy']['nodes'][4]['id'] = 3
        with self.assertRaises(ValueError):
            preview(source)

    def test_cp932_names_and_virtual_channels(self):
        entries = preview_module['definitions']()
        self.assertEqual(len(entries), 52)
        self.assertEqual(len({entry[0] for entry in entries}), 52)
        self.assertTrue(all(len(name.encode('cp932')) <= 15 for name, _ in entries))
        self.assertNotIn('センター', {entry[0] for entry in entries})
        self.assertFalse(preview(fixture())['writeEnabled'])

    def test_unrestored_pose_write_rejected(self):
        source = fixture()
        source['settersCalled'] = True
        with self.assertRaises(ValueError):
            preview(source)
        source['poseRoundTrip'] = {'published': True, 'localRestored': True, 'worldRestored': True}
        self.assertEqual(preview(source)['suggestedActorNodeId'], 1)


if __name__ == '__main__':
    unittest.main()
