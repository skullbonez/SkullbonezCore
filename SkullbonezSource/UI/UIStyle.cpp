/*
File: SkullbonezSource/UI/UIStyle.cpp
Purpose:
  Implements the immutable palette, radii, typography, spacing, and control-
  style values.

Summary:
  Owns immutable palette, radii, typography,
  control, accent, and footer-toggle values.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIStyle.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UIStyle.h"

namespace SkullbonezCore
{
namespace UI
{
namespace Style
{
namespace
{
constexpr UIPalette kPalette = {
    { 0.075f, 0.080f, 0.090f, 0.86f }, // window
    { 0.130f, 0.140f, 0.160f, 0.92f }, // windowRaised
    { 0.095f, 0.102f, 0.115f, 0.90f }, // windowSubtle
    { 0.180f, 0.190f, 0.210f, 0.96f }, // control
    { 0.240f, 0.250f, 0.275f, 0.98f }, // controlHover
    { 0.920f, 0.930f, 0.945f, 1.00f }, // textPrimary
    { 0.730f, 0.745f, 0.765f, 1.00f }, // textSecondary
    { 0.550f, 0.570f, 0.600f, 1.00f }, // textMuted
    { 1.000f, 1.000f, 1.000f, 0.12f }, // border
    { 1.000f, 1.000f, 1.000f, 0.08f }, // innerBorder
    { 1.000f, 1.000f, 1.000f, 0.10f }, // lineSoft
    { 0.000f, 0.000f, 0.000f, 0.30f }, // shadow
    { 0.604f, 0.647f, 0.561f, 1.00f }, // accent
    { 0.843f, 0.863f, 0.812f, 1.00f }, // accentStrong
    { 0.710f, 0.624f, 0.482f, 1.00f }  // warningAccent
};
constexpr UIRadii kRadii = { 5.0f, 4.0f, 4.0f, 999.0f };
constexpr UIControlStyle kControl = { 6.0f, 30.0f, 16.0f };
constexpr FooterToggleStyle kFooterToggle = { 10.5f, { 0.725f, 0.737f, 0.722f, 1.0f }, 30.0f, 16.0f, 10.0f, 10.0f };
} // namespace

const UIPalette& Palette()
{
    return kPalette;
}


const UIRadii& Radii()
{
    return kRadii;
}


const UIControlStyle& Control()
{
    return kControl;
}


const UIColor& Accent()
{
    return kPalette.accent;
}


const FooterToggleStyle& FooterToggle()
{
    return kFooterToggle;
}

} // namespace Style
} // namespace UI
} // namespace SkullbonezCore
