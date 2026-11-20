"""The games the mod runs in, and how the launcher tells them apart.

Portal: Alive & Kicking and Portal Stories: Mel are standalone Steam apps on Portal 2's
engine. Their install folders look like Portal 2's (portal2.exe, bin) but their game data
lives in an "AAK" or "portal_stories" folder instead of "portal2" (Mel's install also has a
"portal2" folder of its own). The mod files go to the same places in all of them.
"""
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Game:
    key: str             # short id, used in file names
    name: str            # shown to the player
    app_id: str          # Steam app id: -applaunch and appmanifest_<id>.acf
    install_dir: str     # default folder under steamapps/common
    game_folder: str     # the -game folder holding gameinfo.txt
    processes: tuple[str, ...]  # executable names while it runs
    blurb: str = ""      # one line under its name on the launcher's Games page


# The launcher's Games page lists them in this order. A new game needs its entry here and its
# profile in L4D2VR/game_profile.h.
PORTAL2 = Game("portal2", "Portal 2", "620", "Portal 2", "portal2", ("portal2.exe",),
               "Valve's single-player campaign.")
ALIVE_AND_KICKING = Game("aak", "Portal: Alive & Kicking", "270470", "Alive and Kicking", "AAK",
                         ("portal2.exe", "aak.exe"), "A free fan-made campaign on Portal 2's engine.")
PORTAL_STORIES_MEL = Game("mel", "Portal Stories: Mel", "317400", "Portal Stories Mel", "portal_stories",
                          ("portal2.exe",), "A free fan-made story campaign on Portal 2's engine.")
GAMES = (PORTAL2, ALIVE_AND_KICKING, PORTAL_STORIES_MEL)
PROCESS_NAMES = tuple(dict.fromkeys(name for game in GAMES for name in game.processes))


def is_install(folder: Path, game: Game) -> bool:
    """True for an install folder of this game: portal2.exe, bin and its own gameinfo.txt."""
    return ((folder / "portal2.exe").is_file() and (folder / "bin").is_dir() and
            (folder / game.game_folder / "gameinfo.txt").is_file())


def detect(folder: Path) -> Game:
    """The game a chosen folder holds. Anything that is not another game's install counts as
    Portal 2, so a folder chosen by hand keeps working as before."""
    for game in GAMES:
        if game != PORTAL2 and is_install(folder, game):
            return game
    return PORTAL2


def by_key(key: str) -> Game:
    for game in GAMES:
        if game.key == key:
            return game
    raise ValueError(f"Unknown game: {key}")


def state_path(portal2_state: Path, game: Game) -> Path:
    """Install state for one game. Portal 2 keeps the original file; every other game gets a
    folder of its own beside it, so its backups never mix with Portal 2's."""
    if game == PORTAL2:
        return portal2_state
    return portal2_state.parent / f".launcher-{game.key}" / portal2_state.name
