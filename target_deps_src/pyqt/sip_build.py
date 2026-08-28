"""Driver for sip-build (pyqt-builder) without relying on the pip console
script (WDAC may block the generated exe launcher). Usage:
    pixi run python sip_build.py <args passed to sip-build>
"""
import sys

from pyqtbuild.build import main

sys.argv = ['sip-build'] + sys.argv[1:]
sys.exit(main())
