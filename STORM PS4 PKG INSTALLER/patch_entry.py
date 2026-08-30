#!/usr/bin/env python3
"""
Patch PS4 ELF entry point.
The OpenOrbis linker is not setting the entry point correctly.
This script patches e_entry in the ELF header to point to _start.
"""
import sys
import struct

def patch_elf_entry(elf_path, new_entry):
    with open(elf_path, 'r+b') as f:
        # Read ELF header
        magic = f.read(4)
        if magic != b'\x7fELF':
            print(f"Error: {elf_path} is not an ELF file")
            return False
        
        # Check class (32/64 bit)
        elf_class = struct.unpack('B', f.read(1))[0]
        if elf_class != 2:  # ELFCLASS64
            print("Error: Not a 64-bit ELF")
            return False
        
        # Patch e_type at offset 0x10 (2 bytes)
        # ET_DYN = 3 for PIE executables
        f.seek(0x10)
        old_type = struct.unpack('<H', f.read(2))[0]
        print(f"Old type: {old_type}")
        f.seek(0x10)
        f.write(struct.pack('<H', 3))  # ET_DYN
        print("New type: 3 (ET_DYN)")
        
        # e_entry is at offset 0x18 for ELF64
        f.seek(0x18)
        old_entry = struct.unpack('<Q', f.read(8))[0]
        print(f"Old entry point: 0x{old_entry:x}")
        
        # Write new entry point
        f.seek(0x18)
        f.write(struct.pack('<Q', new_entry))
        print(f"New entry point: 0x{new_entry:x}")
        
    return True

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: patch_entry.py <elf_file> [entry_address]")
        sys.exit(1)
    
    elf_file = sys.argv[1]
    # Default: 0x4020 = 0x4000 (alignment) + 0x20 (after /libexec string)
    entry = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x4020
    
    if patch_elf_entry(elf_file, entry):
        print("Patch successful!")
    else:
        print("Patch failed!")
        sys.exit(1)
