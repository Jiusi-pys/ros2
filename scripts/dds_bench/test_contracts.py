import unittest
from benchlib import percentiles, summarize, plan_cases


class Contracts(unittest.TestCase):
    def test_nearest_rank_extremes(self):
        self.assertEqual(percentiles(range(1, 101)),
                         dict(n=100, p1=1, p50=50, p95=95, p99=99, max=100))

    def test_empty_has_no_fake_latency(self):
        self.assertEqual(percentiles([])['p99'], None)

    def test_kaihong_meminfo_placeholder(self):
        from board_worker import parse_memory
        self.assertEqual(parse_memory('MemAvailable: 1000 kB\nSwapCached: - kB\n'), {'MemAvailable': 1000})

    def test_invalid_latency_rejected(self):
        for values in ([float('nan')], [-1], [float('inf')]):
            with self.assertRaises(ValueError):
                percentiles(values)

    def test_warmup_timeout_and_publish_error_not_success(self):
        rows = [dict(event='sample', warmup=True, outcome='ok', rtt_us=1, publish_us=2),
                dict(event='sample', warmup=False, outcome='ok', rtt_us=10, publish_us=3),
                dict(event='sample', warmup=False, outcome='timeout', publish_us=4),
                dict(event='sample', warmup=False, outcome='publish_error', publish_us=5)]
        s = summarize(rows)
        self.assertEqual((s['attempts'], s['successes'], s['timeouts'], s['publish_errors']), (3, 1, 1, 1))
        self.assertEqual(s['rtt_us']['p1'], 10)
        self.assertAlmostEqual(s['success_rate'], 1/3)

    def test_smoke_matrix(self):
        cases = plan_cases('smoke')
        self.assertEqual(len(cases), 12)
        self.assertEqual({x['qos'] for x in cases}, {'best_effort', 'reliable'})
        self.assertEqual({x['mode'] for x in cases}, {'latency', 'stream'})

    def test_every_profile_respects_four_mib_ceiling(self):
        for profile in ('smoke','latency','throughput','boundary','slow','soak','restart'):
            self.assertTrue(all(c['bytes']<=4*1024*1024 for c in plan_cases(profile)))

    def test_4_mib_boundary_is_depth_one(self):
        cases = plan_cases('boundary')
        big = [x for x in cases if x['bytes'] == 4*1024*1024]
        self.assertEqual(len(big), 6)
        self.assertTrue(all(x['depth'] == 1 and x['count'] == 1 for x in big))

    def test_oversize_run_rejected_before_any_mutation(self):
        import tempfile
        from pathlib import Path
        from run import run_case
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)/'must_not_exist'
            with self.assertRaises(ValueError):
                run_case([], '/unused', root, {'bytes':8*1024*1024}, 0)
            self.assertFalse(root.exists())

    def test_old_mdds_cannot_silently_run_as_kh(self):
        import tempfile
        from pathlib import Path
        from run import run_case
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)/'must_not_exist'
            with self.assertRaisesRegex(RuntimeError,'KH'):
                run_case([], '/unused', root, {'bytes':1024,'rmw':'rmw_mdds'}, 0)
            self.assertFalse(root.exists())

    def test_kh_provider_requires_pinned_artifacts_and_safe_prefix(self):
        from benchlib import validate_kh_provider
        p=dict(variant='communication_dsoftbus_kh',source_commit='b'*40,
               prefix='/data/local/tmp/dds-kh-'+'a'*16,cfi_bridge=True,cfi_ddsc=True,
               files={n:'c'*64 for n in ('bin/dds_bench_kh','lib/librmw_mdds.so',
                    'lib/libmdds_bridge_shared.z.so','lib/libddsc.z.so')})
        self.assertTrue(validate_kh_provider(p))
        self.assertFalse(validate_kh_provider({**p,'prefix':'/system/lib64'}))
        self.assertFalse(validate_kh_provider({**p,'cfi_bridge':False}))
        self.assertFalse(validate_kh_provider({**p,'files':{}}))

    def test_kh_platform_crypto_precedes_python_libraries(self):
        from benchlib import kh_library_path
        paths=kh_library_path('/data/local/tmp/dds-kh-'+'a'*16).split(':')
        self.assertEqual(paths[0],'/data/local/tmp/dds-kh-'+'a'*16+'/lib')
        self.assertLess(paths.index('/system/lib64/platformsdk'),paths.index('/data/python312-rk3588a/usr/lib'))

    def test_formal_balanced_directions_and_repetitions(self):
        cases = plan_cases('latency')
        self.assertEqual(len(cases), 3*2*10*3*3)
        self.assertEqual({x['direction'] for x in cases}, {'ab', 'ba', 'both'})

    def test_wrong_rmw_cannot_be_reported_as_selected(self):
        from benchlib import validate_config
        case = dict(rmw='rmw_mdds', bytes=1024, qos='reliable', depth=10)
        row = dict(event='config', role='ping', rmw='rmw_fastrtps_cpp', bytes=1024,
                   qos='reliable', depth=10, run=7, header_bytes=40)
        self.assertFalse(validate_config([row], case, 'ping', 7))
        row['rmw']='rmw_mdds'
        self.assertTrue(validate_config([row], case, 'ping', 7))
        self.assertFalse(validate_config([row], case, 'ping', 8))
        self.assertFalse(validate_config([], case, 'ping', 7))

    def test_ethernet_profile_disables_fastdds_builtin_interfaces(self):
        from benchlib import ethernet_profile
        import xml.etree.ElementTree as ET
        x = ET.fromstring(ethernet_profile('rmw_fastrtps_cpp','192.168.77.201'))
        values = {e.tag.split('}')[-1]:e.text for e in x.iter()}
        self.assertEqual(values['useBuiltinTransports'], 'false')
        self.assertEqual(values['address'], '192.168.77.201')
        self.assertIn('name="eth1"', ethernet_profile('rmw_cyclonedds_cpp','192.168.77.201'))
        with self.assertRaises(ValueError):
            ethernet_profile('rmw_fastrtps_cpp','bad"<xml>')

    def test_stream_delivery_is_receiver_based(self):
        import json
        import tempfile
        from pathlib import Path
        from run import report_case
        case=dict(rmw='rmw_mdds',bytes=1024,qos='reliable',depth=10,mode='stream',seconds=1)
        configs={}
        statuses={}
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for board,role in [('A','source'),('B','sink')]:
                name=role+'_ab'
                configs[board]={'processes':[{'name':name,'argv':['binary',role,'1','out','ready']}]}
                statuses[board]=dict(reason='children_completed',elapsed_seconds=1,processes=[{'returncode':0}])
                rows=[dict(event='config',role=role,run=1,header_bytes=40,**{k:case[k] for k in ('rmw','bytes','qos','depth')})]
                if role=='source':
                    rows += [dict(event='sample',seq=n,warmup=False,outcome='published',publish_us=3) for n in range(2)]
                else:
                    rows += [dict(event='receive',seq=0,arrival_ns=1)]
                rows += [dict(event='terminal',status='complete',elapsed_us=1000000)]
                (root/(board+'_'+name+'.jsonl')).write_text('\n'.join(json.dumps(r) for r in rows))
            r=report_case(root,case,configs,statuses)
            self.assertEqual(r['delivery']['ab']['delivery_ratio'],.5)
            self.assertEqual(r['delivery']['ab']['missing_at_cutoff'],1)
            self.assertIsNone(r['streams']['A_source_ab']['success_rate'])
            self.assertEqual(r['streams']['A_source_ab']['publish_acceptance_rate'],1)

    def test_busy_board_deployment_does_not_write_device(self):
        import tempfile
        from pathlib import Path
        from unittest.mock import Mock, patch
        import run
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            source=root/'source'
            source.mkdir()
            (source/'board_worker.py').write_text('# fixture')
            binaries=root/'build_ohos/dds_bench'
            binaries.mkdir(parents=True)
            for name in ('dds_bench','dds_bench_contract_test'):
                (binaries/name).write_bytes(b'fixture')
            device=Mock()
            device.shell.return_value='BUSY'
            with patch.object(run,'WS',root), patch.object(run,'HERE',source), patch.object(run.subprocess,'check_output',return_value='abc'):
                with self.assertRaisesRegex(RuntimeError,'in use'):
                    run.deploy([device],root/'deployment.json')
            device.send.assert_not_called()
            self.assertEqual(device.shell.call_count,1)


if __name__ == '__main__':
    unittest.main()
