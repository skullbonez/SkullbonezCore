#include "UIStyle.h"

namespace SkullbonezCore
{
namespace UI
{
namespace Style
{
namespace
{
constexpr UIColor kAccentCyan = { 0.34f, 0.91f, 1.0f, 1.0f };
constexpr FooterToggleStyle kFooterToggle = { 10.5f, { 0.74f, 0.82f, 0.84f, 1.0f }, 28.0f, 14.0f, 10.0f, 10.0f };
} // namespace

const UIColor& AccentCyan()
{
    return kAccentCyan;
}


const FooterToggleStyle& FooterToggle()
{
    return kFooterToggle;
}

} // namespace Style
} // namespace UI
} // namespace SkullbonezCore
