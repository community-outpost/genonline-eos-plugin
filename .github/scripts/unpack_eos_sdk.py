# SPDX-License-Identifier: MIT
# Checks the pinned SHA-256 of the EOS SDK zip, unpacks it and checks that it holds the Windows x86 files.
#
# python unpack_eos_sdk.py <zip> <expected sha256> <destination folder>
#
# The script prints no file list, so the SDK contents never reach the build log.

import hashlib
import sys
import zipfile
from pathlib import Path

REQUIRED_FILES = ["Include/eos_sdk.h", "Bin/EOSSDK-Win32-Shipping.dll", "Lib/EOSSDK-Win32-Shipping.lib"]


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: unpack_eos_sdk.py <zip> <expected sha256> <destination folder>", file=sys.stderr)
        return 2
    archive = Path(sys.argv[1])
    expected = sys.argv[2].strip().lower()
    destination = Path(sys.argv[3])

    if not archive.is_file():
        print(f"The package holds no {archive.name}", file=sys.stderr)
        return 1
    actual = sha256_of(archive)
    if actual != expected:
        print(f"{archive.name} has SHA-256 {actual}, expected {expected}", file=sys.stderr)
        return 1
    print(f"{archive.name} matches its pinned SHA-256")

    destination.mkdir(parents=True, exist_ok=True)
    root = destination.resolve()
    with zipfile.ZipFile(archive) as content:
        for member in content.infolist():
            # Every entry must land inside the destination folder.
            if not (root / member.filename).resolve().is_relative_to(root):
                print("The EOS SDK zip holds a path outside its folder", file=sys.stderr)
                return 1
        content.extractall(root)

    missing = [name for name in REQUIRED_FILES if not (root / name).is_file()]
    if missing:
        print(f"The EOS SDK lacks {', '.join(missing)} for Windows x86", file=sys.stderr)
        return 1
    print("The EOS SDK for Windows x86 is ready")
    return 0


if __name__ == "__main__":
    sys.exit(main())
