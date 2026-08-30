#!/usr/bin/env python3
"""Extract all API endpoints and BGFT-related strings from RPI eboot"""

import re

def extract_strings(data, min_len=4):
    """Extract printable strings from binary"""
    pattern = rb'[\x20-\x7e]{' + str(min_len).encode() + rb',}'
    return [s.decode('ascii', errors='ignore') for s in re.findall(pattern, data)]

with open('rpi_extracted/uroot/eboot.bin', 'rb') as f:
    data = f.read()

strings = extract_strings(data, 5)

print("=" * 60)
print("RPI API ENDPOINTS:")
print("=" * 60)
for s in strings:
    if '/api/' in s or 'api/install' in s.lower():
        print(f"  {s}")

print("\n" + "=" * 60)
print("BGFT FUNCTION CALLS:")
print("=" * 60)
for s in strings:
    if 'bgft' in s.lower() and 'sce' in s.lower():
        print(f"  {s}")

print("\n" + "=" * 60)
print("HTTP RELATED:")
print("=" * 60)
for s in strings:
    if 'http' in s.lower() and ('://' in s or 'sce' in s.lower()):
        print(f"  {s}")

print("\n" + "=" * 60)
print("PACKAGE/INSTALL PARAMS:")
print("=" * 60)
for s in strings:
    if 'package' in s.lower() or 'entitlement' in s.lower() or 'PS4GD' in s:
        print(f"  {s}")

print("\n" + "=" * 60)
print("ERROR MESSAGES:")
print("=" * 60)
for s in strings:
    if 'Error' in s and 'bgft' in s.lower():
        print(f"  {s}")
