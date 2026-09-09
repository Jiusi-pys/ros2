"""Build-independent HDC deployment and bounded two-board DDS benchmark runner."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import time
import uuid
from benchlib import MAX_BYTES, plan_cases, summarize, validate_config, ethernet_profile, RMWS, runtime_library_path

HERE = Path(__file__).resolve().parent
WS = HERE.parent.parent
BOARDS = ['3e01ff55454d202020104033bf453b00', '3e01ff55454d202020104433991c3b00']
DEFAULT_HDC = 'C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe'
RUNTIME = '/data/local/tmp/ros2'
q = shlex.quote


def sha(path):
    with open(path, 'rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


class Device:
    def __init__(self, hdc, serial):
        self.hdc, self.serial = hdc, serial

    def call(self, *args, timeout=40):
        p = subprocess.run([self.hdc, '-t', self.serial, *args], capture_output=True,
                           text=True, encoding='utf-8', errors='replace', timeout=timeout)
        if p.returncode:
            raise RuntimeError(f'{self.serial}: {p.stdout} {p.stderr}')
        return p.stdout.strip()

    def shell(self, command, timeout=40):
        return self.call('shell', command, timeout=timeout)

    def send(self, source, dest):
        self.call('file', 'send', str(Path(source).resolve()), dest)
        got = self.shell('sha256sum '+q(dest)).split()[0]
        if got != sha(source):
            raise RuntimeError('transfer hash mismatch '+dest)

    def fetch(self, remote, local):
        expected = self.shell('sha256sum '+q(remote)).split()[0]
        self.call('file', 'recv', remote, str(Path(local).resolve()))
        if sha(local) != expected:
            raise RuntimeError('result transfer hash mismatch')


def deploy(devices, output):
    files = {n: WS/'build_ohos/dds_bench'/n for n in ('dds_bench', 'dds_bench_contract_test')}
    files['board_worker.py'] = HERE/'board_worker.py'
    hashes = {n: sha(p) for n, p in files.items()}
    identity = hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()[:16]
    remote = '/data/local/tmp/dds-bench-'+identity
    sources=output.parent/'harness_sources'
    sources.mkdir()
    for path in HERE.iterdir():
        if path.is_file(): shutil.copy2(path,sources/path.name)
    receipt = dict(directory=remote, files=hashes, boards={},
                   harness_sources={p.name:sha(p) for p in sources.iterdir()}, source_heads={})
    for repo in ('','src/eProsima/Fast-DDS','src/eclipse-cyclonedds/cyclonedds'):
        receipt['source_heads'][repo or 'ros2']=subprocess.check_output(['git','-C',str(WS/repo),'rev-parse','HEAD'],text=True).strip()
    for d in devices:
        if d.shell('test -e '+RUNTIME+'/.dds-bench-activity-lock && echo BUSY') == 'BUSY':
            raise RuntimeError('device is in use; deployment refused before mutation')
        d.shell('mkdir -p '+q(remote))
        for name, path in files.items():
            existing=d.shell('sha256sum '+q(remote+'/'+name)+' 2>/dev/null')
            if not existing or existing.split()[0] != hashes[name]:
                d.send(path, remote+'/'+name)
        d.shell('chmod 755 '+q(remote+'/dds_bench')+' '+q(remote+'/dds_bench_contract_test'))
        check = d.shell(f'. {RUNTIME}/env.sh || exit 70; {q(remote)}/dds_bench_contract_test')
        if check.strip() != 'CONTRACT_PASS':
            raise RuntimeError('native contract failed '+check)
        fingerprint = d.shell('sha256sum '+
            ' '.join(RUNTIME+'/Lib/'+n for n in ('libfastrtps.so',
                                                'libddsc.so', 'librmw_fastrtps_cpp.so', 'librmw_cyclonedds_cpp.so', 'librclcpp.so')))
        receipt['boards'][d.serial] = dict(contract=check, runtime=fingerprint,
            environment=d.shell('uname -a; ip -4 addr; cat /proc/meminfo; cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor; for f in /sys/class/net/eth1/mtu /sys/class/net/eth1/speed /proc/sys/net/core/rmem_max /proc/sys/net/core/wmem_max; do echo "$f"; cat "$f"; done'))
        print('DEPLOY_PASS', d.serial, remote, flush=True)
    output.write_text(json.dumps(receipt, indent=2))
    return remote


def read_rows(path):
    rows = []
    if path.exists():
        for line in path.read_text().splitlines():
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                rows.append(dict(event='truncated_record'))
    return rows


def report_case(root, case, configs, statuses):
    result = dict(config=case, boards=statuses, streams={}, valid=True)
    for serial, conf in configs.items():
        for p in conf.get('streams',conf['processes']):
            rows = read_rows(root/(serial+'_'+p['name']+'.jsonl'))
            rows += read_rows(root/(serial+'_'+p['name']+'.jsonl.restart'))
            s = summarize(rows)
            s['identity_valid'] = validate_config(rows,case,p['argv'][1],int(p['argv'][-3]))
            if not s['identity_valid']: result['valid']=False
            s['endpoint_match_wait_us'] = next((x['elapsed_us'] for x in rows if x.get('event')=='discovery'), None)
            s['terminals'] = [x for x in rows if x.get('event') == 'terminal']
            s['truncated_records'] = sum(x.get('event') == 'truncated_record' for x in rows)
            s['published'] = sum(x.get('event') == 'sample' and x.get('outcome') == 'published' for x in rows)
            if p['name'].startswith('source'):
                s['success_rate'] = None  # Delivery is measured at the opposite sink.
                s['publish_acceptance_rate'] = s['published']/s['attempts'] if s['attempts'] else None
            elapsed = sum(x.get('elapsed_us',0) for x in s['terminals'])/1e6
            s['measurement_seconds'] = elapsed
            s['duration_shortfall_seconds'] = max(0,case['seconds']-elapsed) if p['name'].startswith('source') else None
            s['publish_calls_per_second'] = s['attempts']/elapsed if elapsed and p['name'].startswith('source') else None
            arrivals = [x for x in rows if x.get('event') == 'receive']
            s['received_unique'] = len({x['seq'] for x in arrivals})
            s['observed_duplicates'] = len(arrivals)-s['received_unique']+sum(t.get('duplicate',0) for t in s['terminals'])
            span = (max(x['arrival_ns'] for x in arrivals)-min(x['arrival_ns'] for x in arrivals))/1e9 if len(arrivals)>1 else 0
            s['arrival_span_seconds'] = span
            s['payload_mib_s_arrival_span'] = (len(arrivals)-1)*case['bytes']/1048576/span if span else None
            s['received_messages_per_second_arrival_span'] = (len(arrivals)-1)/span if span else None
            s['sample_shortfall'] = max(0, case['count']-s['rtt_us']['n']) if p['name'].startswith('ping') else None
            restart = statuses[serial].get('restart_ns')
            after_restart = [x['arrival_ns'] for x in arrivals if restart and x['arrival_ns'] >= restart]
            s['restart_to_receive_ms'] = (min(after_restart)-restart)/1e6 if after_restart else None
            if not s['terminals'] or any(x.get('status')!='complete' or x.get('invalid',0) for x in s['terminals']) or s['truncated_records']:
                result['valid'] = False
            if p['name'].startswith('ping') and (s['successes']==0 or s['timeouts'] or s['publish_errors']):
                result['valid'] = False
            result['streams'][serial+'_'+p['name']] = s
    if case['mode']=='stream':
        result['delivery'] = {}
        for lane in ('ab','ba'):
            sources = [v for k,v in result['streams'].items() if k.endswith('source_'+lane)]
            sinks = [v for k,v in result['streams'].items() if k.endswith('sink_'+lane)]
            if sources and sinks:
                sent, got = sources[0]['published'], sinks[0]['received_unique']
                result['delivery'][lane] = dict(publish_accepted=sent, received_unique=got,
                    missing_at_cutoff=max(0,sent-got), unexpected_received=max(0,got-sent),
                    delivery_ratio=got/sent if sent else None)
                if not sent or not got or got>sent:
                    result['valid'] = False
    for serial, status in statuses.items():
        samples = read_rows(root/(serial+'_resources.jsonl'))
        ticks = {}
        for row in samples:
            for p in row.get('processes',[]):
                key = (p['pid'],p['start'])
                ticks[key] = max(ticks.get(key,0),p['ticks'])
        duration = status.get('elapsed_seconds',0)
        cpu_seconds = status.get('child_cpu_seconds',sum(ticks.values())/status.get('clock_ticks',100))
        status['cpu_one_core_percent'] = cpu_seconds/duration*100 if duration else None
        cpu_before=status.get('before',{}).get('system_cpu_ticks',[])
        cpu_after=status.get('after',{}).get('system_cpu_ticks',[])
        if len(cpu_before)==8 and len(cpu_after)==8:
            delta=[b-a for a,b in zip(cpu_before,cpu_after)]
            status['system_cpu_busy_percent']=(sum(delta)-delta[3]-delta[4])/sum(delta)*100 if sum(delta)>0 else None
        shared_before={(p['pid'],p['start']):p for p in status.get('before',{}).get('shared_softbus',[])}
        shared_ticks=sum(max(0,p['ticks']-shared_before[(p['pid'],p['start'])]['ticks']) for p in status.get('after',{}).get('shared_softbus',[]) if (p['pid'],p['start']) in shared_before)
        status['shared_softbus_cpu_one_core_percent']=shared_ticks/status.get('clock_ticks',100)/duration*100 if duration and shared_before else None
        before, after = status.get('before',{}).get('net',{}), status.get('after',{}).get('net',{})
        status['interface_deltas'] = {iface:{k:after[iface][k]-v for k,v in counters.items()}
                                      for iface,counters in before.items() if iface in after}
        if status.get('reason') not in ('children_completed','host_stop'):
            result['valid'] = False
        if any(p['returncode'] not in (0,-15) for p in status.get('processes',[])):
            result['valid'] = False
    reasons=[]
    for status in statuses.values():
        if status.get('reason') not in ('children_completed','host_stop'):
            reasons.append(status.get('reason','missing_status'))
    for s in result['streams'].values():
        if s['publish_errors']: reasons.append('publish_error')
        if s['timeouts']: reasons.append('echo_timeout')
        if not s['identity_valid']: reasons.append('identity_mismatch_or_missing')
        for t in s['terminals']:
            if t.get('status')!='complete': reasons.append(t.get('status','missing_terminal_status'))
            if t.get('invalid',0): reasons.append('integrity_failure')
    result['failure_reasons']=sorted(set(reasons)) if not result['valid'] else []
    (root/'result.json').write_text(json.dumps(result, indent=2))
    return result


def run_case(devices, remote, root, case, index):
    if not isinstance(case.get('bytes'),int) or not 0 < case['bytes'] <= MAX_BYTES:
        raise ValueError('Current test scope permits payloads from 1 byte through 4 MiB only')
    if case.get('rmw') not in RMWS:
        raise ValueError('Unsupported RMW: '+str(case.get('rmw')))
    root.mkdir()
    run = int(uuid.uuid4().hex[:12],16)
    configs, paths, acquired, launched = {}, {}, [], []
    owner = 'DDS_BENCH '+root.name+' '+str(run)
    lock = RUNTIME+'/.dds-bench-activity-lock'
    statuses = {}
    try:
        for d in devices:
            cmd = f'if (umask 077; mkdir {lock}) 2>/dev/null; then printf "%s\\n" {q(owner)} > {lock}/owner; printf LOCKED; else printf BUSY; fi'
            if d.shell(cmd) != 'LOCKED':
                raise RuntimeError('board is busy; existing owner preserved')
            acquired.append(d)
            path = remote+'/runs/'+str(run)
            paths[d.serial] = path
            if d.shell('mkdir -p '+q(remote+'/runs')+'; (umask 077; mkdir '+q(path)+') && echo CREATED') != 'CREATED':
                raise RuntimeError('run directory collision')
            env = dict(RMW_IMPLEMENTATION=case['rmw'], ROS_DOMAIN_ID='83',
                       ROS_AUTOMATIC_DISCOVERY_RANGE='SYSTEM_DEFAULT', ROS_LOCALHOST_ONLY='0',
                       FASTDDS_BUILTIN_TRANSPORTS='UDPv4', LD_PRELOAD='',
                       LD_LIBRARY_PATH=runtime_library_path())
            if case.get('debug_crash_trace'): env['DDS_BENCH_CRASH_TRACE']='1'
            duplex=case['direction']=='both'
            if duplex: env['DDS_BENCH_DUPLEX']='1'
            addresses=d.shell('ip -o -4 addr show eth1').split()
            if 'inet' not in addresses:
                raise RuntimeError('eth1 has no IPv4 address')
            address=addresses[addresses.index('inet')+1].split('/')[0]
            profile=root/(d.serial+'_ethernet.xml')
            profile.write_text(ethernet_profile(case['rmw'],address))
            d.send(profile,path+'/ethernet.xml')
            env['CYCLONEDDS_URI' if case['rmw']=='rmw_cyclonedds_cpp' else 'FASTRTPS_DEFAULT_PROFILES_FILE']=path+'/ethernet.xml'
            processes = []
            streams=[]
            for lane in (('ab','ba') if case['direction']=='both' else (case['direction'],)):
                sender_index = 0 if lane=='ab' else 1
                sender = d.serial == devices[sender_index].serial
                if duplex and not sender: continue
                role = ('ping' if sender else 'pong') if case['mode']=='latency' else ('source' if sender else 'sink')
                name = role+'_'+lane
                duration = case['seconds'] if sender else case['seconds']+22
                n = case['count'] if case['mode']=='latency' else 1000000
                args = [role,f'/dds_bench_{run}_{lane}',case['bytes'],case['qos'],case['depth'],n,
                        case['warmup'],duration,case['timeout_ms'],case['rate'],case['slow_ms'],run,
                        path+'/'+name+'.jsonl',path+'/'+name+'.ready']
                executable=remote+'/dds_bench'
                process=dict(name=name, argv=[executable]+list(map(str,args)),
                             restart=role=='sink' and bool(case['restart_after']))
                processes.append(process)
                streams.append(process)
                if duplex and case['mode']=='stream':
                    other='ba' if lane=='ab' else 'ab'
                    virtual=list(process['argv'])
                    virtual[1]='sink';virtual[2]=f'/dds_bench_{run}_{other}'
                    virtual[-2]=path+'/sink_'+other+'.jsonl'
                    streams.append(dict(name='sink_'+other,argv=virtual))
            # Start receivers before senders within each board.
            processes.sort(key=lambda p: 1 if p['name'].startswith(('ping','source')) else 0)
            conf = dict(env=env, bytes=case['bytes'], processes=processes,streams=streams,
                        wall_seconds=case['seconds']+50, rss_limit_kib=3*1024*1024,
                        restart_after=case['restart_after'])
            configs[d.serial] = conf
            local = root/(d.serial+'_config.json')
            local.write_text(json.dumps(conf,indent=2))
            d.send(local,path+'/config.json')
        # The supervisor remains responsible for deadline and cleanup if the host disappears.
        for d in reversed(devices):
            path=paths[d.serial]
            command=f'. {RUNTIME}/env.sh || exit 70; unset CYCLONEDDS_URI FASTRTPS_DEFAULT_PROFILES_FILE; exec python3.12 {q(remote)}/board_worker.py {q(path)}'
            launch=f'nohup sh -c {q(command)} > {q(path)}/worker.log 2>&1 < /dev/null &'
            d.shell(launch)
            launched.append(d)
        deadline=time.monotonic()+case['seconds']+65
        drain_at = None
        while time.monotonic()<deadline:
            for d in devices:
                if d.serial not in statuses:
                    raw=d.shell(f'if test -f {q(paths[d.serial])}/status.json; then cat {q(paths[d.serial])}/status.json; fi')
                    if raw:
                        statuses[d.serial]=json.loads(raw)
            if len(statuses)==len(devices): break
            if drain_at is None:
                all_done = True
                for d in devices:
                    for p in configs[d.serial]['processes']:
                        if p['name'].startswith(('ping','source')):
                            file = paths[d.serial]+'/'+p['name']+'.jsonl'
                            if configs[d.serial]['env'].get('DDS_BENCH_DUPLEX'):
                                if d.shell('test -f '+q(p['argv'][-1]+'.done')+' && echo DONE')!='DONE': all_done=False
                            elif '"event":"terminal"' not in d.shell('tail -n 1 '+q(file)+' 2>/dev/null'):
                                all_done=False
                if all_done: drain_at=time.monotonic()+2
            if drain_at is not None and time.monotonic()>=drain_at:
                for d in devices:
                    if d.serial not in statuses: d.shell('touch '+q(paths[d.serial]+'/stop'))
            time.sleep(.5)
        if len(statuses)!=len(devices): raise RuntimeError('supervisor status deadline')
        for d in devices:
            path=paths[d.serial]
            names=d.shell('find '+q(path)+' -maxdepth 1 -type f').splitlines()
            for name in names:
                if name.endswith(('.json','.jsonl','.log','.restart')):
                    d.fetch(name,root/(d.serial+'_'+name.rsplit('/',1)[-1]))
        return report_case(root,case,configs,statuses)
    finally:
        safe = True
        for d in launched:
            if d.serial not in statuses:
                try:
                    d.shell('touch '+q(paths[d.serial]+'/stop'))
                    for _ in range(16):
                        if d.shell('test -f '+q(paths[d.serial]+'/status.json')+' && echo DONE')=='DONE': break
                        time.sleep(.5)
                    else: safe=False
                except Exception: safe=False
        for d in acquired:
            if safe:
                d.shell(f'if test "$(cat {lock}/owner)" = {q(owner)}; then rm {lock}/owner; rmdir {lock}; fi')
            else:
                print('LOCK_RETAINED cleanup unproven', d.serial, flush=True)


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('action',choices=['plan','deploy','run'])
    ap.add_argument('--profile',default='smoke',choices=['smoke','latency','throughput','boundary','slow','soak','restart'])
    ap.add_argument('--hdc',default=DEFAULT_HDC)
    ap.add_argument('--output',type=Path)
    ap.add_argument('--limit',type=int)
    ap.add_argument('--only-rmw',choices=RMWS)
    ap.add_argument('--only-bytes',type=int)
    ap.add_argument('--min-bytes',type=int,default=1)
    ap.add_argument('--direction',choices=['ab','ba','both'])
    ap.add_argument('--mode',choices=['latency','stream'])
    args=ap.parse_args()
    cases=plan_cases(args.profile)
    for c in cases: c['network_policy']='standard_dds_eth1'
    if args.only_rmw: cases=[c for c in cases if c['rmw']==args.only_rmw]
    if args.only_bytes: cases=[c for c in cases if c['bytes']==args.only_bytes]
    cases=[c for c in cases if c['bytes']>=args.min_bytes]
    if args.mode: cases=[c for c in cases if c['mode']==args.mode]
    if args.direction:
        for c in cases: c['direction']=args.direction
    if args.limit is not None: cases=cases[:args.limit]
    output=args.output or WS.parent/'verification_evidence'/('dds_bench_'+args.profile+'_'+time.strftime('%Y%m%d_%H%M%S'))
    output.mkdir(parents=True,exist_ok=False)
    (output/'plan.json').write_text(json.dumps(cases,indent=2))
    print('PLAN',len(cases),'cases',output,flush=True)
    if args.action=='plan': return
    devices=[Device(args.hdc,serial) for serial in BOARDS]
    remote=deploy(devices,output/'deployment.json')
    if args.action=='deploy': return
    results=[]
    for i,case in enumerate(cases):
        print('CASE_START',i,case,flush=True)
        try:
            result=run_case(devices,remote,output/f'case_{i:04}',case,i)
        except Exception as e:
            result=dict(config=case,valid=False,error=repr(e))
        results.append(result)
        (output/'results.json').write_text(json.dumps(results,indent=2))
        print('CASE_END',i,'VALID' if result['valid'] else 'FAILED_OR_LIMIT',flush=True)
    with open(output/'latency.csv','w',newline='') as f:
        w=csv.writer(f); w.writerow(['case','rmw','qos','bytes','direction','stream','valid','n','p1_us','p50_us','p95_us','p99_us','max_us','timeouts','publish_errors'])
        for i,r in enumerate(results):
            for stream,s in r.get('streams',{}).items():
                p=s['rtt_us']; c=r['config']
                w.writerow([i,c['rmw'],c['qos'],c['bytes'],c['direction'],stream,r['valid']]+[p[k] for k in ('n','p1','p50','p95','p99','max')]+[s['timeouts'],s['publish_errors']])
    with open(output/'throughput.csv','w',newline='') as f:
        w=csv.writer(f)
        w.writerow(['case','rmw','qos','bytes','lane','published','received_unique','missing_at_cutoff','delivery_ratio'])
        for i,r in enumerate(results):
            for lane,d in r.get('delivery',{}).items():
                c=r['config']
                w.writerow([i,c['rmw'],c['qos'],c['bytes'],lane,d['publish_accepted'],d['received_unique'],d['missing_at_cutoff'],d['delivery_ratio']])
    from report import make_report
    make_report(output)
    print('BENCH_RUN_COMPLETE',len(results),'cases;',sum(r['valid'] for r in results),'valid; results=',output,flush=True)
    if args.profile=='smoke' and any(not r['valid'] for r in results):
        raise SystemExit(1)


if __name__=='__main__':
    main()
