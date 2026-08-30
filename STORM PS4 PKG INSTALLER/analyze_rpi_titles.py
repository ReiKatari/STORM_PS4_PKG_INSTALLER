#!/usr/bin/env python3
"""Deep analysis of RPI strings to understand PKG title extraction"""

import re

with open('rpi_extracted/uroot/eboot.bin', 'rb') as f:
    data = f.read()

# Extract all printable strings
def extract_strings(data, min_len=4):
    pattern = rb'[\x20-\x7e]{' + str(min_len).encode() + rb',}'
    return [s.decode('ascii', errors='ignore') for s in re.findall(pattern, data)]

strings = extract_strings(data, 5)

print("=" * 70)
print("RPI TITLE/NAME EXTRACTION STRINGS:")
print("=" * 70)
for s in strings:
    if 'title' in s.lower() or 'name' in s.lower() or 'content' in s.lower():
        print(f"  {s}")

print("\n" + "=" * 70)
print("PKG HEADER/FORMAT STRINGS:")
print("=" * 70)
for s in strings:
    if 'header' in s.lower() or 'magic' in s.lower() or 'offset' in s.lower() or 'format' in s.lower():
        print(f"  {s}")

print("\n" + "=" * 70)
print("JSON PARSING STRINGS:")
print("=" * 70)
for s in strings:
    if 'json' in s.lower() or 'parse' in s.lower():
        print(f"  {s}")

print("\n" + "=" * 70)
print("DOWNLOAD/REGISTER TASK STRINGS:")
print("=" * 70)
for s in strings:
    if 'download' in s.lower() or 'register' in s.lower() or 'task' in s.lower():
        print(f"  {s}")

print("\n" + "=" * 70)
print("CONTENT ID FORMAT STRINGS:")
print("=" * 70)
for s in strings:
    if 'CUSA' in s or 'UP0' in s or 'EP0' in s or 'JP0' in s or 'IV00' in s:
        print(f"  {s}")
