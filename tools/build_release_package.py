"""Assemble the exact runnable Portal 2 VR Enriched release tree as a verified ZIP."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile
import zipfile

from .test_launcher_core import SOURCE_TO_TARGET


LAUNCHER_NAME = "Portal 2 VR Enriched Launcher.exe"
LICENSES = (
    "THIRD_PARTY_SOURCES.txt",
    "dxvk/LICENSE",
    "dxvk/include/openvr/LICENSE",
    "thirdparty/minhook/LICENSE.txt",
    "L4D2VR/sdk/LICENSE.txt",
)
README = ("Portal 2 VR Enriched for Windows\n"
          "================================\n\n"
          "Requirements: 64-bit Windows, installed Portal 2, Steam and SteamVR,\n"
          "a Vulkan-capable graphics driver, and Microsoft Visual C++ v14\n"
          "Redistributable (x86), at least as recent as the MSVC build tools.\n"
          "Latest supported x86 runtime: https://aka.ms/vc14/vc_redist.x86.exe\n"
          "Python and build tools are not required.\n\n"
          "1. Extract this entire ZIP to a writable folder. Keep its folders together.\n"
          "2. Close Portal 2, then double-click Portal 2 VR Enriched Launcher.exe.\n"
          "3. Select your Portal 2 and Steam paths. Choose Apply or Launch.\n"
          "4. Use Restore original game files in the launcher when finished.\n\n"
          "Keep the launcher-created tools folder and .launcher-backups with this package\n"
          "until you have restored your original game files.\n\n"
          "Compatibility hooks require a verified DLL build and loaded code/table references.\n"
          "Verification or installation failures skip that hook with warnings in bin/portal2vr.log.\n"
          "Source's original shoot position or viewmodel FOV may be used.\n"
          "Verbose diagnostics in Advanced enables repeated texture/MSAA details and periodic summaries.\n").encode("utf-8")


def _source_bytes(repo: Path, relative: str) -> bytes:
    current = repo
    for part in Path(relative).parts:
        current = current / part
        if current.is_symlink():
            raise ValueError(f"Package source must not be a symlink: {current}")
    if not current.is_file() or not current.resolve().is_relative_to(repo.resolve()):
        raise FileNotFoundError(f"Missing or unsafe release input: {current}")
    payload = current.read_bytes()
    if not payload:
        raise ValueError(f"Empty release input: {current}")
    return payload


def _pe_machine(payload: bytes, name: str) -> int:
    if len(payload) < 64 or payload[:2] != b"MZ":
        raise ValueError(f"Invalid PE executable: {name}")
    header = struct.unpack_from("<I", payload, 0x3c)[0]
    if header > len(payload) - 6 or payload[header:header + 4] != b"PE\0\0":
        raise ValueError(f"Invalid PE executable: {name}")
    return struct.unpack_from("<H", payload, header + 4)[0]


def _entry(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = 0o644 << 16
    return info


def assemble_release(repo: Path, launcher_exe: Path, output: Path,
                     version: str, commit: str) -> Path:
    """Write an allowlisted, no-clobber release ZIP and return its path."""
    if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("Release version must be a stable vMAJOR.MINOR.PATCH tag")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Release commit must be a lowercase 40-character SHA-1")
    repo = Path(repo)
    launcher_exe = Path(launcher_exe)
    output = Path(output)
    if launcher_exe.is_symlink() or not launcher_exe.is_file():
        raise FileNotFoundError(f"Missing or unsafe launcher executable: {launcher_exe}")
    payloads = {LAUNCHER_NAME: launcher_exe.read_bytes(), "README.txt": README}
    if _pe_machine(payloads[LAUNCHER_NAME], LAUNCHER_NAME) not in (0x14c, 0x8664):
        raise ValueError("Launcher executable has an unsupported PE architecture")
    for relative, _ in SOURCE_TO_TARGET:
        payloads[relative] = _source_bytes(repo, relative)
    for name in LICENSES:
        payloads[name] = _source_bytes(repo, name)
    if (repo / "LICENSE").exists() or (repo / "LICENSE").is_symlink():
        payloads["LICENSE"] = _source_bytes(repo, "LICENSE")
    for dll in ("Release/d3d9.dll", "thirdparty/openvr/bin/win32/openvr_api.dll"):
        if _pe_machine(payloads[dll], dll) != 0x14c:
            raise ValueError(f"Release DLL must be 32-bit x86 PE: {dll}")
    manifest = {
        "schema": 1, "version": version, "commit": commit, "architecture": "x86",
        "files": {name: {"sha256": hashlib.sha256(contents).hexdigest(),
                         "size": len(contents)}
                  for name, contents in sorted(payloads.items())},
    }
    payloads["manifest.json"] = (json.dumps(manifest, sort_keys=True, indent=2) + "\n").encode("utf-8")
    if output.is_symlink():
        raise ValueError(f"Release output must not be a symlink: {output}")
    output.mkdir(parents=True, exist_ok=True)
    destination = output / f"Portal2VR-Enriched-{version}-win32.zip"
    if destination.exists() or destination.is_symlink():
        raise FileExistsError(f"Release archive already exists: {destination}")
    with tempfile.NamedTemporaryFile(prefix="Portal2VR-Enriched-", suffix=".zip.tmp",
                                     dir=output, delete=False) as stream:
        temporary = Path(stream.name)
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=9) as archive:
            for name, contents in sorted(payloads.items()):
                archive.writestr(_entry(name), contents, compress_type=zipfile.ZIP_DEFLATED,
                                 compresslevel=9)
        with temporary.open("r+b") as stream:
            stream.flush()
            os.fsync(stream.fileno())
        # A same-directory hard link publishes atomically and refuses to clobber.
        os.link(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--launcher-exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    print(assemble_release(args.repo, args.launcher_exe, args.output,
                           args.version, args.commit))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
