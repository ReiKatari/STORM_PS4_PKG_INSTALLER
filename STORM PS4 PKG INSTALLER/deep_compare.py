#!/usr/bin/env python3
"""Deep comparison of RPI vs Our build"""
import os
import struct

def read_fself_header(path):
    """Read FSELF header info"""
    try:
        with open(path, 'rb') as f:
            data = f.read(512)
        
        # FSELF magic
        magic = data[0:4].hex()
        
        # Parse more header fields
        result = {
            'file_size': os.path.getsize(path),
            'magic_hex': magic,
            'first_32_bytes': data[0:32].hex(),
        }
        
        # Check for OELF signature (0x4F, 0x15, 0x3D, 0x1D)
        if data[0:4] == b'O\x15=\x1d' or data[0:4] == b'\x4f\x15\x3d\x1d':
            result['format'] = 'FSELF/OELF'
            # Extract more info
            result['header_bytes_0x10_0x20'] = data[0x10:0x20].hex()
            result['header_bytes_0x20_0x30'] = data[0x20:0x30].hex()
        elif data[0:4] == b'\x7fELF':
            result['format'] = 'ELF'
        else:
            result['format'] = 'Unknown'
        
        return result
    except Exception as e:
        return {'error': str(e)}

def compare_param_sfo(path):
    """Read param.sfo entries"""
    try:
        with open(path, 'rb') as f:
            data = f.read()
        
        # Simple extraction
        result = {
            'file_size': len(data),
            'magic': data[0:4].hex() if len(data) >= 4 else 'too_short'
        }
        
        # Find strings in SFO
        strings = []
        i = 0
        while i < len(data):
            if data[i:i+4] == b'TITL':
                strings.append(('TITLE area', i))
            if data[i:i+4] == b'CONT':
                strings.append(('CONTENT area', i))
            i += 1
        result['string_markers'] = strings[:5]
        
        return result
    except Exception as e:
        return {'error': str(e)}

print("="*70)
print("COMPARISON: Working RPI vs Our Build")
print("="*70)

# RPI files
rpi_dir = "rpi_extracted/uroot"
our_dir = "."

print("\n=== RPI Package Contents ===")
for root, dirs, files in os.walk("rpi_extracted"):
    for f in files:
        full_path = os.path.join(root, f)
        size = os.path.getsize(full_path)
        print(f"  {full_path}: {size:,} bytes")

print("\n=== EBOOT.BIN Comparison ===")
print("\n--- Working RPI eboot.bin ---")
rpi_info = read_fself_header("rpi_extracted/uroot/eboot.bin")
for k, v in rpi_info.items():
    print(f"  {k}: {v}")

print("\n--- Our eboot.bin ---")
our_info = read_fself_header("eboot.bin")
for k, v in our_info.items():
    print(f"  {k}: {v}")

print("\n=== Key Differences ===")
if rpi_info.get('file_size', 0) != our_info.get('file_size', 0):
    print(f"  FILE SIZE: RPI={rpi_info.get('file_size', 0):,} vs OUR={our_info.get('file_size', 0):,}")
if rpi_info.get('first_32_bytes') != our_info.get('first_32_bytes'):
    print(f"  HEADER DIFFERS!")
    print(f"    RPI: {rpi_info.get('first_32_bytes', 'N/A')}")
    print(f"    OUR: {our_info.get('first_32_bytes', 'N/A')}")

# Check sce_sys contents
print("\n=== sce_sys Comparison ===")
rpi_sce_sys = "rpi_extracted/uroot/sce_sys"
our_sce_sys = "sce_sys"

if os.path.exists(rpi_sce_sys):
    print("RPI sce_sys files:")
    for f in os.listdir(rpi_sce_sys):
        path = os.path.join(rpi_sce_sys, f)
        if os.path.isfile(path):
            print(f"  {f}: {os.path.getsize(path):,} bytes")

if os.path.exists(our_sce_sys):
    print("\nOur sce_sys files:")
    for f in os.listdir(our_sce_sys):
        path = os.path.join(our_sce_sys, f)
        if os.path.isfile(path):
            print(f"  {f}: {os.path.getsize(path):,} bytes")

# Check for sce_module
print("\n=== sce_module Check ===")
rpi_module = "rpi_extracted/uroot/sce_module"
if os.path.exists(rpi_module):
    print("RPI has sce_module folder:")
    for f in os.listdir(rpi_module):
        path = os.path.join(rpi_module, f)
        if os.path.isfile(path):
            print(f"  {f}: {os.path.getsize(path):,} bytes")
else:
    print("RPI has NO sce_module folder")

our_module = "sce_module" 
if os.path.exists(our_module):
    print("\nOur sce_module folder:")
    for f in os.listdir(our_module):
        print(f"  {f}")
else:
    print("We have NO sce_module folder")
