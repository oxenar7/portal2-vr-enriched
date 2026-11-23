#pragma once

#include "vector.h"

// First five methods of Valve's VDebugOverlay004 interface. Keep the ABI
// subset in vtable order; this interface is optional in Portal2VR.
class IVDebugOverlay
{
public:
    virtual void AddEntityTextOverlay(int entityIndex, int lineOffset, float duration,
                                      int r, int g, int b, int a, const char *format, ...) = 0;
    virtual void AddBoxOverlay(const Vector &origin, const Vector &mins, const Vector &maxs,
                               const QAngle &orientation, int r, int g, int b, int a,
                               float duration) = 0;
    virtual void AddSphereOverlay(const Vector &origin, float radius, int theta, int phi,
                                  int r, int g, int b, int a, float duration) = 0;
    virtual void AddTriangleOverlay(const Vector &a, const Vector &b, const Vector &c,
                                    int r, int g, int blue, int alpha,
                                    bool noDepthTest, float duration) = 0;
    virtual void AddLineOverlay(const Vector &origin, const Vector &dest,
                                int r, int g, int b, bool noDepthTest,
                                float duration) = 0;
};

// VDebugOverlay004 slot 16, ClearAllOverlays: deletes every line, triangle, box and text
// overlay. In the engine.dll of Portal 2, Alive & Kicking, Mel, Time Machine and Reloaded it
// empties the list AddLineOverlay appends to (checked by RTTI, 2026-11-23).
inline void ClearAllDebugOverlays(IVDebugOverlay *overlay)
{
    using ClearAll = void(__thiscall *)(IVDebugOverlay *);
    (*reinterpret_cast<ClearAll **>(overlay))[16](overlay);
}
