#!/usr/bin/env python3
"""Run the shared build driver with logging disabled."""

import sys

from build import main


if __name__ == "__main__":
    sys.exit(main([*sys.argv[1:], "--no-log"]))
