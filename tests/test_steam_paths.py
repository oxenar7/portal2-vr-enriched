"""Steam and Portal 2 folders are found from Steam's own files, never guessed."""
from pathlib import Path
from tempfile import TemporaryDirectory
import os
import unittest

from tools import steam_paths

LIBRARY_FOLDERS = r'''"libraryfolders"
{
	"0"
	{
		"path"		"%s"
		"label"		""
		"apps"
		{
			"228980"		"312197492"
		}
	}
	// Steam writes one block per library.
	"1"
	{
		"path"		"%s"
		"apps"
		{
			"620"		"12345678"
		}
	}
}
'''

APP_MANIFEST = '''"AppState"
{
	"appid"		"620"
	"name"		"Portal 2"
	"installdir"		"%s"
}
'''


def escaped(path: Path) -> str:
    return str(path).replace("\\", "\\\\")


class KeyValuesTests(unittest.TestCase):
    def test_parses_nested_blocks_escapes_and_comments(self):
        data = steam_paths.parse_keyvalues(LIBRARY_FOLDERS % ("C:\\\\Steam", "D:\\\\Games \\\"A\\\""))
        self.assertEqual(data["libraryfolders"]["0"]["path"], "C:\\Steam")
        self.assertEqual(data["libraryfolders"]["1"]["path"], 'D:\\Games "A"')
        self.assertEqual(data["libraryfolders"]["1"]["apps"], {"620": "12345678"})

    def test_keys_are_case_insensitive_like_old_steam_files(self):
        data = steam_paths.parse_keyvalues('"LibraryFolders" { "TimeNextStatsReport" "1" "1" "E:\\\\Lib" }')
        self.assertEqual(data["libraryfolders"]["1"], "E:\\Lib")

    def test_malformed_text_is_rejected(self):
        for text in ('"a" {', '"a" }', '{ }', '"key"', '"a" "b" }'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                steam_paths.parse_keyvalues(text)


class FindInstallationTests(unittest.TestCase):
    def setUp(self):
        temporary = TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name).resolve()
        self.steam_root = self.folder / "Steam"
        (self.steam_root / "steamapps").mkdir(parents=True)
        self.steam = self.steam_root / "steam.exe"
        self.steam.write_bytes(b"fixture")
        self.library = self.folder / "Second Library"

    def install_portal2(self, library: Path, folder="Portal 2", manifest=True) -> Path:
        game = library / "steamapps" / "common" / folder
        game.mkdir(parents=True)
        (game / "portal2.exe").write_bytes(b"fixture")
        if manifest:
            (library / "steamapps" / "appmanifest_620.acf").write_text(APP_MANIFEST % folder, encoding="utf-8")
        return game

    def write_libraries(self):
        (self.steam_root / "steamapps" / "libraryfolders.vdf").write_text(
            LIBRARY_FOLDERS % (escaped(self.steam_root), escaped(self.library)), encoding="utf-8")

    def registry(self):
        return self.steam

    def test_fills_both_paths_from_the_registered_steam_client(self):
        self.write_libraries()
        game = self.install_portal2(self.library)
        found = steam_paths.find_installation(r"Z:\missing\Portal 2", r"Z:\missing\steam.exe", self.registry)
        self.assertEqual(found, (str(game), str(self.steam)))

    def test_valid_paths_are_never_replaced(self):
        self.write_libraries()
        self.install_portal2(self.library)
        chosen = self.install_portal2(self.folder / "Elsewhere")
        other_steam = self.folder / "Other" / "steam.exe"
        other_steam.parent.mkdir()
        other_steam.write_bytes(b"fixture")
        found = steam_paths.find_installation(str(chosen), str(other_steam), self.registry)
        self.assertEqual(found, (str(chosen), str(other_steam)))

    def test_game_is_searched_in_the_chosen_steam_client(self):
        game = self.install_portal2(self.steam_root, manifest=False)
        found = steam_paths.find_installation("", str(self.steam), lambda: None)
        self.assertEqual(found, (str(game), str(self.steam)))

    def test_custom_install_folder_from_the_app_manifest(self):
        self.write_libraries()
        game = self.install_portal2(self.library, folder="Portal 2 Copy")
        self.assertEqual(steam_paths.find_portal2(self.steam_root), game)

    def test_missing_game_keeps_the_old_value_but_still_finds_steam(self):
        self.write_libraries()
        found = steam_paths.find_installation(r"Z:\missing", "", self.registry)
        self.assertEqual(found, (r"Z:\missing", str(self.steam)))

    def test_no_steam_changes_nothing(self):
        self.assertEqual(steam_paths.find_installation("a", "b", lambda: None), ("a", "b"))

    def test_broken_library_file_still_checks_the_steam_folder(self):
        (self.steam_root / "steamapps" / "libraryfolders.vdf").write_text('"libraryfolders" {', encoding="utf-8")
        game = self.install_portal2(self.steam_root)
        self.assertEqual(steam_paths.find_portal2(self.steam_root), game)

    def test_a_folder_without_portal2_exe_is_not_returned(self):
        self.write_libraries()
        (self.library / "steamapps" / "common" / "Portal 2").mkdir(parents=True)
        self.assertIsNone(steam_paths.find_portal2(self.steam_root))

    def test_libraries_listing_portal2_come_first_without_duplicates(self):
        self.write_libraries()
        libraries = [os.path.normcase(str(path)) for path in steam_paths.library_folders(self.steam_root)]
        self.assertEqual(libraries, [os.path.normcase(str(self.library)), os.path.normcase(str(self.steam_root))])


if __name__ == "__main__":
    unittest.main()
