"""Release ZIP contains only runnable launcher inputs and attribution."""

import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest
import zipfile

from tests.test_test_launcher import TEMPLATE
from tools import test_launcher_core as core
from tools.build_release_package import assemble_release


COMMIT = "a" * 40
PACKAGE_FILES = {
    "Portal 2 VR Enriched Launcher.exe",
    "Release/d3d9.dll",
    "thirdparty/openvr/bin/win32/openvr_api.dll",
    "L4D2VR/config.txt",
    "L4D2VR/manifest.vrmanifest",
    "L4D2VR/portal2vr_capsule_main.png",
    "L4D2VR/SteamVRActionManifest/action_manifest.json",
    "L4D2VR/SteamVRActionManifest/bindings_knuckles.json",
    "L4D2VR/SteamVRActionManifest/bindings_oculus_touch.json",
    "L4D2VR/SteamVRActionManifest/bindings_vive_cosmos_controller.json",
    "THIRD_PARTY_SOURCES.txt",
    "dxvk/LICENSE",
    "dxvk/include/openvr/LICENSE",
    "thirdparty/minhook/LICENSE.txt",
    "L4D2VR/sdk/LICENSE.txt",
    "README.txt",
    "manifest.json",
}


def pe(machine: int) -> bytes:
    contents = bytearray(128)
    contents[:2] = b"MZ"
    struct.pack_into("<I", contents, 0x3c, 0x40)
    contents[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<H", contents, 0x44, machine)
    return bytes(contents)


def fixture(root: Path) -> tuple[Path, Path, Path]:
    repo = root / "repo"
    exe = root / "compiled" / "Portal 2 VR Enriched Launcher.exe"
    files = {name: b"fixture payload" for name in PACKAGE_FILES -
             {"Portal 2 VR Enriched Launcher.exe", "README.txt", "manifest.json"}}
    files["Release/d3d9.dll"] = pe(0x14c)
    files["thirdparty/openvr/bin/win32/openvr_api.dll"] = pe(0x14c)
    files["L4D2VR/config.txt"] = TEMPLATE.encode("utf-8")
    for name, contents in files.items():
        target = repo / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(contents)
    (repo / "secret.txt").write_bytes(b"do not package")
    (repo / "tools").mkdir()
    (repo / "tools/.launcher-state.json").write_bytes(b"local state")
    exe.parent.mkdir(parents=True)
    exe.write_bytes(pe(0x8664))
    return repo, exe, root / "packages"


class ReleasePackageTests(unittest.TestCase):
    def test_allowlisted_zip_has_verified_binary_payload_and_manifest(self):
        try:
            from tools.build_release_package import assemble_release
        except ModuleNotFoundError:
            self.fail("Release ZIP assembler is missing")

        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, exe, output = fixture(root)
            archive = assemble_release(repo, exe, output, "v0.1.0", COMMIT)
            self.assertEqual(archive.name, "Portal2VR-Enriched-v0.1.0-win32.zip")
            with zipfile.ZipFile(archive) as package:
                self.assertEqual(set(package.namelist()), PACKAGE_FILES)
                manifest = json.loads(package.read("manifest.json"))
                self.assertEqual(manifest["version"], "v0.1.0")
                self.assertEqual(manifest["commit"], COMMIT)
                self.assertEqual(manifest["architecture"], "x86")
                self.assertEqual(set(manifest["files"]), PACKAGE_FILES - {"manifest.json"})
                for name, recorded in manifest["files"].items():
                    payload = package.read(name)
                    self.assertEqual(recorded["size"], len(payload))
                    self.assertEqual(recorded["sha256"], hashlib.sha256(payload).hexdigest())

    def test_extracted_layout_is_usable_by_launcher_plan(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, exe, output = fixture(root)
            archive = assemble_release(repo, exe, output, "v0.1.0", COMMIT)
            extracted = root / "extracted"
            with zipfile.ZipFile(archive) as package:
                package.extractall(extracted)
            game = root / "Portal 2"
            (game / "bin").mkdir(parents=True)
            (game / "portal2.exe").write_bytes(b"fixture game")
            planned = core.plan_install(extracted, game, "baseline")
            self.assertEqual(len(planned), 9)
            self.assertEqual(planned[game / "bin/d3d9.dll"], pe(0x14c))
            self.assertIn(b"TrackingMode=Seated", planned[game / "VR/config.txt"])
            self.assertFalse((game / "bin/d3d9.dll").exists())

    def test_zip_bytes_are_deterministic_across_output_directories(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, exe, output = fixture(root)
            one = assemble_release(repo, exe, output, "v0.1.0", COMMIT)
            two = assemble_release(repo, exe, root / "other", "v0.1.0", COMMIT)
            self.assertEqual(one.read_bytes(), two.read_bytes())

    def test_wrong_architecture_missing_input_and_empty_input_make_no_zip(self):
        for defect in ("wrong_arch", "missing", "empty"):
            with self.subTest(defect=defect), TemporaryDirectory() as temporary:
                root = Path(temporary)
                repo, exe, output = fixture(root)
                dll = repo / "Release/d3d9.dll"
                if defect == "wrong_arch":
                    dll.write_bytes(pe(0x8664))
                elif defect == "missing":
                    dll.unlink()
                else:
                    dll.write_bytes(b"")
                with self.assertRaises((ValueError, FileNotFoundError)):
                    assemble_release(repo, exe, output, "v0.1.0", COMMIT)
                self.assertFalse(output.exists())

    def test_existing_archive_is_never_overwritten(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, exe, output = fixture(root)
            output.mkdir()
            archive = output / "Portal2VR-Enriched-v0.1.0-win32.zip"
            archive.write_bytes(b"unrelated artifact")
            with self.assertRaises(FileExistsError):
                assemble_release(repo, exe, output, "v0.1.0", COMMIT)
            self.assertEqual(archive.read_bytes(), b"unrelated artifact")

    def test_unsafe_version_commit_and_symlink_source_are_rejected(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, exe, output = fixture(root)
            for version, commit in (("../escape", COMMIT), ("v0.1.0", "wrong"),
                                    ("v0.1.0-beta", COMMIT)):
                with self.subTest(version=version, commit=commit), self.assertRaises(ValueError):
                    assemble_release(repo, exe, output, version, commit)
            source = repo / "L4D2VR/manifest.vrmanifest"
            source.unlink()
            try:
                source.symlink_to(repo / "secret.txt")
            except OSError:
                self.skipTest("Symlink creation is unavailable")
            with self.assertRaises(ValueError):
                assemble_release(repo, exe, output, "v0.1.0", COMMIT)
            self.assertFalse(output.exists())

    def test_cli_writes_the_package_to_requested_output_directory(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, exe, output = fixture(root)
            result = subprocess.run(
                [sys.executable, "-m", "tools.build_release_package", "--repo", str(repo),
                 "--launcher-exe", str(exe), "--output", str(output),
                 "--version", "v0.1.0", "--commit", COMMIT],
                cwd=Path(__file__).resolve().parents[1], capture_output=True,
                text=True, check=True)
            archive = output / "Portal2VR-Enriched-v0.1.0-win32.zip"
            self.assertEqual(result.stdout.strip(), str(archive))
            self.assertTrue(archive.is_file())


if __name__ == "__main__":
    unittest.main()
