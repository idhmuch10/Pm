#!/usr/bin/env python3
"""Verify and normalise a Paper Mario (USA) ROM for the port.

The port only supports the US release. This script:

* accepts a .z64 / .v64 / .n64 image or a .zip containing one,
* converts byte-swapped (.v64) and word-swapped (.n64) images to big-endian .z64,
* checks the SHA-1 against the value published by the decompilation project,
* writes the normalised ROM under the file name the port looks for.

Usage:
    python3 tools/port/prepare_us_rom.py "Paper Mario (USA).zip"
    python3 tools/port/prepare_us_rom.py rom.v64 --output build/"Paper Mario (USA).z64"
    python3 tools/port/prepare_us_rom.py rom.z64 --check-only
"""
import argparse
import hashlib
import sys
import zipfile
from pathlib import Path

US_SHA1 = "3837f44cda784b466c9a2d99df70d77c322b97a0"
KNOWN_HASHES = {
    US_SHA1: "US",
    "b9cca3ff260b9ff427d981626b82f96de73586d3": "JP",
    "2111d39265a317414d359e35a7d971c4dfa5f9e1": "PAL",
    "5c724685085eba796537573dd6f84aaddedc8582": "iQue",
}
ROM_SIZE = 40 * 1024 * 1024
DEFAULT_NAME = "Paper Mario (USA).z64"

Z64_MAGIC = b"\x80\x37\x12\x40"
V64_MAGIC = b"\x37\x80\x40\x12"
N64_MAGIC = b"\x40\x12\x37\x80"


def normalise(data: bytes) -> bytes:
    """Return the image in big-endian (.z64) byte order."""
    magic = data[:4]
    if magic == Z64_MAGIC:
        return data
    if magic == V64_MAGIC:
        out = bytearray(data)
        out[0::2], out[1::2] = data[1::2], data[0::2]
        return bytes(out)
    if magic == N64_MAGIC:
        out = bytearray(data)
        out[0::4], out[1::4], out[2::4], out[3::4] = data[3::4], data[2::4], data[1::4], data[0::4]
        return bytes(out)
    raise ValueError("input does not look like an N64 ROM image (unknown magic %s)" % magic.hex())


def read_rom(path: Path) -> bytes:
    """Read a ROM from a raw image or the first ROM-like member of a zip archive."""
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as zf:
            members = [m for m in zf.infolist() if not m.is_dir()]
            members.sort(key=lambda m: (m.file_size != ROM_SIZE, m.filename))
            for member in members:
                data = zf.read(member)
                if data[:4] in (Z64_MAGIC, V64_MAGIC, N64_MAGIC):
                    return normalise(data)
        raise ValueError("no N64 ROM image found inside %s" % path)
    data = path.read_bytes()
    if len(data) < 0x1000:
        raise ValueError("%s is too small to be a ROM" % path)
    return normalise(data)


def describe(data: bytes) -> str:
    name = data[0x20:0x34].decode("ascii", "replace").strip()
    code = data[0x3B:0x3F].decode("ascii", "replace")
    return "internal name '%s', game code '%s', %d bytes" % (name, code, len(data))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rom", type=Path, help="ROM image (.z64/.v64/.n64) or .zip containing one")
    parser.add_argument("--output", "-o", type=Path, default=Path(DEFAULT_NAME),
                        help="where to write the normalised .z64 (default: ./%s)" % DEFAULT_NAME)
    parser.add_argument("--check-only", action="store_true", help="only verify, do not write anything")
    parser.add_argument("--allow-mismatch", action="store_true",
                        help="write the output even if the SHA-1 is not the known US hash")
    args = parser.parse_args()

    try:
        data = read_rom(args.rom)
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1

    sha1 = hashlib.sha1(data).hexdigest()
    region = KNOWN_HASHES.get(sha1)
    print("ROM: %s" % describe(data))
    print("SHA-1: %s (%s)" % (sha1, region or "unknown"))

    if sha1 != US_SHA1:
        if region:
            print("error: this is the %s release; only the US ROM is supported by the port" % region, file=sys.stderr)
        else:
            print("error: SHA-1 does not match the US ROM (%s); the image may be modified or a bad dump" % US_SHA1,
                  file=sys.stderr)
        if not args.allow_mismatch:
            return 2

    if args.check_only:
        print("OK" if sha1 == US_SHA1 else "verification failed")
        return 0 if sha1 == US_SHA1 else 2

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print("wrote %s" % args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
