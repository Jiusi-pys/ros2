"""Strict explicit-policy and monotonic native socket-counter requirements."""
def validate_policy(value):
    if value!={'mode':'selector','profile':None,'selector':'dsoftbus','discovery_range':'SYSTEM_DEFAULT'}:raise ValueError('DSoftBus was not explicitly selected')

def validate_counter(value,pid):
    keys={'abi','pid','total_calls','ipv4_datagram_calls','ipv6_datagram_calls','datagram_successes','failed_calls','in_flight','instrumentation_errors'}
    if set(value)!=keys or any(type(v) is not int or v<0 for v in value.values()):raise ValueError('invalid native audit counters')
    if value['abi']!=1 or value['pid']!=pid or value['total_calls']<1 or value['failed_calls']>value['total_calls']:raise ValueError('audit did not observe the real process')
    if any(value[k]!=0 for k in ('ipv4_datagram_calls','ipv6_datagram_calls','datagram_successes','in_flight','instrumentation_errors')):raise ValueError('UDP attempt or incomplete audit')
