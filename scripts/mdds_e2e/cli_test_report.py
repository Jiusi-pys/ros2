"""Validate the exact active and post-shutdown launch-testing assertions."""
import xml.etree.ElementTree as ET

EXPECTED={('mdds_cli_fixture.TestPeerProcess','test_native_publication'),
          ('mdds_cli_fixture.TestPeerProcess','test_peer_exchange'),
          ('mdds_cli_fixture.TestPeerShutdown','test_native_exit')}


def validate_xml(raw):
    if len(raw)>2*1024*1024:raise ValueError('test report exceeds size bound')
    try:root=ET.fromstring(raw)
    except ET.ParseError as error:raise ValueError('malformed test report') from error
    if root.tag!='testsuites' or any(root.get(k)!=v for k,v in {'tests':'3','failures':'0','errors':'0'}.items()):raise ValueError('test totals differ')
    if root.get('skipped','0')!='0':raise ValueError('test run contains skipped cases')
    suites=root.findall('testsuite');cases=list(root.iter('testcase'))
    if len(suites)!=1 or suites[0].get('name')!='mdds_cli_fixture.peer_talker_test.launch_tests' or len(cases)!=3 or {(c.get('classname'),c.get('name')) for c in cases}!=EXPECTED:raise ValueError('required launch tests differ')
    if any(n.tag in ('failure','error','skipped') for n in root.iter()):raise ValueError('launch test did not pass')
    for suite in suites:
        if suite.get('tests')!=str(len(suite.findall('testcase'))) or any(suite.get(k)!='0' for k in ('failures','errors','skipped')):raise ValueError('suite results differ')
    return {'tests':3,'failures':0,'errors':0,'skipped':0}
