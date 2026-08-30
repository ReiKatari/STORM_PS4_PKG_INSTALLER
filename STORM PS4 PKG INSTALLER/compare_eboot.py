#!/usr/bin/env python3
"""Compare FSELF headers between working RPI and our build"""

def hexdump(path, count=256):
    """Display hex dump of file"""
    print(f"\n{'='*60}")
    print(f"File: {path}")
    print('='*60)
    
    try:
        with open(path, 'rb') as f:
            data = f.read(count)
        
        for i in range(0, len(data), 16):
            hex_part = ' '.join(f'{b:02x}' for b in data[i:i+16])
            ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
            print(f'{i:04x}: {hex_part:<48} {ascii_part}')
        
        return data
    except Exception as e:
        print(f"ERROR: {e}")
        return None

# Compare files
print("FSELF Header Comparison")
rpi_data = hexdump('rpi_extracted/uroot/eboot.bin', 256)
our_data = hexdump('eboot.bin', 256)

if rpi_data and our_data:
    print(f"\n{'='*60}")
    print("DIFFERENCES:")
    print('='*60)
    for i in range(min(len(rpi_data), len(our_data))):
        if rpi_data[i] != our_data[i]:
            print(f"Offset 0x{i:04x}: RPI={rpi_data[i]:02x}, OUR={our_data[i]:02x}")
    
    # File sizes
    import os
    rpi_size = os.path.getsize('rpi_extracted/uroot/eboot.bin')
    our_size = os.path.getsize('eboot.bin')
    print(f"\nRPI file size: {rpi_size} bytes")
    print(f"OUR file size: {our_size} bytes")
