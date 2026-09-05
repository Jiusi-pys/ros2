import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('manifest', Path(__file__).with_name('normalize_ohos_manifest.py'))
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)


class ManifestTests(unittest.TestCase):
    def test_modes_and_create_only(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / 'raw', Path(directory) / 'out'
            source.write_text('a' * 64 + ' *./Lib/a.so\n' + 'b' * 64 + '  ./share/a b\n')
            manifest.normalize(source, output)
            self.assertEqual(output.read_bytes(), ('a' * 64 + '  ./Lib/a.so\n' + 'b' * 64 + '  ./share/a b\n').encode())
            with self.assertRaises(FileExistsError):
                manifest.normalize(source, output)

    def test_unsafe_paths_leave_no_output(self):
        for name in ('../escape', './a/../escape', './a//b', './a/./b', './a\\b',
                     './env.sh', './build_receipt.json', './.ros2-owned-runs/log', './a\tb'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                source, output = Path(directory) / 'raw', Path(directory) / 'out'
                source.write_text('a' * 64 + ' *' + name + '\n')
                with self.assertRaises(ValueError):
                    manifest.normalize(source, output)
                self.assertFalse(output.exists())

    def test_duplicate_and_empty(self):
        for content in ('', ('a' * 64 + ' *./a\n') * 2):
            with tempfile.TemporaryDirectory() as directory:
                source, output = Path(directory) / 'raw', Path(directory) / 'out'
                source.write_text(content)
                with self.assertRaises(ValueError):
                    manifest.normalize(source, output)
                self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
