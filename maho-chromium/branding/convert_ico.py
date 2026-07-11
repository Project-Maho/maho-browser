#!/usr/bin/env python3
import os
import struct
import sys

def main():
    branding_dir = os.path.dirname(os.path.abspath(__file__))
    sizes = [16, 32, 48, 256]
    png_paths = []
    
    for size in sizes:
        png_path = os.path.join(branding_dir, f"product_logo_{size}.png")
        if not os.path.isfile(png_path):
            print(f"Error: {png_path} not found")
            sys.exit(1)
        png_paths.append(png_path)
        
    png_data_list = []
    for path in png_paths:
        with open(path, 'rb') as f:
            png_data_list.append(f.read())
            
    num_images = len(png_paths)
    header = struct.pack('<HHH', 0, 1, num_images)
    
    entries = []
    current_offset = 6 + num_images * 16
    
    for i, data in enumerate(png_data_list):
        w, h = struct.unpack('>II', data[16:24])
        ico_w = 0 if w >= 256 else w
        ico_h = 0 if h >= 256 else h
        size = len(data)
        
        entry = struct.pack('<BBBBHHII', ico_w, ico_h, 0, 0, 1, 32, size, current_offset)
        entries.append(entry)
        current_offset += size
        
    for name in ["maho.ico", "chrome.ico"]:
        out_path = os.path.join(branding_dir, name)
        with open(out_path, 'wb') as f:
            f.write(header)
            for entry in entries:
                f.write(entry)
            for data in png_data_list:
                f.write(data)
        print(f"Created ICO file at {out_path}")

if __name__ == "__main__":
    main()
