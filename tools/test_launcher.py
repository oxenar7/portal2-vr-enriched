"""Temporary, local-only Portal2VR manual test launcher."""

from pathlib import Path
import os
import subprocess
import sys
import tkinter as tk
from tkinter import messagebox, ttk

if __package__:
    from . import games, test_launcher_core as core
else:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from tools import games, test_launcher_core as core


REPO = Path(__file__).resolve().parent.parent
STATE_PATH = Path(__file__).resolve().parent / ".launcher-state.json"


def game_is_running() -> bool:
    """Fail closed if the Windows process list cannot be checked. Both games run as
    portal2.exe; Alive & Kicking can also start as aak.exe."""
    for name in games.PROCESS_NAMES:
        result = subprocess.run(
            ["tasklist", "/FI", f"IMAGENAME eq {name}", "/FO", "CSV", "/NH"],
            capture_output=True, text=True, check=False, timeout=5,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        if result.returncode:
            raise RuntimeError(f"Cannot check {name} process: {result.stderr.strip()}")
        if f'"{name}"' in result.stdout.lower():
            return True
    return False


class TestLauncher:
    def __init__(self, root: tk.Tk, state: dict):
        self.root = root
        self.state = state
        self.root.title("Portal2VR — Local M0–M6 Tests")
        self.root.geometry("920x740")
        self.game_var = tk.StringVar(value=state["game_dir"])
        self.steam_var = tk.StringVar(value=state["steam_exe"])
        self.profile_ids = list(core.PROFILES)
        self.profile_labels = [core.PROFILES[item].label for item in self.profile_ids]
        selected = state["profile"] if state["profile"] in core.PROFILES else "baseline"
        self.profile_var = tk.StringVar(value=core.PROFILES[selected].label)
        checked = set(state["checks"])
        self.check_vars: dict[str, tk.BooleanVar] = {
            check.id: tk.BooleanVar(value=check.id in checked)
            for check in core.CHECKLIST
        }
        self.show_completed_var = tk.BooleanVar(value=False)

        outer = ttk.Frame(root, padding=12)
        outer.pack(fill="both", expand=True)
        outer.columnconfigure(1, weight=1)
        ttk.Label(outer, text="Portal 2 game directory").grid(row=0, column=0, sticky="w")
        ttk.Entry(outer, textvariable=self.game_var).grid(row=0, column=1, sticky="ew", padx=8)
        ttk.Label(outer, text="Steam.exe").grid(row=1, column=0, sticky="w", pady=6)
        ttk.Entry(outer, textvariable=self.steam_var).grid(row=1, column=1, sticky="ew", padx=8)
        ttk.Label(outer, text="Profile").grid(row=2, column=0, sticky="w")
        selector = ttk.Combobox(outer, textvariable=self.profile_var,
                                values=self.profile_labels, state="readonly")
        selector.grid(row=2, column=1, sticky="ew", padx=8)
        ttk.Label(outer, text="Recommended aim: 'Native reticle + muzzle line (fallback)' "
                  "keeps the original Portal artwork/status and the line at the gun muzzle. "
                  "'HUD reticle only, aim line off' removes the line for comparison. "
                  "'Stereo atlas + muzzle line (rollback)' is only the older reticle fallback. "
                  "All three preserve model, captions and active roomscale settings. "
                  "Active roomscale remains experimental movement; test small steps first. "
                  "M3 Observe remains diagnostic only.",
                  wraplength=850).grid(row=3, column=0, columnspan=2, sticky="w", pady=(10, 6))

        checklist = ttk.LabelFrame(outer, text="Checklist — tick only after a real test")
        checklist.grid(row=4, column=0, columnspan=2, sticky="nsew")
        outer.rowconfigure(4, weight=1)
        ttk.Checkbutton(checklist, text="Show completed checks",
                        variable=self.show_completed_var,
                        command=self.render_checklist).pack(anchor="w", padx=6, pady=4)
        canvas = tk.Canvas(checklist, highlightthickness=0)
        scrollbar = ttk.Scrollbar(checklist, orient="vertical", command=canvas.yview)
        self.inside = ttk.Frame(canvas)
        self.inside.bind("<Configure>", lambda _event: canvas.configure(scrollregion=canvas.bbox("all")))
        window_id = canvas.create_window((0, 0), window=self.inside, anchor="nw")
        canvas.bind("<Configure>",
                    lambda event: canvas.itemconfigure(window_id, width=event.width))
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        self.render_checklist()

        ttk.Label(outer, text="Test notes").grid(row=5, column=0, columnspan=2,
                                                       sticky="w", pady=(10, 2))
        self.notes = tk.Text(outer, height=5, wrap="word")
        self.notes.insert("1.0", state["notes"])
        self.notes.grid(row=6, column=0, columnspan=2, sticky="nsew")
        buttons = ttk.Frame(outer)
        buttons.grid(row=7, column=0, columnspan=2, sticky="ew", pady=(10, 0))
        for label, callback in (("Save checklist", self.save),
                                ("Launch game", self.launch),
                                ("Open log", self.open_log),
                                ("Restore files", self.restore)):
            ttk.Button(buttons, text=label, command=callback).pack(side="left", padx=(0, 8))
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def choose_profile(self, profile: str) -> None:
        self.profile_var.set(core.PROFILES[profile].label)

    def render_checklist(self) -> None:
        for child in self.inside.winfo_children():
            child.destroy()
        completed = {item_id for item_id, variable in self.check_vars.items()
                     if variable.get()}
        current_group = None
        for check in core.visible_checks(completed, self.show_completed_var.get()):
            if check.group != current_group:
                current_group = check.group
                ttk.Label(self.inside, text=current_group,
                          font=("Segoe UI", 10, "bold")).pack(anchor="w", pady=(10, 2))
            row = ttk.Frame(self.inside)
            row.pack(fill="x", anchor="w")
            ttk.Checkbutton(row, text=check.label,
                            variable=self.check_vars[check.id]).pack(side="left", anchor="w")
            ttk.Button(row, text=core.PROFILES[check.profile].label,
                       command=lambda profile=check.profile: self.choose_profile(profile)).pack(
                           side="right", padx=8)

    def selected_profile(self) -> str:
        return self.profile_ids[self.profile_labels.index(self.profile_var.get())]

    def save(self) -> None:
        self.state["game_dir"] = self.game_var.get().strip()
        self.state["steam_exe"] = self.steam_var.get().strip()
        self.state["profile"] = self.selected_profile()
        known = set(self.check_vars)
        self.state["checks"] = [item.id for item in core.CHECKLIST
                                if self.check_vars[item.id].get()] + [
                                    item_id for item_id in self.state["checks"]
                                    if item_id not in known]
        self.state["notes"] = self.notes.get("1.0", "end-1c")
        self.state = core.save_preferences(STATE_PATH, self.state)

    def launch(self) -> None:
        try:
            game = Path(self.game_var.get().strip())
            steam = Path(self.steam_var.get().strip())
            if game_is_running():
                raise RuntimeError("Portal 2 is running. Close it before staging files.")
            core.validate_launch_paths(REPO, game, steam)
            planned = core.plan_install(REPO, game, self.selected_profile())
            existing = [target for target in planned if target.is_file()]
            preview = "\n".join(str(target) for target in planned)
            prompt = (f"Profile: {self.profile_var.get()}\n"
                      f"Destination files ({len(planned)}):\n{preview}\n\n"
                      f"Existing files: {len(existing)} — the launcher keeps local backups "
                      "and can restore them later.\n"
                      "Staged files remain after the game exits; use Restore files. "
                      "Continue?")
            if not messagebox.askyesno("Portal2VR — Confirm staging", prompt):
                return
            core.recover_pending_transaction(STATE_PATH)
            self.save()
            core.stage_install(REPO, game, STATE_PATH, self.selected_profile())
            self.state = core.load_state(STATE_PATH)
            subprocess.Popen(core.build_steam_command(steam), cwd=steam.parent)
            messagebox.showinfo("Portal2VR", "Steam launch requested with -insecure. "
                                "Confirm Portal 2 starts, then record results manually. "
                                "Original backups are in tools/.launcher-backups.")
        except Exception as error:
            messagebox.showerror("Portal2VR — Launch failed", str(error) +
                                 "\n\nIf files were staged, use Restore files after "
                                 "closing the game. Backups remain local.")

    def restore(self) -> None:
        try:
            if game_is_running():
                raise RuntimeError("Close Portal 2 before restoring files.")
            core.recover_pending_transaction(STATE_PATH)
            self.save()
            game = Path(self.game_var.get().strip())
            restored = core.restore_install(game, STATE_PATH)
            self.state = core.load_state(STATE_PATH)
            messagebox.showinfo("Portal2VR", f"Restored {len(restored)} files. "
                                "Original backups remain in tools/.launcher-backups.")
        except Exception as error:
            messagebox.showerror("Portal2VR — Restore failed", str(error) +
                                 "\n\nDo not remove backups or game files until "
                                 "you have checked their state.")

    def open_log(self) -> None:
        log = Path(self.game_var.get().strip()) / "bin/portal2vr.log"
        if not log.is_file():
            messagebox.showinfo("Portal2VR", f"Log not found: {log}")
            return
        os.startfile(log)

    def close(self) -> None:
        try:
            self.save()
        except Exception as error:
            messagebox.showerror("Portal2VR", f"Cannot save checklist: {error}")
            return
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    try:
        state = core.load_state(STATE_PATH)
    except ValueError as error:
        messagebox.showerror("Portal2VR — Launcher state", str(error))
        root.destroy()
        return
    TestLauncher(root, state)
    root.mainloop()


if __name__ == "__main__":
    main()
