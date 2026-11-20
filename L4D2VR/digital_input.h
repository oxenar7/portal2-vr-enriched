#pragma once

// Balance Source +commands on OpenVR transitions, including tracking loss.
class DigitalButtonState
{
public:
    const char* HeldCommand(bool active, bool changed, bool pressed,
                            const char* pressCommand, const char* releaseCommand)
    {
        if (!active) {
            if (!m_Pressed) return nullptr;
            m_Pressed = false;
            return releaseCommand;
        }
        if (!changed || pressed == m_Pressed) return nullptr;
        m_Pressed = pressed;
        return pressed ? pressCommand : releaseCommand;
    }

    static bool PressEdge(bool active, bool changed, bool pressed)
    {
        return active && changed && pressed;
    }

private:
    bool m_Pressed = false;
};

// Portal 2's gesture key (Q by default, also the gamepad's d-pad up). Tapped, it gestures;
// held, it opens a wheel of gestures steered by the mouse, and the release performs the
// highlighted one (cl_fastradial). The end of the co-op campaign waits for each robot to
// gesture at a camera, so the Gesture action sends exactly what the key sends. Single
// player ignores it, like the key.
namespace CoopGesture {
inline constexpr const char *kPressCommand = "+mouse_menu_taunt";
inline constexpr const char *kReleaseCommand = "-mouse_menu_taunt";
}
