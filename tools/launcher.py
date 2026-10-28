"""User-facing Portal2VR launcher; deployment uses the shared backup installer."""

from pathlib import Path
import os
import subprocess
import sys
import tkinter as tk
from tkinter import filedialog, font as tkfont, messagebox, ttk

if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from tools import launcher_art as art, launcher_settings as settings, test_launcher_core as core
    from tools.test_launcher import game_is_running
    from tools.launcher_paths import application_root, prepare_frozen_process
else:
    from . import launcher_art as art, launcher_settings as settings, test_launcher_core as core
    from .test_launcher import game_is_running
    from .launcher_paths import application_root, prepare_frozen_process


REPO = application_root()
PREFERENCES_PATH = REPO / "tools/.user-launcher-settings.json"
INSTALL_STATE_PATH = REPO / "tools/.launcher-state.json"
BG, PANEL, TEXT, MUTED, ACCENT = "#0a121b", "#0d1925", "#e9f1f8", "#9fb3c6", "#4fc3ff"
ACCENT_DEEP, ORANGE, FIELD, FIELD_BORDER, LINE = "#1d7fd0", "#ff9d42", "#08121c", "#33506b", "#1c2d3e"
# Layout in 96-DPI pixels; Launcher.px scales them to the monitor.
NAV_WIDTH, PANEL_TOP, FOOTER, MIN_SIZE = 276, 150, 64, (960, 640)
PAGES = (("Play", ""), ("Settings", ""), ("Advanced", ""))
PLAY, HEADSET, SYNC, RESTORE, LOG, FOLDER, INFO, SAVE, UNDO, PREVIEW, WARNING = (
    "", "", "", "", "", "", "", "", "", "", "")


def tracked(text, spaces=1):
    """Letter-spaced capitals. Tk has no tracking, so thin spaces stand in for it;
    words keep a plain space so wrapped lines start flush."""
    gap = " " * spaces
    return (gap + " ").join(gap.join(word) for word in text.upper().split(" "))


def dark_title_bar(window):
    """Windows 11 dark caption matching the launcher; older systems keep the default."""
    if sys.platform != "win32":
        return
    try:
        import ctypes
        window.update_idletasks()  # The frame handle changes once Tk creates the wrapper.
        hwnd = int(window.wm_frame(), 16)
        for attribute, value in ((20, 1), (35, 0x001B120A), (36, 0x00F8F1E9)):
            data = ctypes.c_int(value)
            ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, attribute, ctypes.byref(data), ctypes.sizeof(data))
    except (AttributeError, OSError, ValueError, tk.TclError):
        pass


class CanvasButton:
    """Button drawn on a canvas, so it sits on the artwork without a widget box."""

    def __init__(self, app, canvas, text, command, icon="", style="outline"):
        self.app, self.canvas, self.text, self.command, self.icon, self.style = app, canvas, text, command, icon, style
        self.tag, self.box, self.state, self.emphasis = f"button{id(self)}", None, "normal", False
        self.images, self.image_size, self.labels = {}, None, []
        app.buttons.append(self)
        canvas.tag_bind(self.tag, "<Enter>", lambda _: self._set("hover"))
        canvas.tag_bind(self.tag, "<Leave>", lambda _: self._set("normal"))
        canvas.tag_bind(self.tag, "<ButtonPress-1>", lambda _: self._set("pressed"))
        canvas.tag_bind(self.tag, "<ButtonRelease-1>", self._release)

    @property
    def height(self):
        return self.app.px(56 if self.style == "launch" else 36)

    def width(self):
        fonts, icon = self.app.fonts, 0
        if self.icon and self.app.icons:
            icon = fonts["icon"].measure(self.icon) + self.app.px(12)
        return self.app.px(40) + icon + fonts["button"].measure(tracked(self.text))

    def draw(self, x, y, width=None):
        c, px, fonts = self.canvas, self.app.px, self.app.fonts
        c.delete(self.tag)
        w, h = width or self.width(), self.height
        self.box, tags = (x, y, x + w, y + h), (self.tag, "layout")
        if self.style == "launch":
            if self.app.art:
                if self.image_size != (w, h):
                    self.image_size = (w, h)
                    self.images = {state: art.photo(art.launch_button((w, h), self.app.scale, state))
                                   for state in ("normal", "hover", "pressed")}
                self.face = c.create_image(x, y, anchor="nw", image=self.images[self.state], tags=tags)
            else:
                self.face = c.create_rectangle(x, y, x + w - 1, y + h - 1, tags=tags)
            left, right, cy = x + px(92), x + w - px(84), y + h // 2
            self.labels = [c.create_text((left + right) // 2, cy, text=tracked(self.text), font=fonts["launch"],
                                         fill="#ffffff", tags=tags)]
            if self.app.icons:
                self.labels += [c.create_text(x + px(46), cy, text=self.icon, font=fonts["launch_icon"],
                                              fill="#ffffff", tags=tags),
                                c.create_text(x + w - px(42), cy, text=HEADSET, font=fonts["launch_icon"],
                                              fill="#ffffff", tags=tags)]
        else:
            self.face = c.create_rectangle(x, y, x + w - 1, y + h - 1, tags=tags)
            label, content = tracked(self.text), self.width() - px(40)
            cursor, cy, self.labels = x + (w - content) // 2, y + h // 2, []
            if self.icon and self.app.icons:
                self.labels.append(c.create_text(cursor, cy, text=self.icon, anchor="w", font=fonts["icon"], tags=tags))
                cursor += fonts["icon"].measure(self.icon) + px(12)
            self.labels.append(c.create_text(cursor, cy, text=label, anchor="w", font=fonts["button"], tags=tags))
        self.paint()

    def paint(self):
        if not self.box:
            return
        c, state = self.canvas, self.state
        if self.style == "launch":
            if self.app.art:
                c.itemconfigure(self.face, image=self.images[state])
            else:
                c.itemconfigure(self.face, outline=ACCENT,
                                fill={"normal": ACCENT_DEEP, "hover": "#2a95e4", "pressed": "#176bb0"}[state])
            return
        c.itemconfigure(self.face, fill={"normal": "#0b1621", "hover": "#12304a", "pressed": "#0a2338"}[state],
                        outline=ACCENT if self.emphasis or state != "normal" else "#71879c")
        color = "#ffffff" if state != "normal" else ACCENT if self.emphasis else "#dce6ef"
        for item in self.labels:
            c.itemconfigure(item, fill=color)

    def _set(self, state):
        self.state = state
        self.canvas.configure(cursor="" if state == "normal" else "hand2")
        self.paint()

    def _release(self, event):
        x, y = self.canvas.canvasx(event.x), self.canvas.canvasy(event.y)
        inside = self.box and self.box[0] <= x < self.box[2] and self.box[1] <= y < self.box[3]
        self._set("hover" if inside else "normal")
        if inside:
            self.command()

    def invoke(self):
        self.command()


class Toggle:
    """Square on/off switch for a "true"/"false" setting."""

    def __init__(self, app, canvas, var):
        self.app, self.canvas, self.var = app, canvas, var
        self.tag, self.items, self.hover = f"toggle{id(self)}", None, False
        self.width, self.height = app.px(44), app.px(22)
        canvas.tag_bind(self.tag, "<ButtonRelease-1>", lambda _: var.set("false" if var.get() == "true" else "true"))
        canvas.tag_bind(self.tag, "<Enter>", lambda _: self._hover(True))
        canvas.tag_bind(self.tag, "<Leave>", lambda _: self._hover(False))
        var.trace_add("write", lambda *_: self.paint())

    def draw(self, x, y):
        c, px, tags = self.canvas, self.app.px, (self.tag, "layout")
        c.delete(self.tag)
        self.origin = (x, y)
        state = c.create_text(x - px(10), y + self.height // 2, anchor="e", font=self.app.fonts["muted"], tags=tags)
        track = c.create_rectangle(x, y, x + self.width - 1, y + self.height - 1, tags=tags)
        knob = c.create_rectangle(0, 0, 0, 0, outline="", tags=tags)
        self.items = (state, track, knob)
        self.paint()

    def paint(self):
        if not self.items or not self.canvas.winfo_exists():
            return
        c, px, on = self.canvas, self.app.px, self.var.get() == "true"
        state, track, knob = self.items
        x, y = self.origin
        size = self.height - px(8)
        left = x + self.width - px(4) - size if on else x + px(4)
        c.itemconfigure(state, text="On" if on else "Off", fill=TEXT if on else MUTED)
        c.itemconfigure(track, fill=ACCENT_DEEP if on else FIELD,
                        outline=ACCENT if on or self.hover else FIELD_BORDER)
        c.itemconfigure(knob, fill="#ffffff" if on else "#7f95aa")
        c.coords(knob, left, y + px(4), left + size, y + px(4) + size)

    def _hover(self, hover):
        self.hover = hover
        self.canvas.configure(cursor="hand2" if hover else "")
        self.paint()


class Page:
    """Scrolling panel body. The frosted backdrop crop stays fixed behind the content."""

    def __init__(self, app):
        self.app, self.layout, self.box, self.visible = app, None, None, False
        self.width = self.height = self.content = 0
        self.canvas = tk.Canvas(app.canvas, bg=PANEL, bd=0, highlightthickness=0, yscrollincrement=app.px(24))
        self.backdrop = self.canvas.create_image(0, 0, anchor="nw")
        self.scrollbar = ttk.Scrollbar(app.canvas, orient="vertical", command=self.scroll,
                                       style="Launcher.Vertical.TScrollbar")
        self.canvas.configure(yscrollcommand=self.scrollbar.set)
        self.bind_wheel(self.canvas)

    def bind_wheel(self, widget):
        # Widget bindings also keep comboboxes from cycling values while the page scrolls.
        widget.bind("<MouseWheel>", self._wheel)

    def _wheel(self, event):
        if self.content > self.height:
            self.scroll("scroll", (-2 if event.delta > 0 else 2) * max(1, abs(event.delta) // 120), "units")
        return "break"

    def scroll(self, *args):
        self.canvas.yview(*args)
        self.canvas.coords(self.backdrop, 0, self.canvas.canvasy(0))

    def place(self, box, visible):
        self.box = box
        width = box[2] - box[0]
        if width != self.width:
            self.width = width
            self.content = self.layout(self, width)
        self.height = box[3] - box[1]
        self.show(visible)

    def relayout(self):
        if self.box:
            self.content = self.layout(self, self.width)
            self.show(self.visible)

    def show(self, visible):
        self.visible = visible
        if not self.box:
            return
        px, (x0, y0, x1, y1) = self.app.px, self.box
        self.canvas.configure(scrollregion=(0, 0, self.width, max(self.content, self.height)))
        if visible:
            self.canvas.place(x=x0, y=y0, width=x1 - x0, height=y1 - y0)
        else:
            self.canvas.place_forget()
        if visible and self.content > self.height:
            self.scrollbar.place(x=x1 - px(12), y=y0 + px(6), width=px(8), height=self.height - px(12))
            self.scrollbar.lift()
        else:
            self.scrollbar.place_forget()
        self.scroll("moveto", self.canvas.yview()[0])


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
        self.status_error = self._next_error = False
        self.root.title("Portal 2 VR Launcher")
        self.scale = max(1.0, root.winfo_fpixels("1i") / 96)
        # Fixed size, so the artwork is composed once instead of on every resize step.
        # Small screens get a smaller window; pages scroll when their content does not fit.
        screen_width, screen_height = root.winfo_screenwidth(), root.winfo_screenheight()
        width = self.px(max(MIN_SIZE[0], min(1280, round(screen_width / self.scale) - 80)))
        height = self.px(max(MIN_SIZE[1], min(820, round(screen_height / self.scale) - 120)))
        self.root.geometry(f"{width}x{height}+{max(0, (screen_width - width) // 2)}"
                           f"+{max(0, (screen_height - height) // 2 - self.px(20))}")
        self.root.resizable(False, False)
        self.root.configure(bg=BG)
        if art.ICON.is_file():
            try:
                self.root.iconbitmap(default=str(art.ICON))
            except tk.TclError:
                pass
        dark_title_bar(self.root)
        self._fonts()
        self._theme()
        self._build(width, height)
        for var in (*self.vars.values(), self.game_var, self.steam_var):
            var.trace_add("write", self._changed)
        self.status_var.trace_add("write", self._status_changed)
        self._changed()
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def px(self, value):
        return round(value * self.scale)

    def _fonts(self):
        families = set(tkfont.families(self.root))
        def pick(*names):
            return next((name for name in names if name in families), "Segoe UI")
        display = pick("Bahnschrift")
        semibold, light = pick("Bahnschrift SemiBold", "Segoe UI Semibold"), pick("Bahnschrift Light", "Segoe UI Light")
        self.icons = next((name for name in ("Segoe Fluent Icons", "Segoe MDL2 Assets") if name in families), None)
        icons = self.icons or "Segoe UI"
        spec = {"kicker": (semibold, 13), "title": (light, 27), "section": (semibold, 15), "nav": (semibold, 19),
                "launch": (semibold, 26), "button": (display, 14), "body": ("Segoe UI", 15),
                "muted": ("Segoe UI", 14), "small": ("Segoe UI", 13), "entry": ("Segoe UI", 14),
                "mono": ("Consolas", 12), "logo": (display, 56, "bold"), "logo_vr": (light, 40),
                "icon": (icons, 16), "nav_icon": (icons, 22), "launch_icon": (icons, 26)}
        self.fonts = {name: tkfont.Font(self.root, family=family, size=-self.px(size), weight=weight[0] if weight else "normal")
                      for name, (family, size, *weight) in spec.items()}

    def _theme(self):
        px = self.px
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure(".", background=BG, foreground=TEXT, font=("Segoe UI", 10))
        field = dict(fieldbackground=FIELD, foreground=TEXT, insertcolor=TEXT, bordercolor=FIELD_BORDER,
                     lightcolor=FIELD, darkcolor=FIELD, padding=(px(8), px(3)))
        style.configure("Launcher.TEntry", **field)
        style.map("Launcher.TEntry", bordercolor=[("focus", ACCENT), ("hover", "#4d6d8c")])
        style.configure("Launcher.TCombobox", **field, background=FIELD, arrowcolor=MUTED, arrowsize=px(11),
                        selectbackground=FIELD, selectforeground=TEXT)
        style.map("Launcher.TCombobox", fieldbackground=[("readonly", FIELD)], foreground=[("readonly", TEXT)],
                  background=[("active", "#132a40"), ("readonly", FIELD)], arrowcolor=[("hover", TEXT)],
                  bordercolor=[("focus", ACCENT), ("hover", "#4d6d8c")],
                  selectbackground=[("readonly", FIELD)], selectforeground=[("readonly", TEXT)])
        style.layout("Launcher.Vertical.TScrollbar", [("Vertical.Scrollbar.trough", {"sticky": "ns", "children": [
            ("Vertical.Scrollbar.thumb", {"expand": "1", "sticky": "nswe"})]})])
        style.configure("Launcher.Vertical.TScrollbar", troughcolor=PANEL, background="#34536f", bordercolor=PANEL,
                        lightcolor="#34536f", darkcolor="#34536f", gripcount=0)
        style.map("Launcher.Vertical.TScrollbar", background=[("active", "#3d6286")])
        for option, value in (("background", FIELD), ("foreground", TEXT), ("selectBackground", ACCENT_DEEP),
                              ("selectForeground", "#ffffff"), ("font", self.fonts["entry"])):
            self.root.option_add(f"*TCombobox*Listbox.{option}", value)

    def _build(self, width, height):
        self.art = art.Backdrop() if art.enabled() else None
        self.buttons, self.images, self.backdrop_image = [], {}, None
        self.size, self.current, self.nav_hover = None, "Play", None
        self.canvas = tk.Canvas(self.root, bg=BG, bd=0, highlightthickness=0, width=width, height=height)
        self.canvas.pack(fill="both", expand=True)
        self.backdrop = self.canvas.create_image(0, 0, anchor="nw")
        for name, _ in PAGES:
            tag = f"nav:{name}"
            self.canvas.tag_bind(tag, "<Enter>", lambda _, name=name: self._hover_nav(name))
            self.canvas.tag_bind(tag, "<Leave>", lambda _: self._hover_nav(None))
            self.canvas.tag_bind(tag, "<ButtonRelease-1>", lambda _, name=name: self.show_page(name))
        self.pages = {name: Page(self) for name, _ in PAGES}
        self._build_play(self.pages["Play"])
        self._build_settings(self.pages["Settings"], advanced=False)
        self._build_settings(self.pages["Advanced"], advanced=True)
        self.save_button = CanvasButton(self, self.canvas, "Save settings", self.save, SAVE)
        self.footer_buttons = (self.save_button,
                               CanvasButton(self, self.canvas, "Restore recommended", self.reset_recommended, UNDO),
                               CanvasButton(self, self.canvas, "Preview config", self.show_preview, PREVIEW))
        self.canvas.bind("<Configure>", lambda event: self._relayout(event.width, event.height))
        self._relayout(width, height)

    def _build_play(self, page):
        c = page.canvas
        self.launch_button = CanvasButton(self, c, "Launch Portal 2 in VR", self.launch, PLAY, "launch")
        self.path_rows = []
        for label, var, browse in (("Portal 2 folder", self.game_var, self.browse_game),
                                   ("Steam executable", self.steam_var, self.browse_steam)):
            entry = ttk.Entry(c, textvariable=var, style="Launcher.TEntry", font=self.fonts["entry"])
            page.bind_wheel(entry)
            self.path_rows.append((label, c.create_window(0, 0, window=entry, anchor="nw"),
                                   CanvasButton(self, c, "Browse…", browse)))
        self.game_actions = (CanvasButton(self, c, "Apply without launching", self.apply, SYNC),
                             CanvasButton(self, c, "Restore original game files", self.restore, RESTORE))
        self.diagnostics = (CanvasButton(self, c, "Open mod log", self.open_log, LOG),
                            CanvasButton(self, c, "Open backups", self.open_backups, FOLDER),
                            CanvasButton(self, c, "About / limitations", self.about, INFO))
        page.layout = self._layout_play

    def _build_settings(self, page, advanced):
        c, group, page.rows = page.canvas, None, []
        for item in settings.SETTINGS:
            if (item.key in settings.BASIC_KEYS) == advanced:
                continue
            if group != item.group:
                group = item.group
                page.rows.append((group, None, None))
            var = self.vars[item.key]
            if item.choices == ("false", "true"):
                control = Toggle(self, c, var)
            else:
                if item.choices:
                    widget = ttk.Combobox(c, textvariable=var, values=item.choices, state="readonly",
                                          style="Launcher.TCombobox", font=self.fonts["entry"])
                    widget.bind("<<ComboboxSelected>>", lambda event: event.widget.selection_clear())
                else:
                    widget = ttk.Entry(c, textvariable=var, style="Launcher.TEntry", font=self.fonts["entry"],
                                       justify="right")
                page.bind_wheel(widget)
                control = c.create_window(0, 0, window=widget, anchor="nw")
            page.rows.append((None, item, control))
        page.layout = lambda page, width: self._layout_settings(page, width, advanced)

    def _relayout(self, width, height):
        px = self.px
        if (width, height) == self.size or width < px(200) or height < px(200):
            return
        self.size = (width, height)
        c = self.canvas
        c.delete("layout")
        x0, y0 = px(NAV_WIDTH), px(PANEL_TOP)
        self.panel = (x0, y0, max(x0 + px(400), width - px(56 if width >= px(1120) else 28)),
                      max(y0 + px(200), height - px(FOOTER + 10)))
        self.footer_top = height - px(FOOTER)
        self.nav = {name: (px(16), y0 + px(28 + 66 * i), x0, y0 + px(84 + 66 * i)) for i, (name, _) in enumerate(PAGES)}
        self.separators = [(px(40), x0, y0 + px(89 + 66 * i)) for i in range(len(PAGES) - 1)]
        if not self.art:
            c.create_rectangle(0, self.footer_top, width, height, fill="#070d14", outline="", tags="layout")
            c.create_line(0, self.footer_top, width, self.footer_top, fill=LINE, tags="layout")
            c.create_rectangle(*self.panel, fill=PANEL, outline="#2d4a66", tags="layout")
            for sx0, sx1, sy in self.separators:
                c.create_line(sx0, sy, sx1, sy, fill=LINE, tags="layout")
        if not (self.art and self.art.wordmark):
            c.create_text(px(36), px(24), text="PORTAL 2", anchor="nw", font=self.fonts["logo"], fill=TEXT,
                          tags="layout")
            c.create_text(px(38), px(92), text=tracked("VR", 2), anchor="nw", font=self.fonts["logo_vr"],
                          fill="#d6e2ee", tags="layout")
        self._draw_nav()
        self._draw_footer()
        x0, y0, x1, y1 = self.panel
        for name, page in self.pages.items():
            page.place((x0 + 1, y0 + 1, x1 - 1, y1 - 1), name == self.current)
        if self.art:
            self._render_backdrop()

    def _render_backdrop(self):
        px, (x0, y0, x1, y1) = self.px, self.panel
        self.backdrop_image = self.art.render(self.size, self.panel, self.footer_top, self.separators,
                                              (px(34), px(2)), self.scale)
        self.images = {"backdrop": art.photo(self.backdrop_image),
                       "panel": art.photo(self.backdrop_image.crop((x0 + 1, y0 + 1, x1 - 1, y1 - 1)))}
        self.canvas.itemconfigure(self.backdrop, image=self.images["backdrop"])
        for page in self.pages.values():
            page.canvas.itemconfigure(page.backdrop, image=self.images["panel"])
        self._draw_nav()

    def _nav_state(self, name):
        return "selected" if name == self.current else "hover" if name == self.nav_hover else "normal"

    def _nav_image(self, name, state):
        key = ("nav", name, state)
        if key not in self.images:
            crop = self.backdrop_image.crop(self.nav[name])
            self.images[key] = art.photo(crop if state == "normal" else art.highlight(crop, self.scale, state == "selected"))
        return self.images[key]

    def _draw_nav(self):
        c, px = self.canvas, self.px
        c.delete("nav")
        self.nav_items = {}
        for name, glyph in PAGES:
            x0, y0, x1, y1 = self.nav[name]
            tags, cy = ("nav", f"nav:{name}"), (y0 + y1) // 2
            if self.backdrop_image:
                face, bar = c.create_image(x0, y0, anchor="nw", tags=tags), None
            else:
                face = c.create_rectangle(x0, y0, x1 - 1, y1 - 1, outline="", tags=tags)
                bar = c.create_rectangle(x0, y0, x0 + px(4) - 1, y1 - 1, fill=ACCENT, outline="", tags=tags)
            icon = c.create_text(x0 + px(32), cy, text=glyph if self.icons else "", font=self.fonts["nav_icon"], tags=tags)
            label = c.create_text(x0 + px(78), cy, text=tracked(name), anchor="w", font=self.fonts["nav"], tags=tags)
            self.nav_items[name] = (face, bar, icon, label)
        self._paint_nav()

    def _paint_nav(self):
        c = self.canvas
        for name, (face, bar, icon, label) in self.nav_items.items():
            state = self._nav_state(name)
            if bar is None:
                c.itemconfigure(face, image=self._nav_image(name, state))
            else:
                c.itemconfigure(face, fill={"selected": "#15324d", "hover": "#10263a", "normal": BG}[state])
                c.itemconfigure(bar, state="normal" if state == "selected" else "hidden")
            color = {"selected": ACCENT, "hover": "#ffffff", "normal": "#c9d6e2"}[state]
            c.itemconfigure(icon, fill=color)
            c.itemconfigure(label, fill=color)

    def _hover_nav(self, name):
        self.nav_hover = name
        self.canvas.configure(cursor="hand2" if name and name != self.current else "")
        self._paint_nav()

    def show_page(self, name):
        self.current = name
        for page_name, page in self.pages.items():
            page.show(page_name == name)
        self.canvas.configure(cursor="")
        self._paint_nav()

    def _draw_footer(self):
        px = self.px
        x, y = self.panel[2], self.footer_top + (px(FOOTER) - px(36)) // 2
        for button in reversed(self.footer_buttons):
            x -= button.width()
            button.draw(x, y)
            x -= px(14)
        self.status_right = x - px(10)
        self._draw_status()

    def _status_changed(self, *_):
        self.status_error, self._next_error = self._next_error, False
        self._draw_status()

    def _draw_status(self):
        c, px = self.canvas, self.px
        c.delete("status")
        if not self.size:
            return
        cy, x = self.footer_top + px(FOOTER) // 2, px(36)
        if self.icons:
            c.create_text(x, cy, text=WARNING if self.status_error else INFO, anchor="w", font=self.fonts["icon"],
                          fill=ORANGE if self.status_error else ACCENT, tags="status")
        full = self.status_var.get()
        item = c.create_text(x + px(30), cy, text=full, anchor="w", width=max(px(120), self.status_right - x - px(30)),
                             font=self.fonts["small"], fill=TEXT if self.status_error else MUTED, tags="status")
        # Long messages are cut to fit the footer; errors are also shown in a dialog.
        length = len(full)
        while length > 24 and c.bbox(item)[3] - c.bbox(item)[1] > px(FOOTER - 8):
            length = int(length * .85)
            c.itemconfigure(item, text=full[:length].rstrip() + "…")

    def _text(self, canvas, x, y, right, text, font, fill):
        item = canvas.create_text(x, y, text=text, anchor="nw", width=max(1, right - x), font=self.fonts[font],
                                  fill=fill, tags="layout")
        return canvas.bbox(item)[3]

    def _heading(self, canvas, x, y, right, kicker, title):
        canvas.create_text(x, y, text=tracked(kicker, 2), anchor="nw", font=self.fonts["kicker"], fill=ACCENT,
                           tags="layout")
        y = self._text(canvas, x, y + self.px(20), right, tracked(title), "title", TEXT) + self.px(8)
        canvas.create_rectangle(x, y, x + self.px(56), y + self.px(2), fill=ACCENT, outline="", tags="layout")
        return y + self.px(16)

    def _flow(self, buttons, x, y, right):
        cursor = x
        for button in buttons:
            width = button.width()
            if cursor > x and cursor + width > right:
                cursor, y = x, y + button.height + self.px(12)
            button.draw(cursor, y)
            cursor += width + self.px(14)
        return y + buttons[0].height

    def _layout_play(self, page, width):
        c, px = page.canvas, self.px
        c.delete("layout")
        x, right = px(36), width - px(36)
        y = self._heading(c, x, px(22), right, "Portal 2 VR", "Experience Portal 2 in virtual reality")
        profile, _, note = self.profile_var.get().partition(" • ")
        item = c.create_text(x, y, text=tracked(profile), anchor="nw", font=self.fonts["section"], fill=TEXT,
                             tags="layout")
        if note:
            c.create_text(c.bbox(item)[2] + px(12), y, text=tracked("• " + note), anchor="nw",
                          font=self.fonts["section"], fill=ORANGE, tags="layout")
        y = self._text(c, x, y + px(24), right, self.summary_var.get(), "body", TEXT) + px(8)
        y = self._text(c, x, y, right, "Use SteamVR / OpenVR. Start SteamVR and connect your headset before launching.",
                       "muted", MUTED) + px(14)
        self.launch_button.draw(x, y, right - x)
        y = self._text(c, x, y + self.launch_button.height + px(8), right,
                       "Launch applies these settings first. Restart the game after changing them.",
                       "muted", MUTED) + px(18)
        c.create_text(x, y, text=tracked("Game files"), anchor="nw", font=self.fonts["section"], fill=TEXT,
                      tags="layout")
        y += px(28)
        browse_width = max(button.width() for _, _, button in self.path_rows)
        for label, entry, browse in self.path_rows:
            c.create_text(x, y + px(17), text=label, anchor="w", font=self.fonts["body"], fill=TEXT, tags="layout")
            c.coords(entry, x + px(156), y)
            c.itemconfigure(entry, width=max(px(80), right - browse_width - px(12) - x - px(156)), height=px(34))
            browse.draw(right - browse_width, y - px(1), browse_width)
            y += px(42)
        y = self._flow(self.game_actions, x, y + px(2), right)
        y = self._text(c, x, y + px(8), right,
                       "Original files are backed up before first install. Updates keep that same snapshot.\n"
                       "Restore removes only files created by the launcher and restores pre-existing files.",
                       "muted", MUTED) + px(12)
        return self._flow(self.diagnostics, x, y, right) + px(20)

    def _layout_settings(self, page, width, advanced):
        c, px, fonts = page.canvas, self.px, self.fonts
        c.delete("layout")
        x, right = px(36), width - px(36)
        y = self._heading(c, x, px(22), right, *(("Advanced", "Full mod configuration") if advanced else
                                                  ("Settings", "Comfort and controls")))
        y = self._text(c, x, y, right, "Changes take effect on your next Apply or Launch. "
                       "Save alone does not touch the game.", "muted", MUTED) + px(6)
        control_width = min(px(230), (right - x) // 3)
        text_right, first = right - control_width - px(28), True
        for group, item, control in page.rows:
            if group:
                label = c.create_text(x, y + px(18), text=tracked(group), anchor="nw", font=fonts["section"],
                                      fill=ACCENT, tags="layout")
                x0, y0, x1, y1 = c.bbox(label)
                c.create_line(x1 + px(14), (y0 + y1) // 2, right, (y0 + y1) // 2, fill=LINE, tags="layout")
                y, first = y1 + px(16), True
                continue
            if not first:
                c.create_line(x, y - px(9), right, y - px(9), fill=LINE, tags="layout")
            first, top = False, y
            bottom = self._text(c, x, y, text_right, item.label, "body", TEXT)
            if advanced:
                bottom = self._text(c, x, bottom + px(1), text_right, item.key, "mono", "#6f8aa3")
            help_text = item.help
            if item.bounds:
                low, high = item.bounds
                help_text = f"Range: {low:g} to {high:g}. " + help_text
            if help_text:
                bottom = self._text(c, x, bottom + px(3), text_right, help_text, "muted", MUTED)
            if isinstance(control, Toggle):
                control.draw(right - control.width, top)
                bottom = max(bottom, top + control.height)
            else:
                control_x = right - (control_width if item.choices else px(130))
                c.coords(control, control_x, top - px(5))
                c.itemconfigure(control, width=right - control_x, height=px(32))
                bottom = max(bottom, top + px(27))
            y = bottom + px(18)
        return y + px(8)

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
            self.summary_var.set("  •  ".join(feature[:1].upper() + feature[1:] for feature in features))
        except ValueError as error:
            dirty = True
            self.profile_var.set("Custom setup • needs attention")
            self.summary_var.set(str(error))
        self.save_button.emphasis = dirty
        self.save_button.paint()
        self.pages["Play"].relayout()

    def _error(self, error):
        self._next_error = True
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
        window = tk.Toplevel(self.root, bg=BG)
        window.title("Config preview — not applied")
        window.geometry(f"{self.px(930)}x{self.px(640)}")
        dark_title_bar(window)
        view = tk.Text(window, bg=FIELD, fg=TEXT, insertbackground=TEXT, selectbackground=ACCENT_DEEP,
                       font=("Consolas", 10), wrap="none", padx=12, pady=12, bd=0, highlightthickness=0)
        scroll = ttk.Scrollbar(window, command=view.yview, style="Launcher.Vertical.TScrollbar")
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
    if sys.platform == "win32":
        # Crisp text and artwork on scaled displays; Launcher.px follows the system DPI.
        try:
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except (AttributeError, OSError):
            pass
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
