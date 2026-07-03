"""Small psutil subset for ROS 2 CLI on minimal OpenHarmony Python runtimes."""

from collections import namedtuple
import socket


snicaddr = namedtuple('snicaddr', ['family', 'address', 'netmask', 'broadcast', 'ptp'])
snicstats = namedtuple('snicstats', ['isup', 'duplex', 'speed', 'mtu'])


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


def net_if_stats():
    return {
        'lo': snicstats(
            isup=True,
            duplex=0,
            speed=0,
            mtu=65536),
    }
