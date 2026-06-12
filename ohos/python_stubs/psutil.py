"""Small psutil subset for ROS 2 CLI on minimal OpenHarmony Python runtimes."""

from collections import namedtuple
import socket


snicaddr = namedtuple('snicaddr', ['family', 'address', 'netmask', 'broadcast', 'ptp'])


def net_if_addrs():
    return {
        'lo': [
            snicaddr(
                family=socket.AF_INET,
                address='127.0.0.1',
                netmask='255.0.0.0',
                broadcast=None,
                ptp=None),
        ],
    }
