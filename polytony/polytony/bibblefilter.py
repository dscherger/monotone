import re
import bibblebabble

sha1_string = re.compile(r"[0-9a-f]{40,40}")

def sha1_subber(match):
    return bibblebabble.hex_to_bibble(match.group())

def filter_string(string):
    return sha1_string.sub(sha1_subber, string)


if __name__ == "__main__":
    import sys
    for line in sys.stdin:
        sys.stdout.write(filter_string(line))
