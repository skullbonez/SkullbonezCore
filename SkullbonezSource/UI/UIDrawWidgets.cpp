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
  - Component draw and hit geometry derives from the same caller-supplied
    bounds model.
  - Hidden controls append no commands; disabled controls remain pointer-
    blocking but cannot activate.
  - Appearance profiles change presentation only; they never retain state or
    alter caller command identities.

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


void DrawComboChevron( const UIDrawContext& draw, const UIRect& field, bool open, const Style::UIColor& color,
                       ComponentAppearance appearance )
{
    if ( appearance != ComponentAppearance::Established )
    {
        const UIRect iconBounds = { field.x + field.w - 22.0f, field.y, 20.0f, field.h };
        DrawChevronGlyph( draw, iconBounds, open ? ComponentIcon::ChevronUp : ComponentIcon::ChevronDown, color );
        return;
    }

    const float centerX = field.x + field.w - 12.0f;
    const float centerY = field.y + field.h * 0.5f;

    for ( int index = 0; index < 3; ++index )
    {
        const float offset = static_cast<float>( index ) * 2.0f;
        const float y = open ? centerY + 3.0f - offset : centerY - 3.0f + offset;
        draw.Rect( centerX - 4.0f + offset, y, 2.0f, 2.0f, color.r, color.g, color.b, color.a );
        draw.Rect( centerX + 2.0f - offset, y, 2.0f, 2.0f, color.r, color.g, color.b, color.a );
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


void DrawButton( const UIDrawContext& draw, const UIRect& bounds, const char* label, UIVisualState state,
                 ComponentAppearance appearance )
{
    if ( !IsVisible( state ) )
    {
        return;
    }

    const float textSize = 11.0f;
    const char* safeLabel = SafeText( label );
    const float labelWidth = UIFontMetrics::MeasureText( textSize, safeLabel );
    const float labelX = bounds.x + (std::max)( 8.0f, ( bounds.w - labelWidth ) * 0.5f );
    const Style::UIPalette& palette = Style::Palette();
    const bool established = appearance == ComponentAppearance::Established;
    const Style::UIColor fill = established && IsEnabled( state )
                                    ? ( HasVisualState( state, UIVisualState::Hovered ) ? palette.controlHover
                                                                                        : palette.control )
                                    : ControlFill( state );
    const Style::UIColor border = established && IsEnabled( state ) ? palette.border : ControlBorder( state );
    const Style::UIColor text = established && IsEnabled( state )
                                    ? ( HasVisualState( state, UIVisualState::Hovered ) ? palette.textPrimary
                                                                                        : palette.textSecondary )
                                    : ControlText( state );

    draw.RoundedPanel( bounds, Style::Radii().control, fill, border );

    if ( HasVisualState( state, UIVisualState::Selected ) )
    {
        const Style::UIColor& accent = palette.accent;
        draw.Rect( bounds.x + 8.0f, bounds.y + bounds.h - 3.0f, (std::max)( 1.0f, bounds.w - 16.0f ), 2.0f, accent.r,
                   accent.g, accent.b, 0.86f );
    }

    draw.Text( labelX, bounds.y + ( bounds.h - textSize ) * 0.5f - 1.0f, textSize, text.r, text.g, text.b, safeLabel );
}


void DrawToggle( const UIDrawContext& draw, const UIRect& bounds, const char* label, const Style::UIColor& accent,
                 UIVisualState state, ComponentAppearance appearance )
{
    if ( !IsVisible( state ) )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();

    if ( appearance == ComponentAppearance::Footer )
    {
        // Why: footer toggles share the state and drawing algorithm but use a
        // centered label column and a two-pixel-later switch anchor. Keeping
        // that geometry here prevents the footer helper from becoming a
        // second toggle implementation.
        const Style::FooterToggleStyle& footer = Style::FooterToggle();
        const bool checked = HasVisualState( state, UIVisualState::Checked );
        const bool enabled = IsEnabled( state );
        const float switchX = bounds.x + bounds.w - footer.switchW - 2.0f;
        const float switchY = bounds.y + 5.0f;
        const float labelAreaWidth = (std::max)( 1.0f, switchX - bounds.x - 6.0f );
        const float labelWidth = UIFontMetrics::MeasureText( footer.labelTextSize, SafeText( label ) );
        const float labelX = bounds.x + (std::max)( 0.0f, ( labelAreaWidth - labelWidth ) * 0.5f );
        const Style::UIColor labelColor = enabled ? footer.label : palette.textMuted;
        const Style::UIColor offFill = enabled ? WithAlpha( palette.control, 0.78f )
                                               : WithAlpha( palette.windowSubtle, 0.58f );
        const Style::UIColor switchFill = enabled && checked ? accent : offFill;
        const Style::UIColor border = enabled ? palette.border : ControlBorder( state );
        const Style::UIColor knob = enabled && checked ? palette.accentStrong : palette.textMuted;

        draw.Text( labelX, bounds.y + 4.0f, footer.labelTextSize, labelColor.r, labelColor.g, labelColor.b,
                   SafeText( label ) );
        draw.RoundedPanel( { switchX, switchY, footer.switchW, footer.switchH }, footer.switchH * 0.5f, switchFill, border );
        draw.RoundedRect( switchX + ( checked ? footer.switchW - footer.knobW - 3.0f : 3.0f ), bounds.y + 8.0f, footer.knobW,
                          footer.knobH, footer.knobW * 0.5f, knob.r, knob.g, knob.b, enabled ? 0.96f : 0.62f );
        return;
    }

    const Style::UIControlStyle& control = Style::Control();
    const float switchX = bounds.x + (std::max)( 66.0f, bounds.w - control.switchW - 4.0f );
    const float switchY = bounds.y + 4.0f;
    const bool checked = HasVisualState( state, UIVisualState::Checked );
    const bool established = appearance == ComponentAppearance::Established;
    const Style::UIColor labelColor = established && IsEnabled( state ) ? palette.textSecondary : ControlText( state );
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
    const Style::UIColor border = established && IsEnabled( state ) ? palette.border : ControlBorder( state );
    draw.RoundedPanel( { switchX, switchY, control.switchW, control.switchH }, control.switchH * 0.5f, switchFill, border );
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
                 float minValue, float maxValue, UIVisualState state, ComponentAppearance appearance )
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
    const Style::UIColor thumbBorder = appearance == ComponentAppearance::Established && IsEnabled( state )
                                           ? palette.border
                                           : ControlBorder( state );
    draw.RoundedPanel( thumb, 5.0f, thumbFill, thumbBorder );
}


TabLayout ResolveTabLayout( const UIRect& stripBounds, int tabIndex, int tabCount, ComponentAppearance appearance )
{
    if ( tabCount <= 0 || tabIndex < 0 || tabIndex >= tabCount )
    {
        return {};
    }

    const float tabWidth = stripBounds.w / static_cast<float>( tabCount );
    const UIRect visualBounds = { stripBounds.x + static_cast<float>( tabIndex ) * tabWidth + 2.0f, stripBounds.y + 11.0f,
                                  (std::max)( 0.0f, tabWidth - 8.0f ), 30.0f };

    if ( appearance == ComponentAppearance::Established )
    {
        // Compatibility: UIWindowInteractionOwner routes clicks across the
        // complete tab strip, and UIEditorMiniPalette draws those full-cell
        // hitboxes. Established preserves that public pointer partition while
        // exposing its inset pill in the same value. Delete the split when
        // UITabBar no longer requests Established geometry and both production
        // witnesses consume the adaptive pill bounds.
        const UIRect interactionBounds = { stripBounds.x + static_cast<float>( tabIndex ) * tabWidth, stripBounds.y,
                                           tabWidth, stripBounds.h };
        return { interactionBounds, visualBounds };
    }

    return { visualBounds, visualBounds };
}


int HitTestTab( const UIRect& stripBounds, UIVisualState state, int pointerX, int pointerY, int tabCount,
                ComponentAppearance appearance )
{
    if ( tabCount <= 0 || stripBounds.w <= 0.0f || !IsEnabled( state ) )
    {
        return -1;
    }

    if ( appearance == ComponentAppearance::Established )
    {
        // Invariant: resolve the exact partition index first so a shared edge
        // belongs to the tab on its right, then validate the named interaction
        // rectangle from the same layout value that supplies drawing.
        const float tabWidth = stripBounds.w / static_cast<float>( tabCount );
        const int tabIndex = static_cast<int>( ( static_cast<float>( pointerX ) - stripBounds.x ) / tabWidth );

        if ( tabIndex < 0 || tabIndex >= tabCount )
        {
            return -1;
        }

        const TabLayout layout = ResolveTabLayout( stripBounds, tabIndex, tabCount, appearance );
        return ContainsComponent( layout.interactionBounds, state, pointerX, pointerY ) ? tabIndex : -1;
    }

    // Invariant: adaptive tabs use one rectangle for drawing and interaction,
    // so the empty spacing between pills is not interactive.
    for ( int tabIndex = 0; tabIndex < tabCount; ++tabIndex )
    {
        const TabLayout layout = ResolveTabLayout( stripBounds, tabIndex, tabCount, appearance );

        if ( ContainsComponent( layout.interactionBounds, state, pointerX, pointerY ) )
        {
            return tabIndex;
        }
    }

    return -1;
}


void DrawTab( const UIDrawContext& draw, const UIRect& bounds, const char* label, UIVisualState state,
              ComponentAppearance appearance )
{
    if ( !IsVisible( state ) || bounds.w <= 0.0f || bounds.h <= 0.0f )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    const bool selected = HasVisualState( state, UIVisualState::Selected );
    const bool established = appearance == ComponentAppearance::Established;

    // Invariant: callers pass TabLayout::visualBounds. DrawTab never derives a
    // second rectangle, so a layout change cannot silently move rendering away
    // from the geometry value inspected by hit-routing tests.
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

    if ( established && !selected )
    {
        draw.RoundedRect( bounds.x, bounds.y, bounds.w, bounds.h, Style::Radii().control, fill.r, fill.g, fill.b, fill.a );
    }
    else
    {
        const Style::UIColor border = established ? palette.innerBorder : ControlBorder( state );
        draw.RoundedPanel( bounds, Style::Radii().control, fill, border );
    }

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
    const Style::UIColor text = established && IsEnabled( state )
                                    ? ( selected ? palette.textPrimary : palette.textSecondary )
                                    : ControlText( state );
    const float labelY = established ? bounds.y + 8.0f : bounds.y + ( bounds.h - textSize ) * 0.5f - 1.0f;
    draw.Text( bounds.x + (std::max)( 6.0f, ( bounds.w - labelWidth ) * 0.5f ), labelY, textSize, text.r, text.g, text.b,
               safeLabel );
}


UIRect ScrollThumbBounds( const UIRect& trackBounds, float contentHeight, float viewportHeight, float scrollOffset,
                          ComponentAppearance appearance )
{
    const float maxScroll = (std::max)( 0.0f, contentHeight - viewportHeight );

    if ( maxScroll <= 0.0f || contentHeight <= 0.0f || trackBounds.h <= 0.0f )
    {
        return {};
    }

    if ( appearance == ComponentAppearance::Established )
    {
        // Invariant: established wrappers define scroll travel in viewport
        // pixels. Do not substitute track height or clamp the ratio here; that
        // changes the retained command stream when layout and viewport differ.
        const float thumbHeight = (std::max)( 28.0f, viewportHeight * viewportHeight / contentHeight );
        return { trackBounds.x - 1.0f, trackBounds.y + ( viewportHeight - thumbHeight ) * ( scrollOffset / maxScroll ),
                 trackBounds.w + 2.0f, thumbHeight };
    }

    const float thumbHeight = (std::min)( trackBounds.h,
                                          (std::max)( 28.0f, trackBounds.h * viewportHeight / contentHeight ) );
    const float ratio = std::clamp( scrollOffset / maxScroll, 0.0f, 1.0f );
    return { trackBounds.x - 1.0f, trackBounds.y + ( trackBounds.h - thumbHeight ) * ratio, trackBounds.w + 2.0f,
             thumbHeight };
}


void DrawScrollBar( const UIDrawContext& draw, const UIRect& trackBounds, float contentHeight, float viewportHeight,
                    float scrollOffset, float alpha, UIVisualState state, ComponentAppearance appearance )
{
    alpha = std::clamp( alpha, 0.0f, 1.0f );
    const UIRect thumb = ScrollThumbBounds( trackBounds, contentHeight, viewportHeight, scrollOffset, appearance );

    if ( !IsVisible( state ) || alpha <= 0.02f || thumb.h <= 0.0f )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    const float enabledAlpha = IsEnabled( state ) ? alpha : alpha * 0.38f;
    const Style::UIColor thumbColor = appearance == ComponentAppearance::Established
                                          ? palette.accent
                                          : ( IsEnabled( state ) && HasVisualState( state, UIVisualState::Active )
                                                  ? palette.accentStrong
                                                  : ( IsEnabled( state ) && HasVisualState( state, UIVisualState::Hovered )
                                                          ? palette.textPrimary
                                                          : palette.accent ) );

    draw.RoundedRect( trackBounds.x, trackBounds.y, trackBounds.w, trackBounds.h, trackBounds.w * 0.5f, palette.control.r,
                      palette.control.g, palette.control.b, enabledAlpha * 0.52f );
    draw.RoundedRect( thumb.x, thumb.y, thumb.w, thumb.h, thumb.w * 0.5f, thumbColor.r, thumbColor.g, thumbColor.b,
                      enabledAlpha );
}


ComboLayout ResolveComboLayout( const UIRect& bounds, bool labelVisible, bool dropUp, int optionCount )
{
    const float labelWidth = labelVisible ? COMBO_LABEL_WIDTH : 0.0f;
    const UIRect fieldBounds = { bounds.x + labelWidth, bounds.y + COMBO_FIELD_Y, (std::max)( 54.0f, bounds.w - labelWidth ),
                                 COMBO_FIELD_HEIGHT };
    const float popupHeight = COMBO_OPTION_HEIGHT * static_cast<float>( (std::max)( 1, optionCount ) );
    const float popupY = dropUp ? fieldBounds.y - popupHeight - COMBO_POPUP_GAP
                                : fieldBounds.y + fieldBounds.h + COMBO_POPUP_GAP;
    const UIRect popupBounds = { fieldBounds.x, popupY, fieldBounds.w, popupHeight };

    // Compatibility: UIComboBox::HitBox is the retained activation seam used
    // by UIWindowInteractionOwner and UITabScene. It has always accepted the
    // full component, including the label column, while only fieldBounds is
    // visible as the control. Delete this split when those callers migrate to
    // explicit field-only activation and their pointer oracles are updated.
    return { bounds, fieldBounds, popupBounds };
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


void DrawComboField( const UIDrawContext& draw, const ComboLayout& layout, const char* label, const char* selectedText,
                     bool labelVisible, bool open, UIVisualState state, bool selectedEnabled,
                     ComponentAppearance appearance )
{
    if ( !IsVisible( state ) )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    const bool established = appearance == ComponentAppearance::Established;
    const bool hovered = HasVisualState( state, UIVisualState::Hovered );
    const Style::UIColor text = established && IsEnabled( state ) ? ( hovered ? palette.textPrimary : palette.textSecondary )
                                                                  : ControlText( state );
    const Style::UIColor labelColor = established && IsEnabled( state ) ? palette.textSecondary : text;

    if ( labelVisible && label && label[0] != '\0' )
    {
        draw.Text( layout.interactionBounds.x, layout.interactionBounds.y + 4.0f, 10.5f, labelColor.r, labelColor.g,
                   labelColor.b, label );
    }

    const Style::UIColor fill = established && IsEnabled( state ) ? ( hovered ? palette.controlHover : palette.control )
                                                                  : ControlFill( state );
    const Style::UIColor border = established && IsEnabled( state )
                                      ? ( hovered ? palette.innerBorder : palette.border )
                                      : ( open && IsEnabled( state ) ? palette.accent : ControlBorder( state ) );
    draw.RoundedPanel( layout.fieldBounds, Style::Radii().control, fill, border );

    if ( selectedText && selectedText[0] != '\0' )
    {
        const Style::UIColor selectedTextColor = selectedEnabled ? text : palette.textMuted;
        draw.Text( layout.fieldBounds.x + 6.0f, layout.fieldBounds.y + 3.0f, 10.0f, selectedTextColor.r, selectedTextColor.g,
                   selectedTextColor.b, selectedText );
    }

    const Style::UIColor chevronColor = established
                                            ? WithAlpha( IsEnabled( state ) ? palette.textSecondary : palette.textMuted,
                                                         IsEnabled( state ) ? 0.96f : 0.56f )
                                            : WithAlpha( text, IsEnabled( state ) ? 0.96f : 0.56f );
    DrawComboChevron( draw, layout.fieldBounds, open, chevronColor, appearance );
}


void DrawComboPopup( const UIDrawContext& draw, const ComboLayout& layout, const char* const* options, int optionCount,
                     int selectedIndex, int hoveredIndex, uint32_t disabledOptionMask, UIVisualState state,
                     ComponentAppearance appearance )
{
    const bool established = appearance == ComponentAppearance::Established;

    if ( !IsVisible( state ) || ( optionCount <= 0 && !established ) )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    const float radius = Style::Radii().control;
    draw.RoundedRect( layout.popupBounds.x - 4.0f, layout.popupBounds.y - 4.0f, layout.popupBounds.w + 8.0f,
                      layout.popupBounds.h + 8.0f, radius + 2.0f, 0.0f, 0.0f, 0.0f, 0.26f );
    draw.RoundedPanel( layout.popupBounds, radius, palette.windowRaised,
                       established ? palette.border : ControlBorder( state ) );

    // Why: Adaptive popups author an explicit clip contract. Established
    // popups preserve their existing clipping behavior and command order;
    // adding clip commands there changes the recorded stream. Both modes
    // share option geometry and disabled-row policy below.
    if ( !established )
    {
        draw.PushClip( layout.popupBounds );
    }

    const float optionHeight = optionCount > 0 ? layout.popupBounds.h / static_cast<float>( optionCount ) : 0.0f;

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

        const float optionY = layout.popupBounds.y + static_cast<float>( optionIndex ) * optionHeight;

        if ( HasVisualState( optionState, UIVisualState::Selected ) ||
             HasVisualState( optionState, UIVisualState::Hovered ) )
        {
            const bool optionEnabled = IsEnabled( optionState );
            const bool optionHovered = HasVisualState( optionState, UIVisualState::Hovered );
            const Style::UIColor fill = established
                                            ? ( !optionEnabled ? palette.windowSubtle
                                                               : ( optionHovered ? palette.controlHover : palette.control ) )
                                            : ControlFill( optionState );
            draw.RoundedRect( layout.popupBounds.x + 2.0f, optionY + 2.0f, (std::max)( 0.0f, layout.popupBounds.w - 4.0f ),
                              (std::max)( 0.0f, optionHeight - 4.0f ), (std::max)( 0.0f, radius - 2.0f ), fill.r, fill.g,
                              fill.b, fill.a );
        }

        const Style::UIColor text = established ? ( !IsEnabled( optionState )
                                                        ? palette.textMuted
                                                        : ( HasVisualState( optionState, UIVisualState::Hovered )
                                                                ? palette.textPrimary
                                                                : ( HasVisualState( optionState, UIVisualState::Selected )
                                                                        ? palette.accentStrong
                                                                        : palette.textSecondary ) ) )
                                                : ControlText( optionState );
        draw.Text( layout.popupBounds.x + 10.0f, optionY + 4.0f, 10.5f, text.r, text.g, text.b,
                   SafeText( options[optionIndex] ) );
    }

    if ( !established )
    {
        draw.PopClip();
    }
}


void DrawIconButton( const UIDrawContext& draw, const UIRect& bounds, ComponentIcon icon, UIVisualState state,
                     ComponentAppearance appearance )
{
    if ( !IsVisible( state ) )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    const bool established = appearance == ComponentAppearance::Established;
    const bool hovered = HasVisualState( state, UIVisualState::Hovered );
    const bool active = HasVisualState( state, UIVisualState::Active );
    const bool expander = icon == ComponentIcon::Minus || icon == ComponentIcon::Plus;
    Style::UIColor iconColor = established && IsEnabled( state ) ? ( hovered ? palette.textPrimary : palette.textSecondary )
                                                                 : ControlText( state );

    if ( icon == ComponentIcon::Close && IsEnabled( state ) && hovered )
    {
        iconColor = palette.warningAccent;
    }

    const Style::UIColor fill = established && IsEnabled( state )
                                    ? ( hovered ? palette.controlHover
                                                : ( active ? palette.windowRaised : palette.control ) )
                                    : ControlFill( state );
    const Style::UIColor border = established && IsEnabled( state ) ? palette.border : ControlBorder( state );
    const float establishedAlpha = expander ? 0.96f : ( hovered || active ? 0.98f : 0.88f );

    if ( established )
    {
        iconColor.a = IsEnabled( state ) ? establishedAlpha : 0.56f;
    }

    draw.RoundedPanel( bounds, Style::Radii().smallButton, fill, border );
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;

    switch ( icon )
    {
    case ComponentIcon::ChevronLeft:
    case ComponentIcon::ChevronRight:
    case ComponentIcon::ChevronUp:
    case ComponentIcon::ChevronDown:

        if ( established )
        {
            const float tipX = icon == ComponentIcon::ChevronLeft    ? cx - 4.0f
                               : icon == ComponentIcon::ChevronRight ? cx + 4.0f
                                                                     : cx;
            const float rearX = icon == ComponentIcon::ChevronLeft    ? cx + 4.0f
                                : icon == ComponentIcon::ChevronRight ? cx - 4.0f
                                                                      : cx;

            if ( icon == ComponentIcon::ChevronLeft || icon == ComponentIcon::ChevronRight )
            {
                draw.Triangle( tipX, cy, rearX, cy - 5.5f, rearX, cy + 5.5f, iconColor.r, iconColor.g, iconColor.b,
                               iconColor.a );
            }
            else
            {
                DrawChevronGlyph( draw, bounds, icon, iconColor );
            }
        }
        else
        {
            DrawChevronGlyph( draw, bounds, icon, iconColor );
        }

        break;
    case ComponentIcon::Minus:
    case ComponentIcon::Minimize:
        draw.Rect( cx - ( established && expander ? 4.0f : 5.0f ), cy + ( icon == ComponentIcon::Minimize ? 4.0f : -1.0f ),
                   established && expander ? 8.0f : 10.0f, 2.0f, iconColor.r, iconColor.g, iconColor.b, iconColor.a );
        break;
    case ComponentIcon::Plus:
    {
        const float arm = established ? 8.0f : 10.0f;
        draw.Rect( cx - arm * 0.5f, cy - 1.0f, arm, 2.0f, iconColor.r, iconColor.g, iconColor.b, iconColor.a );
        draw.Rect( cx - 1.0f, cy - arm * 0.5f, 2.0f, arm, iconColor.r, iconColor.g, iconColor.b, iconColor.a );
        break;
    }
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
    ComponentIcon componentIcon = ComponentIcon::Minimize;

    switch ( icon )
    {
    case TitleButtonIcon::Minimize:
        componentIcon = ComponentIcon::Minimize;
        break;
    case TitleButtonIcon::Maximize:
        componentIcon = ComponentIcon::Maximize;
        break;
    case TitleButtonIcon::Restore:
        componentIcon = ComponentIcon::Restore;
        break;
    case TitleButtonIcon::Close:
        componentIcon = ComponentIcon::Close;
        break;
    }

    UIVisualState state = UIVisualState::Visible | UIVisualState::Enabled;

    if ( hot )
    {
        state |= UIVisualState::Hovered;
    }

    if ( active )
    {
        state |= UIVisualState::Active;
    }

    DrawIconButton( draw, bounds, componentIcon, state, ComponentAppearance::Established );
}


void DrawPipelineStepButton( const UIDrawContext& draw, const UIRect& bounds, bool previous, bool hot )
{
    UIVisualState state = UIVisualState::Visible | UIVisualState::Enabled;

    if ( hot )
    {
        state |= UIVisualState::Hovered;
    }

    DrawIconButton( draw, bounds, previous ? ComponentIcon::ChevronLeft : ComponentIcon::ChevronRight, state,
                    ComponentAppearance::Established );
}


void DrawFooterToggle( const UIDrawContext& draw, const UIRect& bounds, const char* label, bool checked )
{
    UIVisualState state = UIVisualState::Visible | UIVisualState::Enabled;

    if ( checked )
    {
        state |= UIVisualState::Checked;
    }

    DrawToggle( draw, bounds, label, Style::Accent(), state, ComponentAppearance::Footer );
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
