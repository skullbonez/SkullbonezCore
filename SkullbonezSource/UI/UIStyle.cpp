/*
File: SkullbonezSource/UI/UIStyle.cpp
Purpose:
  Implements UI Style widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UIStyle.cpp implements UI Style widgets, layout, drawing, or UI state for
  the in-engine controls.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIStyle.h
  - Agentic/Reference/comment-style-guide.md
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
    { 0.153f, 0.157f, 0.153f, 0.86f }, // window
    { 0.200f, 0.204f, 0.192f, 0.92f }, // windowRaised
    { 0.125f, 0.129f, 0.122f, 0.78f }, // windowSubtle
    { 0.220f, 0.231f, 0.216f, 0.90f }, // control
    { 0.263f, 0.275f, 0.247f, 0.94f }, // controlHover
    { 0.953f, 0.953f, 0.941f, 1.00f }, // textPrimary
    { 0.725f, 0.737f, 0.722f, 1.00f }, // textSecondary
    { 0.561f, 0.580f, 0.561f, 1.00f }, // textMuted
    { 1.000f, 1.000f, 1.000f, 0.12f }, // border
    { 1.000f, 1.000f, 1.000f, 0.08f }, // innerBorder
    { 1.000f, 1.000f, 1.000f, 0.10f }, // lineSoft
    { 0.000f, 0.000f, 0.000f, 0.30f }, // shadow
    { 0.604f, 0.647f, 0.561f, 1.00f }, // accent
    { 0.843f, 0.863f, 0.812f, 1.00f }, // accentStrong
    { 0.710f, 0.624f, 0.482f, 1.00f }  // warningAccent
};
constexpr UIRadii kRadii = { 12.0f, 8.0f, 7.0f, 999.0f };
constexpr UITextStyle kText = { 13.0f, 11.0f, 10.5f, 13.0f, 10.5f };
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
