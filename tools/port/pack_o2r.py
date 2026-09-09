#!/usr/bin/env python3
"""Pack a directory into a libultraship .o2r archive without building Torch.

An .o2r is a plain zip file. Torch additionally stores a 6-byte big-endian
(major, minor, patch) entry named "portVersion" that the port compares with its
own build version. This script reproduces `torch pack <dir> <out> o2r -u x.y.z`.

Usage:
    python3 tools/port/pack_o2r.py assets/port build/papership.o2r
    python3 tools/port/pack_o2r.py assets/port papership.o2r --version 0.1.0
"""
import argparse
import re
import struct
import sys
import zipfile
from pathlib import Path


def version_from_cmake(cmake_lists: Path) -> str:
    match = re.search(r"project\(PaperShip VERSION (\d+\.\d+\.\d+)", cmake_lists.read_text())
    return match.group(1) if match else "0.1.0"


def pack(folder: Path, output: Path, version: str) -> int:
    major, minor, patch = (int(part) for part in version.split("."))
    files = sorted(p for p in folder.rglob("*") if p.is_file())
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for path in files:
            zf.write(path, path.relative_to(folder).as_posix())
        zf.writestr("portVersion", struct.pack(">HHH", major, minor, patch))
    return len(files)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", type=Path, help="directory to pack (e.g. assets/port)")
    parser.add_argument("output", type=Path, help="archive to write (e.g. papership.o2r)")
    parser.add_argument("--version", help="port version to store (default: from CMakeLists.txt)")
    args = parser.parse_args()

    if not args.folder.is_dir():
        print("error: %s is not a directory" % args.folder, file=sys.stderr)
        return 1
    version = args.version or version_from_cmake(Path(__file__).resolve().parents[2] / "CMakeLists.txt")
    count = pack(args.folder, args.output, version)
    print("packed %d files into %s (portVersion %s)" % (count, args.output, version))
    return 0


if __name__ == "__main__":
    sys.exit(main())
