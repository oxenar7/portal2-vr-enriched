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


# Match ConfigSnapshot / ParseConfig in L4D2VR/config.h. Labels are presentation only.
SETTINGS = (
    Setting("TrackingMode", "Tracking mode", "Movement", "Seated", ("Seated", "Standing"),
            help="Seated uses a recentered eye height; Standing uses the SteamVR floor."),
    Setting("MovementDirection", "Stick movement relative to", "Movement", "HMD",
            ("HMD", "LeftController", "RightController"), labels=("Headset", "Left controller", "Right controller")),
    Setting("RoomscaleMode", "Physical walking", "Movement", "Off", ("Off", "Observe", "ActiveExperimental"),
            help="On: your real steps move you in the game, with collisions. Requires positional head tracking "
                 "and LegacyYaw portal orientation.",
            labels=("Off", "Log only (diagnostics)", "On")),
    Setting("SnapTurning", "Snap turning", "Movement", "false", ("false", "true"),
            help="Off uses smooth turning; on uses the angle below."),
    Setting("SnapTurnAngle", "Snap angle (degrees)", "Movement", "45", bounds=(1, 180)),
    Setting("TurnSpeed", "Smooth turn speed", "Movement", "0.15", bounds=(.01, 2)),
    Setting("PhysicalCrouch", "Physical crouch", "Movement", "true", ("false", "true"),
            help="Duck when your real head drops below your standing height. Requires positional head tracking."),
    Setting("PhysicalCrouchDropMeters", "Crouch head drop (m)", "Movement", "0.25", bounds=(.15, .8),
            help="How far the head must drop to duck; you stand up 5 cm earlier."),
    Setting("RenderWindow", "Extra view on the monitor", "Rendering", "0", ("0", "1"),
            help="Draws a third view for the desktop window. Costs performance; off is recommended."),
    Setting("6DOF", "Positional head tracking", "Tracking", "true", ("false", "true")),
    Setting("HeightOffsetMeters", "Standing height offset (m)", "Tracking", "0", bounds=(-.5, .5),
            help="Standing only; applied when recentering."),
    Setting("ControllerPitchDegrees", "Controller pitch correction (degrees)", "Tracking", "-30", bounds=(-60, 60)),
    Setting("LeftHanded", "Left-handed controls", "Tracking", "false", ("false", "true"),
            help="Swaps the hands: the gun goes in the left hand. Not tested yet."),
    Setting("VRScale", "Source units per real-world metre", "Tracking", "43.2", bounds=(1, 200)),
    Setting("IPDScale", "Interpupillary distance multiplier", "Tracking", "1", bounds=(.5, 1.5)),
    Setting("PortalOrientationMode", "Portal orientation", "Portals", "LegacyYaw",
            ("LegacyYaw", "FullRotation", "YawOnly", "PreserveHorizon"),
            help="LegacyYaw is recommended. Other modes are experimental; floor/ceiling comfort remains unverified."),
    Setting("AimMode", "Aim mode", "Aiming", "2", ("0", "1", "2"),
            help="Beam and crosshair is recommended. Legacy crosshair has known limitations.",
            labels=("None", "Legacy crosshair", "Beam and crosshair")),
    Setting("ShowSourceCrosshair", "Show legacy flat crosshair", "Aiming", "true", ("false", "true"),
            help="Only with aim mode None or Legacy crosshair. Portal 2's portal status artwork stays visible."),
    Setting("ExperimentalWorldAimMarker", "World aim line", "Aiming", "false", ("false", "true"),
            help="A line from the gun to where the portal will land. Off falls back to Portal 2's own beam, "
                 "which is currently invisible."),
    Setting("ExperimentalStereoReticle", "Atlas reticle rollback", "Aiming", "false", ("false", "true"),
            help="Leave off to keep Portal 2's own crosshair artwork. On uses an older experimental crosshair."),
    Setting("ReticleDistanceScaling", "Gentle reticle distance scaling", "Aiming", "true", ("false", "true"),
            help="The crosshair shrinks slightly with distance: full size at 1 m, 80% at 10 m."),
    Setting("LaserCubeAimAssist", "Laser cube aim assist", "Aiming", "true", ("false", "true"),
            help="Steers a laser leaving a reflector cube onto a catcher, as in co-op."),
    Setting("HeldObjectAnchor", "Attach held objects to the gun", "Aiming", "true", ("false", "true"),
            help="Held objects move with the gun and turn with the wrist. Walls still stop them."),
    Setting("HeldObjectColumn", "Held object vertical plane", "Aiming", "false", ("false", "true"),
            help="Only with the gun attachment off. On is Portal 2's default."),
    Setting("ExperimentalViewmodelAlignment", "Controller-locked gun and effects", "Gun calibration", "false", ("false", "true")),
    Setting("AimFromViewmodelMuzzle", "Start aim line at animated gun muzzle", "Gun calibration", "false", ("false", "true"),
            help="Requires the controller-locked gun. When the muzzle position is unknown, the line is hidden."),
    Setting("ViewmodelPosCustomOffsetX", "Gun X position (Source units)", "Gun calibration", "0", bounds=(-100, 100)),
    Setting("ViewmodelPosCustomOffsetY", "Gun Y position (Source units)", "Gun calibration", "0", bounds=(-100, 100)),
    Setting("ViewmodelPosCustomOffsetZ", "Gun Z position (Source units)", "Gun calibration", "0", bounds=(-100, 100)),
    Setting("ViewmodelAngCustomOffsetX", "Gun X angle (degrees)", "Gun calibration", "0", bounds=(-180, 180)),
    Setting("ViewmodelAngCustomOffsetY", "Gun Y angle (degrees)", "Gun calibration", "0", bounds=(-180, 180)),
    Setting("ViewmodelAngCustomOffsetZ", "Gun Z angle (degrees)", "Gun calibration", "0", bounds=(-180, 180)),
    Setting("ExperimentalHUDOverlay", "Readable caption overlay", "Captions", "false", ("false", "true"),
            help="Shows subtitles on a readable panel in front of you. Requires aim mode Beam and crosshair, "
                 "and subtitles turned on in Portal 2."),
    Setting("HUDInEyeCentered", "Center legacy HUD in eyes", "Legacy HUD", "false", ("false", "true"),
            help="Experimental; restart the game after changing it. Requires aim mode None or Legacy crosshair "
                 "and the caption overlay off. Keeps the original layout if the HUD does not fit."),
    Setting("HUDInEyeScale", "Legacy HUD width fraction", "Legacy HUD", "0.45", bounds=(.2, 1)),
    Setting("HUDInEyeVerticalOffset", "Legacy HUD vertical offset fraction", "Legacy HUD", "0.05", bounds=(-.4, .4),
            help="Positive moves down. Applies only with legacy HUD centering enabled."),
    Setting("HUDDistanceMeters", "Caption distance (m)", "Captions", "1.3", bounds=(.6, 3)),
    Setting("HUDWidthMeters", "Caption panel width (m)", "Captions", "1.4", bounds=(.5, 2.5)),
    Setting("HUDVerticalOffsetMeters", "Caption height relative to eyes (m)", "Captions", "-0.15", bounds=(-.6, .6),
            help="Positive raises the panel; negative lowers it."),
    Setting("HUDLazyFollow", "Captions follow the head lazily", "Captions", "true", ("false", "true"),
            help="Captions follow your gaze, sideways and up or down, once you look past the deadzone. Off locks them to the headset."),
    Setting("HUDFollowDeadzoneDegrees", "Caption follow deadzone (degrees)", "Captions", "10", bounds=(0, 60)),
    Setting("HUDFollowSpeed", "Caption follow speed", "Captions", "8", bounds=(1, 20),
            help="How fast the captions catch up with your gaze. Higher is quicker."),
    Setting("HUDCaptionOpacityBoost", "Caption background opacity boost", "Captions", "3", bounds=(1, 4),
            help="Makes the caption box darker over bright scenes. 1 keeps the game's own transparency."),
    Setting("BinocularZoom", "Binocular zoom", "Zoom", "true", ("false", "true"),
            help="Hold the right stick in to look through binoculars. Also completes Portal 2's zoom lesson."),
    Setting("ZoomMagnification", "Zoom magnification", "Zoom", "3", bounds=(1.5, 8),
            help="How much larger things look through the binoculars."),
    Setting("ZoomWindowWidthMeters", "Binocular window width (m)", "Zoom", "0.8", bounds=(.3, 1.5)),
    Setting("ZoomDistanceMeters", "Binocular window distance (m)", "Zoom", "1", bounds=(.5, 3),
            help="How far from your eyes the binocular window floats."),
    Setting("ExperimentalPortalShotHaptics", "Portal shot vibration", "Haptics", "false", ("false", "true")),
    Setting("PortalShotHapticAmplitude", "Vibration strength", "Haptics", "0.35", bounds=(0, 1)),
    Setting("PortalShotHapticDurationSeconds", "Vibration duration (seconds)", "Haptics", "0.05", bounds=(.01, .15)),
    Setting("Haptics", "Controller vibration", "Haptics", "true", ("false", "true"),
            help="Native game rumble, held objects and buttons. Portal shots use the native rumble below."),
    Setting("HapticsIntensity", "Controller vibration strength", "Haptics", "1", bounds=(0, 1)),
    Setting("HapticsNativeRumble", "Game rumble (shots, landings, impacts)", "Haptics", "true", ("false", "true"),
            help="Forwards Portal 2's own rumble effects; replaces the experimental portal shot vibration."),
    Setting("HapticsHeldObject", "Held object vibration", "Haptics", "true", ("false", "true"),
            help="Pulse on pickup and drop, light vibration while holding."),
    Setting("HapticsButtons", "Button vibration", "Haptics", "true", ("false", "true"),
            help="Pedestal buttons pressed by you and floor buttons."),
    Setting("AntiAliasing", "Mod anti-aliasing", "Rendering", "0", ("0", "2", "4", "8"),
            help="Off is recommended. Higher values smooth edges but cost performance.",
            labels=("Off", "2x", "4x", "8x")),
    Setting("VerboseDiagnostics", "Verbose diagnostics", "Diagnostics", "false", ("false", "true"),
            help="Writes much more detail to the mod log, for bug reports. Off still logs startup and errors."),
)
BASIC_KEYS = frozenset(("TrackingMode", "MovementDirection", "RoomscaleMode", "SnapTurning",
                        "SnapTurnAngle", "TurnSpeed", "PhysicalCrouch", "RenderWindow"))
_EYE_HUD_KEYS = frozenset(("HUDInEyeCentered", "HUDInEyeScale", "HUDInEyeVerticalOffset", "ShowSourceCrosshair"))
_HAPTICS_KEYS = frozenset(("Haptics", "HapticsIntensity", "HapticsNativeRumble", "HapticsHeldObject", "HapticsButtons"))
_CROUCH_KEYS = frozenset(("PhysicalCrouch", "PhysicalCrouchDropMeters"))
_HELD_KEYS = frozenset(("LaserCubeAimAssist", "HeldObjectColumn"))
_HELD_ANCHOR_KEYS = frozenset(("HeldObjectAnchor",))
_ZOOM_KEYS = frozenset(("BinocularZoom", "ZoomMagnification", "ZoomWindowWidthMeters", "ZoomDistanceMeters"))
# Key groups added after the first complete schema. A saved schema missing whole groups gains their defaults.
_ADDED_KEY_GROUPS = (_EYE_HUD_KEYS, _HAPTICS_KEYS, _CROUCH_KEYS, _HELD_KEYS, _HELD_ANCHOR_KEYS, _ZOOM_KEYS)


# Options > VR in the game writes these keys to VR/ingame_settings.txt, which the mod
# layers over config.txt. Match VrSettings::Pages() in L4D2VR/vr_settings.h.
INGAME_SETTINGS = "VR/ingame_settings.txt"
INGAME_KEYS = frozenset((
    "SnapTurning", "SnapTurnAngle", "TurnSpeed", "MovementDirection", "PhysicalCrouch", "LeftHanded",
    "Haptics", "HapticsIntensity", "ExperimentalWorldAimMarker", "6DOF", "ControllerPitchDegrees",
    "HeightOffsetMeters", "PhysicalCrouchDropMeters", "VRScale", "IPDScale", "HapticsNativeRumble", "HapticsHeldObject",
    "HapticsButtons", "ReticleDistanceScaling", "LaserCubeAimAssist", "HeldObjectAnchor", "RenderWindow"))


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


def recommended_settings() -> dict[str, str]:
    # Reuse the tested profile's calibration rather than creating a second copy.
    from .test_launcher_core import BASE_VALUES, PROFILES
    values = {item.key: item.default for item in SETTINGS}
    values.update(BASE_VALUES)
    values.update(PROFILES["roomscale_active_experimental"].changes)
    return validate_settings(values)


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
    if not isinstance(prefs, dict) or prefs.keys() != {"version", "game_dir", "steam_exe", "values"}:
        raise ValueError("Invalid launcher preferences fields")
    if type(prefs["version"]) is not int or prefs["version"] != 1:
        raise ValueError("Unsupported launcher preferences version")
    for key in ("game_dir", "steam_exe"):
        if not isinstance(prefs[key], str) or not prefs[key].strip() or "\0" in prefs[key]:
            raise ValueError(f"Invalid launcher preferences path: {key}")
    return {**prefs, "values": validate_settings(prefs["values"])}


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
            if prefs["values"].keys() <= all_keys and missing and                     all(group <= missing for group in groups) and missing == frozenset().union(*groups):
                prefs = {**prefs, "values": {**prefs["values"],
                         **{item.key: item.default for item in SETTINGS if item.key in missing}}}
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
