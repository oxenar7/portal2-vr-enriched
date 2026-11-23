"""Find, check and install a newer release of the packaged launcher.

Only a release package updates itself: its manifest.json names the version it is. A source
checkout has no manifest and never checks. The update replaces the package's own files (the
launcher, the mod files it installs, licenses); the install state and backups beside them are
not in a release and stay. The game gets the new mod files the next time Play in VR installs.
"""
from __future__ import annotations

import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import urllib.request
import zipfile

REPOSITORY = "oxenar7/portal2-vr-enriched"
LATEST_URL = f"https://api.github.com/repos/{REPOSITORY}/releases/latest"
USER_AGENT = "Portal2VR-Enriched-Launcher"
SUMS_NAME = "SHA256SUMS.txt"
OLD_SUFFIX = ".old"
STAGING = ".update-staging"
MAX_PACKAGE = 64 * 1024 * 1024
VERSION = re.compile(r"v([0-9]+)\.([0-9]+)\.([0-9]+)")


class UpdateError(Exception):
    """An update that could not be found, downloaded, checked or installed."""


def parse_version(text: str):
    match = VERSION.fullmatch(text or "")
    return tuple(int(part) for part in match.groups()) if match else None


def current_version(root: Path) -> str | None:
    """The version of the release package in root, or None outside a release package."""
    try:
        manifest = json.loads((Path(root) / "manifest.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    version = manifest.get("version") if isinstance(manifest, dict) else None
    return version if manifest.get("schema") == 1 and parse_version(version) else None


def is_newer(latest: str, current: str) -> bool:
    new, old = parse_version(latest), parse_version(current)
    return bool(new and old and new > old)


def package_name(version: str) -> str:
    return f"Portal2VR-Enriched-{version}-win32.zip"


def _get(url: str, limit: int, timeout: float) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT,
                                                   "Accept": "application/octet-stream, application/json"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        payload = response.read(limit + 1)
    if len(payload) > limit:
        raise UpdateError(f"Download larger than expected: {url}")
    return payload


def latest_release(timeout: float = 6.0, fetch=_get) -> dict:
    """The newest published release: its version, page, ZIP and checksum download links."""
    try:
        release = json.loads(fetch(LATEST_URL, 1024 * 1024, timeout).decode("utf-8"))
    except (OSError, ValueError) as error:
        raise UpdateError(f"Could not check for updates: {error}") from error
    version = release.get("tag_name", "") if isinstance(release, dict) else ""
    if not parse_version(version) or release.get("draft") or release.get("prerelease"):
        raise UpdateError("The latest release has no usable version")
    assets = {asset.get("name"): asset.get("browser_download_url") for asset in release.get("assets", [])
              if isinstance(asset, dict)}
    package, sums = assets.get(package_name(version)), assets.get(SUMS_NAME)
    if not package or not sums:
        raise UpdateError(f"Release {version} lacks its package or checksums")
    return {"version": version, "page": release.get("html_url", ""), "package": package, "sums": sums}


def _safe_name(name: str) -> bool:
    path = PurePosixPath(name)
    return (bool(name) and "\\" not in name and ":" not in name and not path.is_absolute()
            and ".." not in path.parts and not name.endswith("/"))


def verify_package(payload: bytes, sums: str, version: str) -> dict[str, bytes]:
    """Check the ZIP against SHA256SUMS and its own manifest; return its files by name."""
    expected = None
    for line in sums.splitlines():
        digest, _, name = line.strip().partition("  ")
        if name.strip().lstrip("*") == package_name(version):
            expected = digest.lower()
    if not expected or hashlib.sha256(payload).hexdigest() != expected:
        raise UpdateError("The downloaded package does not match its published checksum")
    try:
        archive = zipfile.ZipFile(io.BytesIO(payload))
        manifest = json.loads(archive.read("manifest.json").decode("utf-8"))
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        raise UpdateError(f"The downloaded package is damaged: {error}") from error
    if manifest.get("schema") != 1 or manifest.get("version") != version:
        raise UpdateError("The downloaded package names a different version")
    listed = manifest.get("files")
    if not isinstance(listed, dict) or not listed:
        raise UpdateError("The downloaded package lists no files")
    files = {}
    for name, entry in listed.items():
        if not _safe_name(name):
            raise UpdateError(f"The downloaded package has an unsafe file name: {name}")
        try:
            contents = archive.read(name)
        except KeyError as error:
            raise UpdateError(f"The downloaded package lacks {name}") from error
        if (not isinstance(entry, dict) or entry.get("size") != len(contents) or
                entry.get("sha256") != hashlib.sha256(contents).hexdigest()):
            raise UpdateError(f"The downloaded package's {name} does not match its manifest")
        files[name] = contents
    files["manifest.json"] = archive.read("manifest.json")
    return files


def download_update(release: dict, timeout: float = 60.0, fetch=_get) -> dict[str, bytes]:
    try:
        sums = fetch(release["sums"], 64 * 1024, timeout).decode("utf-8")
        payload = fetch(release["package"], MAX_PACKAGE, timeout)
    except (OSError, ValueError) as error:
        raise UpdateError(f"Could not download {release['version']}: {error}") from error
    return verify_package(payload, sums, release["version"])


def install_update(files: dict[str, bytes], root: Path) -> None:
    """Write the new package files over the old ones in root.

    Everything is written to a staging folder first, so a full disk or a locked folder fails
    before any file changes. A file Windows keeps open, such as the running launcher, is
    renamed aside and removed on the next start.
    """
    root = Path(root)
    staging = root / STAGING
    shutil.rmtree(staging, ignore_errors=True)
    try:
        for name, contents in files.items():
            target = staging / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(contents)
        # The manifest goes last: until it is replaced the package still says the old version.
        for name in sorted(files, key=lambda name: name == "manifest.json"):
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            try:
                os.replace(staging / name, target)
            except PermissionError:
                aside = target.with_name(target.name + OLD_SUFFIX)
                aside.unlink(missing_ok=True)
                os.replace(target, aside)
                os.replace(staging / name, target)
    except OSError as error:
        raise UpdateError(f"Could not install the update: {error}") from error
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def remove_leftovers(root: Path) -> None:
    """Delete the package files an earlier update set aside, once nothing holds them open."""
    root = Path(root)
    shutil.rmtree(root / STAGING, ignore_errors=True)
    try:
        names = json.loads((root / "manifest.json").read_text(encoding="utf-8")).get("files", {})
    except (OSError, ValueError, AttributeError):
        return
    for name in names:
        if _safe_name(name):
            try:
                (root / (name + OLD_SUFFIX)).unlink(missing_ok=True)
            except OSError:
                pass
