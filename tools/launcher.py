"""User-facing Portal 2 VR Enriched launcher; deployment uses the shared backup installer."""

from pathlib import Path
import os
import subprocess
import sys
import tkinter as tk
import webbrowser
from tkinter import filedialog, font as tkfont, messagebox, ttk

if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from tools import games, launcher_art as art, launcher_settings as settings, test_launcher_core as core
    from tools.test_launcher import game_is_running
    from tools.launcher_paths import application_root, prepare_frozen_process
    from tools.steam_paths import find_game, find_installation
else:
    from . import games, launcher_art as art, launcher_settings as settings, test_launcher_core as core
    from .test_launcher import game_is_running
    from .launcher_paths import application_root, prepare_frozen_process
    from .steam_paths import find_game, find_installation


REPO = application_root()
PREFERENCES_PATH = REPO / "tools/.user-launcher-settings.json"
INSTALL_STATE_PATH = REPO / "tools/.launcher-state.json"
PROJECT_URL = "https://github.com/oxenar7/portal2-vr-enriched"
KOFI_URL = "https://ko-fi.com/oxenar7"
BG, CARD, FIELD, LINE, EDGE = "#14181f", "#1b2029", "#10141a", "#2b323d", "#3a4250"
TEXT, MUTED, BLUE, ORANGE, TINT = "#f1f3f6", "#8d96a5", "#4aa8ee", "#ff9a3c", "#17324a"
# Layout in 96-DPI pixels; Launcher.px scales them to the monitor.
WINDOW, MIN_SIZE, MARGIN, PAGES_TOP, FOOTER = (700, 624), (640, 544), 24, 150, 92
PAGES = ("Games", "Settings", "Help")
# Settings are saved this long after the last change, so typing a number writes once.
SAVE_DELAY_MS = 400
# Settings whose value shows or hides other rows.
LAYOUT_KEYS = frozenset(key for item in settings.SETTINGS for key, _ in item.when)
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


def steam_logo(master, steam_exe: Path, app_id: str, width: int, height: int):
    """The game's logo from the Steam client's library cache, shrunk to fit; None without one.

    Steam keeps logo.png per app, in the app's folder or one of its subfolders on newer clients.
    """
    cache = Path(steam_exe).parent / "appcache" / "librarycache"
    try:
        candidates = [cache / app_id / "logo.png", *sorted((cache / app_id).glob("*/logo.png")),
                      cache / f"{app_id}_logo.png"]
    except OSError:
        return None
    for path in candidates:
        try:
            if not path.is_file():
                continue
            image = tk.PhotoImage(master=master, file=str(path))
        except (OSError, tk.TclError):
            continue
        # Some logos sit in a large transparent canvas; keep only the drawn part.
        step, xs, ys = 4, [], []
        for y in range(0, image.height(), step):
            for x in range(0, image.width(), step):
                if not image.transparency_get(x, y):
                    xs.append(x)
                    ys.append(y)
        if not xs:
            continue
        x0, y0 = max(0, min(xs) - step), max(0, min(ys) - step)
        x1, y1 = min(image.width(), max(xs) + step), min(image.height(), max(ys) + step)
        factor = max(1, -(-(x1 - x0) // width), -(-(y1 - y0) // height))
        small = tk.PhotoImage(master=master)
        small.tk.call(small, "copy", image, "-from", x0, y0, x1, y1, "-subsample", factor, factor)
        return small
    return None


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

    def __init__(self, app, canvas, var, values, surface=CARD, group=None):
        self.app, self.canvas, self.var, (self.off, self.on) = app, canvas, var, values
        self.surface, self.group = surface, group
        self.tag, self.items, self.hover = f"toggle{id(self)}", None, False
        self.width, self.height = app.px(40), app.px(22)
        canvas.tag_bind(self.tag, "<ButtonRelease-1>", lambda _: self.flip())
        canvas.tag_bind(self.tag, "<Enter>", lambda _: self._hover(True))
        canvas.tag_bind(self.tag, "<Leave>", lambda _: self._hover(False))
        var.trace_add("write", lambda *_: self.paint())

    def flip(self):
        self.var.set(self.off if self.var.get() == self.on else self.on)

    def draw(self, x, y):
        c = self.canvas
        tags = (self.tag, "layout", self.group) if self.group else (self.tag, "layout")
        c.delete(self.tag)
        self.origin = (x, y)
        self.items = (c.create_image(x, y, anchor="nw", tags=tags), c.create_image(x, y, anchor="nw", tags=tags))
        self.paint()

    @property
    def shown(self):
        return self.items is not None

    def hide(self):
        self.canvas.delete(self.tag)
        self.items = None

    def paint(self):
        if not self.items or not self.canvas.winfo_exists():
            return
        c, app, on = self.canvas, self.app, self.var.get() == self.on
        track, knob = self.items
        x, y = self.origin
        inset, size = app.px(3), self.height - 2 * app.px(3)
        fill = BLUE if on else "#39414e" if self.hover else "#2c333e"
        c.itemconfigure(track, image=app.box((self.width, self.height), self.height // 2, fill, fill, self.surface))
        c.itemconfigure(knob, image=app.box((size, size), size // 2, "#ffffff", "#ffffff", fill))
        c.coords(knob, x + (self.width - inset - size if on else inset), y + inset)

    def _hover(self, hover):
        self.hover = hover
        self.canvas.configure(cursor="hand2" if hover else "")
        self.paint()


class Field:
    """Borderless entry or combobox sitting inside a rounded box that shows its focus."""

    def __init__(self, app, canvas, widget):
        self.app, self.canvas, self.widget, self.size, self.shown = app, canvas, widget, None, False
        self.face = canvas.create_image(0, 0, anchor="nw", tags="field")
        self.window = canvas.create_window(0, 0, anchor="nw", window=widget)
        widget.bind("<FocusIn>", lambda _: self.paint(True), add="+")
        widget.bind("<FocusOut>", lambda _: self.paint(False), add="+")

    def draw(self, x, y, width, height):
        c, inset = self.canvas, self.app.px(6)
        self.size, self.shown = (width, height), True
        c.coords(self.face, x, y)
        c.coords(self.window, x + inset, y + self.app.px(3))
        c.itemconfigure(self.window, width=width - 2 * inset, height=height - 2 * self.app.px(3), state="normal")
        c.itemconfigure(self.face, state="normal")
        self.paint(False)

    def hide(self):
        self.shown = False
        self.canvas.itemconfigure(self.window, state="hidden")
        self.canvas.itemconfigure(self.face, state="hidden")

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
        # Missing or stale paths are filled in from Steam's own records. They count as the
        # saved paths, so a first start writes nothing; the next save stores them.
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
        self.game_var = tk.StringVar(root, prefs["game_dir"])
        self.steam_var = tk.StringVar(root, prefs["steam_exe"])
        self.folders = self._find_folders(prefs)
        self.taken = {key: dict(values) for key, values in prefs.get("ingame_taken", {}).items()}
        self.import_note = ""
        self.installed = self._read_installed()
        self.vars = {key: tk.StringVar(root, value) for key, value in prefs["values"].items()}
        self.advanced_var = tk.StringVar(root, "0")
        self.status_var = tk.StringVar(root, found)
        self.status_error = self._next_error = False
        self.invalid, self.ingame, self.logos, self._save_job, self._announce = False, {}, {}, None, False
        self._quiet = True  # No autosave or linked changes while the window is built.
        self.saved = self.preferences()
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
        for key, var in self.vars.items():
            var.trace_add("write", lambda *_, key=key: self._setting_changed(key))
        self.game_var.trace_add("write", self._game_changed)
        self.steam_var.trace_add("write", self._steam_changed)
        self.advanced_var.trace_add("write", lambda *_: self.pages["Settings"].relayout())
        self.status_var.trace_add("write", self._status_changed)
        self._quiet = False
        # Options > VR changes made in the game become the launcher's settings.
        if self._import_ingame():
            self.save(announce=False)
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
                "check": ("Segoe UI Symbol", 15), "caption": ("Segoe MDL2 Assets", 10)}
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

    # ----- Window ---------------------------------------------------------------------------

    def _build(self, width, height):
        self.boxes, self.buttons, self.size, self.current, self.tab_hover = {}, [], None, PAGES[0], None
        self.card_items = {}
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
        self._build_games(self.pages["Games"])
        self._build_settings(self.pages["Settings"])
        self._build_help(self.pages["Help"])
        self.canvas.tag_bind("drag", "<ButtonPress-1>", self._drag_start)
        self.canvas.tag_bind("drag", "<B1-Motion>", self._drag)
        self.minimize_button = CaptionButton(self, "", self.minimize, "#252c37")
        self.close_button = CaptionButton(self, "", self.close, "#c42b1c")
        self.launch_button = CanvasButton(self, self.canvas, "Play in VR", self.launch, "primary")
        self.reset_button = CanvasButton(self, self.canvas, "Restore recommended", self.reset_recommended,
                                         group="footer:Settings")
        self.advanced_toggle = Toggle(self, self.canvas, self.advanced_var, ("0", "1"), BG, "footer:Settings")
        self.canvas.tag_bind("advanced_label", "<ButtonRelease-1>", lambda _: self.advanced_toggle.flip())
        self.canvas.bind("<Configure>", lambda event: self._relayout(event.width, event.height))
        # A save still waiting when the window goes would run against a destroyed window.
        self.canvas.bind("<Destroy>", lambda _: self._cancel_save(), add="+")
        self._relayout(width, height)

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
        c.create_text(margin + px(56), px(41), text="Portal 2 VR Enriched", anchor="nw", font=self.fonts["title"],
                      fill=TEXT, tags=drag)
        self.subtitle = c.create_text(margin + px(56), px(68), anchor="nw", font=self.fonts["subtitle"], tags=drag)
        launch_width = self.launch_button.width() + px(16)
        self.launch_button.draw(width - margin - launch_width, px(47), launch_width)
        self._draw_tabs(margin, px(100))
        self._draw_footer()
        for name, page in self.pages.items():
            # Only Settings has footer controls; the other pages reach down to the status line.
            bottom = height - px(FOOTER if name == "Settings" else FOOTER - 36)
            page.place((margin, px(PAGES_TOP), width - margin, bottom), name == self.current)
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
        self.reset_button.draw(px(MARGIN), y)
        toggle = self.advanced_toggle
        x = width - px(MARGIN) - toggle.width
        toggle.draw(x, y + (px(34) - toggle.height) // 2)
        label = c.create_text(x - px(12), y + px(17), text="Advanced settings", anchor="e", font=self.fonts["row"],
                              fill=TEXT, tags=("layout", "footer:Settings", "advanced_label"))
        # The label's whole box flips the switch, not only its glyphs.
        c.tag_lower(c.create_rectangle(*c.bbox(label), fill=BG, outline="",
                                       tags=("layout", "footer:Settings", "advanced_label")))
        self._show_footer()
        self._draw_status()

    def _show_footer(self):
        for name in PAGES:
            self.canvas.itemconfigure(f"footer:{name}", state="normal" if name == self.current else "hidden")

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

    def _paint_header(self):
        game = self._game()
        game_found, steam_found = self._paths_found()
        text, color = ("A setting needs attention", ORANGE) if self.invalid else \
            (f"{game.name} not found: choose its folder", ORANGE) if not game_found else \
            ("Steam not found: choose steam.exe", ORANGE) if not steam_found else \
            (f"{game.name}  •  Ready to play", MUTED) if self.installed else \
            (f"{game.name}  •  The mod installs when you play", MUTED)
        self.canvas.itemconfigure(self.subtitle, text=text, fill=color)

    # ----- Drawing helpers ------------------------------------------------------------------

    def _elide(self, text, font, width):
        """Cut the middle of a long path so both of its ends stay readable."""
        font = self.fonts[font]
        if font.measure(text) <= width:
            return text
        keep = len(text) // 2
        while keep > 4 and font.measure(text[:keep] + "…" + text[-keep:]) > width:
            keep -= 1
        return text[:keep] + "…" + text[-keep:]

    def _text(self, canvas, x, y, right, text, font, fill, tags=()):
        item = canvas.create_text(x, y, text=text, anchor="nw", width=max(1, right - x), font=self.fonts[font],
                                  fill=fill, tags=("layout", "row", *tags))
        return canvas.bbox(item)[3]

    def _card(self, canvas, top, bottom, width, outline=LINE, tags=()):
        item = canvas.create_image(0, top, anchor="nw", tags=("layout", *tags),
                                   image=self.box((width, bottom - top), self.px(10), CARD, outline))
        canvas.tag_lower(item)
        return item

    def _heading(self, canvas, y, text):
        return self._text(canvas, self.px(2), y, self.px(400), text, "group", BLUE) + self.px(8)

    def _mark(self, canvas, x, y, state, tags=()):
        """Round status mark: blue check when ready, orange when it needs the user, empty otherwise."""
        size = self.px(28)
        fill, outline, glyph, color = {"ok": (TINT, "#27557c", "✓", BLUE), "warn": ("#3a2a18", "#6b4a23", "!", ORANGE),
                                       "idle": (CARD, EDGE, "", MUTED)}[state]
        canvas.create_image(x, y, anchor="nw", image=self.box((size, size), size // 2, fill, outline, CARD),
                            tags=("layout", *tags))
        canvas.create_text(x + size // 2, y + size // 2, text=glyph, font=self.fonts["check"], fill=color,
                           tags=("layout", *tags))

    def _setup_card(self, canvas, y, width, state, title, detail, buttons=(), path=False):
        px, pad = self.px, self.px(16)
        text_x, right = pad + px(42), width - pad
        for button in buttons:
            right -= button.width() + px(8)
        right -= px(4)
        if path:
            detail = self._elide(detail, "detail", right - text_x)
        title_item = canvas.create_text(text_x, y + px(15), text=title, anchor="nw", font=self.fonts["card"], fill=TEXT,
                                        tags="layout")
        bottom = self._text(canvas, text_x, canvas.bbox(title_item)[3] + px(2), right, detail, "detail", MUTED) + px(15)
        bottom = max(bottom, y + px(70))
        center = (y + bottom) // 2
        self._mark(canvas, pad, center - px(14), state)
        x = width - pad
        for button in reversed(buttons):
            x -= button.width()
            button.draw(x, center - button.height // 2)
            x -= px(8)
        self._card(canvas, y, bottom, width)
        return bottom + px(10)

    # ----- Games page -----------------------------------------------------------------------

    def _build_games(self, page):
        c = page.canvas
        # The selected game's actions; its card draws them.
        self.install_button = CanvasButton(self, c, "Install mod", self.apply, surface=CARD)
        self.open_button = CanvasButton(self, c, "Open folder", self.open_game, surface=CARD)
        self.change_button = CanvasButton(self, c, "Change folder", lambda: self.browse_game(self._game()), surface=CARD)
        self.restore_button = CanvasButton(self, c, "Restore original files", self.restore, "link", CARD)
        # A game that is not installed offers where to get it.
        self.missing_buttons = {game.key: (
            CanvasButton(self, c, "Get it on Steam", lambda game=game: self.open_store(game), surface=CARD),
            CanvasButton(self, c, "Locate folder", lambda game=game: self.browse_game(game), surface=CARD))
            for game in games.GAMES}
        self.steam_actions = (CanvasButton(self, c, "Change", self.browse_steam, surface=CARD),)
        for game in games.GAMES:
            tag = f"card:{game.key}"
            c.tag_bind(tag, "<Enter>", lambda _, game=game: self._hover_card(game, True))
            c.tag_bind(tag, "<Leave>", lambda _, game=game: self._hover_card(game, False))
            c.tag_bind(tag, "<ButtonRelease-1>", lambda _, game=game: self._card_clicked(game))
        page.layout = self._layout_games

    def _layout_games(self, page, width):
        c = page.canvas
        c.delete("layout")
        self.card_items = {}
        y = self._heading(c, 0, "Choose a game")
        current = self._game()
        for game in games.GAMES:
            y = self._game_card(c, y, width, game, game == current)
        steam_found = self._paths_found()[1]
        return self._setup_card(c, y, width, "ok" if steam_found else "warn", "Steam",
                                str(Path(self.steam_var.get())) if steam_found else
                                "Choose steam.exe from your Steam folder.", self.steam_actions, path=steam_found)

    def _game_card(self, c, y, width, game, selected):
        px, pad = self.px, self.px(14)
        folder = self.folders.get(game.key, "")
        found = self._holds(folder, game)
        installed = found and (self.installed if selected else self._read_installed(game))
        tag = f"card:{game.key}"
        art_size = (px(150), px(72))
        art_x, art_y = pad, y + pad
        c.create_image(art_x, art_y, anchor="nw", image=self.box(art_size, px(8), FIELD, FIELD, CARD),
                       tags=("layout", tag))
        logo = self._logo(game, art_size[0] - px(16), art_size[1] - px(12))
        center = (art_x + art_size[0] // 2, art_y + art_size[1] // 2)
        if logo:
            c.create_image(*center, image=logo, tags=("layout", tag))
        else:
            c.create_text(*center, text=game.name, width=art_size[0] - px(16), justify="center",
                          font=self.fonts["card"], fill=TEXT, tags=("layout", tag))
        text_x = art_x + art_size[0] + px(16)
        missing = self.missing_buttons[game.key]
        right = width - pad - (px(44) if found else sum(button.width() + px(8) for button in missing))
        title = c.create_text(text_x, art_y, text=game.name, anchor="nw", font=self.fonts["card"], fill=TEXT,
                              tags=("layout", tag))
        bottom = self._text(c, text_x, c.bbox(title)[3] + px(1), right, game.blurb, "detail", MUTED, (tag,))
        status, color = ("Mod installed", BLUE) if installed else ("Mod not installed yet", MUTED) if found else \
            ("Not installed on this PC", ORANGE)
        status_item = c.create_text(text_x, bottom + px(3), text=status, anchor="nw", font=self.fonts["detail"],
                                    fill=color, tags=("layout", tag))
        if selected and found:
            # Where it is, after the status.
            x = c.bbox(status_item)[2] + px(8)
            c.create_text(x, bottom + px(3), anchor="nw", font=self.fonts["detail"], fill=MUTED, tags=("layout", tag),
                          text=self._elide("•  " + str(Path(folder)), "detail", right - x))
        bottom = max(art_y + art_size[1], c.bbox(status_item)[3])
        if found:
            self._mark(c, width - pad - px(28), art_y + (art_size[1] - px(28)) // 2, "ok" if selected else "idle", (tag,))
        else:
            x = width - pad
            for button in reversed(missing):
                x -= button.width()
                button.draw(x, art_y + (art_size[1] - button.height) // 2)
                x -= px(8)
        if selected and found:
            row = bottom + px(12)
            self.install_button.text = "Apply settings" if installed else "Install mod"
            x = text_x
            for button in (self.install_button, self.open_button, self.change_button):
                button.draw(x, row)
                x += button.width() + px(8)
            if installed:
                self.restore_button.draw(width - pad - self.restore_button.width(), row)
            bottom = row + self.install_button.height
        bottom += pad
        outline = BLUE if selected and found else LINE
        self.card_items[game.key] = (self._card(c, y, bottom, width, outline, (tag,)), (width, bottom - y), outline)
        return bottom + px(10)

    def _hover_card(self, game, hover):
        item, size, outline = self.card_items.get(game.key, (None, None, None))
        if item is None or outline == BLUE or not self._holds(self.folders.get(game.key, ""), game):
            return
        page = self.pages["Games"]
        page.canvas.itemconfigure(item, image=self.box(size, self.px(10), CARD, "#4b5565" if hover else outline))
        page.canvas.configure(cursor="hand2" if hover else "")

    def _card_clicked(self, game):
        if self._holds(self.folders.get(game.key, ""), game) and game != self._game():
            self.pages["Games"].canvas.configure(cursor="")
            self.select_game(game)

    def _logo(self, game, width, height):
        key = (game.key, width, height)
        if key not in self.logos:
            self.logos[key] = steam_logo(self.root, Path(self.steam_var.get().strip()), game.app_id, width, height)
        return self.logos[key]

    # ----- Settings page --------------------------------------------------------------------

    def _build_settings(self, page):
        c, page.rows = page.canvas, []
        for item in settings.SETTINGS:
            if item.fixed is not None:
                continue
            var = self.vars[item.key]
            if item.toggle or item.choices in (("false", "true"), ("0", "1")):
                control = Toggle(self, c, var, item.toggle or item.choices)
            else:
                if item.choices:
                    widget = self._choice(c, var, dict(zip(item.choices, item.labels or item.choices)))
                else:
                    widget = ttk.Entry(c, textvariable=var, style="Launcher.TEntry", font=self.fonts["entry"],
                                       justify="right")
                page.bind_wheel(widget)
                control = Field(self, c, widget)
            page.rows.append((item, control))
        page.layout = self._layout_settings

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

    def _layout_settings(self, page, width):
        c, px = page.canvas, self.px
        c.delete("layout")
        values = {key: var.get() for key, var in self.vars.items()}
        advanced = self.advanced_var.get() == "1"
        pad, y, top, group = px(16), 0, None, None
        right, text_right = width - pad, width - pad - min(px(190), width // 3) - px(20)
        for item, control in page.rows:
            if not settings.visible(item, values, advanced):
                control.hide()
                continue
            if item.group != group:
                if top is not None:
                    self._card(c, top, y, width)
                    y += px(14)
                group = item.group
                y = self._text(c, px(2), y, width, group, "group", BLUE) + px(6)
                top, y, first = y, y + px(14), True
            if not first:
                c.create_line(pad, y, right, y, fill=LINE, tags="layout")
                y += px(13)
            first, row = False, y
            c.dtag("row")
            bottom = self._text(c, pad, y, text_right, item.label, "row", TEXT)
            help_text = item.help
            if item.bounds:
                low, high = item.bounds
                help_text = f"{low:g} to {high:g}. " + help_text
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
        if top is not None:
            self._card(c, top, y, width)
        if not advanced:
            y = self._text(c, px(2), y + px(12), width,
                           "Gun position, caption placement and more are under Advanced settings below.",
                           "help", MUTED)
        return y + px(6)

    # ----- Help page ------------------------------------------------------------------------

    def _build_help(self, page):
        c = page.canvas
        self.help_buttons = (
            CanvasButton(self, c, "Open mod log", self.open_log, surface=CARD),
            CanvasButton(self, c, "Open game folder", self.open_game, surface=CARD),
            CanvasButton(self, c, "Open backups", self.open_backups, surface=CARD),
            CanvasButton(self, c, "Preview config", self.show_preview, surface=CARD))
        self.link_buttons = (
            CanvasButton(self, c, "Guide and downloads", lambda: self.open_url(PROJECT_URL), "link", CARD),
            CanvasButton(self, c, "Report a problem", lambda: self.open_url(PROJECT_URL + "/issues"), "link", CARD),
            CanvasButton(self, c, "Support the mod on Ko-fi", lambda: self.open_url(KOFI_URL), "link", CARD))
        page.layout = self._layout_help

    def _info_card(self, c, y, width, title, body, buttons=(), gap=8):
        px, pad = self.px, self.px(16)
        title_item = c.create_text(pad, y + px(14), text=title, anchor="nw", font=self.fonts["card"], fill=TEXT,
                                   tags="layout")
        bottom = c.bbox(title_item)[3]
        if body:
            bottom = self._text(c, pad, bottom + px(4), width - pad, body, "detail", MUTED)
        x, row = pad, bottom + px(12)
        for button in buttons:
            if x + button.width() > width - pad:
                x, row = pad, row + button.height + px(8)
            button.draw(x, row)
            x += button.width() + px(gap)
            bottom = row + button.height
        bottom += px(16)
        self._card(c, y, bottom, width)
        return bottom + px(12)

    def _layout_help(self, page, width):
        c = page.canvas
        c.delete("layout")
        y = self._info_card(c, 0, width, "Something went wrong?",
                            "The mod log says what happened; attach it to a bug report. Detailed log under "
                            "Advanced settings adds more to it.", self.help_buttons)
        y = self._info_card(c, y, width, "Links", "", self.link_buttons, gap=28)
        return self._info_card(
            c, y, width, "About",
            "Unofficial fan project, not affiliated with Valve. Built on Gistix/portal2vr and "
            "mmadej20/portal2vr-ultimate. Needs a supported game for Windows, Steam and SteamVR.\n\n"
            "Play in VR installs the mod with your settings and starts the game. Settings save as you change "
            "them and apply the next time you play. Your original game files are backed up before the first "
            "install; Restore original files on the Games page puts them back. Keep the launcher's tools "
            "folder: it holds those backups.")

    # ----- Settings state -------------------------------------------------------------------

    def preferences(self):
        values = settings.user_values({key: var.get() for key, var in self.vars.items()})
        prefs = {"version": 1, "game_dir": self.game_var.get().strip(), "steam_exe": self.steam_var.get().strip(),
                 "values": settings.validate_settings(values)}
        folders = {key: path for key, path in self.folders.items() if path}
        if folders:
            prefs["game_dirs"] = folders
        taken = {key: dict(values) for key, values in self.taken.items() if values}
        if taken:
            prefs["ingame_taken"] = taken
        return prefs

    def _friendly(self, error):
        """A validation error in the words the Settings page uses."""
        text = str(error)
        key, sep, rest = text.partition(": ")
        item = settings.BY_KEY.get(key)
        return f"{item.label}: {rest}" if sep and item else text

    def _setting_changed(self, key):
        if self._quiet:
            return
        self._announce = True
        values = {name: var.get() for name, var in self.vars.items()}
        linked = settings.coupled_changes(values, key)
        for name, value in linked.items():
            self.vars[name].set(value)
        if key in LAYOUT_KEYS or linked:
            self.pages["Settings"].relayout()
        self._changed()

    def _changed(self, *_):
        try:
            prefs = self.preferences()
            self.invalid = False
        except ValueError as error:
            # An earlier change waiting to be saved waits for this value to be fixed.
            self._cancel_save()
            self.invalid = True
            self._next_error = True
            self.status_var.set(self._friendly(error))
        else:
            if not self._quiet and prefs != self.saved:
                self._schedule_save()
        self._paint_header()

    def _cancel_save(self):
        if self._save_job:
            self.root.after_cancel(self._save_job)
            self._save_job = None

    def _schedule_save(self):
        self._cancel_save()
        self._save_job = self.root.after(SAVE_DELAY_MS, self._autosave)

    def _autosave(self):
        # Only a setting the user changed says so; a game switch keeps its own message.
        announce, self._announce, self._save_job = self._announce, False, None
        self.save(announce=announce)

    def _error(self, error):
        self._next_error = True
        self.status_var.set(str(error))
        messagebox.showerror("Portal 2 VR Enriched", str(error), parent=self.root)

    def save(self, announce=True):
        """Write the settings to the launcher's own file now; game files do not change."""
        self._cancel_save()
        try:
            prefs = self.preferences()
            settings.save_preferences(self.preferences_path, prefs)
            self.saved = prefs
            if announce:
                self.status_var.set("Settings saved. They apply the next time you play.")
            return True
        except (OSError, ValueError, RuntimeError) as error:
            self._error(error)
            return False

    def reset_recommended(self):
        if not messagebox.askyesno("Restore recommended settings",
                                   "Set every setting back to the recommended value?\n\n"
                                   "Your games and Steam folder stay as they are.", parent=self.root):
            return
        for key, value in settings.recommended_settings().items():
            self.vars[key].set(value)
        self.save(announce=False)
        self.status_var.set("Recommended settings restored and saved.")

    def _import_ingame(self):
        """Take the Options > VR changes made in the selected game since the launcher last looked.

        The game keeps its file until the next install, so values already taken are remembered:
        a later change here must not be undone by the same old file at the next start.
        Returns True when the settings or that record changed.
        """
        game = self._game().key
        self.ingame = settings.read_ingame_settings(Path(self.game_var.get().strip()))
        before = self.taken.get(game, {})
        fresh = {key: value for key, value in self.ingame.items() if before.get(key) != value}
        current = {key: var.get() for key, var in self.vars.items()}
        merged = settings.merge_ingame_settings(current, fresh)
        changed = [key for key in merged if merged[key] != current[key]]
        for key in changed:
            self.vars[key].set(merged[key])
        if self.ingame:
            self.taken[game] = dict(self.ingame)
        else:
            self.taken.pop(game, None)
        count = f"{len(changed)} setting{'s' if len(changed) > 1 else ''}"
        self.import_note = f"Took {count} you changed in the game under Options > VR." if changed else ""
        if changed:
            self.status_var.set(self.import_note)
        self._announce = False  # The import note says it.
        return bool(changed) or self.taken.get(game, {}) != before

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

    # ----- Games and install ----------------------------------------------------------------

    @staticmethod
    def _holds(folder, game):
        """True when the folder is an install of this game."""
        return bool(folder and folder.strip()) and (Path(folder) / "portal2.exe").is_file() and \
            games.detect(Path(folder)) == game

    def _lookup(self, game):
        steam = Path(self.steam_var.get().strip())
        try:
            found = find_game(steam.parent, game) if steam.is_file() else None
        except (OSError, ValueError):
            found = None
        return str(found) if found else ""

    def _find_folders(self, prefs):
        """Each game's folder: the selected one, one chosen earlier, or the one Steam knows."""
        current, saved = games.detect(Path(prefs["game_dir"])), prefs.get("game_dirs", {})
        steam = Path(prefs["steam_exe"])
        folders = {}
        for game in games.GAMES:
            folder = prefs["game_dir"] if game == current else saved.get(game.key, "")
            if game != current and not self._holds(folder, game):
                try:
                    found = find_game(steam.parent, game) if steam.is_file() else None
                except (OSError, ValueError):
                    found = None
                folder = str(found) if found else ""
            folders[game.key] = folder
        return folders

    def _paths_found(self):
        game, steam = Path(self.game_var.get()), Path(self.steam_var.get())
        return (game / "portal2.exe").is_file(), steam.is_file() and steam.name.lower() == "steam.exe"

    def _game(self):
        return games.detect(Path(self.game_var.get().strip()))

    def _state_path(self, game=None):
        """Each game keeps its own install record and backups."""
        return games.state_path(self.install_state_path, game or self._game())

    def _read_installed(self, game=None):
        try:
            return bool(core.load_state(self._state_path(game))["managed_files"])
        except (OSError, ValueError, RuntimeError):
            return False

    def _game_changed(self, *_):
        folder = self.game_var.get().strip()
        if folder:
            self.folders[self._game().key] = folder
        self.installed = self._read_installed()
        if not self._quiet:
            self._import_ingame()
        self._changed()
        self.pages["Games"].relayout()

    def _steam_changed(self, *_):
        self.logos.clear()
        for game in games.GAMES:
            if not self._holds(self.folders.get(game.key, ""), game):
                self.folders[game.key] = self._lookup(game) or self.folders.get(game.key, "")
        self._changed()
        self.pages["Games"].relayout()

    def _refresh_install(self):
        self.installed = self._read_installed()
        for button in (self.restore_button, self.install_button):
            button._set("normal")  # The action may move or remove it from under the pointer.
        self._paint_header()
        self.pages["Games"].relayout()

    def select_game(self, game):
        """Make a game the one Play in VR starts, finding its folder in Steam when needed."""
        folder = self.folders.get(game.key, "")
        if not self._holds(folder, game):
            folder = self._lookup(game)
        if not folder:
            self._error(FileNotFoundError(f"{game.name} was not found in your Steam libraries. "
                                          "Install it from Steam, or use Locate folder."))
            return
        self.folders[game.key] = folder
        self.import_note = ""
        if folder != self.game_var.get():
            self.game_var.set(folder)
        self.status_var.set(" ".join(filter(None, (f"{game.name} selected.", (
            "The mod is installed there." if self.installed else
            "Play in VR installs the mod there; its original files are backed up first."), self.import_note))))

    def browse_game(self, game=None):
        game = game or self._game()
        path = filedialog.askdirectory(parent=self.root, title=f"Select the {game.name} folder (it holds portal2.exe)",
                                       initialdir=self.folders.get(game.key) or self.game_var.get())
        if not path:
            return
        folder = Path(path)
        if not (folder / "portal2.exe").is_file():
            self._error(FileNotFoundError(f"portal2.exe is not in {folder}. Choose the folder of {game.name}."))
            return
        self.import_note = ""
        chosen = games.detect(folder)
        self.folders[chosen.key] = str(folder)
        self.game_var.set(str(folder))
        lead = f"That folder holds {chosen.name}, which is now selected." if chosen != game else \
            f"{game.name} folder set."
        self.status_var.set(" ".join(filter(None, (lead, self.import_note))))

    def browse_steam(self):
        path = filedialog.askopenfilename(parent=self.root, title="Select Steam.exe",
                                         initialdir=str(Path(self.steam_var.get()).parent),
                                         filetypes=[("Steam executable", "steam.exe")])
        if path:
            self.steam_var.set(path)

    def _prepare(self, launch=False):
        if game_is_running():
            raise RuntimeError(f"Close {self._game().name} before changing game files or launching again.")
        prefs = self.preferences()
        game, steam = Path(prefs["game_dir"]), Path(prefs["steam_exe"])
        if launch:
            core.validate_launch_paths(self.repo, game, steam)
        core.plan_install(self.repo, game, "baseline", config_values=prefs["values"])
        core.recover_pending_transaction(self._state_path())
        state = core.load_state(self._state_path())
        if not state["managed_files"] and not messagebox.askyesno(
                "Install Portal 2 VR Enriched", f"Install the mod in:\n{game}\n\n"
                "Existing files will be backed up first. You can restore them from the Games page.",
                parent=self.root):
            return None
        return prefs

    def _stage(self, prefs):
        settings.save_preferences(self.preferences_path, prefs)
        self.saved = prefs
        core.stage_install(self.repo, Path(prefs["game_dir"]), self._state_path(),
                           "baseline", config_values=prefs["values"])
        # config.txt now carries the imported in-game values; an unretired file would
        # override later launcher edits.
        if settings.retire_ingame_settings(Path(prefs["game_dir"]), self.ingame):
            self.taken.pop(self._game().key, None)
            self.saved = self.preferences()
            settings.save_preferences(self.preferences_path, self.saved)
        self.ingame = {}
        self._refresh_install()
        self._changed()

    def apply(self):
        """Install the mod with the current settings without starting the game."""
        try:
            prefs = self._prepare()
            if prefs is None:
                return
            self._stage(prefs)
            self.status_var.set(f"Mod and settings installed in {self._game().name}. "
                                "Your original files stay backed up.")
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            self._error(self._friendly(error) if isinstance(error, ValueError) else error)
        except Exception as error:  # noqa: BLE001 - see _unexpected
            self._unexpected(error)

    def _unexpected(self, error):
        """A fault nobody planned for. The packaged launcher has no console, so an exception
        that escaped a button would vanish and the button would seem to do nothing."""
        self._error(f"Unexpected error ({type(error).__name__}): {error}. "
                    "Please report it on GitHub with this message.")

    def launch(self):
        staged = False
        try:
            prefs = self._prepare(launch=True)
            if prefs is None:
                return
            self._stage(prefs)
            staged = True
            steam = Path(prefs["steam_exe"])
            game = self._game()
            subprocess.Popen(core.build_steam_command(steam, game.app_id), cwd=str(steam.parent))
            self.status_var.set(f"{game.name} is starting through Steam; SteamVR opens with it.")
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            if staged:
                error = RuntimeError(f"Steam launch failed: {error}. Mod files and backups are retained; "
                                     "retry or use Restore original files.")
            self._error(self._friendly(error) if isinstance(error, ValueError) else error)
        except Exception as error:  # noqa: BLE001 - see _unexpected
            self._unexpected(error)

    def restore(self):
        try:
            if game_is_running():
                raise RuntimeError(f"Close {self._game().name} before restoring game files.")
            state_path = self._state_path()
            recovered = core.recover_pending_transaction(state_path)
            state = core.load_state(state_path)
            if not state["managed_files"]:
                core.restore_install(Path(state["game_dir"]), state_path)
                self.status_var.set("Interrupted operation recovered; original files restored." if recovered else
                                    "No files are currently managed by the launcher.")
                return
            game = Path(state["installed_game_dir"])
            if not messagebox.askyesno("Restore original files", f"Remove the mod and restore the original files in:"
                                       f"\n{game}\n\nYour settings and the backup are kept.", parent=self.root):
                return
            core.restore_install(game, state_path)
            self.status_var.set("Original files restored. Your settings and the backup are kept.")
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            self._error(error)
        except Exception as error:  # noqa: BLE001 - see _unexpected
            self._unexpected(error)
        finally:
            self._refresh_install()

    # ----- Opening things -------------------------------------------------------------------

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
        self._open(self._state_path().parent / ".launcher-backups")

    def open_store(self, game):
        try:
            os.startfile(f"steam://store/{game.app_id}")
        except OSError:
            self.open_url(f"https://store.steampowered.com/app/{game.app_id}/")

    def open_url(self, url):
        webbrowser.open(url)

    def close(self):
        if self.invalid:
            if not messagebox.askyesno("Setting not saved", "One setting has a value that is not valid, so it was "
                                       "not saved. Close anyway?", parent=self.root):
                return
        elif self._save_job or self.preferences() != self.saved:
            if not self.save(announce=False):
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
