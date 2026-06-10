#include "UIDrawWidgets.h"
#include "../SkullbonezText.h"
#include "UICheckBox.h"
#include "UIStyle.h"

#include <algorithm>

using namespace SkullbonezCore::Text;

namespace SkullbonezCore
{
namespace UI
{
namespace Widgets
{

bool IsRowVisible( float contentY, float contentH, float rowY, float rowH )
{
    return rowY >= contentY && rowY + rowH <= contentY + contentH;
}


void DrawTitleButton( const UIDrawContext& draw, const UIRect& bounds, TitleButtonIcon icon, bool hot, bool active )
{
    const float bgR = hot ? 0.050f : ( active ? 0.038f : 0.026f );
    const float bgG = hot ? 0.210f : ( active ? 0.145f : 0.080f );
    const float bgB = hot ? 0.285f : ( active ? 0.188f : 0.102f );
    const float outlineR = hot ? 0.44f : 0.18f;
    const float outlineG = hot ? 0.92f : 0.40f;
    const float outlineB = hot ? 1.00f : 0.48f;
    const float outlineA = hot ? 0.96f : ( active ? 0.90f : 0.58f );
    const float iconR = icon == TitleButtonIcon::Close && hot ? 0.95f : 0.68f;
    const float iconG = icon == TitleButtonIcon::Close && hot ? 0.99f : 0.86f;
    const float iconB = 1.00f;
    const float iconA = hot || active ? 0.98f : 0.88f;
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;

    draw.Rect( bounds.x, bounds.y, bounds.w, bounds.h, bgR, bgG, bgB, hot ? 0.92f : 0.78f );
    draw.Outline( bounds.x, bounds.y, bounds.w, bounds.h, outlineR, outlineG, outlineB, outlineA );
    if ( hot )
    {
        draw.Rect( bounds.x + 1.0f, bounds.y + 1.0f, bounds.w - 2.0f, 1.0f, 0.44f, 0.92f, 1.0f, 0.32f );
    }

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
    const float bgR = hot ? 0.050f : 0.024f;
    const float bgG = hot ? 0.235f : 0.108f;
    const float bgB = hot ? 0.315f : 0.142f;
    const float outlineR = hot ? 0.44f : 0.24f;
    const float outlineG = hot ? 0.92f : 0.58f;
    const float outlineB = hot ? 1.00f : 0.70f;
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;
    const float tipX = previous ? cx - 4.0f : cx + 4.0f;
    const float rearX = previous ? cx + 4.0f : cx - 4.0f;

    draw.Rect( bounds.x, bounds.y, bounds.w, bounds.h, bgR, bgG, bgB, hot ? 0.92f : 0.78f );
    draw.Outline( bounds.x, bounds.y, bounds.w, bounds.h, outlineR, outlineG, outlineB, hot ? 0.96f : 0.78f );
    draw.Triangle( tipX, cy, rearX, cy - 5.5f, rearX, cy + 5.5f, hot ? 0.96f : 0.78f, hot ? 1.0f : 0.92f, 1.0f, hot ? 0.98f : 0.88f );
}


void DrawFooterToggle( const UIDrawContext& draw, const UIRect& bounds, const char* label, bool checked )
{
    const Style::FooterToggleStyle& style = Style::FooterToggle();
    const Style::UIColor& accent = Style::AccentCyan();
    const float switchW = style.switchW;
    const float switchH = style.switchH;
    const float switchX = bounds.x + bounds.w - switchW - 2.0f;
    const float switchY = bounds.y + 5.0f;
    const float labelAreaW = (std::max)( 1.0f, switchX - bounds.x - 6.0f );
    const float labelW = Text2d::MeasureText( style.labelTextSize, label );
    const float labelX = bounds.x + (std::max)( 0.0f, ( labelAreaW - labelW ) * 0.5f );

    draw.Text( labelX, bounds.y + 4.0f, style.labelTextSize, style.label.r, style.label.g, style.label.b, label );
    draw.Rect( switchX, switchY, switchW, switchH,
               checked ? accent.r * 0.32f : 0.05f,
               checked ? accent.g * 0.32f : 0.08f,
               checked ? accent.b * 0.32f : 0.09f,
               0.92f );
    draw.Outline( switchX, switchY, switchW, switchH,
                  checked ? accent.r : 0.20f,
                  checked ? accent.g : 0.30f,
                  checked ? accent.b : 0.34f,
                  checked ? 0.82f : 0.58f );
    draw.Rect( switchX + ( checked ? 14.0f : 2.0f ), bounds.y + 7.0f, style.knobW, style.knobH,
               checked ? 0.82f : 0.34f,
               checked ? 0.98f : 0.46f,
               checked ? 1.0f : 0.52f,
               0.96f );
}


void DrawLabelValueAt( const UIDrawContext& draw, float contentY, float contentH, float tx, float rowY, const char* label, const char* value, float vr, float vg, float vb )
{
    if ( !IsRowVisible( contentY, contentH, rowY, 18.0f ) )
    {
        return;
    }
    draw.Text( tx, rowY, 11.5f, 0.52f, 0.76f, 0.84f, label );
    draw.Text( tx + 126.0f, rowY, 11.5f, vr, vg, vb, value );
}


void DrawSectionTitle( const UIDrawContext& draw, float contentX, float contentY, float contentH, float rowY, float textSize, const char* text )
{
    if ( !IsRowVisible( contentY, contentH, rowY, textSize + 4.0f ) )
    {
        return;
    }
    draw.Text( contentX, rowY, textSize, 1.0f, 0.85f, 0.34f, text );
}


void DrawContentToggle( const UIDrawContext& draw, float contentY, float contentH, UICheckBox& toggle, float tx, float rowY, float controlW, const char* label, bool checked )
{
    if ( !IsRowVisible( contentY, contentH, rowY, 24.0f ) )
    {
        return;
    }
    const Style::UIColor& accent = Style::AccentCyan();
    toggle.SetBounds( tx, rowY, controlW, 24.0f );
    toggle.DrawToggle( draw, label, checked, accent.r, accent.g, accent.b );
}


void DrawFooterStatCell( const UIDrawContext& draw, float tx, float bottomY, const char* name, const char* value, float r, float g, float b )
{
    draw.Text( tx, bottomY + 25.0f, 10.0f, 0.67f, 0.74f, 0.77f, name );
    draw.Text( tx, bottomY + 47.0f, 11.5f, r, g, b, value );
}


void DrawCompactFooterStat( const UIDrawContext& draw, float statsX, float ty, const char* name, const char* value, float r, float g, float b )
{
    draw.Text( statsX + 12.0f, ty, 9.0f, 0.67f, 0.74f, 0.77f, name );
    draw.Text( statsX + 66.0f, ty, 9.5f, r, g, b, value );
}


void DrawFooterStatDivider( const UIDrawContext& draw, float x, float bottomY )
{
    draw.Rect( x, bottomY + 23.0f, 1.0f, 42.0f, 0.28f, 0.38f, 0.42f, 0.78f );
}

} // namespace Widgets
} // namespace UI
} // namespace SkullbonezCore
