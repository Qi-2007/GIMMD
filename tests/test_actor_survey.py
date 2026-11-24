import runpy
import unittest
from pathlib import Path

summarize = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/analyze-actors.py'))['summarize']


class ActorSurveyTests(unittest.TestCase):
    def snapshot(self):
        return {'buildMatch': True, 'hierarchy': {'published': True, 'status': 'hierarchy-read-observed',
            'nodes': [{'id': 0, 'parent': -1, 'name': 'AvatarRoot'},
                      {'id': 1, 'parent': 0, 'name': 'Avatar_Test(Clone)', 'upstreamActive': True},
                      {'id': 2, 'parent': 0, 'name': 'Avatar_Test(Clone)', 'upstreamActive': False},
                      {'id': 3, 'parent': 1, 'name': 'Body'},
                      {'id': 4, 'parent': 2, 'name': 'Body'}]}}

    def test_same_name_instances_remain_separate(self):
        result = summarize(self.snapshot())
        self.assertTrue(result['snapshotComplete'])
        self.assertEqual(result['activeRootCount'], 1)
        self.assertEqual([actor['skinCandidates'][0]['node'] for actor in result['actors']], [3, 4])
        self.assertTrue(all(not actor['spawnSourceApproved'] for actor in result['actors']))

    def test_partial_is_not_admitted(self):
        report = self.snapshot()
        report['hierarchy']['truncated'] = True
        self.assertFalse(summarize(report)['snapshotComplete'])

    def test_duplicate_node_is_rejected(self):
        report = self.snapshot()
        report['hierarchy']['nodes'].append(report['hierarchy']['nodes'][1])
        with self.assertRaises(ValueError):
            summarize(report)

    def test_multiple_active_roots_are_counted(self):
        report = self.snapshot()
        report['hierarchy']['nodes'][2]['upstreamActive'] = True
        self.assertEqual(summarize(report)['activeRootCount'], 2)
