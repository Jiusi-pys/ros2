"""Bind OFF isolation and enabled peer traffic to actual graph and callback evidence."""
import json
from cli_acceptance import TARGET
from discovery_off_contract import payloads,validate_snapshot,validate_counts,validate_data

def validate(value,root,run,board,nonce):
    role='A' if board==TARGET['board_serials'][0] else 'B';peer='B' if role=='A' else 'A';peer_board=next(b for b in TARGET['board_serials'] if b!=board)
    record=value['discovery_off'];raw=(root/(board+'.ros.log')).read_text().splitlines()
    if any(record.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'role':role}.items()) or json.loads((root/(board+'.discovery_off.json')).read_bytes())!=record:raise ValueError('OFF report identity differs')
    if raw.count('DISCOVERY_OFF_RESULT '+json.dumps(record))!=1:raise ValueError('OFF report lacks native evidence')
    if record['settings']!=[{'mode':'off','range':'OFF','rmw':'rmw_mdds'},{'mode':'on','range':'SYSTEM_DEFAULT','rmw':'rmw_mdds'}]:raise ValueError('OFF/ON initialization settings differ')
    hashes=json.loads((root/'late_graph_hashes.json').read_bytes())['hashes']
    for mode in ('off','on'):validate_snapshot(record['snapshots'][mode],run,role,mode,hashes)
    validate_counts(record['counts']);validate_data(record['received'],run,nonce,role)
    expected_ready={k:k in ('off_local','on_remote') for k in ('off_local','off_on_local','off_on_remote','on_remote','on_off_local','on_off_remote')}
    if record['ready']!=expected_ready:raise ValueError('service readiness crossed OFF boundary')
    if record['sent']!={m:payloads(run,nonce,role,m) for m in ('off','on')}:raise ValueError('OFF negative/positive publications missing')
    if type(record['observation_ns']) is not int or record['observation_ns']<1_000_000_000:raise ValueError('OFF negative observation window missing')
    other=json.loads((root/(peer_board+'.discovery_off.json')).read_bytes())
    if record['snapshots']['on']!=other['snapshots']['on'] or other['sent']!={m:payloads(run,nonce,peer,m) for m in ('off','on')}:raise ValueError('enabled peer graph/publications disagree')
    prefixes=[{tuple(e['gid'][:12]) for e in r['snapshots'][m]['endpoints']} for r,m in ((record,'off'),(other,'off'),(record,'on'))]
    if any(prefixes[i]&prefixes[j] for i in range(3) for j in range(i)):raise ValueError('OFF and enabled contexts share identities')
    seed=int(nonce[:7],16)+(1 if role=='A' else 2)
    if record['results']!={'off_local':seed+101,'on_remote':seed+202}:raise ValueError('OFF local or enabled remote RPC failed')
    for mode in ('off','on'):
        requester=role if mode=='off' else peer;operand=int(nonce[:7],16)+(1 if requester=='A' else 2);b=101 if mode=='off' else 202
        served={'run_id':run,'nonce':nonce,'role':role,'mode':mode,'a':operand,'b':b,'sum':operand+b}
        if record['served'][mode]!=served or raw.count('DISCOVERY_OFF_SERVER '+json.dumps(served))!=1:raise ValueError('OFF/control server callback missing')
        for message in record['received'][mode]:
            marker={'run_id':run,'nonce':nonce,'role':role,'mode':mode,'data':message}
            if raw.count('DISCOVERY_OFF_RX '+json.dumps(marker))!=1:raise ValueError('OFF/control receiving callback missing or duplicated')
    for b in TARGET['board_serials']:
        for name in ('hidden_source.go','hidden_cli.ready','hidden_cli.go','discovery_off.sent','discovery_off.observe','hidden_cli.done','hidden_source.stop','hidden_source.done'):
            if (root/(b+'.'+name)).read_text().strip()!=nonce:raise ValueError('OFF lifecycle/send barrier differs')
