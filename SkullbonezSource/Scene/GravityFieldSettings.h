#pragma once
namespace SkullbonezCore::Scene
{
// Level-authored display values; height is a world-space vertical offset.
struct GravityFieldSettings
{
    float height = 0.0f;
    float opacity = 1.0f;
    int color = 0;          // Blue, orange, grey.
    bool snapBalls = false; // Presentation only; never constrains the solver.
};
} // namespace SkullbonezCore::Scene
