"""Controlled topic statistics fixtures and output checks."""
import math
import re

VERBS=('hz','bw','delay')
COUNT=20
WINDOW=8


def topic(run,role,verb):return '/stats_'+run+'/'+role+'/'+verb
def payload(run,nonce,board,verb,index):return f'{run}|{nonce}|{board}|{verb}|{index:02d}'


def recipe(ns,peer,nonce):
    run=ns.removeprefix('/ros_broker_')
    return [('cli:topic/'+verb,'topic_'+verb,['topic',verb,topic(run,peer,verb),'--window',str(WINDOW)],{'verb':verb,'topic':topic(run,peer,verb),'window':WINDOW,'count':COUNT}) for verb in VERBS]


def oracle(verb,text,received):
    times=received.get('receive_ns',[])
    if len(times)!=COUNT or any(b<=a for a,b in zip(times,times[1:])):return False
    rates=[8e9/(times[i+8]-times[i]) for i in range(COUNT-8)]
    if verb=='bw':
        pattern=r'([0-9.]+) (B|KB|MB)/s from ([0-9]+) messages\n\s*Message size mean: ([0-9.]+) (B|KB|MB) min: ([0-9.]+) (B|KB|MB) max: ([0-9.]+) (B|KB|MB)'
        sizes=received.get('sizes',[])
        if len(sizes)!=COUNT or len(set(sizes))!=1:return False
        scales={'B':1,'KB':1000,'MB':1000000}
        checks=[]
        for row in re.findall(pattern,text):
            if int(row[2])!=WINDOW:continue
            bandwidth=float(row[0])*scales[row[1]]
            size_ok=all(abs(float(row[i])*scales[row[i+1]]-sizes[0])<=(.5 if row[i+1]=='B' else .005*scales[row[i+1]]) for i in (3,5,7))
            checks.append(size_ok and min(rates)*sizes[0]*.65<=bandwidth<=max(rates)*sizes[0]*1.4)
        return bool(checks) and all(checks)
    pattern=r'average '+('rate' if verb=='hz' else 'delay')+r': ([0-9.+-]+)\n\s*min: ([0-9.+-]+)s max: ([0-9.+-]+)s std dev: ([0-9.+-]+)s window: ([0-9]+)'
    checks=[]
    for row in re.findall(pattern,text):
        mean,low,high,std=map(float,row[:4])
        if int(row[4])!=WINDOW:continue
        if not all(math.isfinite(x) for x in (mean,low,high,std)) or low>high or std<0:return False
        if verb=='hz':
            checks.append(low>0 and min(rates)*.75<=mean<=max(rates)*1.25 and 1/high-.1<=mean<=1/low+.1)
        elif verb=='delay':
            ages=[(t-received['stamp_ns'])/1e9 for t in times]
            checks.append(min(ages)-.3<=low<=mean<=high<=max(ages)+.3 and std<=high-low+.001)
    return bool(checks) and all(checks)
