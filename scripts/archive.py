#!/usr/bin/python3

import os
import struct
import sys

def get_file_info(directory: str):
    file_info = []

    for root, _, files in os.walk(directory):
        for file in files:
            file_path = os.path.relpath(os.path.join(root, file), directory)
            full_path = os.path.join(directory, file_path)

            if os.path.isfile(full_path):
                file_size = os.path.getsize(full_path)

                with open(full_path, 'rb') as file:
                    file_data = file.read()

                file_info.append((file_path, file_size, file_data))

    return file_info

def djb2(data: bytes):
    # http://www.cse.yorku.ca/~oz/hash.html
    hash_value = 5381
    for ch in data:
        hash_value = (((hash_value << 5) + hash_value) + ch) & 0xffffffffffffffff # hash * 33 + c
    return hash_value

HEADER_SIZE = 24
INDEX_SIZE = 36
MAGIC_STRING = b'TATIX_AR'
HEADER_FORMAT ='<8sqq'
INDEX_FORMAT = '<QqqqI'
TEXT_ENC = 'utf-8'

def create_archive(file_info):
    # All paths in the archive are converted into absolute paths because the archive will be used as a rootfs.
    file_info = [('/' + file_path, file_size, file_data) for (file_path, file_size, file_data) in file_info]

    total_size = HEADER_SIZE
    index_length = len(file_info)
    total_size += index_length * INDEX_SIZE
    for file_path, file_size, _ in file_info:
        total_size += len(file_path) + file_size

    archive_header = struct.pack(HEADER_FORMAT, MAGIC_STRING, index_length, total_size)

    index_entries = b''
    file_offset = HEADER_SIZE + index_length * INDEX_SIZE
    for file_path, file_size, file_data in file_info:
        hash_value = djb2(file_path.encode(TEXT_ENC) + file_data)
        path_length = len(file_path)
        index_entry = struct.pack(INDEX_FORMAT, hash_value, file_offset, file_size + path_length, path_length, 0)
        index_entries += index_entry
        file_offset += file_size + path_length

    file_entries = b''
    for file_path, _, file_data in file_info:
        file_entry = file_path.encode(TEXT_ENC) + file_data
        file_entries += file_entry

    archive_data = archive_header + index_entries + file_entries

    return archive_data

def extract_archive(archive_data):
    file_info = []

    magic, index_length, _ = struct.unpack(HEADER_FORMAT, archive_data[:HEADER_SIZE])

    if magic != MAGIC_STRING:
        raise ValueError("Invalid archive format")

    index_offset = HEADER_SIZE
    for _ in range(index_length):
        hash_value, file_offset, file_size_with_path, path_length, _ = struct.unpack(INDEX_FORMAT, archive_data[index_offset:index_offset + INDEX_SIZE])
        index_offset += INDEX_SIZE

        if hash_value != djb2(archive_data[file_offset:file_offset + file_size_with_path]):
            raise ValueError("Data doesn't match hash")

        file_path = archive_data[file_offset:file_offset + path_length].decode(TEXT_ENC)
        file_data = archive_data[file_offset + path_length:file_offset + file_size_with_path]

        # Removing the extra slash at the beginning (see comment create_archive).
        file_info.append((file_path.strip('/'), file_size_with_path - path_length, file_data))

    return file_info

def test_archive(directory):
    original_file_info = get_file_info(directory)

    archive_data = create_archive(original_file_info)

    extracted_file_info = extract_archive(archive_data)

    if len(original_file_info) != len(extracted_file_info):
        raise AssertionError("Number of files in the archive does not match the original")

    for original_info, extracted_info in zip(original_file_info, extracted_file_info):
        if original_info != extracted_info:
            raise AssertionError(f"File information mismatch:\nOriginal: {original_info}\nExtracted: {extracted_info}")

    print("Archive test passed successfully!")

def main():
    if len(sys.argv) < 3:
        print("Usage:")
        print("  ./archive.py enc DIR_PATH ARCHIVE_FILE")
        print("  ./archive.py dec ARCHIVE_FILE DIR_PATH")
        print("  ./archive.py test DIR_PATH")
        sys.exit(1)

    command = sys.argv[1]
    if command == "enc":
        if len(sys.argv) < 4:
            print("Usage: ./archive.py enc DIR_PATH ARCHIVE_FILE")
            sys.exit(1)
        dir_path = sys.argv[2]
        archive_file = sys.argv[3]
        encode_archive(dir_path, archive_file)
    elif command == "dec":
        if len(sys.argv) < 4:
            print("Usage: ./archive.py dec ARCHIVE_FILE DIR_PATH")
            sys.exit(1)
        archive_file = sys.argv[2]
        dir_path = sys.argv[3]
        decode_archive(archive_file, dir_path)
    elif command == "test":
        dir_path = sys.argv[2]
        test_archive(dir_path)
    else:
        print("Invalid command. Use 'enc', 'dec', or 'test'.")
        sys.exit(1)

def encode_archive(dir_path, archive_file):
    file_info = get_file_info(dir_path)
    archive_data = create_archive(file_info)

    with open(archive_file, 'wb') as file:
        file.write(archive_data)

    print(f"Directory '{dir_path}' archived successfully to '{archive_file}'.")

def decode_archive(archive_file, dir_path):
    with open(archive_file, 'rb') as file:
        archive_data = file.read()

    file_info = extract_archive(archive_data)

    for file_path, _, file_data in file_info:
        full_path = os.path.join(dir_path, file_path)
        os.makedirs(os.path.dirname(full_path), exist_ok=True)

        with open(full_path, 'wb') as file:
            file.write(file_data)

    print(f"Archive '{archive_file}' extracted successfully to '{dir_path}'.")

if __name__ == "__main__":
    main()
