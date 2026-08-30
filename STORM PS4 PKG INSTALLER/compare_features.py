#!/usr/bin/env python3
"""Full comparison analysis of RPI features"""

import re

with open('rpi_extracted/uroot/eboot.bin', 'rb') as f:
    data = f.read()

def extract_strings(data, min_len=4):
    pattern = rb'[\x20-\x7e]{' + str(min_len).encode() + rb',}'
    return [s.decode('ascii', errors='ignore') for s in re.findall(pattern, data)]

strings = extract_strings(data, 5)
strings_set = set(strings)

print("=" * 80)
print("RPI FEATURE ANALYSIS")
print("=" * 80)

# Check for specific features
features = {
    "System Notifications": "sceKernelSendNotificationRequest" in str(strings),
    "HTTP Client": "sceHttpInit" in str(strings),
    "SSL Support": "sceSslInit" in str(strings),
    "JSON Parsing": "libSceJson" in str(strings),
    "BGFT Download": "sceBgftServiceIntDownloadRegisterTask" in str(strings),
    "BGFT Debug": "sceBgftServiceIntDebugDownloadRegisterPkg" in str(strings),
    "User Service": "sceUserService" in str(strings),
    "File Logging": "/data/" in str(strings) or "fopen" in str(strings),
    "Threading": "pthread" in str(strings) or "scePthread" in str(strings),
    "Graphics/Video": "sceVideoOut" in str(strings),
    "Content ID Extract": "content_id" in str(strings),
    "Title Extract": "Unable to get title" in str(strings),
    "Package Header": "package header" in str(strings).lower(),
    "Background Tasks": "background" in str(strings).lower(),
}

print("\nFEATURE DETECTION:")
for feat, present in features.items():
    status = "✓" if present else "✗"
    print(f"  {status} {feat}")

print("\n" + "=" * 80)
print("API ENDPOINTS (RPI):")
print("=" * 80)
apis = [s for s in strings if s.startswith('/api/')]
for api in sorted(set(apis)):
    print(f"  {api}")

print("\n" + "=" * 80)
print("ERROR MESSAGES (key ones):")
print("=" * 80)
errors = [s for s in strings if 'Error at' in s and ('bgft' in s.lower() or 'http' in s.lower())]
for e in errors[:10]:
    print(f"  {e[:80]}...")

print("\n" + "=" * 80)
print("JSON RESPONSE FORMATS:")
print("=" * 80)
for s in strings:
    if '{ "status"' in s or '{"success"' in s or '"task_id"' in s:
        print(f"  {s}")
