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
// The approved Canvas, Editor and Solver Lab mockups share blue-gray chrome.
// Keep byte colors here so rendered samples can be compared with the references.
constexpr UIColor Rgb( unsigned int rgb, float alpha = 1.0f )
{
    return { static_cast<float>( ( rgb >> 16 ) & 255 ) / 255.0f, static_cast<float>( ( rgb >> 8 ) & 255 ) / 255.0f,
             static_cast<float>( rgb & 255 ) / 255.0f, alpha };
}
constexpr UIPalette kPalette = {
    Rgb( 0x172029, 0.96f ), // window
    Rgb( 0x1F2A35, 0.98f ), // windowRaised
    Rgb( 0x121A22, 0.94f ), // windowSubtle
    Rgb( 0x202B36, 0.98f ), // control
    Rgb( 0x2B3B4A ),        // controlHover
    Rgb( 0x163958 ),        // selection
    Rgb( 0xE8F0F8 ),        // textPrimary
    Rgb( 0xAFC2D3 ),        // textSecondary
    Rgb( 0x7D92A2 ),        // textMuted
    Rgb( 0x58748A, 0.50f ), // border
    Rgb( 0x58748A, 0.35f ), // innerBorder
    Rgb( 0x58748A, 0.30f ), // lineSoft
    Rgb( 0x000000, 0.30f ), // shadow
    Rgb( 0x0091F5 ),        // accent
    Rgb( 0x2AC8F0 ),        // accentStrong
    Rgb( 0xF4A645 )         // warningAccent
};
constexpr UIRadii kRadii = { 5.0f, 4.0f, 4.0f, 999.0f };
constexpr UIControlStyle kControl = { 6.0f, 30.0f, 16.0f };
constexpr FooterToggleStyle kFooterToggle = { 10.5f, kPalette.textSecondary, 30.0f, 16.0f, 10.0f, 10.0f };
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
