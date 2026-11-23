"""The packaged launcher finds, checks and installs a newer release without the network here."""
import hashlib
import io
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch
import zipfile

from tools import launcher_update as update


def make_package(version, files, manifest_version=None, tamper=None):
    """A release ZIP shaped like build_release_package's, and its SHA256SUMS text."""
    listed = {name: {"sha256": hashlib.sha256(data).hexdigest(), "size": len(data)} for name, data in files.items()}
    manifest = {"schema": 1, "version": manifest_version or version, "commit": "0" * 40, "files": listed}
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        archive.writestr("manifest.json", json.dumps(manifest))
        for name, data in files.items():
            archive.writestr(name, tamper.get(name, data) if tamper else data)
    payload = buffer.getvalue()
    sums = f"{hashlib.sha256(payload).hexdigest()}  {update.package_name(version)}\n"
    return payload, sums


def release_json(version, prerelease=False, assets=None):
    names = assets if assets is not None else [update.package_name(version), update.SUMS_NAME]
    return json.dumps({"tag_name": version, "draft": False, "prerelease": prerelease,
                       "html_url": f"https://example.invalid/{version}",
                       "assets": [{"name": name, "browser_download_url": f"https://example.invalid/{name}"}
                                  for name in names]}).encode()


class VersionTests(unittest.TestCase):
    def test_newer_compares_numbers_not_text(self):
        self.assertTrue(update.is_newer("v1.10.0", "v1.9.9"))
        self.assertTrue(update.is_newer("v1.2.0", "v1.1.2"))
        self.assertFalse(update.is_newer("v1.1.2", "v1.1.2"))
        self.assertFalse(update.is_newer("v1.1.0", "v1.1.2"))
        self.assertFalse(update.is_newer("latest", "v1.1.2"))

    def test_current_version_only_from_a_release_manifest(self):
        with TemporaryDirectory() as folder:
            root = Path(folder)
            self.assertIsNone(update.current_version(root))
            (root / "manifest.json").write_text(json.dumps({"schema": 1, "version": "v1.2.0"}), encoding="utf-8")
            self.assertEqual(update.current_version(root), "v1.2.0")
            (root / "manifest.json").write_text("not json", encoding="utf-8")
            self.assertIsNone(update.current_version(root))


class LatestReleaseTests(unittest.TestCase):
    def test_reads_the_package_and_checksum_links(self):
        release = update.latest_release(fetch=lambda url, limit, timeout: release_json("v1.2.0"))
        self.assertEqual(release["version"], "v1.2.0")
        self.assertTrue(release["package"].endswith(update.package_name("v1.2.0")))
        self.assertTrue(release["sums"].endswith(update.SUMS_NAME))

    def test_refuses_prereleases_and_incomplete_releases(self):
        for payload in (release_json("v1.2.0", prerelease=True), release_json("v1.2.0", assets=[]),
                        release_json("nightly"), b"<html>"):
            with self.subTest(payload=payload[:40]), self.assertRaises(update.UpdateError):
                update.latest_release(fetch=lambda url, limit, timeout, payload=payload: payload)

    def test_network_failure_is_an_update_error(self):
        def offline(url, limit, timeout):
            raise OSError("offline")
        with self.assertRaises(update.UpdateError):
            update.latest_release(fetch=offline)


class VerifyTests(unittest.TestCase):
    FILES = {"Portal 2 VR Enriched Launcher.exe": b"new launcher", "VR/d3d9.dll": b"new dll"}

    def test_accepts_a_package_that_matches_its_checksum_and_manifest(self):
        payload, sums = make_package("v1.2.0", self.FILES)
        files = update.verify_package(payload, sums, "v1.2.0")
        self.assertEqual(files["VR/d3d9.dll"], b"new dll")
        self.assertIn("manifest.json", files)

    def test_rejects_a_changed_download(self):
        payload, sums = make_package("v1.2.0", self.FILES)
        with self.assertRaises(update.UpdateError):
            update.verify_package(payload + b"x", sums, "v1.2.0")

    def test_rejects_a_file_that_differs_from_the_manifest(self):
        payload, sums = make_package("v1.2.0", self.FILES, tamper={"VR/d3d9.dll": b"other dll"})
        with self.assertRaises(update.UpdateError):
            update.verify_package(payload, sums, "v1.2.0")

    def test_rejects_a_package_that_names_another_version(self):
        payload, sums = make_package("v1.2.0", self.FILES, manifest_version="v1.1.2")
        with self.assertRaises(update.UpdateError):
            update.verify_package(payload, sums, "v1.2.0")

    def test_rejects_names_that_leave_the_package_folder(self):
        for name in ("../evil.dll", "C:/evil.dll", "/evil.dll", "VR\\..\\evil.dll"):
            payload, sums = make_package("v1.2.0", {name: b"evil"})
            with self.subTest(name=name), self.assertRaises(update.UpdateError):
                update.verify_package(payload, sums, "v1.2.0")

    def test_download_checks_what_it_fetched(self):
        payload, sums = make_package("v1.2.0", self.FILES)
        release = {"version": "v1.2.0", "package": "package", "sums": "sums"}
        fetched = {"package": payload, "sums": sums.encode()}
        files = update.download_update(release, fetch=lambda url, limit, timeout: fetched[url])
        self.assertEqual(files["Portal 2 VR Enriched Launcher.exe"], b"new launcher")


class InstallTests(unittest.TestCase):
    def test_replaces_package_files_and_keeps_everything_else(self):
        with TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "VR").mkdir()
            (root / "VR/d3d9.dll").write_bytes(b"old dll")
            (root / "launcher_preferences.json").write_text("{}", encoding="utf-8")
            (root / "manifest.json").write_text(json.dumps({"schema": 1, "version": "v1.1.2"}), encoding="utf-8")
            payload, sums = make_package("v1.2.0", {"VR/d3d9.dll": b"new dll", "README.txt": b"readme"})
            update.install_update(update.verify_package(payload, sums, "v1.2.0"), root)
            self.assertEqual((root / "VR/d3d9.dll").read_bytes(), b"new dll")
            self.assertEqual((root / "README.txt").read_bytes(), b"readme")
            self.assertEqual((root / "launcher_preferences.json").read_text(encoding="utf-8"), "{}")
            self.assertEqual(update.current_version(root), "v1.2.0")
            self.assertFalse((root / update.STAGING).exists())

    def test_a_file_in_use_is_set_aside_and_removed_later(self):
        with TemporaryDirectory() as folder:
            root = Path(folder)
            exe = root / "launcher.exe"
            exe.write_bytes(b"running launcher")
            payload, sums = make_package("v1.2.0", {"launcher.exe": b"new launcher"})
            files = update.verify_package(payload, sums, "v1.2.0")
            real_replace, refused = update.os.replace, []

            def replace(source, target):
                # Windows refuses to overwrite the running executable but lets it be renamed.
                if Path(target) == exe and Path(source).parent.name == update.STAGING and not refused:
                    refused.append(target)
                    raise PermissionError("in use")
                return real_replace(source, target)

            with patch.object(update.os, "replace", replace):
                update.install_update(files, root)
            self.assertEqual(exe.read_bytes(), b"new launcher")
            self.assertEqual((root / "launcher.exe.old").read_bytes(), b"running launcher")
            update.remove_leftovers(root)
            self.assertFalse((root / "launcher.exe.old").exists())
            self.assertTrue(exe.exists())

    def test_a_failed_write_leaves_the_old_version(self):
        with TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "manifest.json").write_text(json.dumps({"schema": 1, "version": "v1.1.2"}), encoding="utf-8")
            payload, sums = make_package("v1.2.0", {"a.dll": b"a"})
            files = update.verify_package(payload, sums, "v1.2.0")
            with patch.object(Path, "write_bytes", side_effect=OSError("disk full")), \
                    self.assertRaises(update.UpdateError):
                update.install_update(files, root)
            self.assertEqual(update.current_version(root), "v1.1.2")
            self.assertFalse((root / "a.dll").exists())


if __name__ == "__main__":
    unittest.main()
