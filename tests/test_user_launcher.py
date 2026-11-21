"""Offline user settings and launcher tests. Only temporary game files are used."""

import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

from tools import test_launcher_core as core
from tests.test_test_launcher import fixture_install

REPO = Path(__file__).resolve().parent.parent
TEMPLATE = (REPO / "L4D2VR/config.txt").read_text(encoding="utf-8")


def config_values(text):
    return dict(line.split("#", 1)[0].strip().split("=", 1)
                for line in text.splitlines() if "=" in line.split("#", 1)[0])


def complete_fixture(root):
    repo, game, steam = fixture_install(root)
    (repo / "L4D2VR/config.txt").write_text(TEMPLATE, encoding="utf-8")
    return repo, game, steam


class SettingsTests(unittest.TestCase):
    def setUp(self):
        from importlib.util import find_spec
        self.assertIsNotNone(find_spec("tools.launcher_settings"),
                             "The validated user settings component is missing")
        from tools import launcher_settings
        self.settings = launcher_settings

    def test_recommended_config_emits_confirmed_setup_and_every_runtime_key(self):
        with TemporaryDirectory() as temporary:
            repo, game, _ = complete_fixture(Path(temporary))
            planned = core.plan_install(repo, game, "baseline",
                                        config_values=self.settings.recommended_settings())
            result = config_values(planned[game / "VR/config.txt"].decode("utf-8"))
            self.assertEqual(result.keys(), config_values(TEMPLATE).keys())
            for key, want in {
                "TrackingMode": "Seated", "RoomscaleMode": "ActiveExperimental",
                "PortalOrientationMode": "LegacyYaw", "6DOF": "true",
                "ExperimentalWorldAimMarker": "true", "ExperimentalStereoReticle": "false",
                "ExperimentalViewmodelAlignment": "true", "AimFromViewmodelMuzzle": "true",
                "ExperimentalHUDOverlay": "true", "ExperimentalPortalShotHaptics": "true",
                "VerboseDiagnostics": "false", "RenderWindow": "0",
                "Haptics": "true", "HapticsNativeRumble": "true",
                "HapticsHeldObject": "true", "HapticsButtons": "true",
                "PhysicalCrouch": "true", "PhysicalCrouchDropMeters": "0.25",
            }.items():
                self.assertEqual(result[key], want, key)
            for key, want in {"HUDDistanceMeters": 1.25, "HUDWidthMeters": 1.65,
                              "HUDVerticalOffsetMeters": .1, "ViewmodelPosCustomOffsetX": 0,
                              "ViewmodelPosCustomOffsetY": 0, "ViewmodelPosCustomOffsetZ": 0}.items():
                self.assertEqual(float(result[key]), want, key)

    def test_invalid_values_are_rejected_instead_of_emitted_to_game(self):
        invalid = {"TurnSpeed": ["0", "2.01", "nan", "1_0", "1x"],
                   "SnapTurnAngle": ["0", "181"], "VRScale": ["0", "201", "inf"],
                   "IPDScale": ["0.49", "1.51"], "AntiAliasing": ["1", "16", "2.0"],
                   "AimMode": ["3", "-1"], "TrackingMode": ["Flying"],
                   "SnapTurning": ["yes", "1"], "HUDWidthMeters": ["0", "2.6"],
                   "ControllerPitchDegrees": ["61"], "PortalShotHapticDurationSeconds": ["0.16"],
                   "ViewmodelAngCustomOffsetZ": ["181"], "Haptics": ["on"],
                   "HapticsIntensity": ["1.1", "-0.1"], "PhysicalCrouch": ["yes"],
                   "PhysicalCrouchDropMeters": ["0.14", "0.81", "nan"]}
        for key, bad_values in invalid.items():
            for value in bad_values:
                with self.subTest(key=key, value=value):
                    values = self.settings.recommended_settings()
                    values[key] = value
                    with self.assertRaisesRegex(ValueError, key):
                        self.settings.validate_settings(values)

    def test_boundaries_and_custom_numbers_are_normalized(self):
        values = self.settings.recommended_settings()
        values.update(TurnSpeed=" 2.000 ", SnapTurnAngle="180", VRScale="200",
                      IPDScale="0.5", AntiAliasing="8", AimMode="0")
        normalized = self.settings.validate_settings(values)
        self.assertEqual(float(normalized["TurnSpeed"]), 2)
        self.assertEqual(normalized["AntiAliasing"], "8")
        self.assertNotIn(" ", normalized["TurnSpeed"])

    def test_incomplete_unknown_and_incompatible_settings_fail(self):
        changes = [{"6DOF": "false"}, {"PortalOrientationMode": "FullRotation"},
                   {"ExperimentalViewmodelAlignment": "false"}, {"ThirdAttack": "true"}]
        for change in changes:
            values = {**self.settings.recommended_settings(), **change}
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.settings.validate_settings(values)
        values = self.settings.recommended_settings()
        del values["AimMode"]
        with self.assertRaises(ValueError):
            self.settings.validate_settings(values)

    def test_preferences_roundtrip_separate_from_install_state(self):
        with TemporaryDirectory() as temporary:
            path = Path(temporary) / "settings.json"
            legacy = core.load_state(Path(temporary) / "legacy.json")
            legacy["profile"] = "roomscale_active_native_aim"
            preferences = self.settings.load_preferences(path, legacy)
            self.assertEqual(preferences["values"]["ExperimentalWorldAimMarker"], "true")
            self.assertFalse(path.exists())
            preferences["values"]["SnapTurning"] = "true"
            self.settings.save_preferences(path, preferences)
            self.assertEqual(self.settings.load_preferences(path, legacy), preferences)
            self.assertFalse((Path(temporary) / "legacy.json").exists())

    def test_invalid_save_and_corrupt_load_do_not_replace_preferences(self):
        with TemporaryDirectory() as temporary:
            path = Path(temporary) / "settings.json"
            prefs = self.settings.load_preferences(path, core.load_state(path.parent / "state"))
            self.settings.save_preferences(path, prefs)
            original = path.read_bytes()
            prefs["values"]["TurnSpeed"] = "NaN"
            with self.assertRaises(ValueError):
                self.settings.save_preferences(path, prefs)
            self.assertEqual(path.read_bytes(), original)
            path.write_bytes(b"broken json")
            with self.assertRaisesRegex(ValueError, "preferences"):
                self.settings.load_preferences(path, {})
            prefs["values"]["TurnSpeed"] = "0.15"
            with self.assertRaisesRegex(ValueError, "preferences"):
                self.settings.save_preferences(path, prefs)
            self.assertEqual(path.read_bytes(), b"broken json")

    def test_failed_preferences_replace_retains_previous_file(self):
        with TemporaryDirectory() as temporary:
            path = Path(temporary) / "settings.json"
            prefs = self.settings.load_preferences(path, core.load_state(path.parent / "state"))
            self.settings.save_preferences(path, prefs)
            original = path.read_bytes()
            prefs["values"]["SnapTurning"] = "true"
            with patch.object(self.settings.os, "replace", side_effect=OSError("replace denied")):
                with self.assertRaises(OSError):
                    self.settings.save_preferences(path, prefs)
            self.assertEqual(path.read_bytes(), original)
            self.assertEqual(list(path.parent.glob("*.tmp")), [])

    def test_custom_install_retains_first_original_snapshot_and_restores(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = complete_fixture(root)
            state_path = root / "state.json"
            original = game / "bin/d3d9.dll"
            original.write_bytes(b"first original")
            core.stage_install(repo, game, state_path, "baseline")
            before = core.load_state(state_path)
            values = self.settings.recommended_settings()
            values["SnapTurnAngle"] = "30"
            core.stage_install(repo, game, state_path, "baseline", config_values=values)
            after = core.load_state(state_path)
            self.assertEqual(after["backup_id"], before["backup_id"])
            self.assertEqual(after["original_files"], before["original_files"])
            self.assertEqual(config_values((game / "VR/config.txt").read_text())["SnapTurnAngle"], "30")
            core.restore_install(game, state_path)
            self.assertEqual(original.read_bytes(), b"first original")
            self.assertFalse((game / "VR").exists())

    def test_invalid_custom_install_writes_no_game_files_or_snapshot(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = complete_fixture(root)
            values = self.settings.recommended_settings()
            values["IPDScale"] = "inf"
            with self.assertRaises(ValueError):
                core.stage_install(repo, game, root / "state.json", "baseline", config_values=values)
            self.assertFalse((game / "VR").exists())
            self.assertFalse((game / "bin/d3d9.dll").exists())
            self.assertFalse((root / ".launcher-backups").exists())

    def test_custom_install_keeps_external_edit_and_transaction_guards(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = complete_fixture(root)
            state_path = root / "state.json"
            values = self.settings.recommended_settings()
            with core._transaction_lock(state_path):
                with self.assertRaisesRegex(RuntimeError, "in progress"):
                    core.stage_install(repo, game, state_path, "baseline", config_values=values)
            core.stage_install(repo, game, state_path, "baseline", config_values=values)
            config = game / "VR/config.txt"
            config.write_bytes(b"user edit")
            with self.assertRaisesRegex(RuntimeError, "outside launcher"):
                core.stage_install(repo, game, state_path, "baseline", config_values=values)
            self.assertEqual(config.read_bytes(), b"user edit")

    def test_ingame_settings_merge_valid_menu_keys_only(self):
        with TemporaryDirectory() as temporary:
            game = Path(temporary)
            (game / "VR").mkdir()
            (game / "VR/ingame_settings.txt").write_text(
                "# Written by the game\nSnapTurning=true\nTurnSpeed=9 # out of range\n"
                "RoomscaleMode=Off\nSnapTurnAngle = 30\n", encoding="utf-8")
            ingame = self.settings.read_ingame_settings(game)
            self.assertEqual(ingame, {"SnapTurning": "true", "TurnSpeed": "9", "SnapTurnAngle": "30"})
            values = self.settings.recommended_settings()
            merged = self.settings.merge_ingame_settings(values, ingame)
            self.assertEqual(merged["SnapTurning"], "true")
            self.assertEqual(merged["SnapTurnAngle"], "30")
            self.assertEqual(merged["TurnSpeed"], values["TurnSpeed"])
            self.assertEqual(merged["RoomscaleMode"], values["RoomscaleMode"])
            self.assertEqual(self.settings.read_ingame_settings(game / "missing"), {})

    def test_ingame_keys_match_the_mod_menu_and_launcher_schema(self):
        header = (REPO / "L4D2VR/vr_settings.h").read_text(encoding="utf-8")
        import re
        menu_keys = set(re.findall(r'(?:Toggle|Numeric)\("(\w+)"', header))
        menu_keys |= set(re.findall(r'\.key = "(\w+)"', header))
        self.assertEqual(menu_keys, set(self.settings.INGAME_KEYS))
        self.assertLessEqual(self.settings.INGAME_KEYS, {item.key for item in self.settings.SETTINGS})

    def test_retire_keeps_an_ingame_file_changed_after_import(self):
        with TemporaryDirectory() as temporary:
            game = Path(temporary)
            (game / "VR").mkdir()
            path = game / "VR/ingame_settings.txt"
            path.write_text("SnapTurning=true\n", encoding="utf-8")
            imported = self.settings.read_ingame_settings(game)
            path.write_text("SnapTurning=false\n", encoding="utf-8")
            self.assertFalse(self.settings.retire_ingame_settings(game, imported))
            self.assertTrue(path.exists())
            self.assertTrue(self.settings.retire_ingame_settings(game, {"SnapTurning": "false"}))
            self.assertFalse(path.exists())

    def test_retired_options_keep_fixed_values(self):
        values = self.settings.recommended_settings()
        values.update(AimMode="0", ShowSourceCrosshair="false", RoomscaleMode="Observe", Haptics="false",
                      ExperimentalPortalShotHaptics="true")
        kept = self.settings.user_values(values)
        self.assertEqual((kept["AimMode"], kept["ShowSourceCrosshair"]), ("2", "true"))
        self.assertEqual(kept["RoomscaleMode"], "Off")
        self.assertEqual(kept["ExperimentalPortalShotHaptics"], "false", "portal shot vibration follows Haptics")
        for item in self.settings.SETTINGS:
            if item.fixed is not None:
                self.assertFalse(self.settings.visible(item, kept, True), item.key)

    def test_conditions_and_advanced_flag_choose_visible_settings(self):
        by_key, values = self.settings.BY_KEY, self.settings.recommended_settings()
        visible = lambda key, advanced=False: self.settings.visible(by_key[key], values, advanced)
        values["SnapTurning"] = "true"
        self.assertTrue(visible("SnapTurnAngle"))
        self.assertFalse(visible("TurnSpeed"))
        values["SnapTurning"] = "false"
        self.assertEqual((visible("SnapTurnAngle"), visible("TurnSpeed")), (False, True))
        self.assertFalse(visible("VRScale"))
        self.assertTrue(visible("VRScale", advanced=True))

    def test_preferences_keep_game_folders_and_taken_ingame_values(self):
        with TemporaryDirectory() as temporary:
            path = Path(temporary) / "settings.json"
            prefs = self.settings.load_preferences(path, core.load_state(path.parent / "state"))
            prefs = {**prefs, "game_dirs": {"aak": r"C:\Games\AAK"},
                     "ingame_taken": {"aak": {"SnapTurning": "true"}}}
            self.settings.save_preferences(path, prefs)
            self.assertEqual(self.settings.load_preferences(path, {}), prefs)
            for bad in ({"game_dirs": {"aak": ""}}, {"ingame_taken": {"aak": "x"}},
                        {"ingame_taken": {"aak": {"SnapTurning": 1}}}):
                with self.subTest(bad=bad), self.assertRaises(ValueError):
                    self.settings.save_preferences(path, {**prefs, **bad})


class LauncherTests(unittest.TestCase):
    def setUp(self):
        from importlib.util import find_spec
        self.assertIsNotNone(find_spec("tools.launcher"), "The user launcher is missing")
        import tkinter as tk
        from tools import launcher
        self.launcher = launcher
        self.temporary = TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.repo, self.game, self.steam = complete_fixture(self.folder)
        self.state_path = self.folder / "state.json"
        self.prefs_path = self.folder / "prefs.json"
        legacy = core.load_state(self.state_path)
        legacy.update(game_dir=str(self.game), steam_exe=str(self.steam),
                      profile="roomscale_active_native_aim")
        core.save_state(self.state_path, legacy)
        self.original_state = self.state_path.read_bytes()
        self.root = tk.Tk()
        self.root.withdraw()
        self.callback_errors = []
        self.root.report_callback_exception = lambda *error: self.callback_errors.append(error)
        def close_root():
            self.root.update_idletasks()
            self.root.destroy()
            self.assertEqual(self.callback_errors, [], "Unexpected Tk callback errors")
        self.addCleanup(close_root)
        self.app = launcher.Launcher(self.root, self.repo, self.prefs_path, self.state_path)
        self.addCleanup(patch.stopall)
        patch.object(launcher, "game_is_running", return_value=False).start()
        patch.object(launcher.messagebox, "askyesno", return_value=True).start()
        patch.object(launcher.messagebox, "showerror").start()
        patch.object(launcher.messagebox, "showinfo").start()

    def test_first_run_and_preview_do_not_deploy_or_save(self):
        result = config_values(self.app.config_preview())
        self.assertEqual(result["ExperimentalWorldAimMarker"], "true")
        self.assertEqual(result["RoomscaleMode"], "ActiveExperimental")
        self.assertFalse(self.prefs_path.exists())
        self.assertFalse((self.game / "VR").exists())
        self.assertEqual(self.state_path.read_bytes(), self.original_state)

    def test_small_window_keeps_recovery_actions_reachable(self):
        core.stage_install(self.repo, self.game, self.state_path, "baseline")
        self.app._refresh_install()
        width, height = self.app.px(640), self.app.px(520)
        self.root.geometry(f"{width}x{height}")
        self.root.deiconify()
        self.root.update()
        for name, text in (("Games", "Restore original files"), ("Help", "Open backups")):
            with self.subTest(page=name):
                self.app.show_page(name)
                page = self.app.pages[name]
                button = next(button for button in self.app.buttons if button.text == text)
                self.assertIs(button.canvas, page.canvas)
                self.assertLessEqual(button.box[3], max(page.content, page.height))
                page.scroll("moveto", max(0, button.box[1] - self.app.px(40)) / max(page.content, 1))
                self.root.update()
                top = page.canvas.canvasy(0)
                self.assertGreaterEqual(button.box[1], top)
                self.assertLessEqual(button.box[3], top + page.canvas.winfo_height())

    def test_wheel_over_a_choice_scrolls_the_page_without_changing_it(self):
        from tkinter import ttk

        self.root.deiconify()
        self.app.show_page("Settings")
        self.root.update()
        page = self.app.pages["Settings"]
        choice = next(widget for widget in page.canvas.winfo_children() if isinstance(widget, ttk.Combobox))
        before = choice.get()
        choice.event_generate("<MouseWheel>", delta=-120)
        self.root.update()
        self.assertEqual(choice.get(), before)
        self.assertGreater(page.canvas.canvasy(0), 0)

    def test_rounded_box_blends_its_corners_into_the_background(self):
        from tools import launcher_art

        image = launcher_art.rounded_box(self.root, (40, 30), 8, "#102030", "#405060", "#ff0000")
        self.assertEqual(image.get(0, 0), (255, 0, 0))
        self.assertEqual(image.get(20, 0), (64, 80, 96))
        self.assertEqual(image.get(20, 15), (16, 32, 48))

    def test_window_has_its_own_caption_and_moves_from_the_header(self):
        from types import SimpleNamespace

        self.root.geometry("+100+120")
        self.root.deiconify()
        self.root.update()
        self.assertEqual(self.app.canvas.type("drag"), "rectangle")
        left, top = self.root.winfo_x(), self.root.winfo_y()
        self.app._drag_start(SimpleNamespace(x_root=left + 50, y_root=top + 20))
        self.app._drag(SimpleNamespace(x_root=left + 80, y_root=top + 60))
        self.root.update()
        self.assertEqual((self.root.winfo_x(), self.root.winfo_y()), (left + 30, top + 40))
        self.assertEqual(self.app.close_button.box[2], self.root.winfo_width())
        self.assertEqual(self.app.minimize_button.box[2], self.app.close_button.box[0])
        self.assertEqual(self.app.launch_button.text, "Play in VR")
        self.assertEqual(self.app.launch_button.box[2], self.root.winfo_width() - self.app.px(self.launcher.MARGIN))
        with patch.object(self.app, "close") as close:
            self.app.close_button.command = close
            self.app.close_button.invoke()
        close.assert_called_once()

    def test_shortcut_carries_the_icon_and_taskbar_identity(self):
        from tools import launcher_art, launcher_shortcut

        path = self.folder / "launcher.lnk"
        launcher_shortcut.create_shortcut(path, Path(sys.executable), self.repo / "launcher.pyw", launcher_art.ICON)
        data = path.read_bytes()
        self.assertIn(str(launcher_art.ICON).encode("utf-16-le"), data)
        self.assertIn(launcher_art.APP_ID.encode("utf-16-le"), data)

    def test_icon_has_every_taskbar_size(self):
        from tools import launcher_art

        data = launcher_art.ICON.read_bytes()
        count = int.from_bytes(data[4:6], "little")
        sizes = {data[6 + 16 * index] or 256 for index in range(count)}
        self.assertEqual(sizes, set(launcher_art.ICON_SIZES))

    def test_choices_show_names_but_store_config_values(self):
        from tkinter import ttk

        page = self.app.pages["Settings"]
        widget = next(control.widget for item, control in page.rows if item.key == "MovementDirection")
        self.assertIsInstance(widget, ttk.Combobox)
        self.assertIn("Left controller", widget.cget("values"))
        widget.set("Left controller")
        widget.event_generate("<<ComboboxSelected>>")
        self.assertEqual(self.app.vars["MovementDirection"].get(), "LeftController")
        self.app.vars["MovementDirection"].set("HMD")
        self.assertEqual(widget.get(), "Headset")
        toggle = next(control for item, control in page.rows if item.key == "RenderWindow")
        self.assertIsInstance(toggle, self.launcher.Toggle)
        self.assertEqual((toggle.off, toggle.on), ("0", "1"))

    def test_pages_are_games_settings_and_help(self):
        self.assertEqual(tuple(self.app.pages), ("Games", "Settings", "Help"))
        self.assertEqual(self.app.current, "Games")
        preview = next(button for button in self.app.buttons if button.text == "Preview config")
        self.assertIs(preview.canvas, self.app.pages["Help"].canvas)
        # The settings footer shows only on the Settings page.
        for name in self.app.pages:
            self.app.show_page(name)
            shown = "normal" if name == "Settings" else "hidden"
            self.assertEqual(self.app.canvas.itemcget(self.app.reset_button.label, "state"), shown, name)

    def test_header_follows_the_selected_game_and_its_install(self):
        self.assertFalse(self.app.installed)
        for name in self.app.pages:
            self.app.show_page(name)
        self.root.update_idletasks()
        subtitle = lambda: self.app.canvas.itemcget(self.app.subtitle, "text")
        self.assertIn("The mod installs when you play", subtitle())
        self.app.apply()
        self.assertTrue(self.app.installed)
        self.assertTrue(subtitle().startswith("Portal 2"))
        self.assertIn("Ready to play", subtitle())
        self.assertEqual(self.app.install_button.text, "Apply settings")
        self.app.game_var.set(str(self.folder / "missing"))
        self.assertEqual(subtitle(), "Portal 2 not found: choose its folder")
        self.app.game_var.set(str(self.game))
        self.app.restore()
        self.assertFalse(self.app.installed)
        self.assertIn("The mod installs when you play", subtitle())
        self.assertEqual(self.app.install_button.text, "Install mod")

    def test_process_query_has_bounded_wait_and_hidden_console(self):
        import subprocess
        from tools import test_launcher

        observed = []
        def query_boundary(command, **kwargs):
            observed.append(kwargs)
            return subprocess.CompletedProcess(command, 0, b'"portal2.exe","123"', b"")

        with patch.object(test_launcher.subprocess, "run", side_effect=query_boundary):
            self.assertTrue(test_launcher.game_is_running())
        self.assertEqual(observed[0].get("timeout"), 5)
        self.assertEqual(observed[0].get("creationflags"), getattr(subprocess, "CREATE_NO_WINDOW", 0))
        self.assertNotIn("text", observed[0], "tasklist output is read as bytes")

    def test_process_query_reads_tasklist_in_any_console_code_page(self):
        # German Windows, nothing running: cp850 writes the u-umlaut as 0x81, which cp1252
        # cannot decode (issue #3: Apply and Play in VR did nothing).
        import subprocess
        from tools import test_launcher
        german = (b"INFORMATION: Es werden keine Aufgaben mit den angegebenen Kriterien "
                  b"ausgef\x81hrt.\r\n")
        with patch.object(test_launcher.subprocess, "run",
                          return_value=subprocess.CompletedProcess([], 0, german, b"")):
            self.assertFalse(test_launcher.game_is_running())
        failed = subprocess.CompletedProcess([], 1, b"", b"FEHLER: Zugriff verweigert \x81")
        with patch.object(test_launcher.subprocess, "run", return_value=failed):
            with self.assertRaisesRegex(RuntimeError, "Cannot check portal2.exe process: FEHLER"):
                test_launcher.game_is_running()

    def test_unexpected_button_fault_is_shown_not_swallowed(self):
        for button in (self.app.apply, self.app.launch, self.app.restore):
            with patch.object(self.launcher, "game_is_running", side_effect=IndexError("list index out of range")):
                button()
            self.assertIn("Unexpected error (IndexError): list index out of range", self.app.status_var.get())

    def test_changes_save_by_themselves_and_only_locally(self):
        with patch.object(self.launcher, "SAVE_DELAY_MS", 1):
            self.app.vars["SnapTurning"].set("true")
        self.assertIsNotNone(self.app._save_job, "a change schedules a save")
        self.assertFalse(self.prefs_path.exists())
        self.root.after(30)
        self.root.update()
        self.assertIsNone(self.app._save_job)
        self.assertIn("Settings saved", self.app.status_var.get())
        self.assertEqual(json.loads(self.prefs_path.read_text())["values"]["SnapTurning"], "true")
        self.assertEqual(self.state_path.read_bytes(), self.original_state)
        self.assertFalse((self.game / "VR").exists())

    def test_closing_saves_pending_changes(self):
        self.app.vars["SnapTurnAngle"].set("30")
        with patch.object(self.root, "destroy") as destroy:
            self.app.close()
        destroy.assert_called_once()
        self.assertEqual(json.loads(self.prefs_path.read_text())["values"]["SnapTurnAngle"], "30")

    def test_invalid_value_is_named_and_not_saved(self):
        self.app.vars["SnapTurning"].set("true")
        self.app.vars["SnapTurnAngle"].set("999")
        self.assertIsNone(self.app._save_job)
        self.assertTrue(self.app.invalid)
        self.assertIn("Snap angle", self.app.status_var.get())
        self.assertEqual(self.app.canvas.itemcget(self.app.subtitle, "text"), "A setting needs attention")
        self.app.vars["SnapTurnAngle"].set("30")
        self.assertFalse(self.app.invalid)
        self.assertIsNotNone(self.app._save_job)

    def test_reset_asks_then_restores_recommended_and_saves(self):
        self.app.vars["SnapTurning"].set("true")
        self.app.save()
        with patch.object(self.launcher.messagebox, "askyesno", return_value=False):
            self.app.reset_recommended()
        self.assertEqual(self.app.vars["SnapTurning"].get(), "true")
        self.app.reset_recommended()
        self.assertEqual(self.app.game_var.get(), str(self.game))
        self.assertEqual(self.app.steam_var.get(), str(self.steam))
        self.assertEqual(self.app.vars["SnapTurning"].get(), "false")
        self.assertEqual(json.loads(self.prefs_path.read_text())["values"]["SnapTurning"], "false")
        self.assertEqual(self.state_path.read_bytes(), self.original_state)
        self.assertFalse((self.game / "VR").exists())

    def test_saved_custom_settings_survive_new_window(self):
        self.app.vars["HUDWidthMeters"].set("1.8")
        self.app.save()
        other = self.launcher.Launcher(self.root, self.repo, self.prefs_path, self.state_path)
        self.assertEqual(other.vars["HUDWidthMeters"].get(), "1.8")

    def test_linked_settings_change_together(self):
        values = self.app.vars
        self.assertEqual(values["RoomscaleMode"].get(), "ActiveExperimental")
        values["6DOF"].set("false")
        self.assertEqual(values["RoomscaleMode"].get(), "Off")
        values["PortalOrientationMode"].set("FullRotation")
        values["RoomscaleMode"].set("ActiveExperimental")
        self.assertEqual((values["6DOF"].get(), values["PortalOrientationMode"].get()), ("true", "LegacyYaw"))
        self.assertFalse(self.app.invalid)

    def test_advanced_switch_and_conditions_choose_the_rows(self):
        page = self.app.pages["Settings"]
        shown = lambda key: next(control for item, control in page.rows if item.key == key).shown
        self.assertFalse(shown("ViewmodelPosCustomOffsetX"))
        self.assertTrue(shown("ExperimentalWorldAimMarker"))
        self.assertFalse(shown("VerboseDiagnostics"))
        self.app.advanced_var.set("1")
        self.assertTrue(shown("VerboseDiagnostics"))
        keys = {item.key for item, _ in page.rows}
        self.assertNotIn("AimMode", keys, "retired options have no row")
        self.assertNotIn("ExperimentalStereoReticle", keys)

    def test_found_paths_fill_a_first_start_without_unsaved_changes(self):
        legacy = core.load_state(self.state_path)
        legacy.update(game_dir=r"Z:\missing\Portal 2", steam_exe=r"Z:\missing\steam.exe")
        core.save_state(self.state_path, legacy)
        asked = []
        def find(game_dir, steam_exe):
            asked.append((game_dir, steam_exe))
            return str(self.game), str(self.steam)
        other = self.launcher.Launcher(self.root, self.repo, self.prefs_path, self.state_path, find_paths=find)
        self.assertEqual(asked, [(r"Z:\missing\Portal 2", r"Z:\missing\steam.exe")])
        self.assertEqual((other.game_var.get(), other.steam_var.get()), (str(self.game), str(self.steam)))
        self.assertEqual(other._paths_found(), (True, True))
        self.assertIsNone(other._save_job)
        self.assertIn("Found Portal 2 and Steam automatically", other.status_var.get())
        self.assertFalse(self.prefs_path.exists())
        other.save()
        saved = json.loads(self.prefs_path.read_text())
        self.assertEqual((saved["game_dir"], saved["steam_exe"]), (str(self.game), str(self.steam)))

    def test_path_lookup_failure_keeps_the_saved_paths(self):
        def find(game_dir, steam_exe):
            raise OSError("registry unavailable")
        other = self.launcher.Launcher(self.root, self.repo, self.prefs_path, self.state_path, find_paths=find)
        self.assertEqual((other.game_var.get(), other.steam_var.get()), (str(self.game), str(self.steam)))
        self.assertEqual(other.status_var.get(), "")

    def test_invalid_value_blocks_apply_before_any_settings_or_game_write(self):
        self.app.vars["IPDScale"].set("3")
        self.app.apply()
        self.assertFalse((self.game / "VR").exists())
        self.assertFalse(self.prefs_path.exists())
        self.assertEqual(self.state_path.read_bytes(), self.original_state)
        self.assertIn("Eye distance multiplier", self.app.status_var.get())

    def test_apply_and_restore_use_the_existing_original_backup(self):
        original = self.game / "bin/d3d9.dll"
        original.write_bytes(b"original")
        core.stage_install(self.repo, self.game, self.state_path, "baseline")
        first_id = core.load_state(self.state_path)["backup_id"]
        self.app.vars["SnapTurnAngle"].set("30")
        self.app.apply()
        self.assertEqual(config_values((self.game / "VR/config.txt").read_text())["SnapTurnAngle"], "30")
        self.assertEqual(core.load_state(self.state_path)["backup_id"], first_id)
        self.app.restore()
        self.assertEqual(original.read_bytes(), b"original")
        self.assertFalse((self.game / "VR").exists())

    def test_restore_recovers_interrupted_first_install_before_empty_state_return(self):
        from tests.test_launcher_transactions import CrashRecoveryTests
        original = self.game / "bin/d3d9.dll"
        original.write_bytes(b"original")
        CrashRecoveryTests().crash(self.folder, "install", "after_write")
        self.assertFalse(core.load_state(self.state_path)["managed_files"])
        self.app.restore()
        self.assertEqual(original.read_bytes(), b"original")
        self.assertFalse((self.game / "VR").exists())
        self.assertFalse(self.state_path.with_name("state.json.transaction.json").exists())

    def test_restore_rejects_legacy_empty_state_with_retained_install(self):
        original = self.game / "bin/d3d9.dll"
        original.write_bytes(b"original")
        core.stage_install(self.repo, self.game, self.state_path, "baseline")
        self.state_path.unlink()
        legacy = core.load_state(self.state_path)
        legacy["game_dir"] = str(self.game)
        core.save_state(self.state_path, legacy)
        before = self.state_path.read_bytes()

        self.app.restore()

        self.assertIn("ownership", self.app.status_var.get())
        self.assertEqual(original.read_bytes(), b"test-d3d9")
        self.assertEqual(self.state_path.read_bytes(), before)

    def test_running_game_blocks_interrupted_transaction_recovery(self):
        from tests.test_launcher_transactions import CrashRecoveryTests
        original = self.game / "bin/d3d9.dll"
        original.write_bytes(b"original")
        CrashRecoveryTests().crash(self.folder, "install", "after_write")
        journal = self.state_path.with_name("state.json.transaction.json")
        before = journal.read_bytes()
        with patch.object(self.launcher, "game_is_running", return_value=True):
            self.app.restore()
        self.assertEqual(original.read_bytes(), b"test-d3d9")
        self.assertEqual(journal.read_bytes(), before)

    def test_launch_stages_selected_settings_before_external_steam_call(self):
        observed = []

        def steam_boundary(command, **kwargs):
            # No process runs. Observe the real config already staged by the callback.
            observed.append((command, kwargs, config_values((self.game / "VR/config.txt").read_text())))

        self.app.vars["SnapTurning"].set("true")
        with patch.object(self.launcher.subprocess, "Popen", side_effect=steam_boundary):
            self.app.launch()
        self.assertEqual(len(observed), 1)
        command, kwargs, values = observed[0]
        self.assertEqual(command, core.build_steam_command(self.steam))
        self.assertEqual(kwargs["cwd"], str(self.steam.parent))
        self.assertEqual(values["SnapTurning"], "true")
        self.assertEqual(values["ExperimentalWorldAimMarker"], "true")

    def make_aak(self):
        aak = self.folder / "Alive and Kicking"
        (aak / "bin").mkdir(parents=True)
        (aak / "AAK").mkdir()
        (aak / "portal2.exe").write_bytes(b"fixture")
        (aak / "AAK/gameinfo.txt").write_text('"GameInfo" {}', encoding="utf-8")
        return aak

    def test_alive_and_kicking_installs_with_its_own_record_and_app_id(self):
        from tools import games
        self.app.apply()
        self.assertTrue(self.app.installed)
        portal_state = self.state_path.read_bytes()
        aak = self.make_aak()
        self.app.game_var.set(str(aak))
        self.assertFalse(self.app.installed, "Alive & Kicking has no install yet")
        observed = []
        with patch.object(self.launcher.subprocess, "Popen", side_effect=lambda command, **_: observed.append(command)):
            self.app.launch()
        self.assertEqual(observed, [core.build_steam_command(self.steam, "270470")])
        self.assertTrue((aak / "bin/d3d9.dll").is_file())
        self.assertEqual(self.state_path.read_bytes(), portal_state, "Portal 2's record is untouched")
        aak_state = games.state_path(self.state_path, games.ALIVE_AND_KICKING)
        self.assertEqual(core.load_state(aak_state)["installed_game_dir"], str(aak.resolve()))
        self.assertTrue(self.app.installed)
        self.app.restore()
        self.assertFalse((aak / "bin/d3d9.dll").exists())
        self.app.game_var.set(str(self.game))
        self.assertTrue(self.app.installed, "Portal 2 stays installed")

    def test_select_game_finds_the_other_game_in_the_steam_libraries(self):
        from tools import games
        aak = self.make_aak()
        with patch.object(self.launcher, "find_game", return_value=aak) as find:
            self.app.select_game(games.ALIVE_AND_KICKING)
        find.assert_called_once_with(self.steam.parent, games.ALIVE_AND_KICKING)
        self.assertEqual(self.app.game_var.get(), str(aak))
        self.assertIn("Portal: Alive & Kicking selected", self.app.status_var.get())
        self.app.select_game(games.PORTAL2)
        self.assertEqual(self.app.game_var.get(), str(self.game), "each game keeps its folder")
        self.app.save()
        self.assertEqual(json.loads(self.prefs_path.read_text())["game_dirs"],
                         {"portal2": str(self.game), "aak": str(aak)})
        (aak / "AAK/gameinfo.txt").unlink()
        with patch.object(self.launcher, "find_game", return_value=None):
            self.app.select_game(games.ALIVE_AND_KICKING)
        self.assertEqual(self.app.game_var.get(), str(self.game), "nothing found keeps the selection")
        self.assertIn("Locate folder", self.app.status_var.get())

    def test_game_cards_select_only_installed_games(self):
        from tools import games
        aak = self.make_aak()
        self.app.folders["aak"] = str(aak)
        self.app.pages["Games"].relayout()
        self.app._card_clicked(games.ALIVE_AND_KICKING)
        self.assertEqual(self.app.game_var.get(), str(aak))
        self.app.folders["portal2"] = str(self.folder / "gone")
        self.app._card_clicked(games.PORTAL2)
        self.assertEqual(self.app.game_var.get(), str(aak))

    def test_running_game_blocks_apply_without_writes(self):
        with patch.object(self.launcher, "game_is_running", return_value=True):
            self.app.apply()
        self.assertFalse((self.game / "VR").exists())
        self.assertFalse(self.prefs_path.exists())
        self.assertIn("Close Portal 2", self.app.status_var.get())

    def test_steam_failure_keeps_backups_and_explains_recovery(self):
        with patch.object(self.launcher.subprocess, "Popen", side_effect=OSError("Steam unavailable")):
            self.app.launch()
        state = core.load_state(self.state_path)
        self.assertTrue(state["backup_id"])
        self.assertTrue(state["managed_files"])
        self.assertIn("Restore", self.app.status_var.get())

    def test_ingame_changes_are_taken_once_and_retire_after_apply(self):
        (self.game / "VR").mkdir(parents=True, exist_ok=True)
        ingame = self.game / "VR/ingame_settings.txt"
        ingame.write_text("SnapTurning=true\nSnapTurnAngle=30\n", encoding="utf-8")
        other = self.launcher.Launcher(self.root, self.repo, self.prefs_path, self.state_path)
        self.assertEqual(other.vars["SnapTurning"].get(), "true")
        self.assertIn("Took 2 settings", other.status_var.get())
        saved = json.loads(self.prefs_path.read_text())
        self.assertEqual(saved["values"]["SnapTurnAngle"], "30")
        self.assertEqual(saved["ingame_taken"], {"portal2": {"SnapTurning": "true", "SnapTurnAngle": "30"}})
        # A later change in the launcher wins over the same, already taken file.
        other.vars["SnapTurnAngle"].set("45")
        other.save()
        again = self.launcher.Launcher(self.root, self.repo, self.prefs_path, self.state_path)
        self.assertEqual(again.vars["SnapTurnAngle"].get(), "45")
        # A new change in the game is taken.
        ingame.write_text("SnapTurning=true\nSnapTurnAngle=60\n", encoding="utf-8")
        third = self.launcher.Launcher(self.root, self.repo, self.prefs_path, self.state_path)
        self.assertEqual(third.vars["SnapTurnAngle"].get(), "60")
        third.apply()
        values = config_values((self.game / "VR/config.txt").read_text())
        self.assertEqual((values["SnapTurning"], values["SnapTurnAngle"]), ("true", "60"))
        self.assertFalse(ingame.exists())
        saved = json.loads(self.prefs_path.read_text())
        self.assertEqual(saved["values"]["SnapTurnAngle"], "60")
        self.assertNotIn("ingame_taken", saved)

if __name__ == "__main__":
    unittest.main()
