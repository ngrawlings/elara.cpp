#!/usr/bin/env python3

import sys
import time


def main():
    count = 5
    if len(sys.argv) > 1:
        count = int(sys.argv[1])

    for index in range(count):
        print(f"tick {index}", flush=True)
        time.sleep(0.1)


if __name__ == "__main__":
    main()
