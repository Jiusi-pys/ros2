"""Real launch tests for a private native publisher and its peer observations."""
import json
from pathlib import Path
import re
import time
import unittest
import launch
import launch_testing
import launch_testing.actions
import launch_testing.asserts
from launch_ros.actions import Node
import pytest

ROOT=Path(__file__).resolve().parents[3]
CONFIG=json.loads((ROOT/'process_test_config.json').read_text())
assert ROOT.parent==Path('/data/local/tmp/ros2/.mdds-owned-runs') and ROOT.name==CONFIG['run_id']
assert (ROOT/'owner').read_text()==f'MDDS_RUN_OWNER RUN_ID={ROOT.name} LABEL=ros_broker\n'
ASSERTIONS=[]


def record(name,**details):
    assert name not in ASSERTIONS
    ASSERTIONS.append(name)
    value={'run_id':CONFIG['run_id'],'nonce':CONFIG['nonce'],'board':CONFIG['board'],'assertions':list(ASSERTIONS),**details}
    path=ROOT/'process_test_assertions.json';temporary=ROOT/'process_test_assertions.tmp'
    temporary.write_text(json.dumps(value)+'\n');temporary.replace(path)
    print('CLI_LAUNCH_TEST_ASSERTION '+json.dumps(value),flush=True)


@pytest.mark.launch_test
def generate_test_description():
    expected=CONFIG['expected']
    talker=Node(package='demo_nodes_cpp',executable='talker',name=expected['node'],
                namespace=expected['namespace'],remappings=[('chatter',expected['topic'])],
                output='screen',emulate_tty=False)
    return launch.LaunchDescription([talker,launch_testing.actions.ReadyToTest()]),{'talker':talker}


class TestPeerProcess(unittest.TestCase):
    def test_native_publication(self,proc_output,talker):
        proc_output.assertWaitFor('dsoftbus(local=AF_UNIX physical=dsoftbus_broker',process=talker,timeout=10,stream='stderr')
        for index in (1,2,3):
            proc_output.assertWaitFor("Publishing: 'Hello World: "+str(index)+"'",process=talker,timeout=10,stream='stderr')
        record('native_publication')

    def test_peer_exchange(self):
        deadline=time.monotonic()+20
        while time.monotonic()<deadline and not (ROOT/'process.stop').exists():time.sleep(.05)
        self.assertTrue((ROOT/'process.stop').is_file())
        self.assertEqual((ROOT/'process.stop').read_text().strip(),CONFIG['nonce'])
        received=json.loads((ROOT/'process_received.json').read_text())
        self.assertEqual(received['run_id'],CONFIG['run_id']);self.assertEqual(received['nonce'],CONFIG['nonce'])
        self.assertEqual(received['board'],CONFIG['board'])
        numbers=[]
        for text in received['received']:
            match=re.fullmatch('Hello World: ([1-9][0-9]*)',text);self.assertIsNotNone(match);numbers.append(int(match[1]))
        self.assertGreaterEqual(len(numbers),3);self.assertEqual(numbers,list(range(numbers[0],numbers[0]+len(numbers))))
        peer='B' if CONFIG['expected']['role']=='A' else 'A'
        self.assertEqual(received['endpoint']['node'],'test_'+peer)
        self.assertEqual(received['endpoint']['namespace'],CONFIG['expected']['namespace'])
        self.assertEqual(received['endpoint']['type'],'std_msgs/msg/String')
        record('peer_exchange')


@launch_testing.post_shutdown_test()
class TestPeerShutdown(unittest.TestCase):
    def test_native_exit(self,proc_info,talker):
        launch_testing.asserts.assertExitCodes(proc_info,allowable_exit_codes=[0],process=talker)
        event=proc_info[talker]
        self.assertEqual(event.returncode,0)
        record('native_exit',native_pid=event.pid,native_returncode=event.returncode)
