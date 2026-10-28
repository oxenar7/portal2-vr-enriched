"""User-facing Portal2VR launcher; deployment uses the shared backup installer."""

from pathlib import Path
import os
import subprocess
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from tools import launcher_settings as settings, test_launcher_core as core
    from tools.test_launcher import game_is_running
    from tools.launcher_paths import application_root, prepare_frozen_process
else:
    from . import launcher_settings as settings, test_launcher_core as core
    from .test_launcher import game_is_running
    from .launcher_paths import application_root, prepare_frozen_process


REPO = application_root()
PREFERENCES_PATH = REPO / "tools/.user-launcher-settings.json"
INSTALL_STATE_PATH = REPO / "tools/.launcher-state.json"
BG, PANEL, TEXT, MUTED, ACCENT = "#111923", "#1b2838", "#edf4fc", "#a9b9cd", "#53c9f5"


class Launcher:
    def __init__(self, root: tk.Tk, repo: Path = REPO,
                 preferences_path: Path = PREFERENCES_PATH,
                 install_state_path: Path = INSTALL_STATE_PATH):
        self.root, self.repo = root, repo
        self.preferences_path, self.install_state_path = preferences_path, install_state_path
        legacy = core.load_state(install_state_path)
        prefs = settings.load_preferences(preferences_path, legacy)
        self.saved = prefs
        self.game_var = tk.StringVar(root, prefs["game_dir"])
        self.steam_var = tk.StringVar(root, prefs["steam_exe"])
        # Options > VR changes made in the game show up here as unsaved edits.
        self.ingame = settings.read_ingame_settings(Path(prefs["game_dir"]))
        values = settings.merge_ingame_settings(prefs["values"], self.ingame)
        self.vars = {key: tk.StringVar(root, value) for key, value in values.items()}
        self.profile_var, self.summary_var = tk.StringVar(root), tk.StringVar(root)
        changed = sum(values[key] != prefs["values"][key] for key in values)
        self.status_var = tk.StringVar(root, (
            f"Loaded {changed} setting(s) changed in game under Options > VR. "
            "Save, Apply or Launch to keep them here.") if changed else
            "Ready. Save is local; Apply and Launch update the game with backups.")
        self.root.title("Portal2VR Launcher")
        width = max(860, min(1040, root.winfo_screenwidth() - 80))
        height = max(620, min(820, root.winfo_screenheight() - 120))
        self.root.geometry(f"{width}x{height}")
        self.root.minsize(860, 620)
        self.root.configure(bg=BG)
        self._theme()
        self._build()
        for var in (*self.vars.values(), self.game_var, self.steam_var):
            var.trace_add("write", self._changed)
        self._changed()
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def _theme(self):
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure(".", background=BG, foreground=TEXT, font=("Segoe UI", 10))
        style.configure("TFrame", background=BG)
        style.configure("TLabel", background=BG, foreground=TEXT)
        style.configure("Muted.TLabel", foreground=MUTED)
        style.configure("Title.TLabel", font=("Segoe UI", 27, "bold"))
        style.configure("Section.TLabel", font=("Segoe UI", 13, "bold"), foreground=ACCENT)
        style.configure("TButton", padding=(14, 9), background=PANEL, foreground=TEXT)
        style.map("TButton", background=[("active", "#2c415b")])
        style.configure("Launch.TButton", padding=(24, 15), font=("Segoe UI", 15, "bold"),
                        background=ACCENT, foreground=BG)
        style.map("Launch.TButton", background=[("active", "#9ae4ff")])
        style.configure("TEntry", fieldbackground=PANEL, foreground=TEXT, insertcolor=TEXT, padding=7)
        style.configure("TCombobox", fieldbackground=PANEL, background=PANEL, arrowcolor=TEXT, padding=7)
        style.map("TCombobox", fieldbackground=[("readonly", PANEL)], foreground=[("readonly", TEXT)])
        style.configure("TCheckbutton", background=BG, foreground=TEXT)
        style.map("TCheckbutton", background=[("active", BG)])
        style.configure("TNotebook", background=BG, borderwidth=0)
        style.configure("TNotebook.Tab", padding=(22, 11), background=PANEL)
        style.map("TNotebook.Tab", background=[("selected", "#2c415b")], foreground=[("selected", ACCENT)])
        self.root.option_add("*TCombobox*Listbox.background", PANEL)
        self.root.option_add("*TCombobox*Listbox.foreground", TEXT)

    def _build(self):
        outer = ttk.Frame(self.root, padding=24)
        outer.pack(fill="both", expand=True)
        ttk.Label(outer, text="Portal2VR", style="Title.TLabel").pack(anchor="w")
        ttk.Label(outer, text="Your Portal 2 VR setup.",
                  style="Muted.TLabel").pack(anchor="w", pady=(2, 16))
        self.notebook = ttk.Notebook(outer)
        self.notebook.pack(fill="both", expand=True)
        play, canvas = self._scroll_page("Play")
        self._play(play)
        self._bind_wheel(play, canvas)
        self._settings_page("Settings", advanced=False)
        self._settings_page("Advanced", advanced=True)
        footer = ttk.Frame(outer)
        footer.pack(fill="x", pady=(14, 0))
        ttk.Button(footer, text="Save settings", command=self.save).pack(side="left")
        ttk.Button(footer, text="Restore recommended", command=self.reset_recommended).pack(side="left", padx=8)
        ttk.Button(footer, text="Preview config", command=self.show_preview).pack(side="right")
        status = ttk.Label(outer, textvariable=self.status_var, style="Muted.TLabel", wraplength=940,
                           justify="left")
        status.pack(fill="x", pady=(10, 0))
        status.bind("<Configure>", lambda event: status.configure(wraplength=max(300, event.width)))

    def _play(self, page):
        page.columnconfigure(0, weight=1)
        def wrap_labels(event):
            for child in page.winfo_children():
                if isinstance(child, ttk.Label) and int(child.cget("wraplength") or 0):
                    child.configure(wraplength=max(300, event.width - 40))
        page.bind("<Configure>", wrap_labels, add="+")
        ttk.Label(page, textvariable=self.profile_var, style="Section.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(page, textvariable=self.summary_var, wraplength=880, justify="left").grid(
            row=1, column=0, sticky="ew", pady=(12, 18))
        ttk.Label(page, text="Use SteamVR / OpenVR. Start SteamVR and connect your headset before launching.",
                  style="Muted.TLabel", wraplength=880).grid(row=2, column=0, sticky="w")
        ttk.Button(page, text="Launch Portal 2 in VR", command=self.launch, style="Launch.TButton").grid(
            row=3, column=0, sticky="w", pady=(16, 8))
        ttk.Label(page, text="Launch applies these settings first. Restart the game after changing them.",
                  style="Muted.TLabel").grid(row=4, column=0, sticky="w", pady=(0, 18))
        locations = ttk.Frame(page)
        locations.grid(row=5, column=0, sticky="ew")
        locations.columnconfigure(1, weight=1)
        for row, (label, var, browse) in enumerate((
                ("Portal 2 folder", self.game_var, self.browse_game),
                ("Steam executable", self.steam_var, self.browse_steam))):
            ttk.Label(locations, text=label).grid(row=row, column=0, sticky="w", padx=(0, 14), pady=7)
            ttk.Entry(locations, textvariable=var).grid(row=row, column=1, sticky="ew", pady=7)
            ttk.Button(locations, text="Browse...", command=browse).grid(row=row, column=2, padx=(10, 0))
        actions = ttk.Frame(page)
        actions.grid(row=6, column=0, sticky="w", pady=(16, 12))
        ttk.Button(actions, text="Apply without launching", command=self.apply).pack(side="left")
        ttk.Button(actions, text="Restore original game files", command=self.restore).pack(side="left", padx=10)
        ttk.Label(page, text="Original files are backed up before first install. Updates keep that same snapshot.\n"
                  "Restore removes only files created by the launcher and restores pre-existing files.",
                  style="Muted.TLabel", wraplength=880, justify="left").grid(row=7, column=0, sticky="w")
        diagnostics = ttk.Frame(page)
        diagnostics.grid(row=8, column=0, sticky="w", pady=(18, 0))
        ttk.Button(diagnostics, text="Open mod log", command=self.open_log).pack(side="left")
        ttk.Button(diagnostics, text="Open backups", command=self.open_backups).pack(side="left", padx=10)
        ttk.Button(diagnostics, text="About / limitations", command=self.about).pack(side="left")

    def _scroll_page(self, title):
        page = ttk.Frame(self.notebook)
        self.notebook.add(page, text=title)
        canvas = tk.Canvas(page, bg=BG, highlightthickness=0)
        scroll = ttk.Scrollbar(page, orient="vertical", command=canvas.yview)
        scroll.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True)
        canvas.configure(yscrollcommand=scroll.set)
        body = ttk.Frame(canvas, padding=20)
        window = canvas.create_window((0, 0), window=body, anchor="nw")
        body.bind("<Configure>", lambda event: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind("<Configure>", lambda event: canvas.itemconfigure(window, width=event.width))
        body.columnconfigure(0, weight=1)
        return body, canvas

    @staticmethod
    def _bind_wheel(body, canvas):
        # Local bindings avoid stale global handlers after closing the launcher.
        def wheel(event):
            canvas.yview_scroll(-int(event.delta / 120), "units")
            return "break"
        def bind(widget):
            if not isinstance(widget, ttk.Combobox):
                widget.bind("<MouseWheel>", wheel)
            for child in widget.winfo_children():
                bind(child)
        bind(body)
        canvas.bind("<MouseWheel>", wheel)

    def _settings_page(self, title, advanced):
        body, canvas = self._scroll_page(title)
        ttk.Label(body, text="Changes take effect on your next Apply or Launch. Save alone does not touch the game.",
                  style="Muted.TLabel", wraplength=760).grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 14))
        row, group = 1, None
        for item in settings.SETTINGS:
            if (item.key in settings.BASIC_KEYS) == advanced:
                continue
            if group != item.group:
                group = item.group
                ttk.Label(body, text=group, style="Section.TLabel").grid(
                    row=row, column=0, columnspan=2, sticky="w", pady=(14, 8))
                row += 1
            ttk.Label(body, text=item.label).grid(row=row, column=0, sticky="w", padx=(0, 15), pady=5)
            if item.choices == ("false", "true"):
                control = ttk.Checkbutton(body, text="Enabled", variable=self.vars[item.key],
                                          onvalue="true", offvalue="false")
            elif item.choices:
                control = ttk.Combobox(body, textvariable=self.vars[item.key], values=item.choices,
                                       state="readonly", width=23)
            else:
                control = ttk.Entry(body, textvariable=self.vars[item.key], width=25)
            control.grid(row=row, column=1, sticky="e", pady=5)
            row += 1
            help_text = item.help
            if item.bounds:
                low, high = item.bounds
                help_text = f"Range: {low:g} to {high:g}. " + help_text
            if advanced:
                help_text = f"{item.key} — " + help_text
            if help_text:
                ttk.Label(body, text=help_text, style="Muted.TLabel", wraplength=720,
                          justify="left").grid(row=row, column=0, columnspan=2, sticky="w", pady=(0, 10))
                row += 1
        self._bind_wheel(body, canvas)

    def preferences(self):
        return {"version": 1, "game_dir": self.game_var.get().strip(),
                "steam_exe": self.steam_var.get().strip(),
                "values": settings.validate_settings({key: var.get() for key, var in self.vars.items()})}

    def _changed(self, *_):
        try:
            prefs = self.preferences()
            recommended = prefs["values"] == settings.recommended_settings()
            dirty = prefs != self.saved
            self.profile_var.set(("Recommended setup" if recommended else "Custom setup") +
                                 (" • unsaved changes" if dirty else ""))
            values = prefs["values"]
            features = [values["TrackingMode"] + " tracking", "portal orientation: " + values["PortalOrientationMode"]]
            if values["RoomscaleMode"] == "ActiveExperimental":
                features.append("physical roomscale walking")
            if values["AimMode"] == "2":
                features.append("world aim line" if values["ExperimentalWorldAimMarker"] == "true" else
                                "native beam (currently invisible on the tested setup)")
                features.append("original Portal reticle" if values["ExperimentalStereoReticle"] == "false" else "atlas reticle rollback")
            if values["ExperimentalViewmodelAlignment"] == "true":
                features.append("aligned portal gun / glow")
            if values["ExperimentalHUDOverlay"] == "true":
                features.append("caption overlay")
            if values["Haptics"] == "true" and values["HapticsIntensity"] != "0":
                features.append("controller haptics")
            elif values["ExperimentalPortalShotHaptics"] == "true":
                features.append("portal shot haptics")
            self.summary_var.set(" • ".join(features))
        except ValueError as error:
            self.profile_var.set("Custom setup • needs attention")
            self.summary_var.set(str(error))

    def _error(self, error):
        self.status_var.set(str(error))
        messagebox.showerror("Portal2VR", str(error), parent=self.root)

    def save(self):
        try:
            prefs = self.preferences()
            settings.save_preferences(self.preferences_path, prefs)
            self.saved = prefs
            self._changed()
            self.status_var.set("Settings saved locally. Game files have not changed; use Apply or Launch.")
            return True
        except (OSError, ValueError, RuntimeError) as error:
            self._error(error)
            return False

    def reset_recommended(self):
        for key, value in settings.recommended_settings().items():
            self.vars[key].set(value)
        self.status_var.set("Recommended settings restored in the form. Paths are unchanged; Save or Launch when ready.")

    def config_preview(self):
        template = (self.repo / "L4D2VR/config.txt").read_text(encoding="utf-8")
        return core.apply_config_values(template, self.preferences()["values"])

    def show_preview(self):
        try:
            text = self.config_preview()
        except (OSError, ValueError) as error:
            self._error(error)
            return
        window = tk.Toplevel(self.root)
        window.title("Config preview — not applied")
        window.geometry("930x640")
        view = tk.Text(window, bg=BG, fg=TEXT, font=("Consolas", 10), wrap="none", padx=12, pady=12)
        scroll = ttk.Scrollbar(window, command=view.yview)
        scroll.pack(side="right", fill="y")
        view.configure(yscrollcommand=scroll.set)
        view.pack(fill="both", expand=True)
        view.insert("1.0", text)
        view.configure(state="disabled")

    def _prepare(self, launch=False):
        if game_is_running():
            raise RuntimeError("Close Portal 2 before changing game files or launching again.")
        prefs = self.preferences()
        game, steam = Path(prefs["game_dir"]), Path(prefs["steam_exe"])
        if launch:
            core.validate_launch_paths(self.repo, game, steam)
        core.plan_install(self.repo, game, "baseline", config_values=prefs["values"])
        core.recover_pending_transaction(self.install_state_path)
        state = core.load_state(self.install_state_path)
        if not state["managed_files"] and not messagebox.askyesno(
                "Install Portal2VR", f"Install the mod in:\n{game}\n\n"
                "Existing files will be backed up first. You can restore them from this launcher.", parent=self.root):
            return None
        return prefs

    def _stage(self, prefs):
        settings.save_preferences(self.preferences_path, prefs)
        self.saved = prefs
        core.stage_install(self.repo, Path(prefs["game_dir"]), self.install_state_path,
                           "baseline", config_values=prefs["values"])
        # config.txt now carries the imported in-game values; an unretired file would
        # override later launcher edits.
        settings.retire_ingame_settings(Path(prefs["game_dir"]), self.ingame)
        self.ingame = {}
        self._changed()

    def apply(self):
        try:
            prefs = self._prepare()
            if prefs is None:
                return
            self._stage(prefs)
            self.status_var.set("Settings applied. Original backups retained. No game was launched.")
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            self._error(error)

    def launch(self):
        staged = False
        try:
            prefs = self._prepare(launch=True)
            if prefs is None:
                return
            self._stage(prefs)
            staged = True
            steam = Path(prefs["steam_exe"])
            subprocess.Popen(core.build_steam_command(steam), cwd=str(steam.parent))
            self.status_var.set("Launch request sent to Steam. Mod files stay installed until you choose Restore original game files.")
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            if staged:
                error = RuntimeError(f"Steam launch failed: {error}. Mod files and backups are retained; retry or use Restore original game files.")
            self._error(error)

    def restore(self):
        try:
            if game_is_running():
                raise RuntimeError("Close Portal 2 before restoring game files.")
            recovered = core.recover_pending_transaction(self.install_state_path)
            state = core.load_state(self.install_state_path)
            if not state["managed_files"]:
                core.restore_install(Path(state["game_dir"]), self.install_state_path)
                self.status_var.set("Interrupted operation recovered; original files restored." if recovered else
                                    "No files are currently managed by the launcher.")
                return
            game = Path(state["installed_game_dir"])
            if not messagebox.askyesno("Restore originals", f"Restore pre-install files in:\n{game}\n\n"
                                      "Your settings and backup snapshot will be kept.", parent=self.root):
                return
            core.restore_install(game, self.install_state_path)
            self.status_var.set("Original files restored. Settings and backup snapshot retained.")
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            self._error(error)

    def browse_game(self):
        path = filedialog.askdirectory(parent=self.root, title="Select the folder containing portal2.exe",
                                       initialdir=self.game_var.get())
        if path:
            self.game_var.set(path)

    def browse_steam(self):
        path = filedialog.askopenfilename(parent=self.root, title="Select Steam.exe",
                                         initialdir=str(Path(self.steam_var.get()).parent),
                                         filetypes=[("Steam executable", "steam.exe")])
        if path:
            self.steam_var.set(path)

    def _open(self, path):
        try:
            if not path.exists():
                raise FileNotFoundError(f"Not found: {path}")
            os.startfile(str(path))
        except (OSError, ValueError) as error:
            self._error(error)

    def open_log(self):
        self._open(Path(self.game_var.get()) / "bin/portal2vr.log")

    def open_backups(self):
        self._open(self.install_state_path.parent / ".launcher-backups")

    def about(self):
        messagebox.showinfo("About Portal2VR", "Incremental modernization of Gistix/portal2vr.\n\n"
                            "Windows Portal 2, 32-bit runtime, SteamVR / OpenVR, modified DXVK.\n"
                            "Recommended: tested world aim line with original dynamic Portal reticle.\n\n"
                            "Native robot_point_beam remains invisible. Experimental portal modes, floor/ceiling "
                            "comfort, performance and other headset coverage need further testing. No OpenXR.\n\n"
                            "Backups and preferences are local to this repository. Keep tools/.launcher-state.json "
                            "and tools/.launcher-backups together for recovery.", parent=self.root)

    def close(self):
        try:
            dirty = self.preferences() != self.saved
        except ValueError:
            dirty = True
        if dirty:
            answer = messagebox.askyesnocancel("Unsaved settings", "Save your settings locally before closing?", parent=self.root)
            if answer is None or (answer and not self.save()):
                return
        self.root.destroy()


def package_self_test(report: Path) -> int:
    """Exercise the actual packaged UI/installer using temporary game fixtures."""
    import json
    from tempfile import TemporaryDirectory
    result = {"frozen": bool(getattr(sys, "frozen", False)), "package_root": str(REPO)}
    root = None
    try:
        with TemporaryDirectory(prefix="portal2vr-package-test-") as temporary:
            folder = Path(temporary)
            game, steam = folder / "Portal 2", folder / "steam.exe"
            (game / "bin").mkdir(parents=True)
            (game / "portal2.exe").write_bytes(b"fixture")
            steam.write_bytes(b"fixture")
            original = game / "bin/d3d9.dll"
            original.write_bytes(b"fixture original")
            state = folder / "state.json"
            root = tk.Tk()
            root.withdraw()
            app = Launcher(root, REPO, folder / "preferences.json", state)
            values = settings.recommended_settings()
            app.game_var.set(str(game))
            app.steam_var.set(str(steam))
            app.config_preview()
            root.update_idletasks()
            planned = core.plan_install(REPO, game, "baseline", config_values=values)
            core.stage_install(REPO, game, state, "baseline", config_values=values)
            core.restore_install(game, state)
            if original.read_bytes() != b"fixture original" or (game / "VR").exists():
                raise RuntimeError("Packaged installer failed fixture restoration")
            result.update(ok=True, payload_count=len(planned), tkinter=True, restored=True)
    except Exception as error:
        result.update(ok=False, error=str(error))
    finally:
        if root:
            root.destroy()
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps(result, indent=2), encoding="utf-8")
    return 0 if result["ok"] else 1


def main():
    prepare_frozen_process()
    if len(sys.argv) == 3 and sys.argv[1] == "--self-test":
        return package_self_test(Path(sys.argv[2]))
    root = tk.Tk()
    root.withdraw()
    try:
        Launcher(root)
    except (OSError, ValueError, RuntimeError) as error:
        messagebox.showerror("Portal2VR startup", f"{error}\n\nNo game files were changed. "
                             "Keep the state and backup files; do not delete them to repair an installation.", parent=root)
        root.destroy()
        return 1
    root.deiconify()
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
