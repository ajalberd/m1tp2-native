#!/usr/bin/env python3
"""Append checksummed runtime payload files onto installer_stub.exe -> single-file setup EXE."""

import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"M1NATV01"


def pack_installer(stub_exe: Path, out_exe: Path, file_pairs: list[tuple[str, Path]]) -> None:
    stub_bytes = stub_exe.read_bytes()
    payload_start = len(stub_bytes)

    payload = bytearray()
    entries = bytearray()

    for rel_path, src_path in file_pairs:
        blob = src_path.read_bytes()
        crc = zlib.crc32(blob) & 0xFFFFFFFF
        off = len(payload)
        payload += blob
        rel_b = rel_path.replace("/", "\\").encode("ascii")
        if len(rel_b) >= 128:
            raise ValueError(f"Relative path too long: {rel_path}")
        rel_padded = rel_b + b"\x00" * (128 - len(rel_b))
        entries += struct.pack("<III", off, len(blob), crc) + rel_padded

    entry_table_off = payload_start + len(payload)
    trailer = struct.pack("<8sIIII", MAGIC, payload_start, entry_table_off, len(file_pairs), 0)

    out_exe.parent.mkdir(parents=True, exist_ok=True)
    out_exe.write_bytes(stub_bytes + payload + entries + trailer)
    print(f"Packed {len(file_pairs)} files into {out_exe} ({out_exe.stat().st_size:,} bytes)")


def main() -> None:
    if len(sys.argv) < 4:
        print("Usage: pack_installer.py <stub.exe> <out_setup.exe> <relpath=srcpath> ...")
        sys.exit(1)
    stub = Path(sys.argv[1])
    out = Path(sys.argv[2])
    pairs = []
    for arg in sys.argv[3:]:
        rel, src = arg.split("=", 1)
        pairs.append((rel, Path(src)))
    pack_installer(stub, out, pairs)


if __name__ == "__main__":
    main()
