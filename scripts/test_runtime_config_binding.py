import hashlib
import json
import unittest
from runtime_config_binding import bind


class BindingTests(unittest.TestCase):
    def test_binding_preserves_build_identity_and_reseals(self):
        original = {'schema': 'ros2-ohos-release-provenance-v1', 'source_snapshot_sha256': 's', 'record_sha256': 'old'}
        result = bind(original, 'a' * 64, 'b' * 64)
        self.assertEqual(original['record_sha256'], 'old')
        self.assertEqual(result['source_snapshot_sha256'], 's')
        digest = result.pop('record_sha256')
        canonical = (json.dumps(result, sort_keys=True, separators=(',', ':'), ensure_ascii=False) + '\n').encode()
        self.assertEqual(digest, hashlib.sha256(canonical).hexdigest())
        self.assertIn('a' * 64, result['runtime_configuration']['python_bootstrap_path'])

    def test_invalid_digest_rejected(self):
        with self.assertRaises(ValueError):
            bind({'schema': 'ros2-ohos-release-provenance-v1'}, '../unsafe', 'b' * 64)


if __name__ == '__main__':
    unittest.main()
