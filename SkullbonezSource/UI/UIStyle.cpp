// Immutable theme definitions; the UI thread selects one shared palette.
#include "UIStyle.h"

namespace SkullbonezCore
{
namespace UI
{
namespace Style
{
namespace
{
// Byte colours keep each theme readable as data and comparable with screenshots.
constexpr UIColor Rgb( unsigned int rgb, float alpha = 1.0f )
{
    return { static_cast<float>( ( rgb >> 16 ) & 255 ) / 255.0f, static_cast<float>( ( rgb >> 8 ) & 255 ) / 255.0f, static_cast<float>( rgb & 255 ) / 255.0f, alpha };
}
struct ThemeDefinition
{
    const char* name;
    UIPalette palette;
};
// Each row uses the role order declared in UIPalette. No widget-specific themes.
constexpr ThemeDefinition themes[] = { { "Blue",
                                         {
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
                                             Rgb( 0xF4A645 ),        // warningAccent
                                             Rgb( 0x2AC8F0 )         // toggleKnob
                                         } }, { "Dark", { Rgb( 0x1B1B1D, .98f ),
                                                   Rgb( 0x262629 ),
                                                   Rgb( 0x141416, .98f ),
                                                   Rgb( 0x303034 ),
                                                   Rgb( 0x3D3D42 ),
                                                   Rgb( 0x344353 ),
                                                   Rgb( 0xF1F1F3 ),
                                                   Rgb( 0xC4C4CA ),
                                                   Rgb( 0x9999A3 ),
                                                   Rgb( 0x777780, .60f ),
                                                   Rgb( 0x777780, .40f ),
                                                   Rgb( 0x777780, .30f ),
                                                   Rgb( 0x000000, .40f ),
                                                   Rgb( 0x62B4F5 ),
                                                   Rgb( 0xA5D6FF ),
                                                   Rgb( 0xF4B85F ),
                                                   Rgb( 0xA5D6FF ) } }, { "Light", { Rgb( 0xF1F3F6 ),
                                                    Rgb( 0xFFFFFF ),
                                                    Rgb( 0xE7EBF0 ),
                                                    Rgb( 0xFFFFFF ),
                                                    Rgb( 0xE2E9F1 ),
                                                    Rgb( 0xD2E7FA ),
                                                    Rgb( 0x182330 ),
                                                    Rgb( 0x384D62 ),
                                                    Rgb( 0x596A7B ),
                                                    Rgb( 0x677D94, .65f ),
                                                    Rgb( 0x677D94, .45f ),
                                                    Rgb( 0x677D94, .30f ),
                                                    Rgb( 0x162A40, .18f ),
                                                    Rgb( 0x0069AD ),
                                                    Rgb( 0x005A82 ),
                                                    Rgb( 0x925000 ),
                                                    Rgb( 0xFFFFFF ),
                                                    .55f } } };
static_assert( sizeof( themes ) / sizeof( themes[0] ) == static_cast<unsigned>( Theme::Count ) );
constexpr unsigned ThemeIndex( Theme theme )
{
    return theme < Theme::Count ? static_cast<unsigned>( theme ) : 0u;
}
Theme currentTheme = Theme::Blue;
// Lifetime: copy into stable storage so presenters retaining role references see
// changes together. Selection happens during UI input, before draw composition.
constinit UIPalette activePalette = themes[0].palette;
constexpr UIRadii kRadii = { 5.0f, 4.0f, 4.0f, 999.0f };
constexpr UIControlStyle kControl = { 6.0f, 30.0f, 16.0f };
FooterToggleStyle kFooterToggle = { 10.5f, themes[0].palette.textSecondary, 30.0f, 16.0f, 10.0f, 10.0f };
} // namespace

Theme CurrentTheme()
{
    return currentTheme;
}
const char* ThemeName( Theme theme )
{
    return themes[ThemeIndex( theme )].name;
}
const UIPalette& Palette( Theme theme )
{
    return themes[ThemeIndex( theme )].palette;
}
void SelectTheme( Theme theme )
{
    currentTheme = static_cast<Theme>( ThemeIndex( theme ) );
    activePalette = Palette( currentTheme );
    kFooterToggle.label = activePalette.textSecondary;
}

const UIPalette& Palette()
{
    return activePalette;
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
    return activePalette.accent;
}


const FooterToggleStyle& FooterToggle()
{
    return kFooterToggle;
}

} // namespace Style
} // namespace UI
} // namespace SkullbonezCore
