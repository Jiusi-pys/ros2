"""Pure planning/statistics; no device access."""
import math
import ipaddress
import re

RMWS = ['rmw_mdds', 'rmw_fastrtps_cpp', 'rmw_cyclonedds_cpp']
MAX_BYTES = 4*1024*1024
SIZES = [n*1024 for n in (1, 4, 16, 32, 64, 128, 256, 512, 1024, 4096)]


def kh_library_path(prefix):
    paths=['/data/local/tmp/ros2/Lib',
        '/system/lib64/platformsdk','/system/lib64/chipset-sdk-sp',
        '/system/lib64/chipset-sdk','/system/lib64','/lib',
        '/data/python312-rk3588a/usr/lib']
    if prefix: paths.insert(0,prefix+'/lib')
    return ':'.join(paths)


def validate_kh_provider(p):
    required={'bin/dds_bench_kh','lib/librmw_mdds.so','lib/libmdds_bridge_shared.z.so','lib/libddsc.z.so'}
    return (isinstance(p,dict) and p.get('variant')=='communication_dsoftbus_kh'
            and bool(re.fullmatch(r'[0-9a-f]{40}',p.get('source_commit','')))
            and bool(re.fullmatch(r'/data/local/tmp/dds-kh-[0-9a-f]{16}',p.get('prefix','')))
            and p.get('cfi_bridge') is True and p.get('cfi_ddsc') is True
            and isinstance(p.get('files'),dict) and set(p['files'])==required
            and all(isinstance(v,str) and re.fullmatch(r'[0-9a-f]{64}',v) for v in p['files'].values()))


def ethernet_profile(rmw, address):
    address = str(ipaddress.IPv4Address(address))
    if rmw == 'rmw_fastrtps_cpp':
        return f'''<dds xmlns="http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles"><profiles>
<transport_descriptors><transport_descriptor><transport_id>bench_eth1</transport_id>
<type>UDPv4</type><interfaceWhiteList><address>{address}</address></interfaceWhiteList>
</transport_descriptor></transport_descriptors>
<participant profile_name="bench_eth1" is_default_profile="true"><rtps>
<useBuiltinTransports>false</useBuiltinTransports><userTransports><transport_id>bench_eth1</transport_id></userTransports>
</rtps></participant></profiles></dds>'''
    if rmw == 'rmw_cyclonedds_cpp':
        return '''<CycloneDDS xmlns="https://cdds.io/config"><Domain Id="any"><General>
<Interfaces><NetworkInterface name="eth1"/></Interfaces><AllowMulticast>true</AllowMulticast>
</General></Domain></CycloneDDS>'''
    raise ValueError('No public MDDS interface pinning setting is assumed')


def validate_config(rows, case, role, run):
    configs = [r for r in rows if r.get('event') == 'config']
    expected = {k: case[k] for k in ('rmw','bytes','qos','depth')}
    expected.update(role=role, run=run, header_bytes=40)
    return bool(configs) and all(all(r.get(k)==v for k,v in expected.items()) for r in configs)


def percentiles(values):
    a = sorted(values)
    if any(not math.isfinite(v) or v < 0 for v in a):
        raise ValueError('latencies must be finite and nonnegative')
    result = {'n': len(a)}
    for name, q in [('p1', .01), ('p50', .5), ('p95', .95), ('p99', .99), ('max', 1)]:
        result[name] = a[math.ceil(q*len(a))-1] if a else None
    return result


def summarize(rows):
    a = [r for r in rows if r.get('event') == 'sample' and not r.get('warmup')]
    ok = [r for r in a if r.get('outcome') == 'ok']
    return dict(attempts=len(a), successes=len(ok),
                timeouts=sum(r.get('outcome') == 'timeout' for r in a),
                publish_errors=sum(r.get('outcome') == 'publish_error' for r in a),
                success_rate=len(ok)/len(a) if a else None,
                rtt_us=percentiles([r['rtt_us'] for r in ok if 'rtt_us' in r]),
                publish_us=percentiles([r['publish_us'] for r in a if 'publish_us' in r]),
                tail_sample_warning=sum('rtt_us' in r for r in ok) < 10000)


def plan_cases(profile):
    if profile not in ('smoke', 'latency', 'throughput', 'boundary', 'slow', 'soak', 'restart'):
        raise ValueError('unknown profile')
    result = []
    repetitions = 3 if profile in ('latency', 'throughput', 'slow') else 1
    for rep in range(repetitions):
        for rmw in RMWS[rep % 3:]+RMWS[:rep % 3]:
            for qos in ('best_effort', 'reliable'):
                sizes = ([1024] if profile in ('smoke', 'restart') else
                         SIZES if profile == 'boundary' else
                         [1024, 65536, 1048576] if profile in ('slow', 'soak') else SIZES)
                for size in sizes:
                    for direction in (('ab', 'ba', 'both') if profile in ('latency', 'throughput') else ('ab',)):
                        for mode in (('latency', 'stream') if profile == 'smoke' else
                                     ('stream',) if profile in ('throughput', 'slow', 'soak', 'restart') else ('latency',)):
                            rates = (10, 100, 1000, 0) if profile == 'throughput' else (0 if mode == 'latency' else 100,)
                            for rate in rates:
                                depths = (1, 10, 100) if profile == 'slow' else (1 if profile == 'boundary' else 10,)
                                for depth in depths:
                                    result.append(dict(rmw=rmw, qos=qos, bytes=size, direction=direction,
                                        mode=mode, rate=rate, depth=depth, repetition=rep,
                                        count=1 if profile == 'boundary' else 30 if profile == 'smoke' else 10000,
                                        warmup=0 if profile == 'boundary' or mode == 'stream' else 5 if profile == 'smoke' else 100,
                                        seconds=1800 if profile == 'soak' else 8 if profile == 'smoke' else 90 if profile == 'boundary' else 60,
                                        timeout_ms=30000 if profile == 'boundary' else 2000,
                                        slow_ms=20 if profile == 'slow' else 0,
                                        restart_after=5 if profile == 'restart' else 0,
                                        profile=profile))
    return result
