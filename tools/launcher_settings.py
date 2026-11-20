"""Complete, validated mod settings. No game writes or GUI side effects."""

from dataclasses import dataclass
import json
import math
import os
from pathlib import Path
import re
import tempfile


@dataclass(frozen=True)
class Setting:
    key: str
    label: str
    group: str
    default: str
    choices: tuple[str, ...] = ()
    bounds: tuple[float, float] | None = None
    help: str = ""
    # Names shown for choices; the stored values stay what config.txt expects.
    labels: tuple[str, ...] = ()
    # Shown only with "Advanced settings" on.
    advanced: bool = False
    # Shown only while each named setting holds one of the listed values.
    when: tuple[tuple[str, tuple[str, ...]], ...] = ()
    # Off and on values of a switch over a setting that has more than two choices.
    toggle: tuple[str, ...] = ()
    # Retired: the launcher no longer offers it and always writes this value, or the value of
    # the setting named here.
    fixed: str | None = None


_SNAP = ("SnapTurning", ("true",))
_SMOOTH = ("SnapTurning", ("false",))
_POSITIONAL = ("6DOF", ("true",))
_CAPTIONS = ("ExperimentalHUDOverlay", ("true",))
_VIBRATION = ("Haptics", ("true",))
_ZOOM = ("BinocularZoom", ("true",))
_GUN_HELP = "Moves the portal gun in your hand; one unit is about 2 cm."

# Match ConfigSnapshot / ParseConfig in L4D2VR/config.h. Labels, groups and order are
# presentation only: the launcher shows the groups in this order.
SETTINGS = (
    Setting("TrackingMode", "Play position", "Comfort", "Seated", ("Seated", "Standing"),
            help="Standing uses your real height from the SteamVR floor. Seated puts the view at "
                 "eye height when you recenter."),
    Setting("HeightOffsetMeters", "Height adjustment (m)", "Comfort", "0", bounds=(-.5, .5),
            help="Raises or lowers your standing view. Applied when you recenter.",
            advanced=True, when=(("TrackingMode", ("Standing",)),)),
    Setting("RoomscaleMode", "Physical walking", "Comfort", "Off", ("Off", "Observe", "ActiveExperimental"),
            help="Your real steps move you in the game. Walls still stop you.",
            toggle=("Off", "ActiveExperimental"), when=(_POSITIONAL,)),
    Setting("PhysicalCrouch", "Physical crouch", "Comfort", "true", ("false", "true"),
            help="Duck in the game when you crouch for real.", when=(_POSITIONAL,)),
    Setting("PhysicalCrouchDropMeters", "Crouch depth (m)", "Comfort", "0.25", bounds=(.15, .8),
            help="How far your head must drop to duck; you stand up 5 cm earlier.",
            advanced=True, when=(_POSITIONAL, ("PhysicalCrouch", ("true",)))),
    Setting("SnapTurning", "Snap turning", "Comfort", "false", ("false", "true"),
            help="Turn in steps instead of smoothly."),
    Setting("SnapTurnAngle", "Snap angle (degrees)", "Comfort", "45", bounds=(1, 180), when=(_SNAP,)),
    Setting("TurnSpeed", "Smooth turning speed", "Comfort", "0.15", bounds=(.01, 2), when=(_SMOOTH,)),
    Setting("MovementDirection", "Stick movement follows", "Comfort", "HMD",
            ("HMD", "LeftController", "RightController"), labels=("Headset", "Left controller", "Right controller")),

    Setting("LeftHanded", "Left-handed", "Controls", "false", ("false", "true"),
            help="The gun goes in the left hand with its buttons: portal trigger and grip, use on Y. Pause moves to B."),
    Setting("SwapSticks", "Swap sticks", "Controls", "false", ("false", "true"),
            help="Move with the right stick, turn with the left. Binocular zoom follows the turning stick."),
    Setting("ControllerAimedUse", "Use what the gun points at", "Controls", "true", ("false", "true"),
            help="Pick up, drop and press act on what the gun points at. Off acts on what you look at."),
    Setting("BinocularZoom", "Binocular zoom", "Controls", "true", ("false", "true"),
            help="Hold the turning stick in to look through binoculars. Also completes Portal 2's zoom lesson."),
    Setting("ZoomMagnification", "Zoom magnification", "Controls", "3", bounds=(1.5, 8),
            advanced=True, when=(_ZOOM,)),
    Setting("ZoomWindowWidthMeters", "Binocular window width (m)", "Controls", "0.8", bounds=(.3, 1.5),
            advanced=True, when=(_ZOOM,)),
    Setting("ZoomDistanceMeters", "Binocular window distance (m)", "Controls", "1", bounds=(.5, 3),
            advanced=True, when=(_ZOOM,)),
    Setting("ControllerPitchDegrees", "Controller angle (degrees)", "Controls", "-30", bounds=(-60, 60),
            help="Tilts the gun and the aim against the controller.", advanced=True),

    Setting("ExperimentalWorldAimMarker", "Aim line", "Aiming and objects", "false", ("false", "true"),
            help="A line from the gun to where the portal will land."),
    Setting("ReticleDistanceScaling", "Smaller crosshair far away", "Aiming and objects", "true", ("false", "true"),
            help="Full size at 1 m, 80% at 10 m.", advanced=True),
    Setting("HeldObjectAnchor", "Held objects follow the gun", "Aiming and objects", "true", ("false", "true"),
            help="Held objects move with the gun and turn with the wrist. Walls still stop them."),
    Setting("HeldObjectColumn", "Held object vertical plane", "Aiming and objects", "false", ("false", "true"),
            help="On is Portal 2's own behaviour.", advanced=True, when=(("HeldObjectAnchor", ("false",)),)),
    Setting("LaserCubeAimAssist", "Laser cube aim assist", "Aiming and objects", "true", ("false", "true"),
            help="Steers a laser leaving a reflector cube onto a catcher, as in co-op."),
    Setting("WorldMenu", "Pause menu in the world", "Aiming and objects", "true", ("false", "true"),
            help="The pause menu appears as panels in the paused world. Off shows the game's own pause screen."),

    Setting("ExperimentalHUDOverlay", "Caption panel", "Captions", "false", ("false", "true"),
            help="Shows subtitles on a readable panel in front of you. Turn subtitles on in the game's audio options."),
    Setting("HUDDistanceMeters", "Caption distance (m)", "Captions", "1.3", bounds=(.6, 3),
            advanced=True, when=(_CAPTIONS,)),
    Setting("HUDWidthMeters", "Caption panel width (m)", "Captions", "1.4", bounds=(.5, 2.5),
            advanced=True, when=(_CAPTIONS,)),
    Setting("HUDVerticalOffsetMeters", "Caption height (m)", "Captions", "-0.15", bounds=(-.6, .6),
            help="Relative to your eyes; negative is lower.", advanced=True, when=(_CAPTIONS,)),
    Setting("HUDLazyFollow", "Captions follow your gaze", "Captions", "true", ("false", "true"),
            help="Off locks them to the headset.", advanced=True, when=(_CAPTIONS,)),
    Setting("HUDFollowDeadzoneDegrees", "Follow deadzone (degrees)", "Captions", "10", bounds=(0, 60),
            advanced=True, when=(_CAPTIONS, ("HUDLazyFollow", ("true",)))),
    Setting("HUDFollowSpeed", "Follow speed", "Captions", "8", bounds=(1, 20),
            help="Higher catches up with your gaze quicker.", advanced=True,
            when=(_CAPTIONS, ("HUDLazyFollow", ("true",)))),
    Setting("HUDCaptionOpacityBoost", "Caption background darkness", "Captions", "3", bounds=(1, 4),
            help="1 keeps the game's own transparency.", advanced=True, when=(_CAPTIONS,)),

    Setting("Haptics", "Controller vibration", "Vibration", "true", ("false", "true"),
            help="Portal shots, landings, impacts, held objects and buttons."),
    Setting("HapticsIntensity", "Vibration strength", "Vibration", "1", bounds=(0, 1), when=(_VIBRATION,)),
    Setting("HapticsNativeRumble", "Game rumble (shots, landings, impacts)", "Vibration", "true", ("false", "true"),
            advanced=True, when=(_VIBRATION,)),
    Setting("HapticsHeldObject", "Held object vibration", "Vibration", "true", ("false", "true"),
            help="A pulse on pickup and drop, a light vibration while holding.", advanced=True, when=(_VIBRATION,)),
    Setting("HapticsButtons", "Button vibration", "Vibration", "true", ("false", "true"),
            help="Pedestal buttons you press and floor buttons.", advanced=True, when=(_VIBRATION,)),

    Setting("AntiAliasing", "Anti-aliasing", "Graphics", "0", ("0", "2", "4", "8"),
            help="Smooths jagged edges; higher values cost performance.", labels=("Off", "2x", "4x", "8x")),
    Setting("RenderWindow", "Show the game on the monitor", "Graphics", "0", ("0", "1"),
            help="Draws an extra view for the desktop window. Costs performance."),

    Setting("ViewmodelPosCustomOffsetX", "Gun offset X", "Gun position", "0", bounds=(-100, 100),
            help=_GUN_HELP, advanced=True),
    Setting("ViewmodelPosCustomOffsetY", "Gun offset Y", "Gun position", "0", bounds=(-100, 100), advanced=True),
    Setting("ViewmodelPosCustomOffsetZ", "Gun offset Z", "Gun position", "0", bounds=(-100, 100), advanced=True),
    Setting("ViewmodelAngCustomOffsetX", "Gun angle X (degrees)", "Gun position", "0", bounds=(-180, 180),
            advanced=True),
    Setting("ViewmodelAngCustomOffsetY", "Gun angle Y (degrees)", "Gun position", "0", bounds=(-180, 180),
            advanced=True),
    Setting("ViewmodelAngCustomOffsetZ", "Gun angle Z (degrees)", "Gun position", "0", bounds=(-180, 180),
            advanced=True),

    Setting("6DOF", "Positional head tracking", "Tracking", "true", ("false", "true"),
            help="Off also turns physical walking off.", advanced=True),
    Setting("VRScale", "World scale (units per metre)", "Tracking", "43.2", bounds=(1, 200),
            help="43.2 matches the game's proportions.", advanced=True),
    Setting("IPDScale", "Eye distance multiplier", "Tracking", "1", bounds=(.5, 1.5),
            help="Above 1 makes the world look smaller.", advanced=True),

    Setting("PortalOrientationMode", "Portal orientation", "Experimental", "LegacyYaw",
            ("LegacyYaw", "FullRotation", "YawOnly", "PreserveHorizon"),
            help="How your view turns through floor and ceiling portals. Modes other than Classic are "
                 "untested and turn physical walking off.",
            labels=("Classic", "Full rotation", "Yaw only", "Level horizon"), advanced=True),

    Setting("VerboseDiagnostics", "Detailed log", "Troubleshooting", "false", ("false", "true"),
            help="Writes much more to the mod log, for bug reports.", advanced=True),

    # Retired options, superseded by what the mod now always does.
    Setting("AimMode", "Aim mode", "Retired", "2", ("0", "1", "2"), fixed="2"),
    Setting("ShowSourceCrosshair", "Legacy flat crosshair", "Retired", "true", ("false", "true"), fixed="true"),
    Setting("ExperimentalStereoReticle", "Atlas reticle rollback", "Retired", "false", ("false", "true"),
            fixed="false"),
    Setting("HUDInEyeCentered", "Center legacy HUD in eyes", "Retired", "false", ("false", "true"), fixed="false"),
    Setting("HUDInEyeScale", "Legacy HUD width fraction", "Retired", "0.45", bounds=(.2, 1), fixed="0.45"),
    Setting("HUDInEyeVerticalOffset", "Legacy HUD vertical offset", "Retired", "0.05", bounds=(-.4, .4),
            fixed="0.05"),
    Setting("ExperimentalViewmodelAlignment", "Controller-locked gun", "Retired", "false", ("false", "true"),
            fixed="true"),
    Setting("AimFromViewmodelMuzzle", "Aim from the gun muzzle", "Retired", "false", ("false", "true"),
            fixed="true"),
    # The fallback shot pulse when the game's own rumble is unavailable follows Controller vibration.
    Setting("ExperimentalPortalShotHaptics", "Portal shot vibration", "Retired", "false", ("false", "true"),
            fixed="Haptics"),
    Setting("PortalShotHapticAmplitude", "Portal shot vibration strength", "Retired", "0.35", bounds=(0, 1),
            fixed="0.35"),
    Setting("PortalShotHapticDurationSeconds", "Portal shot vibration duration", "Retired", "0.05",
            bounds=(.01, .15), fixed="0.05"),
)
BY_KEY = {item.key: item for item in SETTINGS}
# Choices the launcher no longer offers, and what replaces them.
_RETIRED_CHOICES = {"RoomscaleMode": {"Observe": "Off"}}
_EYE_HUD_KEYS = frozenset(("HUDInEyeCentered", "HUDInEyeScale", "HUDInEyeVerticalOffset", "ShowSourceCrosshair"))
_HAPTICS_KEYS = frozenset(("Haptics", "HapticsIntensity", "HapticsNativeRumble", "HapticsHeldObject", "HapticsButtons"))
_CROUCH_KEYS = frozenset(("PhysicalCrouch", "PhysicalCrouchDropMeters"))
_HELD_KEYS = frozenset(("LaserCubeAimAssist", "HeldObjectColumn"))
_HELD_ANCHOR_KEYS = frozenset(("HeldObjectAnchor",))
_USE_AIM_KEYS = frozenset(("ControllerAimedUse",))
_WORLD_MENU_KEYS = frozenset(("WorldMenu",))
_SWAP_STICKS_KEYS = frozenset(("SwapSticks",))
_ZOOM_KEYS = frozenset(("BinocularZoom", "ZoomMagnification", "ZoomWindowWidthMeters", "ZoomDistanceMeters"))
# Key groups added after the first complete schema. A saved schema missing whole groups gains their defaults.
_ADDED_KEY_GROUPS = (_EYE_HUD_KEYS, _HAPTICS_KEYS, _CROUCH_KEYS, _HELD_KEYS, _HELD_ANCHOR_KEYS, _ZOOM_KEYS,
                     _USE_AIM_KEYS, _WORLD_MENU_KEYS, _SWAP_STICKS_KEYS)


# Options > VR in the game writes these keys to VR/ingame_settings.txt, which the mod
# layers over config.txt. Match VrSettings::Pages() in L4D2VR/vr_settings.h.
INGAME_SETTINGS = "VR/ingame_settings.txt"
INGAME_KEYS = frozenset((
    "SnapTurning", "SnapTurnAngle", "TurnSpeed", "MovementDirection", "PhysicalCrouch", "LeftHanded", "SwapSticks",
    "Haptics", "HapticsIntensity", "ExperimentalWorldAimMarker", "6DOF", "ControllerPitchDegrees",
    "HeightOffsetMeters", "PhysicalCrouchDropMeters", "VRScale", "IPDScale", "HapticsNativeRumble", "HapticsHeldObject",
    "HapticsButtons", "ReticleDistanceScaling", "LaserCubeAimAssist", "HeldObjectAnchor", "RenderWindow",
    "ControllerAimedUse"))


def read_ingame_settings(game: Path) -> dict[str, str]:
    """Raw in-game overrides for menu keys; empty when the game has none. Never writes."""
    try:
        text = (game / INGAME_SETTINGS).read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        return {}
    found = {}
    for line in text.splitlines():
        body = line.split("#", 1)[0]
        if "=" not in body:
            continue
        key, value = (part.strip() for part in body.split("=", 1))
        if key in INGAME_KEYS and value:
            found[key] = value
    return found


def merge_ingame_settings(values: dict[str, str], ingame: dict[str, str]) -> dict[str, str]:
    """Launcher values with each valid in-game override applied; invalid ones are skipped."""
    merged = dict(values)
    for key, value in ingame.items():
        try:
            merged = validate_settings({**merged, key: value})
        except ValueError:
            continue
    return merged


def retire_ingame_settings(game: Path, imported: dict[str, str]) -> bool:
    """After config.txt carries the imported values, drop the in-game file if it is unchanged."""
    if not imported or read_ingame_settings(game) != imported:
        return False
    try:
        (game / INGAME_SETTINGS).unlink()
    except OSError:
        return False
    return True


def visible(item: Setting, values: dict[str, str], advanced: bool) -> bool:
    """Whether the launcher shows a setting while the others hold these values."""
    if item.fixed is not None or (item.advanced and not advanced):
        return False
    return all(values.get(key) in allowed for key, allowed in item.when)


def user_values(values: dict[str, str]) -> dict[str, str]:
    """A value set as the launcher keeps it: retired options at their fixed values and retired
    choices replaced. Missing keys stay missing, so validate_settings still reports them."""
    result = dict(values)
    for key, replacements in _RETIRED_CHOICES.items():
        if isinstance(result.get(key), str):
            result[key] = replacements.get(result[key].strip(), result[key])
    for item in SETTINGS:
        if item.fixed is not None and item.key in result:
            result[item.key] = result.get(item.fixed, item.default) if item.fixed in BY_KEY else item.fixed
    return result


def coupled_changes(values: dict[str, str], key: str) -> dict[str, str]:
    """Settings that change with `key` so the set stays playable: physical walking needs
    positional tracking and the classic portal orientation."""
    walking = values.get("RoomscaleMode") == "ActiveExperimental"
    if key == "6DOF" and values.get(key) == "false" and walking:
        return {"RoomscaleMode": "Off"}
    if key == "PortalOrientationMode" and values.get(key) != "LegacyYaw" and walking:
        return {"RoomscaleMode": "Off"}
    if key == "RoomscaleMode" and walking:
        return {name: want for name, want in (("6DOF", "true"), ("PortalOrientationMode", "LegacyYaw"))
                if values.get(name) != want}
    return {}


def recommended_settings() -> dict[str, str]:
    # Reuse the tested profile's calibration rather than creating a second copy.
    from .test_launcher_core import BASE_VALUES, PROFILES
    values = {item.key: item.default for item in SETTINGS}
    values.update(BASE_VALUES)
    values.update(PROFILES["roomscale_active_experimental"].changes)
    return validate_settings(user_values(values))


def validate_settings(values: dict[str, str]) -> dict[str, str]:
    if not isinstance(values, dict):
        raise ValueError("Settings must be an object")
    keys = {item.key for item in SETTINGS}
    if values.keys() != keys:
        raise ValueError("Missing or unknown settings: " + ", ".join(sorted(keys ^ values.keys())))
    result = {}
    for item in SETTINGS:
        value = values[item.key]
        if not isinstance(value, str):
            raise ValueError(f"{item.key}: expected text")
        value = value.strip()
        if item.choices:
            if value not in item.choices:
                raise ValueError(f"{item.key}: choose {', '.join(item.choices)}")
        else:
            if not re.fullmatch(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?", value, re.ASCII):
                raise ValueError(f"{item.key}: enter a finite number (use a decimal point)")
            number = float(value)
            low, high = item.bounds
            if not math.isfinite(number) or not low <= number <= high:
                raise ValueError(f"{item.key}: must be between {low:g} and {high:g}")
            value = format(number, ".12g")
        result[item.key] = value
    if result["RoomscaleMode"] == "ActiveExperimental":
        if result["6DOF"] != "true" or result["PortalOrientationMode"] != "LegacyYaw":
            raise ValueError("ActiveExperimental roomscale requires 6DOF=true and PortalOrientationMode=LegacyYaw")
    if result["AimFromViewmodelMuzzle"] == "true" and result["ExperimentalViewmodelAlignment"] != "true":
        raise ValueError("AimFromViewmodelMuzzle requires ExperimentalViewmodelAlignment=true")
    if result["HUDInEyeCentered"] == "true" and (result["AimMode"] == "2" or result["ExperimentalHUDOverlay"] == "true"):
        raise ValueError("HUDInEyeCentered requires AimMode=0 or 1 and ExperimentalHUDOverlay=false")
    return result


def _validated_preferences(prefs: dict) -> dict:
    required = {"version", "game_dir", "steam_exe", "values"}
    # game_dirs: the folder chosen for each game by its key, kept while another game is selected.
    # ingame_taken: per game key, the Options > VR values the launcher already took from that game.
    if not isinstance(prefs, dict) or not required <= prefs.keys() <= required | {"game_dirs", "ingame_taken"}:
        raise ValueError("Invalid launcher preferences fields")
    taken = prefs.get("ingame_taken", {})
    if not isinstance(taken, dict) or not all(
            isinstance(game, str) and isinstance(values, dict) and
            all(isinstance(key, str) and isinstance(value, str) for key, value in values.items())
            for game, values in taken.items()):
        raise ValueError("Invalid launcher preferences in-game values")
    folders = prefs.get("game_dirs", {})
    if not isinstance(folders, dict) or not all(
            isinstance(key, str) and isinstance(path, str) and path.strip() and "\0" not in path
            for key, path in folders.items()):
        raise ValueError("Invalid launcher preferences game folders")
    if type(prefs["version"]) is not int or prefs["version"] != 1:
        raise ValueError("Unsupported launcher preferences version")
    for key in ("game_dir", "steam_exe"):
        if not isinstance(prefs[key], str) or not prefs[key].strip() or "\0" in prefs[key]:
            raise ValueError(f"Invalid launcher preferences path: {key}")
    return {**prefs, "values": validate_settings(prefs["values"])}


# The gun offsets the README told players to set by hand for the HD Collection's "Portal VR
# Gun Model" before the mod matched the grip to the loaded model by itself (v1.1.0). Kept,
# they would shift that gun twice and move every other gun away from the hand.
_HAND_SET_HD_GUN_OFFSETS = {"ViewmodelPosCustomOffsetX": -7.3, "ViewmodelPosCustomOffsetY": -8.2,
                            "ViewmodelPosCustomOffsetZ": 11.35}


def without_hand_set_hd_gun_offsets(values: dict) -> dict:
    """The values with that old hand-set calibration back at 0; any other offsets unchanged."""
    def matches(key: str, want: float) -> bool:
        try:
            return abs(float(values.get(key)) - want) < 1e-6
        except (TypeError, ValueError):
            return False
    if not all(matches(key, want) for key, want in _HAND_SET_HD_GUN_OFFSETS.items()):
        return values
    return {**values, **{key: "0" for key in _HAND_SET_HD_GUN_OFFSETS}}


def load_preferences(path: Path, legacy: dict) -> dict:
    if not path.exists():
        return _validated_preferences({"version": 1, "game_dir": legacy["game_dir"],
                                       "steam_exe": legacy["steam_exe"], "values": recommended_settings()})
    try:
        prefs = json.loads(path.read_text(encoding="utf-8"))
        # Upgrade only previous complete schemas, without writing on load.
        # Unknown, missing and partially upgraded schemas still fail validation.
        if isinstance(prefs, dict) and isinstance(prefs.get("values"), dict):
            all_keys = {item.key for item in SETTINGS}
            missing = all_keys - prefs["values"].keys()
            groups = [group for group in _ADDED_KEY_GROUPS if group & missing]
            if (prefs["values"].keys() <= all_keys and missing and all(group <= missing for group in groups)
                    and missing == frozenset().union(*groups)):
                prefs = {**prefs, "values": {**prefs["values"],
                         **{item.key: item.default for item in SETTINGS if item.key in missing}}}
            prefs = {**prefs, "values": without_hand_set_hd_gun_offsets(user_values(prefs["values"]))}
        return _validated_preferences(prefs)
    except (OSError, UnicodeError, ValueError) as error:
        raise ValueError(f"Cannot read launcher preferences {path}: {error}. The file was not changed.") from error


def save_preferences(path: Path, prefs: dict) -> None:
    validated = _validated_preferences(prefs)
    from .test_launcher_core import _transaction_lock
    with _transaction_lock(path):
        if path.exists():
            load_preferences(path, {})  # Never silently overwrite corrupted preferences.
        payload = (json.dumps(validated, indent=2) + "\n").encode("utf-8")
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(prefix=path.name + ".", suffix=".tmp",
                                             dir=path.parent, delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(payload)
            os.replace(temporary, path)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
