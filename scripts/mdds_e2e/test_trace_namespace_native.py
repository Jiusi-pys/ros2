"""Native failure injection: an owned namespace must not leak descendants."""
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import board_trace_probe
from board_graph_ownership import process_start


WORKER = r'''
import json, os, subprocess, sys, time
from pathlib import Path
root=Path(sys.argv[2]); mode=(root/'mode').read_text()
def info(pid):
 return {'pid':pid,'start':Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()[19],
         'namespace':os.readlink(f'/proc/{pid}/ns/mnt')}
child=subprocess.Popen([sys.executable,'-c','import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(60)'])
(root/'children.json').write_text(json.dumps([info(os.getpid()),info(child.pid)]))
if mode=='timeout': time.sleep(60)
if mode=='normal': child.terminate(); time.sleep(.05); child.kill(); child.wait()
(root/'trace_probe').mkdir()
(root/'trace_probe/report.json').write_text('{"fixture":true}')
raise SystemExit(7 if mode=='failure' else 0)
'''


def live(record):
    if process_start(record['pid']) != record['start']: return False
    try: return Path(f"/proc/{record['pid']}/stat").read_text().rsplit(')', 1)[1].split()[0] != 'Z'
    except FileNotFoundError: return False


class ShortWait(subprocess.Popen):
    def wait(self, timeout=None): return super().wait(timeout=1 if timeout == 180 else timeout)


class TraceNamespaceNativeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='trace_failure_', dir='/data/local/tmp')
        self.root = Path(self.temp.name)
        (self.root/'board_trace_probe.py').write_text(WORKER)
        shutil.copyfile(Path(__file__).parent/'trace_mount_namespace.py', self.root/'trace_mount_namespace.py')
        self.sentinel = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])
        self.sentinel_start = process_start(self.sentinel.pid)
    def tearDown(self):
        # The RED deliberately exposes a leak. Clean only the two exact fixture
        # identities from its private namespace, never a process-name pattern.
        if (self.root/'children.json').exists():
            for record in json.loads((self.root/'children.json').read_bytes()):
                if live(record):
                    if os.readlink(f"/proc/{record['pid']}/ns/mnt") != record['namespace']:
                        raise RuntimeError('fixture namespace changed before cleanup')
                    os.kill(record['pid'], signal.SIGKILL)
        if self.sentinel.poll() is None:
            assert process_start(self.sentinel.pid) == self.sentinel_start
            self.sentinel.kill()
        self.sentinel.wait(); self.temp.cleanup()
    def check_mode(self, mode):
        (self.root/'mode').write_text(mode)
        with patch.object(board_trace_probe.subprocess, 'Popen', ShortWait):
            if mode == 'normal':
                self.assertEqual(board_trace_probe.execute(self.root, 'fixture', 'A', 'a'*32), {'fixture': True})
            else:
                with self.assertRaises((subprocess.TimeoutExpired, RuntimeError)):
                    board_trace_probe.execute(self.root, 'fixture', 'A', 'a'*32)
        records = json.loads((self.root/'children.json').read_bytes())
        self.assertNotEqual(records[0]['namespace'], os.readlink('/proc/1/ns/mnt'))
        remaining = [r for r in records if live(r)]
        print('TRACE_FAILURE_OBSERVATION ' + json.dumps({'mode':mode,'remaining':remaining}), flush=True)
        self.assertEqual(remaining, [], 'namespace descendants survived supervisor completion')
        self.assertIsNone(self.sentinel.poll(), 'unrelated system-namespace process was killed')
    def test_timeout_reaps_descendants(self): self.check_mode('timeout')
    def test_failure_reaps_descendants(self): self.check_mode('failure')
    def test_success_with_leak_is_rejected(self): self.check_mode('leak')
    def test_normal_completion(self): self.check_mode('normal')
    def check_signal(self, sig):
        (self.root/'mode').write_text('timeout')
        source = str(Path(__file__).parent)
        driver = ('import sys; from pathlib import Path; sys.path.insert(0, ' + repr(source)
                  + '); import board_trace_probe; board_trace_probe.execute(Path('
                  + repr(str(self.root)) + "), 'fixture', 'A', '" + 'a'*32 + "')")
        with subprocess.Popen([sys.executable, '-B', '-c', driver]) as child:
            try:
                deadline = time.monotonic() + 10
                while not (self.root/'children.json').exists() and time.monotonic() < deadline:
                    if child.poll() is not None: self.fail('supervisor exited before fixture readiness')
                    time.sleep(.05)
                records = json.loads((self.root/'children.json').read_bytes())
                os.kill(child.pid, sig)
                self.assertEqual(child.wait(timeout=10), 128 + sig)
                self.assertEqual([r for r in records if live(r)], [])
                self.assertIsNone(self.sentinel.poll())
                proof = json.loads((self.root/'trace_supervisor.json').read_bytes())
                self.assertEqual(proof['signal'], sig)
                self.assertEqual(proof['cleanup_remaining'], [])
                print('TRACE_SIGNAL_OBSERVATION ' + json.dumps({'signal':sig,'remaining':[]}), flush=True)
            finally:
                if child.poll() is None: child.kill(); child.wait()
    def test_supervisor_sigterm(self): self.check_signal(signal.SIGTERM)
    def test_supervisor_sigint(self): self.check_signal(signal.SIGINT)


if __name__ == '__main__': unittest.main()
