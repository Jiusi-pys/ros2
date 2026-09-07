"""Regression for the board install's lib -> Lib symbolic link."""
import unittest
from pathlib import Path
from unittest.mock import patch
import bag_record


class NativeRecorderPaths(unittest.TestCase):
    def test_foreign_prefix_is_rejected(self):
        root=Path('/data/local/tmp/ros2/.mdds-owned-runs/test')
        maps='0000-1000 r-xp 00000000 00:00 1 /tmp/librosbag2_storage_sqlite3.so'
        with patch.object(Path,'read_text',return_value=maps):
            with self.assertRaisesRegex(ValueError,'library paths differ'):
                bag_record.inspect_process(123,root,'sqlite3')

    def test_kernel_canonical_paths_are_accepted(self):
        root=Path('/data/local/tmp/ros2/.mdds-owned-runs/test')
        libraries={str(root/'lib/libmdds.so'),str(root/'lib/librmw_mdds.so'),
                   '/data/local/tmp/ros2/Lib/librosbag2_storage_sqlite3.so'}
        maps='\n'.join('0000-1000 r-xp 00000000 00:00 1 '+name for name in libraries)
        def read_text(path,*args,**kwargs):
            if path.name=='maps':return maps
            return 'header\n'
        with patch.object(Path,'read_text',read_text), patch.object(Path,'iterdir',return_value=iter(())), patch.object(Path,'read_bytes',return_value=b'fixture'), patch.object(bag_record,'process_start',return_value='42'):
            result=bag_record.inspect_process(123,root,'sqlite3')
        self.assertEqual(set(result['hashes']),libraries)
        self.assertEqual(result['owned_udp'],[])


if __name__=='__main__':unittest.main()
