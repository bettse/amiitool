#!/usr/bin/env python3
"""Convert a Flipper Zero NFC text dump into a raw binary tag dump.

Reads "Page N: XX XX XX XX" lines and writes the concatenated page bytes,
ordered by page index, to stdout (or the given output file).
"""
import re
import sys

PAGE_RE = re.compile(r"^Page\s+(\d+):\s+((?:[0-9A-Fa-f]{2}\s*){4})\s*$")


def convert(text):
    pages = {}
    for line in text.splitlines():
        m = PAGE_RE.match(line)
        if not m:
            continue
        idx = int(m.group(1))
        pages[idx] = bytes(int(b, 16) for b in m.group(2).split())
    if not pages:
        raise ValueError("no Page lines found")
    out = bytearray()
    for idx in range(max(pages) + 1):
        out += pages.get(idx, b"\x00\x00\x00\x00")
    return bytes(out)


def main(argv):
    if len(argv) < 2:
        sys.stderr.write("usage: nfc2bin.py <input.nfc> [output.bin]\n")
        return 2
    with open(argv[1], "r") as f:
        data = convert(f.read())
    if len(argv) >= 3:
        with open(argv[2], "wb") as f:
            f.write(data)
    else:
        sys.stdout.buffer.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
