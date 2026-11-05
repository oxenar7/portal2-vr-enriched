"""Find the Steam client and its Portal 2 installation so players need not browse.

Steam records its folder in the registry and its game libraries in
steamapps/libraryfolders.vdf; each installed game has an appmanifest_<appid>.acf
whose installdir names its folder under steamapps/common. Only paths that
actually contain steam.exe or portal2.exe are ever returned.
"""
import os
from pathlib import Path
import re
import sys

PORTAL2_APPID = "620"
_TOKENS = re.compile(r'\s+|//[^\r\n]*|"((?:[^"\\]|\\.)*)"|([{}])|([^\s{}"]+)')
_ESCAPES = {"n": "\n", "t": "\t", "\\": "\\", '"': '"'}


def parse_keyvalues(text: str) -> dict:
    """Parse Valve KeyValues text such as libraryfolders.vdf; keys are lower-cased."""
    root: dict = {}
    stack = [root]
    key = None
    position = 0
    while position < len(text):
        match = _TOKENS.match(text, position)
        if not match:
            raise ValueError(f"Unexpected character at offset {position}")
        position = match.end()
        quoted, brace, bare = match.groups()
        if quoted is None and brace is None and bare is None:
            continue  # whitespace or comment
        if brace == "{":
            if key is None:
                raise ValueError("Block without a key")
            child: dict = {}
            stack[-1][key] = child
            stack.append(child)
            key = None
        elif brace == "}":
            if key is not None or len(stack) == 1:
                raise ValueError("Unbalanced block")
            stack.pop()
        else:
            token = bare if quoted is None else re.sub(
                r"\\(.)", lambda m: _ESCAPES.get(m.group(1), "\\" + m.group(1)), quoted)
            if key is None:
                key = token.lower()
            else:
                stack[-1][key] = token
                key = None
    if key is not None or len(stack) != 1:
        raise ValueError("Unexpected end of file")
    return root


def _read_keyvalues(path: Path) -> dict:
    try:
        return parse_keyvalues(path.read_text(encoding="utf-8", errors="replace"))
    except (OSError, ValueError):
        return {}


def _real_path(path: Path) -> Path:
    try:
        return path.resolve()  # Steam's registry values are lower-case
    except OSError:
        return path


def is_steam_exe(path: Path) -> bool:
    return path.name.lower() == "steam.exe" and path.is_file()


def is_portal2_dir(path: Path) -> bool:
    return (path / "portal2.exe").is_file()


def registry_steam_exe() -> Path | None:
    """steam.exe as recorded by the Steam installer, or in its default folder."""
    candidates = []
    if sys.platform == "win32":
        import winreg
        for hive, key, name in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamExe"),
                                (winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
                                (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"),
                                (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam", "InstallPath")):
            try:
                with winreg.OpenKey(hive, key) as handle:
                    value, _ = winreg.QueryValueEx(handle, name)
            except OSError:
                continue
            if isinstance(value, str) and value.strip():
                path = Path(value.strip())
                candidates.append(path if path.suffix.lower() == ".exe" else path / "steam.exe")
    program_files = os.environ.get("ProgramFiles(x86)") or os.environ.get("ProgramFiles")
    if program_files:
        candidates.append(Path(program_files) / "Steam" / "steam.exe")
    for candidate in candidates:
        if is_steam_exe(candidate):
            return _real_path(candidate)
    return None


def library_folders(steam_root: Path) -> list[Path]:
    """Steam's libraries, the ones listing Portal 2 first, the Steam folder itself included."""
    entries = _read_keyvalues(steam_root / "steamapps" / "libraryfolders.vdf").get("libraryfolders", {})
    with_portal, others = [], [steam_root]
    if isinstance(entries, dict):
        for index, entry in entries.items():
            if not index.isdigit():
                continue
            path = entry.get("path") if isinstance(entry, dict) else entry
            if not isinstance(path, str) or not path:
                continue
            apps = entry.get("apps", {}) if isinstance(entry, dict) else {}
            (with_portal if isinstance(apps, dict) and PORTAL2_APPID in apps else others).append(Path(path))
    unique, seen = [], set()
    for library in with_portal + others:
        key = os.path.normcase(os.path.normpath(str(library)))
        if key not in seen:
            seen.add(key)
            unique.append(library)
    return unique


def find_portal2(steam_root: Path) -> Path | None:
    for library in library_folders(steam_root):
        apps = library / "steamapps"
        manifest = _read_keyvalues(apps / f"appmanifest_{PORTAL2_APPID}.acf").get("appstate", {})
        folder = manifest.get("installdir") if isinstance(manifest, dict) else None
        for name in dict.fromkeys(n for n in (folder, "Portal 2") if isinstance(n, str) and n):
            candidate = apps / "common" / name
            if is_portal2_dir(candidate):
                return _real_path(candidate)
    return None


def find_installation(game_dir: str, steam_exe: str, registry=registry_steam_exe) -> tuple[str, str]:
    """Return (game_dir, steam_exe), replacing only values that do not point at the real files.

    A Portal 2 folder is looked up in the libraries of the given Steam client when
    that one is valid, so the game and the client that launches it stay paired.
    """
    game_ok = bool(game_dir.strip()) and is_portal2_dir(Path(game_dir.strip()))
    steam_ok = bool(steam_exe.strip()) and is_steam_exe(Path(steam_exe.strip()))
    if game_ok and steam_ok:
        return game_dir, steam_exe
    steam = Path(steam_exe.strip()) if steam_ok else registry()
    if steam is None:
        return game_dir, steam_exe
    game = None if game_ok else find_portal2(steam.parent)
    return (str(game) if game else game_dir), (steam_exe if steam_ok else str(steam))
