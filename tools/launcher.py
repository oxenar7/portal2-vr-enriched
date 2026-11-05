"""User-facing Portal 2 VR Enriched launcher; deployment uses the shared backup installer."""

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
    from tools.steam_paths import find_installation
else:
    from . import launcher_art as art, launcher_settings as settings, test_launcher_core as core
    from .test_launcher import game_is_running
    from .launcher_paths import application_root, prepare_frozen_process
    from .steam_paths import find_installation


REPO = application_root()
PREFERENCES_PATH = REPO / "tools/.user-launcher-settings.json"
INSTALL_STATE_PATH = REPO / "tools/.launcher-state.json"
BG, CARD, FIELD, LINE, EDGE = "#14181f", "#1b2029", "#10141a", "#2b323d", "#3a4250"
TEXT, MUTED, BLUE, ORANGE, TINT = "#f1f3f6", "#8d96a5", "#4aa8ee", "#ff9a3c", "#17324a"
# Layout in 96-DPI pixels; Launcher.px scales them to the monitor.
WINDOW, MIN_SIZE, MARGIN, PAGES_TOP, FOOTER = (700, 624), (640, 544), 24, 150, 92
PAGES = ("Setup", "Settings", "Advanced")
BUTTON_STYLES = {
    "outline": {"normal": (BG, EDGE, TEXT), "hover": ("#252c37", "#4b5565", "#ffffff"),
                "pressed": ("#1c222b", "#4b5565", TEXT)},
    "primary": {"normal": (BLUE, BLUE, "#08131d"), "hover": ("#6fbdf6", "#6fbdf6", "#08131d"),
                "pressed": ("#3a90d3", "#3a90d3", "#08131d")},
    "link": {"normal": (None, None, MUTED), "hover": (None, None, TEXT), "pressed": (None, None, TEXT)},
}


def borderless_window(window):
    """Frameless window that keeps its taskbar button, minimizing and rounded corners.

    A native caption always draws maximize, greyed out, next to minimize; the only way
    to show just minimize and close is to draw them ourselves.
    """
    window.overrideredirect(True)
    if sys.platform != "win32":
        return
    import ctypes
    window.update_idletasks()
    hwnd, user32 = int(window.wm_frame(), 16), ctypes.windll.user32
    user32.SetWindowLongW(hwnd, -20, user32.GetWindowLongW(hwnd, -20) & ~0x80 | 0x40000)  # App window, not tool window.
    user32.SetWindowLongW(hwnd, -16, user32.GetWindowLongW(hwnd, -16) | 0x80000 | 0x20000)  # System menu, minimize box.
    try:
        for attribute, value in ((33, 2), (34, 0x0050423A)):  # Round corners, EDGE border.
            data = ctypes.c_int(value)
            ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, attribute, ctypes.byref(data), ctypes.sizeof(data))
    except (AttributeError, OSError):
        pass


def dark_title_bar(window):
    """Windows 11 caption in the window colour; older systems keep the default."""
    if sys.platform != "win32":
        return
    try:
        import ctypes
        window.update_idletasks()  # The frame handle changes once Tk creates the wrapper.
        hwnd = int(window.wm_frame(), 16)
        for attribute, value in ((20, 1), (35, 0x001F1814), (36, 0x00F6F3F1)):
            data = ctypes.c_int(value)
            ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, attribute, ctypes.byref(data), ctypes.sizeof(data))
    except (AttributeError, OSError, ValueError, tk.TclError):
        pass


class CanvasButton:
    """Button drawn on a canvas; its rounded face blends into the surface behind it."""

    def __init__(self, app, canvas, text, command, style="outline", surface=BG, group=None):
        self.app, self.canvas, self.text, self.command = app, canvas, text, command
        self.style, self.surface, self.group = style, surface, group
        self.tag, self.box, self.state, self.face = f"button{id(self)}", None, "normal", None
        app.buttons.append(self)
        canvas.tag_bind(self.tag, "<Enter>", lambda _: self._set("hover"))
        canvas.tag_bind(self.tag, "<Leave>", lambda _: self._set("normal"))
        canvas.tag_bind(self.tag, "<ButtonPress-1>", lambda _: self._set("pressed"))
        canvas.tag_bind(self.tag, "<ButtonRelease-1>", self._release)

    @property
    def height(self):
        return self.app.px(34)

    def width(self):
        text = self.app.fonts["link" if self.style == "link" else "button"].measure(self.text)
        return text + (0 if self.style == "link" else self.app.px(28))

    def draw(self, x, y, width=None):
        c = self.canvas
        c.delete(self.tag)
        w, h = width or self.width(), self.height
        self.box = (x, y, x + w, y + h)
        tags = (self.tag, "layout", self.group) if self.group else (self.tag, "layout")
        if self.style == "link":
            # Canvas text only reacts on its glyphs, so links get an invisible hit area.
            self.face = None
            c.create_rectangle(*self.box, fill=self.surface, outline="", tags=tags)
        else:
            self.face = c.create_image(x, y, anchor="nw", tags=tags)
        self.label = c.create_text(x + w // 2, y + h // 2, text=self.text, tags=tags)
        self.paint()

    def paint(self):
        if not self.box:
            return
        c, app = self.canvas, self.app
        fill, outline, text = BUTTON_STYLES[self.style][self.state]
        if self.face:
            size = (self.box[2] - self.box[0], self.box[3] - self.box[1])
            c.itemconfigure(self.face, image=app.box(size, app.px(8), fill, outline, self.surface))
        font = "link_hover" if self.style == "link" and self.state != "normal" else \
            "link" if self.style == "link" else "button"
        c.itemconfigure(self.label, fill=text, font=app.fonts[font])

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


class CaptionButton(CanvasButton):
    """Minimize or close glyph in the corner of the borderless window."""

    def __init__(self, app, glyph, command, hover):
        super().__init__(app, app.canvas, glyph, command)
        self.hover = hover

    @property
    def height(self):
        return self.app.px(30)

    def width(self):
        return self.app.px(46)

    def draw(self, x, y, width=None):
        c, tags = self.canvas, (self.tag, "layout")
        c.delete(self.tag)
        self.box = (x, y, x + self.width(), y + self.height)
        self.face = c.create_rectangle(*self.box, outline="", tags=tags)
        self.label = c.create_text(x + self.width() // 2, y + self.height // 2, text=self.text,
                                   font=self.app.fonts["caption"], tags=tags)
        self.paint()

    def paint(self):
        if self.box:
            active = self.state != "normal"
            self.canvas.itemconfigure(self.face, fill=self.hover if active else BG)
            self.canvas.itemconfigure(self.label, fill="#ffffff" if active else MUTED)


class Toggle:
    """Pill switch for a two-value setting such as "false"/"true"."""

    def __init__(self, app, canvas, var, values):
        self.app, self.canvas, self.var, (self.off, self.on) = app, canvas, var, values
        self.tag, self.items, self.hover = f"toggle{id(self)}", None, False
        self.width, self.height = app.px(40), app.px(22)
        canvas.tag_bind(self.tag, "<ButtonRelease-1>", lambda _: var.set(self.off if var.get() == self.on else self.on))
        canvas.tag_bind(self.tag, "<Enter>", lambda _: self._hover(True))
        canvas.tag_bind(self.tag, "<Leave>", lambda _: self._hover(False))
        var.trace_add("write", lambda *_: self.paint())

    def draw(self, x, y):
        c, tags = self.canvas, (self.tag, "layout")
        c.delete(self.tag)
        self.origin = (x, y)
        self.items = (c.create_image(x, y, anchor="nw", tags=tags), c.create_image(x, y, anchor="nw", tags=tags))
        self.paint()

    def paint(self):
        if not self.items or not self.canvas.winfo_exists():
            return
        c, app, on = self.canvas, self.app, self.var.get() == self.on
        track, knob = self.items
        x, y = self.origin
        inset, size = app.px(3), self.height - 2 * app.px(3)
        fill = BLUE if on else "#39414e" if self.hover else "#2c333e"
        c.itemconfigure(track, image=app.box((self.width, self.height), self.height // 2, fill, fill, CARD))
        c.itemconfigure(knob, image=app.box((size, size), size // 2, "#ffffff", "#ffffff", fill))
        c.coords(knob, x + (self.width - inset - size if on else inset), y + inset)

    def _hover(self, hover):
        self.hover = hover
        self.canvas.configure(cursor="hand2" if hover else "")
        self.paint()


class Field:
    """Borderless entry or combobox sitting inside a rounded box that shows its focus."""

    def __init__(self, app, canvas, widget):
        self.app, self.canvas, self.widget, self.size = app, canvas, widget, None
        self.face = canvas.create_image(0, 0, anchor="nw", tags="field")
        self.window = canvas.create_window(0, 0, anchor="nw", window=widget)
        widget.bind("<FocusIn>", lambda _: self.paint(True), add="+")
        widget.bind("<FocusOut>", lambda _: self.paint(False), add="+")

    def draw(self, x, y, width, height):
        c, inset = self.canvas, self.app.px(6)
        self.size = (width, height)
        c.coords(self.face, x, y)
        c.coords(self.window, x + inset, y + self.app.px(3))
        c.itemconfigure(self.window, width=width - 2 * inset, height=height - 2 * self.app.px(3))
        self.paint(False)

    def paint(self, focused):
        if self.size:
            self.canvas.itemconfigure(self.face, image=self.app.box(
                self.size, self.app.px(7), FIELD, BLUE if focused else EDGE, CARD))


class Page:
    """Scrolling page body below the tabs."""

    def __init__(self, app):
        self.app, self.layout, self.box, self.visible = app, None, None, False
        self.width = self.height = self.content = 0
        self.canvas = tk.Canvas(app.canvas, bg=BG, bd=0, highlightthickness=0, yscrollincrement=app.px(24))
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

    def place(self, box, visible):
        self.box = box
        width = box[2] - box[0]
        self.height = box[3] - box[1]
        if width != self.width:
            self.width = width
            self.content = self.layout(self, width)
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
            self.scrollbar.place(x=x1 + px(8), y=y0, width=px(7), height=self.height)
            self.scrollbar.lift()
        else:
            self.scrollbar.place_forget()
        self.canvas.yview_moveto(self.canvas.yview()[0])


class Launcher:
    def __init__(self, root: tk.Tk, repo: Path = REPO,
                 preferences_path: Path = PREFERENCES_PATH,
                 install_state_path: Path = INSTALL_STATE_PATH, find_paths=None):
        self.root, self.repo = root, repo
        self.preferences_path, self.install_state_path = preferences_path, install_state_path
        legacy = core.load_state(install_state_path)
        prefs = settings.load_preferences(preferences_path, legacy)
        # Missing or stale paths are filled in from Steam's own records. They count
        # as the saved paths, so a first start shows no unsaved changes; they are
        # written on Save, Apply or Launch, and installing still asks first.
        found = ""
        if find_paths:
            try:
                game_dir, steam_exe = find_paths(prefs["game_dir"], prefs["steam_exe"])
            except (OSError, ValueError):
                game_dir, steam_exe = prefs["game_dir"], prefs["steam_exe"]
            names = [name for name, old, new in (("Portal 2", prefs["game_dir"], game_dir),
                                                  ("Steam", prefs["steam_exe"], steam_exe)) if old != new]
            if names:
                prefs = {**prefs, "game_dir": game_dir, "steam_exe": steam_exe}
                found = f"Found {' and '.join(names)} automatically."
        self.saved = prefs
        self.installed = bool(legacy["managed_files"])
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
            "Save, Apply or Launch to keep them here.") if changed else found)
        self.status_error = self._next_error = False
        self.dirty, self.invalid = False, False
        self.root.title("Portal 2 VR Enriched Launcher")
        self.scale = max(1.0, root.winfo_fpixels("1i") / 96)
        # Fixed size: nothing here needs to grow, and small screens get a smaller
        # window whose pages scroll.
        screen_width, screen_height = root.winfo_screenwidth(), root.winfo_screenheight()
        width = self.px(max(MIN_SIZE[0], min(WINDOW[0], round(screen_width / self.scale) - 80)))
        height = self.px(max(MIN_SIZE[1], min(WINDOW[1], round(screen_height / self.scale) - 120)))
        self.root.geometry(f"{width}x{height}+{max(0, (screen_width - width) // 2)}"
                           f"+{max(0, (screen_height - height) // 2 - self.px(20))}")
        self.root.resizable(False, False)
        self.root.configure(bg=BG)
        if art.ICON.is_file():
            try:
                self.root.iconbitmap(default=str(art.ICON))
            except tk.TclError:
                pass
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

    def box(self, size, radius, fill, outline=None, background=BG):
        key = (size, radius, fill, outline, background)
        if key not in self.boxes:
            self.boxes[key] = art.rounded_box(self.root, size, radius, fill, outline or fill, background, self.px(1))
        return self.boxes[key]

    def _fonts(self):
        body, strong = "Segoe UI", "Segoe UI Semibold"
        spec = {"title": (strong, 20), "subtitle": (body, 13), "tab": (strong, 14), "card": (strong, 15),
                "detail": (body, 13), "button": (strong, 13), "link": (body, 13), "group": (strong, 13),
                "row": (body, 14), "help": (body, 12), "entry": (body, 13), "status": (body, 12),
                "mono": ("Consolas", 11), "check": ("Segoe UI Symbol", 15), "caption": ("Segoe MDL2 Assets", 10)}
        self.fonts = {name: tkfont.Font(self.root, family=family, size=-self.px(size))
                      for name, (family, size) in spec.items()}
        self.fonts["link_hover"] = tkfont.Font(self.root, family=body, size=-self.px(13), underline=True)

    def _theme(self):
        px = self.px
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure(".", background=BG, foreground=TEXT, font=("Segoe UI", 10))
        field = dict(fieldbackground=FIELD, background=FIELD, foreground=TEXT, insertcolor=TEXT, bordercolor=FIELD,
                     lightcolor=FIELD, darkcolor=FIELD, padding=(px(2), px(1)))
        style.configure("Launcher.TEntry", **field)
        style.configure("Launcher.TCombobox", **field, arrowcolor=MUTED, arrowsize=px(11),
                        selectbackground=FIELD, selectforeground=TEXT)
        style.map("Launcher.TCombobox", fieldbackground=[("readonly", FIELD)], foreground=[("readonly", TEXT)],
                  background=[("readonly", FIELD)], arrowcolor=[("active", TEXT)],
                  bordercolor=[("focus", FIELD)], lightcolor=[("focus", FIELD)], darkcolor=[("focus", FIELD)],
                  selectbackground=[("readonly", FIELD)], selectforeground=[("readonly", TEXT)])
        style.map("Launcher.TEntry", bordercolor=[("focus", FIELD)], lightcolor=[("focus", FIELD)],
                  darkcolor=[("focus", FIELD)])
        style.layout("Launcher.Vertical.TScrollbar", [("Vertical.Scrollbar.trough", {"sticky": "ns", "children": [
            ("Vertical.Scrollbar.thumb", {"expand": "1", "sticky": "nswe"})]})])
        style.configure("Launcher.Vertical.TScrollbar", troughcolor=BG, background=EDGE, bordercolor=BG,
                        lightcolor=EDGE, darkcolor=EDGE, gripcount=0)
        style.map("Launcher.Vertical.TScrollbar", background=[("active", "#4b5565")])
        style.configure("ComboboxPopdownFrame", background=FIELD, bordercolor=EDGE, lightcolor=FIELD, darkcolor=FIELD,
                        relief="solid", borderwidth=1)
        for option, value in (("background", FIELD), ("foreground", TEXT), ("selectBackground", "#24557d"),
                              ("selectForeground", "#ffffff"), ("font", self.fonts["entry"]), ("borderWidth", 0)):
            self.root.option_add(f"*TCombobox*Listbox.{option}", value)

    def _build(self, width, height):
        self.boxes, self.buttons, self.size, self.current, self.tab_hover = {}, [], None, PAGES[0], None
        logo = min(art.LOGO_SIZES, key=lambda size: abs(size - self.px(44)))
        self.logo = tk.PhotoImage(master=self.root, file=str(art.ASSETS / f"launcher_logo_{logo}.png"))
        self.canvas = tk.Canvas(self.root, bg=BG, bd=0, highlightthickness=0, width=width, height=height)
        self.canvas.pack(fill="both", expand=True)
        for name in PAGES:
            tag = f"tab:{name}"
            self.canvas.tag_bind(tag, "<Enter>", lambda _, name=name: self._hover_tab(name))
            self.canvas.tag_bind(tag, "<Leave>", lambda _: self._hover_tab(None))
            self.canvas.tag_bind(tag, "<ButtonRelease-1>", lambda _, name=name: self.show_page(name))
        self.pages = {name: Page(self) for name in PAGES}
        self._build_setup(self.pages["Setup"])
        self._build_settings(self.pages["Settings"], advanced=False)
        self._build_settings(self.pages["Advanced"], advanced=True)
        self.canvas.tag_bind("drag", "<ButtonPress-1>", self._drag_start)
        self.canvas.tag_bind("drag", "<B1-Motion>", self._drag)
        self.minimize_button = CaptionButton(self, "\ue921", self.minimize, "#252c37")
        self.close_button = CaptionButton(self, "\ue8bb", self.close, "#c42b1c")
        self.launch_button = CanvasButton(self, self.canvas, "Launch in VR", self.launch, "primary")
        self.apply_button = CanvasButton(self, self.canvas, "Apply", self.apply)
        self.links = tuple(CanvasButton(self, self.canvas, text, command, "link", group="footer:Setup")
                           for text, command in (("Open mod log", self.open_log), ("Open backups", self.open_backups),
                                                 ("About", self.about)))
        self.save_button = CanvasButton(self, self.canvas, "Save settings", self.save, group="footer:Settings")
        # Preview config is a troubleshooting aid, so only Advanced offers it.
        self.form_actions = (CanvasButton(self, self.canvas, "Restore recommended", self.reset_recommended,
                                          group="footer:Settings"),
                             CanvasButton(self, self.canvas, "Preview config", self.show_preview,
                                          group="footer:Advanced"))
        self.canvas.bind("<Configure>", lambda event: self._relayout(event.width, event.height))
        self._relayout(width, height)

    def _build_setup(self, page):
        c = page.canvas
        self.game_actions = (CanvasButton(self, c, "Open", self.open_game, surface=CARD),
                             CanvasButton(self, c, "Change", self.browse_game, surface=CARD))
        self.steam_actions = (CanvasButton(self, c, "Change", self.browse_steam, surface=CARD),)
        self.restore_button = CanvasButton(self, c, "Restore original game files", self.restore, "link", CARD)
        self.customize_button = CanvasButton(self, c, "Customize", lambda: self.show_page("Settings"), surface=CARD)
        page.layout = self._layout_setup

    def _build_settings(self, page, advanced):
        c, group, page.rows = page.canvas, None, []
        for item in settings.SETTINGS:
            if (item.key in settings.BASIC_KEYS) == advanced:
                continue
            if group != item.group:
                group = item.group
                page.rows.append((group, None, None))
            var = self.vars[item.key]
            if item.choices in (("false", "true"), ("0", "1")):
                control = Toggle(self, c, var, item.choices)
            else:
                if item.choices:
                    widget = self._choice(c, var, dict(zip(item.choices, item.labels or item.choices)))
                else:
                    widget = ttk.Entry(c, textvariable=var, style="Launcher.TEntry", font=self.fonts["entry"],
                                       justify="right")
                page.bind_wheel(widget)
                control = Field(self, c, widget)
            page.rows.append((None, item, control))
        page.layout = lambda page, width: self._layout_settings(page, width, advanced)

    def _choice(self, canvas, var, names):
        """Read-only combobox showing names for the stored values."""
        values = {name: value for value, name in names.items()}
        shown = tk.StringVar(canvas, names.get(var.get(), var.get()))
        widget = ttk.Combobox(canvas, textvariable=shown, values=list(names.values()), state="readonly",
                              style="Launcher.TCombobox", font=self.fonts["entry"])
        widget.shown = shown  # Tk forgets a variable once Python drops it.

        def select(_):
            var.set(values[shown.get()])
            widget.selection_clear()
        widget.bind("<<ComboboxSelected>>", select)
        var.trace_add("write", lambda *_: shown.set(names.get(var.get(), var.get())))
        return widget

    def _relayout(self, width, height):
        px = self.px
        if (width, height) == self.size or width < px(200) or height < px(200):
            return
        self.size = (width, height)
        c, margin, drag = self.canvas, px(MARGIN), ("layout", "drag")
        c.delete("layout")
        c.create_rectangle(0, 0, width, px(96), fill=BG, outline="", tags=drag)
        self.strip = art.accent_strip(self.root, width, px(2), BG)
        c.create_image(0, 0, anchor="nw", image=self.strip, tags=drag)
        self.close_button.draw(width - self.close_button.width(), px(2))
        self.minimize_button.draw(width - 2 * self.close_button.width(), px(2))
        c.create_image(margin + px(22), px(64), image=self.logo, tags=drag)
        c.create_text(margin + px(56), px(41), text="Portal 2 VR Enriched", anchor="nw", font=self.fonts["title"], fill=TEXT,
                      tags=drag)
        self.subtitle = c.create_text(margin + px(56), px(68), anchor="nw", font=self.fonts["subtitle"], tags=drag)
        x = width - margin - self.launch_button.width() - px(8)
        self.launch_button.draw(x, px(47), self.launch_button.width() + px(8))
        x -= self.apply_button.width() + px(10)
        self.apply_button.draw(x, px(47))
        self._draw_tabs(margin, px(100))
        self._draw_footer()
        for name, page in self.pages.items():
            page.place((margin, px(PAGES_TOP), width - margin, height - px(FOOTER)), name == self.current)
        self._paint_header()

    def _draw_tabs(self, x, y):
        c, px, height, pad = self.canvas, self.px, self.px(38), self.px(4)
        widths = [self.fonts["tab"].measure(name) + px(36) for name in PAGES]
        c.create_image(x, y, anchor="nw", image=self.box((sum(widths) + 2 * pad, height), px(10), BG, LINE), tags="layout")
        boxes, cursor = {}, x + pad
        for name, width in zip(PAGES, widths):
            boxes[name] = (cursor, y + pad, cursor + width, y + height - pad)
            c.create_rectangle(*boxes[name], fill=BG, outline="", tags=("layout", f"tab:{name}"))
            cursor += width
        self.tab_pill = c.create_image(0, 0, anchor="nw", tags="layout")
        self.tabs = {name: (box, c.create_text((box[0] + box[2]) // 2, (box[1] + box[3]) // 2, text=name,
                                               font=self.fonts["tab"], tags=("layout", f"tab:{name}")))
                     for name, box in boxes.items()}
        self._paint_tabs()

    def _paint_tabs(self):
        c = self.canvas
        for name, (box, label) in self.tabs.items():
            c.itemconfigure(label, fill=TEXT if name in (self.current, self.tab_hover) else MUTED)
            if name == self.current:
                size = (box[2] - box[0], box[3] - box[1])
                c.coords(self.tab_pill, box[0], box[1])
                c.itemconfigure(self.tab_pill, image=self.box(size, self.px(7), "#262c36"))

    def _hover_tab(self, name):
        self.tab_hover = name
        self.canvas.configure(cursor="hand2" if name and name != self.current else "")
        self._paint_tabs()

    def _drag_start(self, event):
        self.drag_offset = (event.x_root - self.root.winfo_x(), event.y_root - self.root.winfo_y())

    def _drag(self, event):
        self.root.geometry(f"+{event.x_root - self.drag_offset[0]}+{event.y_root - self.drag_offset[1]}")

    def minimize(self):
        # Tk refuses to iconify a window without a native frame.
        import ctypes
        ctypes.windll.user32.ShowWindow(int(self.root.wm_frame(), 16), 6)

    def show_page(self, name):
        self.current = name
        for page_name, page in self.pages.items():
            page.show(page_name == name)
        # A page hidden under the pointer never receives its <Leave>.
        for button in self.buttons:
            if button.state != "normal":
                button._set("normal")
        self.canvas.configure(cursor="")
        self._paint_tabs()
        self._show_footer()

    def _draw_footer(self):
        c, px, (width, height) = self.canvas, self.px, self.size
        y = height - px(FOOTER) + px(12)
        gap, widths = px(28), [link.width() for link in self.links]
        x = (width - sum(widths) - gap * (len(widths) - 1)) // 2
        for index, link in enumerate(self.links):
            link.draw(x, y)
            x += widths[index] + gap
            if index < len(widths) - 1:
                c.create_text(x - gap // 2, y + link.height // 2, text="•", font=self.fonts["link"], fill=EDGE,
                              tags=("layout", "footer:Setup"))
        x = px(MARGIN)
        for button in self.form_actions:
            button.draw(x, y)
            x += button.width() + px(10)
        self.save_button.draw(width - px(MARGIN) - self.save_button.width(), y)
        self._show_footer()
        self._draw_status()

    def _show_footer(self):
        setup = self.current == "Setup"
        self.canvas.itemconfigure("footer:Setup", state="normal" if setup else "hidden")
        self.canvas.itemconfigure("footer:Settings", state="hidden" if setup else "normal")
        self.canvas.itemconfigure("footer:Advanced", state="normal" if self.current == "Advanced" else "hidden")

    def _status_changed(self, *_):
        self.status_error, self._next_error = self._next_error, False
        self._draw_status()

    def _draw_status(self):
        c, px = self.canvas, self.px
        c.delete("status")
        if not self.size:
            return
        width, height = self.size
        full = self.status_var.get()
        item = c.create_text(width // 2, height - px(40), text=full, anchor="n", justify="center",
                             width=width - 2 * px(MARGIN), font=self.fonts["status"],
                             fill=ORANGE if self.status_error else MUTED, tags="status")
        # Long messages are cut to fit; errors are also shown in a dialog.
        length = len(full)
        while length > 24 and c.bbox(item)[3] > height - px(4):
            length = int(length * .85)
            c.itemconfigure(item, text=full[:length].rstrip() + "…")

    def _paths_found(self):
        game, steam = Path(self.game_var.get()), Path(self.steam_var.get())
        return (game / "portal2.exe").is_file(), steam.is_file() and steam.name.lower() == "steam.exe"

    def _paint_header(self):
        text, color = ("Needs attention", ORANGE) if self.invalid or not all(self._paths_found()) else \
            ("Unsaved changes", ORANGE) if self.dirty else \
            ("Ready to play", MUTED) if self.installed else ("Not installed yet", MUTED)
        self.canvas.itemconfigure(self.subtitle, text=text, fill=color)

    def _elide(self, text, font, width):
        """Cut the middle of a long path so both of its ends stay readable."""
        font = self.fonts[font]
        if font.measure(text) <= width:
            return text
        keep = len(text) // 2
        while keep > 4 and font.measure(text[:keep] + "…" + text[-keep:]) > width:
            keep -= 1
        return text[:keep] + "…" + text[-keep:]

    def _text(self, canvas, x, y, right, text, font, fill):
        item = canvas.create_text(x, y, text=text, anchor="nw", width=max(1, right - x), font=self.fonts[font],
                                  fill=fill, tags=("layout", "row"))
        return canvas.bbox(item)[3]

    def _card(self, canvas, top, bottom, width):
        canvas.tag_lower(canvas.create_image(0, top, anchor="nw", tags="layout",
                                             image=self.box((width, bottom - top), self.px(10), CARD, LINE)))

    def _mark(self, canvas, x, y, state):
        """Round status mark: blue check when ready, orange when it needs the user, empty otherwise."""
        size = self.px(28)
        fill, outline, glyph, color = {"ok": (TINT, "#27557c", "✓", BLUE), "warn": ("#3a2a18", "#6b4a23", "!", ORANGE),
                                       "idle": (CARD, EDGE, "", MUTED)}[state]
        canvas.create_image(x, y, anchor="nw", image=self.box((size, size), size // 2, fill, outline, CARD), tags="layout")
        canvas.create_text(x + size // 2, y + size // 2, text=glyph, font=self.fonts["check"], fill=color, tags="layout")

    def _setup_card(self, canvas, y, width, state, title, detail, buttons=(), badge=None, title_note=None, path=False):
        px, pad = self.px, self.px(16)
        text_x, right = pad + px(42), width - pad
        for button in buttons:
            right -= button.width() + px(8)
        if badge:
            right -= self.fonts["button"].measure(badge[0]) + px(36)
        right -= px(4)
        if path:
            detail = self._elide(detail, "detail", right - text_x)
        title_item = canvas.create_text(text_x, y + px(15), text=title, anchor="nw", font=self.fonts["card"], fill=TEXT,
                                        tags="layout")
        if title_note:
            canvas.create_text(canvas.bbox(title_item)[2] + px(10), y + px(16), text=title_note, anchor="nw",
                               font=self.fonts["detail"], fill=ORANGE, tags="layout")
        bottom = self._text(canvas, text_x, canvas.bbox(title_item)[3] + px(2), right, detail, "detail", MUTED) + px(15)
        bottom = max(bottom, y + px(70))
        center = (y + bottom) // 2
        self._mark(canvas, pad, center - px(14), state)
        x = width - pad
        if badge:
            text, fill, outline, color = badge
            size = (self.fonts["button"].measure(text) + px(24), px(30))
            x -= size[0]
            canvas.create_image(x, center - size[1] // 2, anchor="nw", tags="layout",
                                image=self.box(size, px(8), fill, outline, CARD))
            canvas.create_text(x + size[0] // 2, center, text=text, font=self.fonts["button"], fill=color, tags="layout")
            x -= px(14)
        for button in reversed(buttons):
            x -= button.width()
            button.draw(x, center - button.height // 2)
            x -= px(8)
        self._card(canvas, y, bottom, width)
        return bottom + px(10)

    def _layout_setup(self, page, width):
        c = page.canvas
        c.delete("layout")
        game_found, steam_found = self._paths_found()
        y = self._setup_card(c, 0, width, "ok" if game_found else "warn", "Portal 2",
                             str(Path(self.game_var.get())) if game_found else
                             "portal2.exe is not in this folder. Choose your Portal 2 folder.",
                             self.game_actions, path=game_found)
        y = self._setup_card(c, y, width, "ok" if steam_found else "warn", "Steam",
                             str(Path(self.steam_var.get())) if steam_found else
                             "Choose steam.exe from your Steam folder.", self.steam_actions, path=steam_found)
        y = self._setup_card(c, y, width, "ok" if self.installed else "idle", "Portal 2 VR Enriched mod",
                             "Your original files are backed up." if self.installed else
                             "Launch or Apply installs it and backs up your original files first.",
                             (self.restore_button,) if self.installed else (),
                             ("Installed", TINT, "#27557c", BLUE) if self.installed else ("Not installed", CARD, EDGE, MUTED))
        profile, _, note = self.profile_var.get().partition(" • ")
        y = self._setup_card(c, y, width, "warn" if self.invalid else "ok", profile, self.summary_var.get(),
                             (self.customize_button,), title_note=note.capitalize() or None)
        return y

    def _layout_settings(self, page, width, advanced):
        c, px = page.canvas, self.px
        c.delete("layout")
        pad, y, top = px(16), 0, None
        right, text_right = width - pad, width - pad - min(px(190), width // 3) - px(20)
        for group, item, control in page.rows:
            if group:
                if top is not None:
                    self._card(c, top, y, width)
                    y += px(14)
                y = self._text(c, px(2), y, width, group, "group", BLUE) + px(6)
                top, y, first = y, y + px(14), True
                continue
            if not first:
                c.create_line(pad, y, right, y, fill=LINE, tags="layout")
                y += px(13)
            first, row = False, y
            c.dtag("row")
            bottom = self._text(c, pad, y, text_right, item.label, "row", TEXT)
            if advanced:
                bottom = self._text(c, pad, bottom + px(1), text_right, item.key, "mono", "#6d7786")
            help_text = item.help
            if item.bounds:
                low, high = item.bounds
                help_text = f"Range: {low:g} to {high:g}. " + help_text
            if help_text:
                bottom = self._text(c, pad, bottom + px(2), text_right, help_text.strip(), "help", MUTED)
            height = control.height if isinstance(control, Toggle) else px(32)
            if bottom - row < height:
                c.move("row", 0, (height - (bottom - row)) // 2)
                bottom = row + height
            if isinstance(control, Toggle):
                control.draw(right - control.width, (row + bottom - height) // 2)
            else:
                field_width = min(px(190), width // 3) if item.choices else px(110)
                control.draw(right - field_width, (row + bottom - height) // 2, field_width, height)
            y = bottom + px(13)
        self._card(c, top, y, width)
        return y + px(6)

    def preferences(self):
        return {"version": 1, "game_dir": self.game_var.get().strip(),
                "steam_exe": self.steam_var.get().strip(),
                "values": settings.validate_settings({key: var.get() for key, var in self.vars.items()})}

    def _changed(self, *_):
        invalid = False
        try:
            prefs = self.preferences()
            recommended = prefs["values"] == settings.recommended_settings()
            dirty = prefs != self.saved
            self.profile_var.set(("Recommended setup" if recommended else "Custom setup") +
                                 (" • unsaved changes" if dirty else ""))
            values = prefs["values"]
            # What the player will notice, in plain words; Advanced has the details.
            features = [values["TrackingMode"],
                        "snap turning" if values["SnapTurning"] == "true" else "smooth turning"]
            if values["RoomscaleMode"] == "ActiveExperimental":
                features.append("physical walking")
            if values["PhysicalCrouch"] == "true" and values["6DOF"] == "true":
                features.append("physical crouch")
            if values["AimMode"] == "2" and values["ExperimentalWorldAimMarker"] == "true":
                features.append("aim line")
            if values["ExperimentalViewmodelAlignment"] == "true":
                features.append("gun follows the controller")
            if values["ExperimentalHUDOverlay"] == "true":
                features.append("readable captions")
            if values["Haptics"] == "true" and values["HapticsIntensity"] != "0" or \
                    values["ExperimentalPortalShotHaptics"] == "true":
                features.append("vibration")
            if values["PortalOrientationMode"] != "LegacyYaw":
                features.append("experimental portal orientation")
            # The no-break space keeps each bullet off the start of a wrapped line.
            self.summary_var.set("  •  ".join(feature[:1].upper() + feature[1:] for feature in features))
        except ValueError as error:
            dirty = invalid = True
            self.profile_var.set("Custom setup • needs attention")
            self.summary_var.set(str(error))
        self.dirty, self.invalid = dirty, invalid
        self.save_button.style = "primary" if dirty else "outline"
        self.save_button.paint()
        self._paint_header()
        self.pages["Setup"].relayout()

    def _error(self, error):
        self._next_error = True
        self.status_var.set(str(error))
        messagebox.showerror("Portal 2 VR Enriched", str(error), parent=self.root)

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
        view = tk.Text(window, bg=FIELD, fg=TEXT, insertbackground=TEXT, selectbackground="#24557d",
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
                "Install Portal 2 VR Enriched", f"Install the mod in:\n{game}\n\n"
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
        self._refresh_install()
        self._changed()

    def _refresh_install(self):
        try:
            self.installed = bool(core.load_state(self.install_state_path)["managed_files"])
        except (OSError, ValueError, RuntimeError):
            return
        self.restore_button._set("normal")  # Restoring removes the link from under the pointer.
        self._paint_header()
        self.pages["Setup"].relayout()

    def apply(self):
        try:
            prefs = self._prepare()
            if prefs is None:
                return
            self._stage(prefs)
            self.status_var.set("Mod and settings installed. Your original files stay backed up.")
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
            self.status_var.set("Portal 2 is starting through Steam; SteamVR opens with it.")
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
        finally:
            self._refresh_install()

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

    def open_game(self):
        self._open(Path(self.game_var.get()))

    def open_log(self):
        self._open(Path(self.game_var.get()) / "bin/portal2vr.log")

    def open_backups(self):
        self._open(self.install_state_path.parent / ".launcher-backups")

    def about(self):
        messagebox.showinfo("About Portal 2 VR Enriched", "Unofficial fan project, not affiliated with Valve.\n"
                            "Incremental modernization of Gistix/portal2vr, "
                            "built on mmadej20/portal2vr-ultimate.\n\n"
                            "Needs Portal 2 for Windows, Steam and SteamVR.\n\n"
                            "Launch installs the mod, applies your settings and starts Portal 2 in VR. "
                            "Apply does the same without starting the game. "
                            "Restart the game after changing settings.\n\n"
                            "Your original game files are backed up before the first install. "
                            "Restore original game files puts them back and removes only what the launcher added. "
                            "Keep the launcher's tools folder: it holds those backups.\n\n"
                            "Experimental portal orientation modes and other headsets are not tested yet.",
                            parent=self.root)

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
            borderless_window(root)
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
            # Its own taskbar identity; under pythonw the button would show Python's icon.
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(art.APP_ID)
        except (AttributeError, OSError):
            pass
    root = tk.Tk()
    root.withdraw()
    try:
        Launcher(root, find_paths=find_installation)
        borderless_window(root)
    except (OSError, ValueError, RuntimeError) as error:
        messagebox.showerror("Portal 2 VR Enriched startup", f"{error}\n\nNo game files were changed. "
                             "Keep the state and backup files; do not delete them to repair an installation.", parent=root)
        root.destroy()
        return 1
    root.deiconify()
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
