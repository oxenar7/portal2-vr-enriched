"""Publish a verified main-commit ZIP through a retry-safe GitHub draft release."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
from tempfile import TemporaryDirectory
import zipfile


class GhClient:
    """Small GitHub CLI boundary; the publisher owns all policy decisions."""

    def __init__(self):
        self._created_release_ids = {}

    @staticmethod
    def _run(*args, missing_ok=False):
        result = subprocess.run(["gh", *args], capture_output=True, text=True,
                                encoding="utf-8", check=False)
        if result.returncode:
            if missing_ok and "HTTP 404" in result.stderr:
                return None
            raise RuntimeError(f"gh {' '.join(args[:2])} failed: {result.stderr.strip()}")
        return result.stdout

    def _api(self, endpoint, missing_ok=False):
        output = self._run("api", endpoint, missing_ok=missing_ok)
        return None if output is None else json.loads(output)

    def get_release(self, repo, tag):
        release_id = self._created_release_ids.get((repo, tag))
        if release_id is not None:
            return self._api(f"repos/{repo}/releases/{release_id}")
        release = self._api(f"repos/{repo}/releases/tags/{tag}", missing_ok=True)
        if release is not None:
            return release
        # The tag endpoint returns published releases only. Authenticated listings
        # include drafts, including ones whose Git tag does not yet exist.
        pages = json.loads(self._run("api", f"repos/{repo}/releases?per_page=100",
                                    "--paginate", "--slurp"))
        if (not isinstance(pages, list) or
                any(not isinstance(page, list) for page in pages) or
                any(not isinstance(item, dict) for page in pages for item in page)):
            raise ValueError("Invalid GitHub release listing")
        matches = [item for page in pages for item in page if item.get("tag_name") == tag]
        if len(matches) > 1:
            raise ValueError(f"Multiple releases have tag {tag}")
        return matches[0] if matches else None

    def get_tag_commit(self, repo, tag):
        ref = self._api(f"repos/{repo}/git/ref/tags/{tag}", missing_ok=True)
        if ref is None:
            return None
        obj = ref.get("object", {})
        for _ in range(5):
            if obj.get("type") == "commit":
                return obj.get("sha")
            if obj.get("type") != "tag" or not re.fullmatch(r"[0-9a-f]{40}",
                                                            obj.get("sha", "")):
                break
            annotated = self._api(f"repos/{repo}/git/tags/{obj['sha']}")
            obj = annotated.get("object", {})
        raise ValueError(f"Tag {tag} does not resolve to a commit")

    def create_draft(self, repo, tag, commit):
        notes = (f"Windows x86 package for commit {commit}.\n\n"
                 "Extract the ZIP and run Portal 2 VR Enriched Launcher.exe. "
                 "See README.txt inside the ZIP and RELEASES.md in the repository.")
        created = json.loads(self._run(
            "api", f"repos/{repo}/releases", "--method", "POST",
            "-f", f"tag_name={tag}", "-f", f"target_commitish={commit}",
            "-F", "draft=true", "-f", f"name=Portal 2 VR Enriched {tag}", "-f", f"body={notes}"))
        if (not isinstance(created, dict) or type(created.get("id")) is not int or
                created["id"] <= 0 or created.get("tag_name") != tag or
                created.get("target_commitish") != commit or created.get("draft") is not True or
                created.get("prerelease") is not False or created.get("assets") != []):
            raise ValueError("Created draft response does not match this release")
        # A new draft can be absent from release listings immediately after POST.
        # Retain its exact identity for readback, asset refresh and publication.
        self._created_release_ids[(repo, tag)] = created["id"]

    def download_asset(self, repo, tag, name):
        with TemporaryDirectory() as temporary:
            self._run("release", "download", tag, "--repo", repo,
                      "--pattern", name, "--dir", temporary)
            path = Path(temporary) / name
            if not path.is_file():
                raise ValueError(f"GitHub did not return release asset {name}")
            return path.read_bytes()

    def upload_asset(self, repo, tag, path):
        self._run("release", "upload", tag, str(path), "--repo", repo)

    def publish_draft(self, repo, tag):
        self._run("release", "edit", tag, "--repo", repo, "--draft=false")


def verify_snapshot(snapshot, checksum, commit, output):
    """Validate an Actions snapshot and extract only its manifest-listed files."""
    snapshot, checksum, output = Path(snapshot), Path(checksum), Path(output)
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected exact lowercase commit SHA")
    if snapshot.name != "Portal2VR-Enriched-v0.0.0-win32.zip" or checksum.name != "SHA256SUMS.txt":
        raise ValueError("Unexpected CI snapshot asset names")
    for path in (snapshot, checksum):
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"Missing or unsafe snapshot asset: {path}")
    expected_line = (hashlib.sha256(snapshot.read_bytes()).hexdigest() +
                     f"  {snapshot.name}\n").encode("ascii")
    if checksum.read_bytes() != expected_line:
        raise ValueError("Snapshot checksum does not match ZIP")
    try:
        with zipfile.ZipFile(snapshot) as package:
            names = package.namelist()
            if len(names) != len(set(names)) or "manifest.json" not in names:
                raise ValueError("Snapshot has duplicate names or no manifest")
            for name in names:
                parts = PurePosixPath(name).parts
                if (not parts or name != PurePosixPath(name).as_posix() or
                        name.startswith("/") or "\\" in name or
                        any(part in (".", "..") for part in parts) or name.endswith("/")):
                    raise ValueError(f"Unsafe snapshot path: {name}")
            manifest_bytes = package.read("manifest.json")
            manifest = json.loads(manifest_bytes)
            if (manifest.get("schema") != 1 or manifest.get("version") != "v0.0.0" or
                    manifest.get("commit") != commit or manifest.get("architecture") != "x86" or
                    not isinstance(manifest.get("files"), dict) or
                    set(names) != set(manifest["files"]) | {"manifest.json"}):
                raise ValueError("Snapshot manifest does not describe this commit and ZIP")
            payloads = {"manifest.json": manifest_bytes}
            for name, record in manifest["files"].items():
                payload = package.read(name)
                if (not isinstance(record, dict) or record.get("size") != len(payload) or
                        record.get("sha256") != hashlib.sha256(payload).hexdigest()):
                    raise ValueError(f"Snapshot manifest hash differs: {name}")
                payloads[name] = payload
    except (zipfile.BadZipFile, KeyError, json.JSONDecodeError) as error:
        raise ValueError("Snapshot ZIP or manifest is invalid") from error
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"Snapshot extraction directory already exists: {output}")
    output.mkdir(parents=True)
    for name, payload in payloads.items():
        destination = output.joinpath(*PurePosixPath(name).parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(payload)


def _asset_names(release, expected):
    if not isinstance(release, dict) or not isinstance(release.get("assets"), list):
        raise ValueError("Invalid GitHub release response")
    names = [asset.get("name") for asset in release["assets"]
             if isinstance(asset, dict)]
    if len(names) != len(release["assets"]) or len(names) != len(set(names)):
        raise ValueError("Invalid or duplicate release assets")
    if not set(names).issubset(expected):
        raise ValueError("Release has unexpected assets")
    return set(names)


def _check_release(release, tag, commit, expected):
    if release.get("tag_name") != tag or release.get("target_commitish") != commit:
        raise ValueError("Release tag or target commit differs from this build")
    if release.get("prerelease") is not False or not isinstance(release.get("draft"), bool):
        raise ValueError("Release has unexpected state")
    return _asset_names(release, expected)


def _check_local(tag, commit, archive, checksum):
    if not re.fullmatch(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", tag):
        raise ValueError("Expected a stable vMAJOR.MINOR.PATCH tag")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected exact lowercase commit SHA")
    if archive.name != f"Portal2VR-Enriched-{tag}-win32.zip" or checksum.name != "SHA256SUMS.txt":
        raise ValueError("Release asset names differ from the version")
    for path in (archive, checksum):
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"Missing or unsafe release asset: {path}")
    archive_bytes = archive.read_bytes()
    expected_line = (hashlib.sha256(archive_bytes).hexdigest() +
                     f"  {archive.name}\n").encode("ascii")
    if checksum.read_bytes() != expected_line:
        raise ValueError("Local release checksum does not match ZIP")
    try:
        with zipfile.ZipFile(archive) as package:
            manifest = json.loads(package.read("manifest.json"))
    except (zipfile.BadZipFile, KeyError, json.JSONDecodeError) as error:
        raise ValueError("Release ZIP has no valid manifest") from error
    if manifest.get("version") != tag or manifest.get("commit") != commit:
        raise ValueError("Release ZIP manifest differs from version or commit")


def publish_release(repo, tag, commit, archive, checksum, client=None):
    """Ensure exactly this commit and these bytes are published, or fail closed."""
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
        raise ValueError("Expected owner/repository")
    archive, checksum = Path(archive), Path(checksum)
    _check_local(tag, commit, archive, checksum)
    client = client or GhClient()
    expected = {archive.name: archive, checksum.name: checksum}

    release = client.get_release(repo, tag)
    tag_commit = client.get_tag_commit(repo, tag)
    if tag_commit is not None and tag_commit != commit:
        raise ValueError("Existing Git tag points to another commit")
    if release is None:
        client.create_draft(repo, tag, commit)
        tag_commit = client.get_tag_commit(repo, tag)
        if tag_commit is not None and tag_commit != commit:
            raise ValueError("Git tag changed during draft creation")
        release = client.get_release(repo, tag)
        if release is None:
            raise ValueError("New draft release could not be read back")
    names = _check_release(release, tag, commit, expected)
    if not release["draft"] and tag_commit is None:
        raise ValueError("Published release has no Git tag")
    if not release["draft"] and names != set(expected):
        raise ValueError("Published release is missing an asset")

    for name, path in expected.items():
        if name in names:
            remote = client.download_asset(repo, tag, name)
            if hashlib.sha256(remote).digest() != hashlib.sha256(path.read_bytes()).digest():
                raise ValueError(f"Existing release asset differs: {name}")
        elif release["draft"]:
            client.upload_asset(repo, tag, path)

    if release["draft"]:
        refreshed = client.get_release(repo, tag)
        if refreshed is None or _check_release(refreshed, tag, commit, expected) != set(expected):
            raise ValueError("Draft release assets could not be verified after upload")
        for name, path in expected.items():
            remote = client.download_asset(repo, tag, name)
            if hashlib.sha256(remote).digest() != hashlib.sha256(path.read_bytes()).digest():
                raise ValueError(f"Uploaded release asset differs: {name}")
        tag_commit = client.get_tag_commit(repo, tag)
        if tag_commit is not None and tag_commit != commit:
            raise ValueError("Git tag changed before publication")
        client.publish_draft(repo, tag)
        published = client.get_release(repo, tag)
        if published is None or published["draft"] or _check_release(
                published, tag, commit, expected) != set(expected):
            raise ValueError("Published release could not be verified")
        if client.get_tag_commit(repo, tag) != commit:
            raise ValueError("Published tag does not point to the release commit")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--zip", type=Path, required=True)
    parser.add_argument("--checksum", type=Path, required=True)
    args = parser.parse_args()
    publish_release(args.repo, args.tag, args.commit, args.zip, args.checksum)
    print(f"Published or verified {args.tag} at {args.commit}")


if __name__ == "__main__":
    main()
