
import os
import sys

def convert_file(input_path, output_path, var_name):
    try:
        with open(input_path, 'rb') as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: {input_path} not found")
        return

    with open(output_path, 'w') as f:
        f.write(f'#pragma once\n\n')
        f.write(f'static const unsigned char {var_name}[] = {{\n')
        
        for i, byte in enumerate(data):
            if i % 16 == 0:
                f.write('    ')
            f.write(f'0x{byte:02X}, ')
            if (i + 1) % 16 == 0:
                f.write('\n')
                
        f.write('\n};\n')
        f.write(f'static const unsigned int {var_name}_len = {len(data)};\n')
        
    print(f"Done: {output_path}")

convert_file(r'f:\MY SOFT\STORM PS4 PKG INSTALLER\assets\centurygothic.ttf', r'f:\MY SOFT\STORM PS4 PKG INSTALLER\include\font_data.h', 'century_gothic_ttf')
