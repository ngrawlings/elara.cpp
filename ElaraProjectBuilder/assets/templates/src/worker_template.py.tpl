>>>>>>>>>>main
#!/usr/bin/env python3

import json
import sys
import time


def main():
    iterations = 8
    if len(sys.argv) > 1:
        iterations = int(sys.argv[1])
    for index in range(iterations):
        print(json.dumps({"worker": "template", "tick": index}), flush=True)
        time.sleep(0.15)


if __name__ == "__main__":
    main()
<<<<<<<<<<main
