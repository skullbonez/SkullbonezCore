// Shared colour roles and fixed geometry for the operator UI.
#pragma once

namespace SkullbonezCore
{
namespace UI
{
namespace Style
{

struct UIColor
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct FooterToggleStyle
{
    float labelTextSize = 10.5f;
    UIColor label;
    float switchW = 28.0f;
    float switchH = 14.0f;
    float knobW = 10.0f;
    float knobH = 10.0f;
};

struct UIPalette
{
    UIColor window;
    UIColor windowRaised;
    UIColor windowSubtle;
    UIColor control;
    UIColor controlHover;
    UIColor selection;
    UIColor textPrimary;
    UIColor textSecondary;
    UIColor textMuted;
    UIColor border;
    UIColor innerBorder;
    UIColor lineSoft;
    UIColor shadow;
    UIColor accent;
    UIColor accentStrong;
    UIColor warningAccent;
    UIColor toggleKnob;
    // Darken diagnostic series colours on pale tables without changing their hue.
    float dataInkScale = 1.0f;
};

struct UIRadii
{
    float window = 12.0f;
    float control = 8.0f;
    float smallButton = 7.0f;
    float switchPill = 999.0f;
};

struct UIControlStyle
{
    float sliderTrackHeight = 6.0f;
    float switchW = 30.0f;
    float switchH = 16.0f;
};

// Stable IDs are persisted; append new themes without renumbering existing ones.
enum class Theme : unsigned char
{
    Blue,
    Dark,
    Light,
    Count
};
Theme CurrentTheme();
const char* ThemeName( Theme theme );
const UIPalette& Palette( Theme theme );
// UI-thread only. References to the active palette remain valid across selection.
void SelectTheme( Theme theme );
const UIPalette& Palette();
const UIRadii& Radii();
const UIControlStyle& Control();
const UIColor& Accent();
const FooterToggleStyle& FooterToggle();

} // namespace Style
} // namespace UI
} // namespace SkullbonezCore
