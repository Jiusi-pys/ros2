"""Full doctor/alias checks and reports, with no omitted check categories."""
import re

REPORTS=('PLATFORM INFORMATION','ROS 2 INFORMATION','NETWORK CONFIGURATION',
         'RMW MIDDLEWARE','TOPIC LIST','QOS COMPATIBILITY LIST','PACKAGE VERSIONS')


def recipe(ns,peer,nonce):
    return [('cli:'+command,command+'_'+kind,[command]+([] if kind=='checks' else ['--report']),{'kind':kind}) for command in ('doctor','wtf') for kind in ('checks','report')]


def oracle(text,expected):
    if expected['kind']=='checks':return text.strip()=='All 5 checks passed'
    if expected['kind']!='report':return False
    if any(len(re.findall(r'(?m)^\s*'+re.escape(name)+r'\s*$',text))!=1 for name in REPORTS):return False
    return all(re.findall(r'(?m)^'+key+r'\s*:\s*(\S+)\s*$',text)==[value] for key,value in (
        ('middleware name','rmw_mdds'),('distribution name','jazzy'),('distribution type','ros2'),('distribution status','active')))
