/*
File: SkullbonezSource/UI/UIDrawWidgets.cpp
Purpose:
  Implements stateless component geometry, value, hit-testing, and drawing
  contracts shared by UI presenters.

Summary:
  Caller-resolved geometry and visual-state flags select immutable foundation
  styles and append ordered draw values. Component helpers never sample input
  or retain product, Runtime, or renderer authority between calls.

Invariants:
  - A component's draw and hit helpers consume the same UIRect value.
  - Hidden controls append no commands; disabled controls remain pointer-
    blocking but cannot activate.

Related:
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UIDrawWidgets.h"
#include "UIFontMetrics.h"
#include "UICheckBox.h"
#include "UIStyle.h"

#include <algorithm>
#include <cmath>

namespace SkullbonezCore
{
namespace UI
{
namespace Widgets
{
namespace
{
constexpr float COMBO_LABEL_WIDTH = 66.0f;
constexpr float COMBO_FIELD_Y = 4.0f;
constexpr float COMBO_FIELD_HEIGHT = 18.0f;
constexpr float COMBO_POPUP_GAP = 4.0f;
constexpr float COMBO_OPTION_HEIGHT = 20.0f;

bool IsVisible( UIVisualState state )
{
    return HasVisualState( state, UIVisualState::Visible );
}


bool IsEnabled( UIVisualState state )
{
    return HasVisualState( state, UIVisualState::Enabled );
}


Style::UIColor WithAlpha( const Style::UIColor& color, float alpha )
{
    return { color.r, color.g, color.b, alpha };
}


Style::UIColor ControlFill( UIVisualState state )
{
    const Style::UIPalette& palette = Style::Palette();

    if ( !IsEnabled( state ) )
    {
        return WithAlpha( palette.windowSubtle, 0.58f );
    }

    if ( HasVisualState( state, UIVisualState::Active ) )
    {
        return WithAlpha( palette.accent, 0.74f );
    }

    if ( HasVisualState( state, UIVisualState::Checked ) )
    {
        return WithAlpha( palette.accent, 0.58f );
    }

    if ( HasVisualState( state, UIVisualState::Selected ) )
    {
        return palette.windowRaised;
    }

    return HasVisualState( state, UIVisualState::Hovered ) ? palette.controlHover : palette.control;
}


Style::UIColor ControlBorder( UIVisualState state )
{
    const Style::UIPalette& palette = Style::Palette();

    if ( !IsEnabled( state ) )
    {
        return WithAlpha( palette.border, 0.05f );
    }

    if ( HasVisualState( state, UIVisualState::Focused ) )
    {
        return WithAlpha( palette.accentStrong, 0.82f );
    }

    if ( HasVisualState( state, UIVisualState::Selected ) )
    {
        return WithAlpha( palette.accent, 0.68f );
    }

    if ( HasVisualState( state, UIVisualState::Checked ) )
    {
        return WithAlpha( palette.accent, 0.52f );
    }

    return HasVisualState( state, UIVisualState::Hovered ) ? palette.innerBorder : palette.border;
}


Style::UIColor ControlText( UIVisualState state )
{
    const Style::UIPalette& palette = Style::Palette();

    if ( !IsEnabled( state ) )
    {
        return palette.textMuted;
    }

    return HasVisualState( state, UIVisualState::Hovered ) || HasVisualState( state, UIVisualState::Focused ) ||
                   HasVisualState( state, UIVisualState::Active ) || HasVisualState( state, UIVisualState::Selected ) ||
                   HasVisualState( state, UIVisualState::Checked )
               ? palette.textPrimary
               : palette.textSecondary;
}


const char* SafeText( const char* text )
{
    return text ? text : "";
}


void DrawChevronGlyph( const UIDrawContext& draw, const UIRect& bounds, ComponentIcon icon, const Style::UIColor& color )
{
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;

    switch ( icon )
    {
    case ComponentIcon::ChevronLeft:
        draw.Triangle( cx - 4.0f, cy, cx + 3.0f, cy - 5.0f, cx + 3.0f, cy + 5.0f, color.r, color.g, color.b, color.a );
        break;
    case ComponentIcon::ChevronRight:
        draw.Triangle( cx + 4.0f, cy, cx - 3.0f, cy - 5.0f, cx - 3.0f, cy + 5.0f, color.r, color.g, color.b, color.a );
        break;
    case ComponentIcon::ChevronUp:
        draw.Triangle( cx, cy - 4.0f, cx - 5.0f, cy + 3.0f, cx + 5.0f, cy + 3.0f, color.r, color.g, color.b, color.a );
        break;
    case ComponentIcon::ChevronDown:
        draw.Triangle( cx, cy + 4.0f, cx - 5.0f, cy - 3.0f, cx + 5.0f, cy - 3.0f, color.r, color.g, color.b, color.a );
        break;
    default:
        break;
    }
}
} // namespace

bool ContainsComponent( const UIRect& bounds, UIVisualState state, int pointerX, int pointerY )
{
    return IsVisible( state ) && bounds.Contains( pointerX, pointerY );
}


bool CanActivateComponent( const UIRect& bounds, UIVisualState state, int pointerX, int pointerY )
{
    return IsEnabled( state ) && ContainsComponent( bounds, state, pointerX, pointerY );
}


void DrawPanel( const UIDrawContext& draw, const UIRect& bounds, UIVisualState state )
{
    if ( !IsVisible( state ) )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    Style::UIColor fill = palette.window;

    if ( !IsEnabled( state ) )
    {
        fill = WithAlpha( palette.windowSubtle, 0.52f );
    }
    else if ( HasVisualState( state, UIVisualState::Active ) )
    {
        fill = WithAlpha( palette.accent, 0.24f );
    }
    else if ( HasVisualState( state, UIVisualState::Selected ) )
    {
        fill = palette.windowRaised;
    }
    else if ( HasVisualState( state, UIVisualState::Checked ) )
    {
        fill = WithAlpha( palette.accent, 0.16f );
    }
    else if ( HasVisualState( state, UIVisualState::Hovered ) )
    {
        fill = WithAlpha( palette.controlHover, 0.76f );
    }

    draw.RoundedPanel( bounds, Style::Radii().window, fill, ControlBorder( state ) );
}


void DrawLabelValueRow( const UIDrawContext& draw, const UIRect& bounds, const char* label, const char* value,
                        const Style::UIColor& valueColor, UIVisualState state )
{
    if ( !IsVisible( state ) )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    const float textSize = 10.5f;
    const char* safeValue = SafeText( value );
    const float valueWidth = UIFontMetrics::MeasureText( textSize, safeValue );
    const Style::UIColor labelColor = ControlText( state );
    const Style::UIColor resolvedValueColor = IsEnabled( state ) ? valueColor : palette.textMuted;

    // Invariant: every visible row records one balanced pair. No return is
    // permitted between these calls because an unmatched clip poisons the
    // remaining ordered draw stream.
    draw.PushClip( bounds );

    if ( HasVisualState( state, UIVisualState::Selected ) || HasVisualState( state, UIVisualState::Hovered ) ||
         HasVisualState( state, UIVisualState::Active ) || HasVisualState( state, UIVisualState::Checked ) )
    {
        const Style::UIColor fill = ControlFill( state );
        draw.RoundedRect( bounds.x + 1.0f, bounds.y + 1.0f, (std::max)( 0.0f, bounds.w - 2.0f ),
                          (std::max)( 0.0f, bounds.h - 2.0f ), Style::Radii().smallButton, fill.r, fill.g, fill.b,
                          fill.a * 0.42f );
    }

    if ( HasVisualState( state, UIVisualState::Focused ) )
    {
        const Style::UIColor& accent = palette.accentStrong;
        draw.Rect( bounds.x, bounds.y + 2.0f, 2.0f, (std::max)( 0.0f, bounds.h - 4.0f ), accent.r, accent.g, accent.b,
                   0.82f );
    }

    draw.Text( bounds.x + 8.0f, bounds.y + ( bounds.h - textSize ) * 0.5f - 1.0f, textSize, labelColor.r, labelColor.g,
               labelColor.b, SafeText( label ) );
    draw.Text( bounds.x + (std::max)( 8.0f, bounds.w - valueWidth - 8.0f ), bounds.y + ( bounds.h - textSize ) * 0.5f - 1.0f,
               textSize, resolvedValueColor.r, resolvedValueColor.g, resolvedValueColor.b, safeValue );
    draw.PopClip();
}


void DrawButton( const UIDrawContext& draw, const UIRect& bounds, const char* label, UIVisualState state )
{
    if ( !IsVisible( state ) )
    {
        return;
    }

    const float textSize = 11.0f;
    const char* safeLabel = SafeText( label );
    const float labelWidth = UIFontMetrics::MeasureText( textSize, safeLabel );
    const float labelX = bounds.x + (std::max)( 8.0f, ( bounds.w - labelWidth ) * 0.5f );
    const Style::UIColor text = ControlText( state );

    draw.RoundedPanel( bounds, Style::Radii().control, ControlFill( state ), ControlBorder( state ) );

    if ( HasVisualState( state, UIVisualState::Selected ) )
    {
        const Style::UIColor& accent = Style::Palette().accent;
        draw.Rect( bounds.x + 8.0f, bounds.y + bounds.h - 3.0f, (std::max)( 1.0f, bounds.w - 16.0f ), 2.0f, accent.r,
                   accent.g, accent.b, 0.86f );
    }

    draw.Text( labelX, bounds.y + ( bounds.h - textSize ) * 0.5f - 1.0f, textSize, text.r, text.g, text.b, safeLabel );
}


void DrawToggle( const UIDrawContext& draw, const UIRect& bounds, const char* label, const Style::UIColor& accent,
                 UIVisualState state )
{
    if ( !IsVisible( state ) )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    const Style::UIControlStyle& control = Style::Control();
    const float switchX = bounds.x + (std::max)( 66.0f, bounds.w - control.switchW - 4.0f );
    const float switchY = bounds.y + 4.0f;
    const bool checked = HasVisualState( state, UIVisualState::Checked );
    const Style::UIColor labelColor = ControlText( state );
    Style::UIColor switchFill = WithAlpha( palette.control, 0.78f );

    if ( !IsEnabled( state ) )
    {
        switchFill = WithAlpha( palette.windowSubtle, 0.58f );
    }
    else if ( checked )
    {
        switchFill = WithAlpha( accent, HasVisualState( state, UIVisualState::Active ) ? 1.0f : 0.90f );
    }
    else if ( HasVisualState( state, UIVisualState::Hovered ) )
    {
        switchFill = palette.controlHover;
    }

    const Style::UIColor knobFill = !IsEnabled( state ) ? palette.textMuted
                                    : checked           ? palette.accentStrong
                                                        : palette.textMuted;
    const float knobSize = 10.0f;
    const float knobX = switchX + ( checked ? control.switchW - knobSize - 3.0f : 3.0f );

    draw.Text( bounds.x, bounds.y + 4.0f, 10.5f, labelColor.r, labelColor.g, labelColor.b, SafeText( label ) );
    draw.RoundedPanel( { switchX, switchY, control.switchW, control.switchH }, control.switchH * 0.5f, switchFill,
                       ControlBorder( state ) );
    draw.RoundedRect( knobX, switchY + 3.0f, knobSize, knobSize, knobSize * 0.5f, knobFill.r, knobFill.g, knobFill.b,
                      IsEnabled( state ) ? 0.98f : 0.62f );
}


float SliderValueFromPointer( const UIRect& bounds, int pointerX, float minValue, float maxValue, float step )
{
    maxValue = (std::max)( minValue, maxValue );
    const UIRect track = SliderTrackBounds( bounds );
    const float t = track.w > 1.0f ? std::clamp( ( static_cast<float>( pointerX ) - track.x ) / track.w, 0.0f, 1.0f ) : 0.0f;
    float value = minValue + ( maxValue - minValue ) * t;

    if ( step > 0.0f )
    {
        value = minValue + std::round( ( value - minValue ) / step ) * step;
    }

    return std::clamp( value, minValue, maxValue );
}


UIRect SliderTrackBounds( const UIRect& bounds )
{
    return { bounds.x + 118.0f, bounds.y + 17.0f, (std::max)( 80.0f, bounds.w - 190.0f ),
             Style::Control().sliderTrackHeight };
}


UIRect SliderThumbBounds( const UIRect& bounds, float value, float minValue, float maxValue )
{
    maxValue = (std::max)( minValue, maxValue );
    const UIRect track = SliderTrackBounds( bounds );
    const float t = maxValue > minValue ? std::clamp( ( value - minValue ) / ( maxValue - minValue ), 0.0f, 1.0f ) : 0.0f;
    return { track.x + track.w * t - 5.0f, track.y - 5.0f, 10.0f, 16.0f };
}


void DrawSlider( const UIDrawContext& draw, const UIRect& bounds, const char* label, const char* valueText, float value,
                 float minValue, float maxValue, UIVisualState state )
{
    if ( !IsVisible( state ) )
    {
        return;
    }

    maxValue = (std::max)( minValue, maxValue );
    const Style::UIPalette& palette = Style::Palette();
    const UIRect track = SliderTrackBounds( bounds );
    const UIRect thumb = SliderThumbBounds( bounds, value, minValue, maxValue );
    const float t = maxValue > minValue ? std::clamp( ( value - minValue ) / ( maxValue - minValue ), 0.0f, 1.0f ) : 0.0f;
    const float textSize = 10.5f;
    const char* safeValue = SafeText( valueText );
    const float valueWidth = UIFontMetrics::MeasureText( textSize, safeValue );
    const Style::UIColor text = ControlText( state );
    const Style::UIColor progress = IsEnabled( state ) ? palette.accent : palette.textMuted;
    const Style::UIColor thumbFill = IsEnabled( state ) && HasVisualState( state, UIVisualState::Active )
                                         ? palette.textPrimary
                                         : ( IsEnabled( state ) ? palette.accentStrong : palette.textMuted );

    draw.Text( bounds.x, bounds.y + 1.0f, textSize, text.r, text.g, text.b, SafeText( label ) );
    draw.Text( bounds.x + bounds.w - valueWidth - 4.0f, bounds.y + 1.0f, textSize,
               IsEnabled( state ) ? palette.accentStrong.r : palette.textMuted.r,
               IsEnabled( state ) ? palette.accentStrong.g : palette.textMuted.g,
               IsEnabled( state ) ? palette.accentStrong.b : palette.textMuted.b, safeValue );
    draw.RoundedRect( track.x, track.y, track.w, track.h, track.h * 0.5f, palette.control.r, palette.control.g,
                      palette.control.b, IsEnabled( state ) ? 0.78f : 0.34f );
    draw.RoundedRect( track.x, track.y, (std::max)( track.h, track.w * t ), track.h, track.h * 0.5f, progress.r, progress.g,
                      progress.b, IsEnabled( state ) ? 0.90f : 0.42f );
    draw.RoundedPanel( thumb, 5.0f, thumbFill, ControlBorder( state ) );
}


UIRect TabBounds( const UIRect& stripBounds, int tabIndex, int tabCount )
{
    if ( tabCount <= 0 || tabIndex < 0 || tabIndex >= tabCount )
    {
        return {};
    }

    const float tabWidth = stripBounds.w / static_cast<float>( tabCount );
    return { stripBounds.x + static_cast<float>( tabIndex ) * tabWidth + 2.0f, stripBounds.y + 11.0f,
             (std::max)( 0.0f, tabWidth - 8.0f ), 30.0f };
}


int HitTestTab( const UIRect& stripBounds, UIVisualState state, int pointerX, int pointerY, int tabCount )
{
    if ( tabCount <= 0 || !IsEnabled( state ) )
    {
        return -1;
    }

    // Invariant: the empty spacing between tab pills is not interactive. Both
    // drawing and hit testing therefore consume TabBounds rather than treating
    // the complete strip partition as drawn geometry.
    for ( int tabIndex = 0; tabIndex < tabCount; ++tabIndex )
    {
        if ( ContainsComponent( TabBounds( stripBounds, tabIndex, tabCount ), state, pointerX, pointerY ) )
        {
            return tabIndex;
        }
    }

    return -1;
}


void DrawTab( const UIDrawContext& draw, const UIRect& bounds, const char* label, UIVisualState state )
{
    if ( !IsVisible( state ) || bounds.w <= 0.0f || bounds.h <= 0.0f )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    const bool selected = HasVisualState( state, UIVisualState::Selected );
    Style::UIColor fill = WithAlpha( palette.windowSubtle, 0.20f );

    if ( !IsEnabled( state ) )
    {
        fill = WithAlpha( palette.windowSubtle, 0.10f );
    }
    else if ( HasVisualState( state, UIVisualState::Active ) )
    {
        fill = WithAlpha( palette.accent, 0.40f );
    }
    else if ( selected )
    {
        fill = palette.windowRaised;
    }
    else if ( HasVisualState( state, UIVisualState::Hovered ) )
    {
        fill = WithAlpha( palette.controlHover, 0.62f );
    }

    draw.RoundedPanel( bounds, Style::Radii().control, fill, ControlBorder( state ) );

    if ( selected )
    {
        draw.Rect( bounds.x + 8.0f, bounds.y + bounds.h - 1.0f, (std::max)( 1.0f, bounds.w - 16.0f ), 2.0f, palette.accent.r,
                   palette.accent.g, palette.accent.b, 0.86f );
    }

    float textSize = 11.5f;
    const char* safeLabel = SafeText( label );

    while ( textSize > 8.5f && UIFontMetrics::MeasureText( textSize, safeLabel ) > bounds.w - 10.0f )
    {
        textSize -= 0.5f;
    }

    const float labelWidth = UIFontMetrics::MeasureText( textSize, safeLabel );
    const Style::UIColor text = ControlText( state );
    draw.Text( bounds.x + (std::max)( 6.0f, ( bounds.w - labelWidth ) * 0.5f ),
               bounds.y + ( bounds.h - textSize ) * 0.5f - 1.0f, textSize, text.r, text.g, text.b, safeLabel );
}


UIRect ScrollThumbBounds( const UIRect& trackBounds, float contentHeight, float viewportHeight, float scrollOffset )
{
    const float maxScroll = (std::max)( 0.0f, contentHeight - viewportHeight );

    if ( maxScroll <= 0.0f || contentHeight <= 0.0f || trackBounds.h <= 0.0f )
    {
        return {};
    }

    const float thumbHeight = (std::min)( trackBounds.h,
                                          (std::max)( 28.0f, trackBounds.h * viewportHeight / contentHeight ) );
    const float ratio = std::clamp( scrollOffset / maxScroll, 0.0f, 1.0f );
    return { trackBounds.x - 1.0f, trackBounds.y + ( trackBounds.h - thumbHeight ) * ratio, trackBounds.w + 2.0f,
             thumbHeight };
}


void DrawScrollBar( const UIDrawContext& draw, const UIRect& trackBounds, float contentHeight, float viewportHeight,
                    float scrollOffset, float alpha, UIVisualState state )
{
    alpha = std::clamp( alpha, 0.0f, 1.0f );
    const UIRect thumb = ScrollThumbBounds( trackBounds, contentHeight, viewportHeight, scrollOffset );

    if ( !IsVisible( state ) || alpha <= 0.02f || thumb.h <= 0.0f )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    const float enabledAlpha = IsEnabled( state ) ? alpha : alpha * 0.38f;
    const Style::UIColor thumbColor = IsEnabled( state ) && HasVisualState( state, UIVisualState::Active )
                                          ? palette.accentStrong
                                          : ( IsEnabled( state ) && HasVisualState( state, UIVisualState::Hovered )
                                                  ? palette.textPrimary
                                                  : palette.accent );

    draw.RoundedRect( trackBounds.x, trackBounds.y, trackBounds.w, trackBounds.h, trackBounds.w * 0.5f, palette.control.r,
                      palette.control.g, palette.control.b, enabledAlpha * 0.52f );
    draw.RoundedRect( thumb.x, thumb.y, thumb.w, thumb.h, thumb.w * 0.5f, thumbColor.r, thumbColor.g, thumbColor.b,
                      enabledAlpha );
}


UIRect ComboFieldBounds( const UIRect& bounds, bool labelVisible )
{
    const float labelWidth = labelVisible ? COMBO_LABEL_WIDTH : 0.0f;
    return { bounds.x + labelWidth, bounds.y + COMBO_FIELD_Y, (std::max)( 54.0f, bounds.w - labelWidth ),
             COMBO_FIELD_HEIGHT };
}


UIRect ComboPopupBounds( const UIRect& bounds, bool labelVisible, bool dropUp, int optionCount )
{
    const UIRect field = ComboFieldBounds( bounds, labelVisible );
    const float popupHeight = COMBO_OPTION_HEIGHT * static_cast<float>( (std::max)( 1, optionCount ) );
    const float popupY = dropUp ? field.y - popupHeight - COMBO_POPUP_GAP : field.y + field.h + COMBO_POPUP_GAP;
    return { field.x, popupY, field.w, popupHeight };
}


int ComboOptionAtPointer( const UIRect& popupBounds, UIVisualState state, int pointerX, int pointerY, int optionCount )
{
    if ( optionCount <= 0 || !ContainsComponent( popupBounds, state, pointerX, pointerY ) )
    {
        return -1;
    }

    const float optionHeight = popupBounds.h / static_cast<float>( optionCount );
    const int index = static_cast<int>( ( static_cast<float>( pointerY ) - popupBounds.y ) / optionHeight );
    return index >= 0 && index < optionCount ? index : -1;
}


bool IsComboOptionEnabled( uint32_t disabledOptionMask, int optionIndex )
{
    return optionIndex >= 0 && ( optionIndex >= 32 || ( disabledOptionMask & ( 1u << optionIndex ) ) == 0 );
}


void DrawComboField( const UIDrawContext& draw, const UIRect& bounds, const char* label, const char* selectedText,
                     bool labelVisible, bool open, UIVisualState state )
{
    if ( !IsVisible( state ) )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    const UIRect field = ComboFieldBounds( bounds, labelVisible );
    const Style::UIColor text = ControlText( state );

    if ( labelVisible && label && label[0] != '\0' )
    {
        draw.Text( bounds.x, bounds.y + 4.0f, 10.5f, text.r, text.g, text.b, label );
    }

    draw.RoundedPanel( field, Style::Radii().control, ControlFill( state ),
                       open && IsEnabled( state ) ? palette.accent : ControlBorder( state ) );

    if ( selectedText && selectedText[0] != '\0' )
    {
        draw.Text( field.x + 6.0f, field.y + 3.0f, 10.0f, text.r, text.g, text.b, selectedText );
    }

    const UIRect iconBounds = { field.x + field.w - 22.0f, field.y, 20.0f, field.h };
    DrawChevronGlyph( draw, iconBounds, open ? ComponentIcon::ChevronUp : ComponentIcon::ChevronDown,
                      WithAlpha( text, IsEnabled( state ) ? 0.96f : 0.56f ) );
}


void DrawComboPopup( const UIDrawContext& draw, const UIRect& popupBounds, const char* const* options, int optionCount,
                     int selectedIndex, int hoveredIndex, uint32_t disabledOptionMask, UIVisualState state )
{
    if ( !IsVisible( state ) || optionCount <= 0 )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    const float radius = Style::Radii().control;
    draw.RoundedRect( popupBounds.x - 4.0f, popupBounds.y - 4.0f, popupBounds.w + 8.0f, popupBounds.h + 8.0f, radius + 2.0f,
                      0.0f, 0.0f, 0.0f, 0.26f );
    draw.RoundedPanel( popupBounds, radius, palette.windowRaised, ControlBorder( state ) );
    draw.PushClip( popupBounds );

    const float optionHeight = popupBounds.h / static_cast<float>( optionCount );

    for ( int optionIndex = 0; optionIndex < optionCount; ++optionIndex )
    {
        if ( !options )
        {
            break;
        }

        UIVisualState optionState = UIVisualState::Visible;

        if ( IsEnabled( state ) && IsComboOptionEnabled( disabledOptionMask, optionIndex ) )
        {
            optionState |= UIVisualState::Enabled;
        }

        if ( optionIndex == selectedIndex )
        {
            optionState |= UIVisualState::Selected;
        }

        if ( optionIndex == hoveredIndex && IsEnabled( optionState ) )
        {
            optionState |= UIVisualState::Hovered;
        }

        const float optionY = popupBounds.y + static_cast<float>( optionIndex ) * optionHeight;

        if ( HasVisualState( optionState, UIVisualState::Selected ) ||
             HasVisualState( optionState, UIVisualState::Hovered ) )
        {
            const Style::UIColor fill = ControlFill( optionState );
            draw.RoundedRect( popupBounds.x + 2.0f, optionY + 2.0f, (std::max)( 0.0f, popupBounds.w - 4.0f ),
                              (std::max)( 0.0f, optionHeight - 4.0f ), (std::max)( 0.0f, radius - 2.0f ), fill.r, fill.g,
                              fill.b, fill.a );
        }

        const Style::UIColor text = ControlText( optionState );
        draw.Text( popupBounds.x + 10.0f, optionY + 4.0f, 10.5f, text.r, text.g, text.b, SafeText( options[optionIndex] ) );
    }

    draw.PopClip();
}


void DrawIconButton( const UIDrawContext& draw, const UIRect& bounds, ComponentIcon icon, UIVisualState state )
{
    if ( !IsVisible( state ) )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    Style::UIColor iconColor = ControlText( state );

    if ( icon == ComponentIcon::Close && IsEnabled( state ) && HasVisualState( state, UIVisualState::Hovered ) )
    {
        iconColor = palette.warningAccent;
    }

    draw.RoundedPanel( bounds, Style::Radii().smallButton, ControlFill( state ), ControlBorder( state ) );
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;

    switch ( icon )
    {
    case ComponentIcon::ChevronLeft:
    case ComponentIcon::ChevronRight:
    case ComponentIcon::ChevronUp:
    case ComponentIcon::ChevronDown:
        DrawChevronGlyph( draw, bounds, icon, iconColor );
        break;
    case ComponentIcon::Minus:
    case ComponentIcon::Minimize:
        draw.Rect( cx - 5.0f, cy + ( icon == ComponentIcon::Minimize ? 4.0f : -1.0f ), 10.0f, 2.0f, iconColor.r, iconColor.g,
                   iconColor.b, iconColor.a );
        break;
    case ComponentIcon::Plus:
        draw.Rect( cx - 5.0f, cy - 1.0f, 10.0f, 2.0f, iconColor.r, iconColor.g, iconColor.b, iconColor.a );
        draw.Rect( cx - 1.0f, cy - 5.0f, 2.0f, 10.0f, iconColor.r, iconColor.g, iconColor.b, iconColor.a );
        break;
    case ComponentIcon::Maximize:
        draw.Outline( cx - 6.0f, cy - 6.0f, 12.0f, 12.0f, iconColor.r, iconColor.g, iconColor.b, iconColor.a );
        draw.Rect( cx - 6.0f, cy - 6.0f, 12.0f, 2.0f, iconColor.r, iconColor.g, iconColor.b, iconColor.a );
        break;
    case ComponentIcon::Restore:
        draw.Outline( cx - 2.0f, cy - 7.0f, 10.0f, 10.0f, iconColor.r, iconColor.g, iconColor.b, iconColor.a * 0.72f );
        draw.Rect( cx - 2.0f, cy - 7.0f, 10.0f, 2.0f, iconColor.r, iconColor.g, iconColor.b, iconColor.a * 0.72f );
        draw.Outline( cx - 7.0f, cy - 2.0f, 10.0f, 10.0f, iconColor.r, iconColor.g, iconColor.b, iconColor.a );
        draw.Rect( cx - 7.0f, cy - 2.0f, 10.0f, 2.0f, iconColor.r, iconColor.g, iconColor.b, iconColor.a );
        break;
    case ComponentIcon::Close:

        for ( int index = 0; index < 5; ++index )
        {
            const float offset = static_cast<float>( index ) * 2.0f;
            draw.Rect( cx - 5.0f + offset, cy - 5.0f + offset, 2.0f, 2.0f, iconColor.r, iconColor.g, iconColor.b,
                       iconColor.a );

            if ( index != 2 )
            {
                draw.Rect( cx + 3.0f - offset, cy - 5.0f + offset, 2.0f, 2.0f, iconColor.r, iconColor.g, iconColor.b,
                           iconColor.a );
            }
        }

        break;
    }
}


bool IsRowVisible( float contentY, float contentH, float rowY, float rowH )
{
    return rowY >= contentY && rowY + rowH <= contentY + contentH;
}


void DrawTitleButton( const UIDrawContext& draw, const UIRect& bounds, TitleButtonIcon icon, bool hot, bool active )
{
    const Style::UIPalette& palette = Style::Palette();
    const Style::UIColor bg = hot ? palette.controlHover : ( active ? palette.windowRaised : palette.control );
    const Style::UIColor iconColor = icon == TitleButtonIcon::Close && hot ? palette.warningAccent : palette.textSecondary;

    const float iconR = iconColor.r;
    const float iconG = iconColor.g;
    const float iconB = iconColor.b;
    const float iconA = hot || active ? 0.98f : 0.88f;
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;

    draw.RoundedPanel( bounds, Style::Radii().smallButton, bg, palette.border );

    switch ( icon )
    {
    case TitleButtonIcon::Minimize:
        draw.Rect( cx - 5.0f, cy + 4.0f, 10.0f, 2.0f, iconR, iconG, iconB, iconA );
        break;
    case TitleButtonIcon::Maximize:
        draw.Outline( cx - 6.0f, cy - 6.0f, 12.0f, 12.0f, iconR, iconG, iconB, iconA );
        draw.Rect( cx - 6.0f, cy - 6.0f, 12.0f, 2.0f, iconR, iconG, iconB, iconA );
        break;
    case TitleButtonIcon::Restore:
        draw.Outline( cx - 2.0f, cy - 7.0f, 10.0f, 10.0f, iconR, iconG, iconB, iconA * 0.72f );
        draw.Rect( cx - 2.0f, cy - 7.0f, 10.0f, 2.0f, iconR, iconG, iconB, iconA * 0.72f );
        draw.Outline( cx - 7.0f, cy - 2.0f, 10.0f, 10.0f, iconR, iconG, iconB, iconA );
        draw.Rect( cx - 7.0f, cy - 2.0f, 10.0f, 2.0f, iconR, iconG, iconB, iconA );
        break;
    case TitleButtonIcon::Close:

        for ( int i = 0; i < 5; ++i )
        {
            const float offset = static_cast<float>( i ) * 2.0f;
            draw.Rect( cx - 5.0f + offset, cy - 5.0f + offset, 2.0f, 2.0f, iconR, iconG, iconB, iconA );

            if ( i != 2 )
            {
                draw.Rect( cx + 3.0f - offset, cy - 5.0f + offset, 2.0f, 2.0f, iconR, iconG, iconB, iconA );
            }
        }

        break;
    }
}


void DrawPipelineStepButton( const UIDrawContext& draw, const UIRect& bounds, bool previous, bool hot )
{
    const Style::UIPalette& palette = Style::Palette();
    const Style::UIColor bg = hot ? palette.controlHover : palette.control;
    const Style::UIColor icon = hot ? palette.textPrimary : palette.textSecondary;
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;
    const float tipX = previous ? cx - 4.0f : cx + 4.0f;
    const float rearX = previous ? cx + 4.0f : cx - 4.0f;

    draw.RoundedPanel( bounds, Style::Radii().smallButton, bg, palette.border );
    draw.Triangle( tipX, cy, rearX, cy - 5.5f, rearX, cy + 5.5f, icon.r, icon.g, icon.b, hot ? 0.98f : 0.88f );
}


void DrawFooterToggle( const UIDrawContext& draw, const UIRect& bounds, const char* label, bool checked )
{
    const Style::FooterToggleStyle& style = Style::FooterToggle();
    const Style::UIPalette& palette = Style::Palette();
    const Style::UIColor& accent = Style::Accent();
    const float switchW = style.switchW;
    const float switchH = style.switchH;
    const float switchX = bounds.x + bounds.w - switchW - 2.0f;
    const float switchY = bounds.y + 5.0f;
    const float labelAreaW = (std::max)( 1.0f, switchX - bounds.x - 6.0f );
    const float labelW = UIFontMetrics::MeasureText( style.labelTextSize, label );
    const float labelX = bounds.x + (std::max)( 0.0f, ( labelAreaW - labelW ) * 0.5f );

    draw.Text( labelX, bounds.y + 4.0f, style.labelTextSize, style.label.r, style.label.g, style.label.b, label );
    const Style::UIColor offFill = { palette.control.r, palette.control.g, palette.control.b, 0.78f };
    draw.RoundedPanel( { switchX, switchY, switchW, switchH }, switchH * 0.5f, checked ? accent : offFill, palette.border );

    draw.RoundedRect( switchX + ( checked ? switchW - style.knobW - 3.0f : 3.0f ), bounds.y + 8.0f, style.knobW, style.knobH,
                      style.knobW * 0.5f, checked ? palette.accentStrong.r : palette.textMuted.r,
                      checked ? palette.accentStrong.g : palette.textMuted.g,
                      checked ? palette.accentStrong.b : palette.textMuted.b, 0.96f );
}


void DrawLabelValueAt( const UIDrawContext& draw, float contentY, float contentH, float tx, float rowY, const char* label,
                       const char* value, float vr, float vg, float vb )
{
    if ( !IsRowVisible( contentY, contentH, rowY, 18.0f ) )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    draw.Text( tx, rowY, 11.5f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b, label );
    draw.Text( tx + 126.0f, rowY, 11.5f, vr, vg, vb, value );
}


void DrawSectionTitle( const UIDrawContext& draw, float contentX, float contentY, float contentH, float rowY, float textSize,
                       const char* text )
{
    if ( !IsRowVisible( contentY, contentH, rowY, textSize + 4.0f ) )
    {
        return;
    }

    const Style::UIColor& section = Style::Palette().textPrimary;
    draw.Text( contentX, rowY, textSize, section.r, section.g, section.b, text );
}


void DrawContentToggle( const UIDrawContext& draw, float contentY, float contentH, UICheckBox& toggle, float tx, float rowY,
                        float controlW, const char* label, bool checked )
{
    if ( !IsRowVisible( contentY, contentH, rowY, 24.0f ) )
    {
        return;
    }

    const Style::UIColor& accent = Style::Accent();
    toggle.SetBounds( tx, rowY, controlW, 24.0f );
    toggle.DrawToggle( draw, label, checked, accent.r, accent.g, accent.b );
}


void DrawFooterStatCell( const UIDrawContext& draw, float tx, float bottomY, const char* name, const char* value, float r,
                         float g, float b )
{
    const Style::UIPalette& palette = Style::Palette();
    draw.Text( tx, bottomY + 25.0f, 10.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, name );
    draw.Text( tx, bottomY + 47.0f, 11.5f, r, g, b, value );
}


void DrawCompactFooterStat( const UIDrawContext& draw, float statsX, float ty, const char* name, const char* value, float r,
                            float g, float b )
{
    const Style::UIPalette& palette = Style::Palette();
    draw.Text( statsX + 12.0f, ty, 9.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, name );
    draw.Text( statsX + 66.0f, ty, 9.5f, r, g, b, value );
}


void DrawFooterStatDivider( const UIDrawContext& draw, float x, float bottomY )
{
    const Style::UIColor& line = Style::Palette().lineSoft;
    draw.Rect( x, bottomY + 23.0f, 1.0f, 42.0f, line.r, line.g, line.b, 0.16f );
}

} // namespace Widgets
} // namespace UI
} // namespace SkullbonezCore
