"""The publishing state machine must be safe to retry after partial uploads."""

import hashlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch
import zipfile

from tests.test_release_package import fixture as package_fixture
from tools.build_release_package import assemble_release
from tools.publish_release import GhClient, publish_release, verify_snapshot


COMMIT = "a" * 40
OTHER = "b" * 40
TAG = "v0.1.0"
ZIP_NAME = "Portal2VR-Enriched-v0.1.0-win32.zip"


class FakeGitHub:
    """Only the remote GitHub boundary is replaced; local validation remains real."""

    def __init__(self, release=None, tag_commit=None, assets=None, race_tag=None,
                 corrupt_upload=False):
        self.release = release
        self.tag_commit = tag_commit
        self.assets = dict(assets or {})
        self.race_tag = race_tag
        self.corrupt_upload = corrupt_upload
        self.actions = []

    def get_release(self, repo, tag):
        self.actions.append("get_release")
        return self.release

    def get_tag_commit(self, repo, tag):
        self.actions.append("get_tag_commit")
        return self.tag_commit

    def create_draft(self, repo, tag, commit):
        self.actions.append("create_draft")
        if self.race_tag:
            self.tag_commit = self.race_tag
        self.release = {"tag_name": tag, "target_commitish": commit,
                        "draft": True, "prerelease": False, "assets": []}

    def download_asset(self, repo, tag, name):
        self.actions.append("download:" + name)
        return self.assets[name]

    def upload_asset(self, repo, tag, path):
        self.actions.append("upload:" + path.name)
        self.assets[path.name] = b"corrupt" if self.corrupt_upload else path.read_bytes()
        self.release["assets"].append({"name": path.name})

    def publish_draft(self, repo, tag):
        self.actions.append("publish")
        self.release["draft"] = False
        self.tag_commit = self.release["target_commitish"]


def files(root: Path):
    archive = root / ZIP_NAME
    with zipfile.ZipFile(archive, "w") as package:
        package.writestr("manifest.json", json.dumps({"version": TAG, "commit": COMMIT}))
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    checksum = root / "SHA256SUMS.txt"
    checksum.write_bytes(f"{digest}  {ZIP_NAME}\n".encode("ascii"))
    return archive, checksum


class GhClientTests(unittest.TestCase):
    def test_new_draft_is_read_and_refreshed_by_returned_id_without_list_lookup(self):
        draft = {"id": 42, "tag_name": TAG, "target_commitish": COMMIT,
                 "draft": True, "prerelease": False, "assets": []}
        refreshed = dict(draft, assets=[{"name": ZIP_NAME}])
        published = dict(refreshed, draft=False)
        with patch.object(GhClient, "_run", side_effect=[json.dumps(draft),
                          json.dumps(refreshed), json.dumps(published)]) as run:
            client = GhClient()
            client.create_draft("owner/repo", TAG, COMMIT)
            self.assertEqual(client.get_release("owner/repo", TAG), refreshed)
            self.assertEqual(client.get_release("owner/repo", TAG), published)
        self.assertEqual(run.call_args_list[0].args[:4],
                         ("api", "repos/owner/repo/releases", "--method", "POST"))
        for call in run.call_args_list[1:]:
            self.assertEqual(call.args, ("api", "repos/owner/repo/releases/42"))

    def test_invalid_created_draft_does_not_enable_id_readback(self):
        draft = {"id": 42, "tag_name": TAG, "target_commitish": COMMIT,
                 "draft": True, "prerelease": False, "assets": []}
        variants = [{"id": None}, {"id": True}, {"id": 0}, {"id": "42"},
                    {"tag_name": "v0.2.0"}, {"target_commitish": OTHER},
                    {"draft": False}, {"prerelease": True},
                    {"assets": [{"name": "unexpected.zip"}]}]
        for fields in variants:
            with self.subTest(fields=fields), patch.object(
                    GhClient, "_run", return_value=json.dumps(dict(draft, **fields))) as run:
                with self.assertRaisesRegex(ValueError, "Created draft"):
                    GhClient().create_draft("owner/repo", TAG, COMMIT)
                self.assertEqual(run.call_count, 1)

    def test_created_draft_readback_failure_does_not_rediscover_or_create(self):
        draft = {"id": 42, "tag_name": TAG, "target_commitish": COMMIT,
                 "draft": True, "prerelease": False, "assets": []}
        with patch.object(GhClient, "_run", side_effect=[json.dumps(draft),
                          RuntimeError("HTTP 404")]) as run:
            client = GhClient()
            client.create_draft("owner/repo", TAG, COMMIT)
            with self.assertRaisesRegex(RuntimeError, "HTTP 404"):
                client.get_release("owner/repo", TAG)
        self.assertEqual(run.call_count, 2)
        self.assertEqual(run.call_args_list[1].args,
                         ("api", "repos/owner/repo/releases/42"))

    def test_published_release_uses_direct_tag_lookup(self):
        release = {"tag_name": TAG, "draft": False}
        with patch.object(GhClient, "_run", return_value=json.dumps(release)) as run:
            self.assertEqual(GhClient().get_release("owner/repo", TAG), release)
        run.assert_called_once_with("api", f"repos/owner/repo/releases/tags/{TAG}",
                                    missing_ok=True)

    def test_unpublished_draft_is_found_after_first_release_page(self):
        draft = {"tag_name": TAG, "target_commitish": COMMIT, "draft": True,
                 "prerelease": False, "assets": []}
        pages = [[{"tag_name": "v0.2.0", "draft": False}], [draft]]
        with patch.object(GhClient, "_run", side_effect=[None, json.dumps(pages)]) as run:
            self.assertEqual(GhClient().get_release("owner/repo", TAG), draft)
        self.assertEqual(run.call_args_list[1].args,
                         ("api", "repos/owner/repo/releases?per_page=100",
                          "--paginate", "--slurp"))

    def test_missing_tag_and_draft_returns_none(self):
        with patch.object(GhClient, "_run", side_effect=[None, "[[]]"]):
            self.assertIsNone(GhClient().get_release("owner/repo", TAG))

    def test_duplicate_matching_releases_refuse_ambiguous_draft(self):
        draft = {"tag_name": TAG, "draft": True}
        with patch.object(GhClient, "_run",
                          side_effect=[None, json.dumps([[draft], [draft]])]):
            with self.assertRaisesRegex(ValueError, "Multiple releases"):
                GhClient().get_release("owner/repo", TAG)

    def test_invalid_release_pages_are_not_treated_as_absent_draft(self):
        for pages in ({}, [None], [[None]]):
            with self.subTest(pages=pages), patch.object(
                    GhClient, "_run", side_effect=[None, json.dumps(pages)]):
                with self.assertRaisesRegex(ValueError, "Invalid GitHub release"):
                    GhClient().get_release("owner/repo", TAG)

    def test_failed_tag_lookup_is_not_treated_as_missing_draft(self):
        with patch.object(GhClient, "_run", side_effect=RuntimeError("HTTP 403")) as run:
            with self.assertRaisesRegex(RuntimeError, "HTTP 403"):
                GhClient().get_release("owner/repo", TAG)
        self.assertEqual(run.call_count, 1)


class PublishReleaseTests(unittest.TestCase):
    def test_snapshot_verification_extracts_exact_manifest_bytes(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, exe, output = package_fixture(root)
            snapshot = assemble_release(repo, exe, output, "v0.0.0", COMMIT)
            checksum = output / "SHA256SUMS.txt"
            checksum.write_bytes((hashlib.sha256(snapshot.read_bytes()).hexdigest() +
                                  f"  {snapshot.name}\n").encode("ascii"))
            extracted = root / "extracted"
            verify_snapshot(snapshot, checksum, COMMIT, extracted)
            self.assertEqual((extracted / "Portal 2 VR Enriched Launcher.exe").read_bytes(),
                             exe.read_bytes())
            self.assertEqual((extracted / "Release/d3d9.dll").read_bytes(),
                             (repo / "Release/d3d9.dll").read_bytes())
            stable = assemble_release(extracted, extracted / "Portal 2 VR Enriched Launcher.exe",
                                      root / "release", TAG, COMMIT)
            with zipfile.ZipFile(snapshot) as source, zipfile.ZipFile(stable) as repackaged:
                self.assertEqual(json.loads(repackaged.read("manifest.json"))["version"], TAG)
                for name in source.namelist():
                    if name != "manifest.json":
                        self.assertEqual(repackaged.read(name), source.read(name))

    def test_snapshot_checksum_mismatch_does_not_extract(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, exe, output = package_fixture(root)
            snapshot = assemble_release(repo, exe, output, "v0.0.0", COMMIT)
            checksum = output / "SHA256SUMS.txt"
            checksum.write_bytes(("0" * 64 + f"  {snapshot.name}\n").encode("ascii"))
            extracted = root / "extracted"
            with self.assertRaisesRegex(ValueError, "checksum"):
                verify_snapshot(snapshot, checksum, COMMIT, extracted)
            self.assertFalse(extracted.exists())

    def test_snapshot_manifest_rejects_modified_payload_even_with_new_checksum(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, exe, output = package_fixture(root)
            original = assemble_release(repo, exe, output, "v0.0.0", COMMIT)
            tampered = root / "tampered" / original.name
            tampered.parent.mkdir()
            with zipfile.ZipFile(original) as source, zipfile.ZipFile(tampered, "w") as target:
                for name in source.namelist():
                    payload = source.read(name)
                    target.writestr(name, b"altered DLL" if name == "Release/d3d9.dll" else payload)
            checksum = tampered.parent / "SHA256SUMS.txt"
            checksum.write_bytes((hashlib.sha256(tampered.read_bytes()).hexdigest() +
                                  f"  {tampered.name}\n").encode("ascii"))
            extracted = root / "extracted"
            with self.assertRaisesRegex(ValueError, "manifest hash"):
                verify_snapshot(tampered, checksum, COMMIT, extracted)
            self.assertFalse(extracted.exists())

    def test_new_release_uploads_verified_assets_before_publish(self):
        with TemporaryDirectory() as temporary:
            archive, checksum = files(Path(temporary))
            remote = FakeGitHub()
            publish_release("owner/repo", TAG, COMMIT, archive, checksum, remote)
            self.assertFalse(remote.release["draft"])
            self.assertEqual(remote.tag_commit, COMMIT)
            self.assertEqual(remote.assets[ZIP_NAME], archive.read_bytes())
            self.assertEqual(remote.assets["SHA256SUMS.txt"], checksum.read_bytes())
            self.assertLess(remote.actions.index("upload:" + ZIP_NAME),
                            remote.actions.index("publish"))
            self.assertLess(remote.actions.index("upload:SHA256SUMS.txt"),
                            remote.actions.index("publish"))

    def test_partial_draft_retry_preserves_matching_asset(self):
        with TemporaryDirectory() as temporary:
            archive, checksum = files(Path(temporary))
            release = {"tag_name": TAG, "target_commitish": COMMIT,
                       "draft": True, "prerelease": False,
                       "assets": [{"name": ZIP_NAME}]}
            remote = FakeGitHub(release, assets={ZIP_NAME: archive.read_bytes()})
            publish_release("owner/repo", TAG, COMMIT, archive, checksum, remote)
            self.assertEqual(remote.actions.count("upload:" + ZIP_NAME), 0)
            self.assertIn("upload:SHA256SUMS.txt", remote.actions)
            self.assertFalse(remote.release["draft"])

    def test_published_retry_does_not_mutate_remote(self):
        with TemporaryDirectory() as temporary:
            archive, checksum = files(Path(temporary))
            release = {"tag_name": TAG, "target_commitish": COMMIT,
                       "draft": False, "prerelease": False,
                       "assets": [{"name": ZIP_NAME}, {"name": "SHA256SUMS.txt"}]}
            remote = FakeGitHub(release, COMMIT,
                                {ZIP_NAME: archive.read_bytes(),
                                 "SHA256SUMS.txt": checksum.read_bytes()})
            publish_release("owner/repo", TAG, COMMIT, archive, checksum, remote)
            self.assertFalse(any(action.startswith(("upload:", "create_", "publish"))
                                 for action in remote.actions))

    def test_conflicting_tag_release_or_asset_refuses_mutation(self):
        with TemporaryDirectory() as temporary:
            archive, checksum = files(Path(temporary))
            cases = [
                FakeGitHub(tag_commit=OTHER),
                FakeGitHub({"tag_name": TAG, "target_commitish": OTHER,
                            "draft": True, "prerelease": False, "assets": []}),
                FakeGitHub({"tag_name": TAG, "target_commitish": COMMIT,
                            "draft": True, "prerelease": False,
                            "assets": [{"name": ZIP_NAME}]},
                           assets={ZIP_NAME: b"different"}),
            ]
            for remote in cases:
                with self.subTest(remote=remote), self.assertRaises(ValueError):
                    publish_release("owner/repo", TAG, COMMIT, archive, checksum, remote)
                self.assertFalse(any(action.startswith(("upload:", "create_", "publish"))
                                     for action in remote.actions))

    def test_bad_local_checksum_refuses_remote_contact(self):
        with TemporaryDirectory() as temporary:
            archive, checksum = files(Path(temporary))
            checksum.write_bytes(("0" * 64 + f"  {ZIP_NAME}\n").encode("ascii"))
            remote = FakeGitHub()
            with self.assertRaisesRegex(ValueError, "checksum"):
                publish_release("owner/repo", TAG, COMMIT, archive, checksum, remote)
            self.assertEqual(remote.actions, [])

    def test_tag_created_during_draft_creation_stops_before_upload(self):
        with TemporaryDirectory() as temporary:
            archive, checksum = files(Path(temporary))
            remote = FakeGitHub(race_tag=OTHER)
            with self.assertRaisesRegex(ValueError, "Git tag"):
                publish_release("owner/repo", TAG, COMMIT, archive, checksum, remote)
            self.assertIn("create_draft", remote.actions)
            self.assertFalse(any(action.startswith("upload:") or action == "publish"
                                 for action in remote.actions))

    def test_corrupted_uploaded_asset_stops_before_publish(self):
        with TemporaryDirectory() as temporary:
            archive, checksum = files(Path(temporary))
            remote = FakeGitHub(corrupt_upload=True)
            with self.assertRaisesRegex(ValueError, "asset differs"):
                publish_release("owner/repo", TAG, COMMIT, archive, checksum, remote)
            self.assertNotIn("publish", remote.actions)


if __name__ == "__main__":
    unittest.main()
