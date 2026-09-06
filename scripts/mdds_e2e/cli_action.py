"""Action graph and goal-output contract for actual cross-board CLI runs."""
import re

TYPE='action_tutorials_interfaces/action/Fibonacci'
RESULT=[0,1,1,2,3,5]
FEEDBACK=[RESULT[:length] for length in range(2,7)]


def recipe(ns,peer):
    other='A' if peer=='B' else 'B'
    action=ns+'/'+peer+'/cli_action'
    info={'action':action,'client':ns+'/beta_'+other,'server':ns+'/alpha_'+peer,'count_only':False}
    return [
        ('cli:action/list','action_list',['action','list','--show-types'],[ns+'/'+role+'/cli_action ['+TYPE+']' for role in ('A','B')]),
        ('cli:action/list','action_list_count',['action','list','--count-actions'],2),
        ('cli:action/type','action_type',['action','type',action],TYPE),
        ('cli:action/info','action_info',['action','info',action,'--show-types'],info),
        ('cli:action/info','action_info_count',['action','info',action,'--count'],{**info,'count_only':True}),
        ('cli:action/send_goal','action_goal',['action','send_goal',action,TYPE,'{order: 5}','--feedback','--timeout','10'],RESULT),
    ]


def goal_id(stdout):
    values=re.findall(r'Goal accepted with ID: ([0-9a-f]{32})',stdout)
    return values[0] if len(values)==1 and int(values[0],16)!=0 else None


def oracle(case,stdout,expected):
    lines=[line.strip() for line in stdout.splitlines() if line.strip()]
    if case=='cli:action/list':return lines==[str(expected)] if isinstance(expected,int) else sorted(lines)==sorted(expected)
    if case=='cli:action/type':return lines==[expected]
    if case=='cli:action/info':
        wanted=['Action: '+expected['action'],'Action clients: 1']
        if not expected['count_only']:wanted.append(expected['client']+' ['+TYPE+']')
        wanted.append('Action servers: 1')
        if not expected['count_only']:wanted.append(expected['server']+' ['+TYPE+']')
        return lines==wanted
    if case=='cli:action/send_goal':
        feedback=re.findall(r'(?m)^\s*partial_sequence:\s*((?:-\s*\d+\s*)+)',stdout)
        result=re.findall(r'(?m)^\s*sequence:\s*((?:-\s*\d+\s*)+)',stdout)
        sequences=lambda values:[[int(n) for n in re.findall(r'-\s*(\d+)',v)] for v in values]
        return (goal_id(stdout) is not None and sequences(feedback)==FEEDBACK and sequences(result)==[expected]
                and [line for line in lines if line.startswith('Goal finished with status:')]==['Goal finished with status: SUCCEEDED'])
    return False
