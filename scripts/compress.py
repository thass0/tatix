#!/usr/bin/python3

import os
import shutil
import gzip
import sys
from pathlib import Path

def compress_file(src_path, dst_path):
   with open(src_path, 'rb') as f_in:
       with gzip.open(dst_path, 'wb', compresslevel=9) as f_out:
           shutil.copyfileobj(f_in, f_out)

def copy_with_compression(src_dir, dst_dir, size_threshold=4096):
   src_path = Path(src_dir)
   dst_path = Path(dst_dir)

   if not src_path.exists():
       raise FileNotFoundError(f"Source directory {src_dir} does not exist")

   dst_path.mkdir(parents=True, exist_ok=True)

   for root, dirs, files in os.walk(src_path):
       root_path = Path(root)
       relative_root = root_path.relative_to(src_path)

       for dir_name in dirs:
           (dst_path / relative_root / dir_name).mkdir(parents=True, exist_ok=True)

       for file_name in files:
           src_file = root_path / file_name
           dst_file = dst_path / relative_root / file_name
           compress_file(src_file, dst_file)

def main():
   if len(sys.argv) != 3:
       print("Usage: ./compress.py SRC_DIR DEST_DIR")
       return 1

   source_dir = sys.argv[1]
   destination_dir = sys.argv[2]

   try:
       copy_with_compression(source_dir, destination_dir)
   except Exception as e:
       print(f"Error: {e}")
       return 1

   return 0

if __name__ == "__main__":
   exit(main())
