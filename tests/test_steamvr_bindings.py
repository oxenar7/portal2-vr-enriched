"""Default SteamVR bindings: the co-op gesture button."""
import json
import unittest
from collections import defaultdict
from pathlib import Path

MANIFEST_DIR = Path(__file__).resolve().parent.parent / "L4D2VR" / "SteamVRActionManifest"
GESTURE = "/actions/main/in/Gesture"


def load(name):
    return json.loads((MANIFEST_DIR / name).read_text(encoding="utf-8"))


def default_bindings():
    manifest = load("action_manifest.json")
    return {entry["controller_type"]: load(entry["binding_url"]) for entry in manifest["default_bindings"]}


def outputs_by_button(binding):
    """(device input path, slot) -> set of short action names bound there."""
    buttons = defaultdict(set)
    for source in binding["bindings"]["/actions/main"]["sources"]:
        for slot, target in source["inputs"].items():
            buttons[(source["path"], slot)].add(target["output"].rsplit("/", 1)[-1])
    return buttons


class GestureBindingTests(unittest.TestCase):
    def test_manifest_declares_optional_localized_gesture(self):
        manifest = load("action_manifest.json")
        actions = {action["name"]: action for action in manifest["actions"]}
        self.assertIn(GESTURE, actions)
        self.assertEqual(actions[GESTURE]["type"], "boolean")
        # Saved custom bindings without it must still load.
        self.assertEqual(actions[GESTURE].get("requirement"), "optional")
        self.assertTrue(manifest["localization"][0].get(GESTURE))

    def test_every_default_binding_puts_gesture_alone_on_a_left_face_button(self):
        bindings = default_bindings()
        self.assertEqual(set(bindings), {"oculus_touch", "knuckles", "vive_cosmos_controller"})
        expected = {"oculus_touch": "/user/hand/left/input/x", "knuckles": "/user/hand/left/input/a",
                    "vive_cosmos_controller": "/user/hand/left/input/x"}
        for controller, binding in bindings.items():
            with self.subTest(controller=controller):
                buttons = outputs_by_button(binding)
                places = [button for button, outputs in buttons.items() if "Gesture" in outputs]
                self.assertEqual(places, [(expected[controller], "click")])
                # Left-handed mode and swapped sticks never move another action onto it,
                # and the unused Scoreboard action no longer sits there.
                self.assertEqual(buttons[places[0]], {"Gesture"})
                self.assertFalse(any("Scoreboard" in outputs for outputs in buttons.values()))


if __name__ == "__main__":
    unittest.main()
