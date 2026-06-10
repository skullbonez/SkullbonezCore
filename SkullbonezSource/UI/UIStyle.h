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

const UIColor& AccentCyan();
const FooterToggleStyle& FooterToggle();

} // namespace Style
} // namespace UI
} // namespace SkullbonezCore
