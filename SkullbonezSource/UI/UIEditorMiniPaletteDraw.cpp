/*
File: UIEditorMiniPaletteDraw.cpp
Purpose:
  Owns editor mini-palette glyphs, flyouts, tooltips, and minimized-window drawing.

Summary:
  The palette is a value-driven UI surface. Layout and selection policy produce
  records that both input and drawing consume without reaching into InGameUI
  retained state.

Invariants:
  - Hit testing and drawing use the same EditorMiniPaletteLayout geometry.
  - Functions retain no frame or owner reference after returning.
  - Object-type mappings remain stable authored editor vocabulary.

Related:
  - UIFrameComposition.h owns shared layout records and helper contracts.
  - UI.cpp owns the surrounding UI frame.
  - Agentic/Reference/engine-glossary.md
*/
#include "UIFrameComposition.h"
#include "UIFontMetrics.h"

namespace SkullbonezCore::UI::FrameComposition
{
void DrawEditorMiniRootSilhouette( const UIDrawContext& draw, const UIRect& bounds, bool largeRoot,
                                   const Style::UIColor& color, float alpha )
{
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.52f;
    const float r = (std::min)( bounds.w, bounds.h ) * ( largeRoot ? 0.36f : 0.31f );
    const float stemW = (std::max)( 2.0f, r * 0.24f );

    draw.Rect( cx - stemW * 0.5f, cy - r * 1.05f, stemW, r * 1.18f, color.r, color.g, color.b, alpha );
    draw.Triangle( cx - stemW * 0.2f, cy - r * 0.10f, cx - r * 1.02f, cy + r * 0.68f, cx - stemW * 1.15f, cy + r * 0.17f,
                   color.r, color.g, color.b, alpha * 0.94f );

    draw.Triangle( cx + stemW * 0.2f, cy - r * 0.08f, cx + r * 1.02f, cy + r * 0.68f, cx + stemW * 1.15f, cy + r * 0.18f,
                   color.r, color.g, color.b, alpha * 0.94f );

    draw.Triangle( cx, cy + r * 0.04f, cx - r * 0.22f, cy + r * 1.02f, cx + r * 0.16f, cy + r * 0.35f, color.r, color.g,
                   color.b, alpha * 0.84f );

    if ( largeRoot )
    {
        draw.Triangle( cx - r * 0.18f, cy + r * 0.18f, cx - r * 0.72f, cy + r * 1.00f, cx - r * 0.38f, cy + r * 0.38f,
                       color.r, color.g, color.b, alpha * 0.78f );

        draw.Triangle( cx + r * 0.14f, cy + r * 0.18f, cx + r * 0.74f, cy + r * 1.00f, cx + r * 0.36f, cy + r * 0.38f,
                       color.r, color.g, color.b, alpha * 0.78f );
    }
}


void DrawEditorMiniTreeSilhouette( const UIDrawContext& draw, const UIRect& bounds, int family, bool rooted, bool slope,
                                   bool shedding, const Style::UIColor& color, float alpha )
{
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.51f;
    const float r = (std::min)( bounds.w, bounds.h ) * 0.32f;
    const float trunkW = (std::max)( 2.0f, r * 0.24f );

    if ( slope )
    {
        draw.Triangle( cx - r * 1.22f, cy + r * 1.10f, cx + r * 1.20f, cy + r * 0.66f, cx + r * 1.20f, cy + r * 1.10f,
                       color.r, color.g, color.b, alpha * 0.42f );
    }

    draw.Rect( cx - trunkW * 0.5f, cy + r * 0.05f, trunkW, r * 0.94f, color.r, color.g, color.b, alpha * 0.74f );

    if ( family == EDITOR_MINI_TREE_TYPE_SMALL )
    {
        draw.RoundedRect( cx - r * 0.72f, cy - r * 0.72f, r * 1.44f, r * 0.92f, 999.0f, color.r, color.g, color.b, alpha );

        draw.RoundedRect( cx - r * 0.54f, cy - r * 1.12f, r * 1.08f, r * 0.82f, 999.0f, color.r, color.g, color.b,
                          alpha * 0.94f );
    }
    else if ( family == EDITOR_MINI_TREE_TYPE_CEDAR )
    {
        draw.Triangle( cx, cy - r * 1.25f, cx - r * 0.48f, cy - r * 0.30f, cx + r * 0.48f, cy - r * 0.30f, color.r, color.g,
                       color.b, alpha );

        draw.Triangle( cx, cy - r * 0.82f, cx - r * 0.70f, cy + r * 0.28f, cx + r * 0.70f, cy + r * 0.28f, color.r, color.g,
                       color.b, alpha * 0.92f );

        draw.Triangle( cx, cy - r * 0.24f, cx - r * 0.82f, cy + r * 0.88f, cx + r * 0.82f, cy + r * 0.88f, color.r, color.g,
                       color.b, alpha * 0.84f );
    }
    else
    {
        draw.Triangle( cx, cy - r * 1.18f, cx - r * 0.70f, cy - r * 0.06f, cx + r * 0.70f, cy - r * 0.06f, color.r, color.g,
                       color.b, alpha );

        draw.Triangle( cx, cy - r * 0.58f, cx - r * 0.96f, cy + r * 0.56f, cx + r * 0.96f, cy + r * 0.56f, color.r, color.g,
                       color.b, alpha * 0.90f );

        draw.Triangle( cx, cy + r * 0.00f, cx - r * 1.12f, cy + r * 0.98f, cx + r * 1.12f, cy + r * 0.98f, color.r, color.g,
                       color.b, alpha * 0.80f );
    }

    if ( rooted )
    {
        draw.Triangle( cx, cy + r * 0.76f, cx - r * 0.62f, cy + r * 1.18f, cx - trunkW * 0.5f, cy + r * 0.88f, color.r,
                       color.g, color.b, alpha * 0.84f );

        draw.Triangle( cx, cy + r * 0.76f, cx + r * 0.62f, cy + r * 1.18f, cx + trunkW * 0.5f, cy + r * 0.88f, color.r,
                       color.g, color.b, alpha * 0.84f );
    }

    if ( shedding )
    {
        draw.RoundedRect( cx - r * 1.12f, cy + r * 1.03f, 3.0f, 3.0f, 999.0f, color.r, color.g, color.b, alpha * 0.80f );

        draw.RoundedRect( cx - r * 0.18f, cy + r * 1.15f, 3.0f, 3.0f, 999.0f, color.r, color.g, color.b, alpha * 0.78f );

        draw.RoundedRect( cx + r * 0.92f, cy + r * 0.94f, 3.0f, 3.0f, 999.0f, color.r, color.g, color.b, alpha * 0.78f );
    }
}


void DrawEditorMiniHullSilhouette( const UIDrawContext& draw, const UIRect& bounds, int objectType,
                                   const Style::UIColor& color, float alpha )
{
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;
    const float r = (std::min)( bounds.w, bounds.h ) * 0.34f;

    switch ( objectType )
    {
    case EditorTab::OBJECT_HULL_WEDGE:
        draw.Triangle( cx - r * 1.05f, cy + r * 0.82f, cx + r * 1.05f, cy + r * 0.82f, cx + r * 0.36f, cy - r * 0.86f,
                       color.r, color.g, color.b, alpha );

        draw.Rect( cx - r * 1.05f, cy + r * 0.62f, r * 2.10f, r * 0.24f, color.r, color.g, color.b, alpha * 0.70f );
        return;
    case EditorTab::OBJECT_HULL_TRI_PRISM:
        draw.Triangle( cx - r * 0.98f, cy + r * 0.78f, cx - r * 0.18f, cy - r * 0.78f, cx + r * 0.58f, cy + r * 0.78f,
                       color.r, color.g, color.b, alpha );

        draw.Triangle( cx - r * 0.36f, cy + r * 0.42f, cx + r * 0.42f, cy - r * 0.96f, cx + r * 1.04f, cy + r * 0.42f,
                       color.r, color.g, color.b, alpha * 0.70f );

        draw.Rect( cx - r * 0.96f, cy + r * 0.64f, r * 1.54f, 2.0f, color.r, color.g, color.b, alpha * 0.82f );
        draw.Rect( cx - r * 0.36f, cy + r * 0.32f, r * 1.38f, 2.0f, color.r, color.g, color.b, alpha * 0.62f );
        return;
    case EditorTab::OBJECT_HULL_TAPERED_BLOCK:
        draw.Rect( cx - r * 0.54f, cy - r * 0.70f, r * 1.08f, r * 1.40f, color.r, color.g, color.b, alpha );
        draw.Triangle( cx - r * 0.54f, cy - r * 0.70f, cx - r * 1.02f, cy + r * 0.72f, cx - r * 0.54f, cy + r * 0.72f,
                       color.r, color.g, color.b, alpha * 0.86f );

        draw.Triangle( cx + r * 0.54f, cy - r * 0.70f, cx + r * 1.02f, cy + r * 0.72f, cx + r * 0.54f, cy + r * 0.72f,
                       color.r, color.g, color.b, alpha * 0.86f );

        return;
    case EditorTab::OBJECT_HULL_PYRAMID:
        draw.Triangle( cx, cy - r * 0.98f, cx - r * 1.04f, cy + r * 0.82f, cx + r * 1.04f, cy + r * 0.82f, color.r, color.g,
                       color.b, alpha );

        draw.Rect( cx - 1.0f, cy - r * 0.40f, 2.0f, r * 1.10f, color.r, color.g, color.b, alpha * 0.54f );
        return;
    case EditorTab::OBJECT_HULL_HEX_PRISM:
        draw.Rect( cx - r * 0.66f, cy - r * 0.86f, r * 1.32f, r * 1.72f, color.r, color.g, color.b, alpha );
        draw.Triangle( cx - r * 0.66f, cy - r * 0.86f, cx - r * 1.12f, cy, cx - r * 0.66f, cy + r * 0.86f, color.r, color.g,
                       color.b, alpha * 0.82f );

        draw.Triangle( cx + r * 0.66f, cy - r * 0.86f, cx + r * 1.12f, cy, cx + r * 0.66f, cy + r * 0.86f, color.r, color.g,
                       color.b, alpha * 0.82f );

        return;
    case EditorTab::OBJECT_HULL_DIAMOND:
        draw.Triangle( cx, cy - r * 1.08f, cx + r * 0.96f, cy, cx, cy + r * 1.08f, color.r, color.g, color.b, alpha );
        draw.Triangle( cx, cy - r * 1.08f, cx - r * 0.96f, cy, cx, cy + r * 1.08f, color.r, color.g, color.b,
                       alpha * 0.88f );

        return;
    default:
        draw.Triangle( cx - r, cy + r, cx + r, cy + r, cx, cy - r, color.r, color.g, color.b, alpha );
        return;
    }
}


void DrawEditorMiniRockSilhouette( const UIDrawContext& draw, const UIRect& bounds, int objectType,
                                   const Style::UIColor& color, float alpha )
{
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.52f;
    const float r = (std::min)( bounds.w, bounds.h ) * 0.34f;

    switch ( objectType )
    {
    case EditorTab::OBJECT_ROCK_SLAB:
        draw.Rect( cx - r * 1.12f, cy + r * 0.10f, r * 2.24f, r * 0.56f, color.r, color.g, color.b, alpha );
        draw.Triangle( cx - r * 1.12f, cy + r * 0.10f, cx - r * 0.66f, cy - r * 0.44f, cx - r * 0.10f, cy + r * 0.10f,
                       color.r, color.g, color.b, alpha * 0.78f );

        draw.Triangle( cx + r * 1.12f, cy + r * 0.10f, cx + r * 0.50f, cy - r * 0.52f, cx + r * 0.08f, cy + r * 0.10f,
                       color.r, color.g, color.b, alpha * 0.74f );

        return;
    case EditorTab::OBJECT_ROCK_LUMP:
        draw.RoundedRect( cx - r * 1.02f, cy - r * 0.30f, r * 2.04f, r * 1.02f, r * 0.40f, color.r, color.g, color.b,
                          alpha );

        draw.Triangle( cx - r * 0.94f, cy + r * 0.12f, cx - r * 0.34f, cy - r * 0.86f, cx + r * 0.12f, cy + r * 0.12f,
                       color.r, color.g, color.b, alpha * 0.82f );

        return;
    case EditorTab::OBJECT_ROCK_SHARD:
        draw.Triangle( cx, cy - r * 1.16f, cx - r * 0.58f, cy + r * 0.90f, cx + r * 0.42f, cy + r * 0.90f, color.r, color.g,
                       color.b, alpha );

        draw.Triangle( cx + r * 0.14f, cy - r * 0.58f, cx + r * 0.96f, cy + r * 0.72f, cx + r * 0.40f, cy + r * 0.90f,
                       color.r, color.g, color.b, alpha * 0.72f );

        return;
    case EditorTab::OBJECT_ROCK_CHIPPED:
        draw.Rect( cx - r * 0.86f, cy - r * 0.62f, r * 1.44f, r * 1.36f, color.r, color.g, color.b, alpha );
        draw.Triangle( cx + r * 0.24f, cy - r * 0.62f, cx + r * 0.86f, cy - r * 0.04f, cx + r * 0.58f, cy - r * 0.62f,
                       color.r, color.g, color.b, alpha * 0.58f );

        draw.Triangle( cx - r * 0.86f, cy + r * 0.74f, cx - r * 0.38f, cy + r * 0.28f, cx + r * 0.58f, cy + r * 0.74f,
                       color.r, color.g, color.b, alpha * 0.76f );

        return;
    default:
        draw.Triangle( cx - r, cy + r, cx + r, cy + r, cx, cy - r, color.r, color.g, color.b, alpha );
        return;
    }
}


void DrawEditorMiniIcon( const UIDrawContext& draw, const UIRect& bounds, int objectType, const Style::UIColor& color,
                         float alpha )
{
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;
    const float r = (std::min)( bounds.w, bounds.h ) * 0.31f;
    const int type = std::clamp( objectType, 0, EditorTab::OBJECT_TYPE_COUNT - 1 );

    if ( type == EditorTab::OBJECT_BALL )
    {
        draw.RoundedRect( cx - r, cy - r, r * 2.0f, r * 2.0f, 999.0f, color.r, color.g, color.b, alpha );
        draw.RoundedRect( cx - r * 0.42f, cy - r * 0.48f, r * 0.38f, r * 0.30f, 999.0f, color.r, color.g, color.b,
                          alpha * 0.46f );

        return;
    }

    if ( type == EditorTab::OBJECT_SPHERE )
    {
        draw.RoundedRect( cx - r, cy - r, r * 2.0f, r * 2.0f, 999.0f, color.r, color.g, color.b, alpha * 0.74f );
        draw.Rect( cx - r * 0.72f, cy - 1.0f, r * 1.44f, 2.0f, color.r, color.g, color.b, alpha * 0.90f );
        draw.Rect( cx - 1.0f, cy - r * 0.72f, 2.0f, r * 1.44f, color.r, color.g, color.b, alpha * 0.58f );
        return;
    }

    if ( type == EditorTab::OBJECT_BOX )
    {
        draw.Rect( cx - r * 0.66f, cy - r * 0.86f, r * 1.42f, r * 1.42f, color.r, color.g, color.b, alpha * 0.48f );
        draw.Rect( cx - r * 0.92f, cy - r * 0.58f, r * 1.48f, r * 1.48f, color.r, color.g, color.b, alpha );
        draw.Rect( cx + r * 0.56f, cy - r * 0.38f, r * 0.22f, r * 1.26f, color.r, color.g, color.b, alpha * 0.56f );
        return;
    }

    if ( type == EditorTab::OBJECT_BRICK_WALL_200_SLEEP )
    {

        for ( int row = 0; row < 4; ++row )
        {
            const float rowY = cy - r * 0.78f + static_cast<float>( row ) * r * 0.42f;
            const float offset = ( row & 1 ) ? r * 0.22f : 0.0f;
            draw.Rect( cx - r * 0.96f + offset, rowY, r * 0.78f, r * 0.28f, color.r, color.g, color.b, alpha );
            draw.Rect( cx - r * 0.10f + offset, rowY, r * 0.78f, r * 0.28f, color.r, color.g, color.b, alpha * 0.82f );
        }

        return;
    }

    if ( type == EditorTab::OBJECT_BRICK_HOUSE_SLEEP || type == EditorTab::OBJECT_BRICK_HOUSE_HIGH_SLEEP ||
         type == EditorTab::OBJECT_CUTE_HOUSE_SLEEP || type == EditorTab::OBJECT_CUTE_HOUSE_HIGH_SLEEP ||
         type == EditorTab::OBJECT_TRIPLE_DECKER_SLEEP || type == EditorTab::OBJECT_TRIPLE_DECKER_HIGH_SLEEP )
    {
        draw.Rect( cx - r * 0.92f, cy - r * 0.12f, r * 1.84f, r * 1.06f, color.r, color.g, color.b, alpha );
        draw.Rect( cx - r * 0.62f, cy + r * 0.28f, r * 0.36f, r * 0.66f, color.r, color.g, color.b, alpha * 0.42f );
        draw.Rect( cx + r * 0.24f, cy + r * 0.24f, r * 0.36f, r * 0.34f, color.r, color.g, color.b, alpha * 0.46f );
        draw.Rect( cx - r * 0.98f, cy - r * 0.34f, r * 1.96f, r * 0.34f, color.r, color.g, color.b, alpha * 0.70f );
        draw.Rect( cx - r * 0.56f, cy - r * 0.82f, r * 1.12f, r * 0.54f, color.r, color.g, color.b, alpha * 0.56f );
        draw.Rect( cx + r * 0.42f, cy - r * 1.00f, r * 0.26f, r * 0.46f, color.r, color.g, color.b, alpha * 0.86f );
        return;
    }

    if ( type == EditorTab::OBJECT_RAGDOLL || type == EditorTab::OBJECT_RAGDOLL_SLEEP )
    {
        draw.RoundedRect( cx - r * 0.34f, cy - r * 1.05f, r * 0.68f, r * 0.68f, 999.0f, color.r, color.g, color.b, alpha );

        draw.Rect( cx - r * 0.42f, cy - r * 0.34f, r * 0.84f, r * 0.92f, color.r, color.g, color.b, alpha );
        draw.Rect( cx - r * 1.05f, cy - r * 0.18f, r * 0.52f, r * 0.32f, color.r, color.g, color.b, alpha * 0.82f );
        draw.Rect( cx + r * 0.53f, cy - r * 0.18f, r * 0.52f, r * 0.32f, color.r, color.g, color.b, alpha * 0.82f );
        draw.Rect( cx - r * 0.50f, cy + r * 0.65f, r * 0.34f, r * 0.68f, color.r, color.g, color.b, alpha * 0.82f );
        draw.Rect( cx + r * 0.16f, cy + r * 0.65f, r * 0.34f, r * 0.68f, color.r, color.g, color.b, alpha * 0.82f );

        if ( type == EditorTab::OBJECT_RAGDOLL_SLEEP )
        {
            draw.Rect( cx + r * 0.56f, cy - r * 1.04f, r * 0.48f, 2.0f, color.r, color.g, color.b, alpha );
            draw.Rect( cx + r * 0.70f, cy - r * 0.82f, r * 0.40f, 2.0f, color.r, color.g, color.b, alpha * 0.78f );
        }

        return;
    }

    if ( IsEditorMiniRootType( type ) )
    {
        DrawEditorMiniRootSilhouette( draw, bounds, type == EditorTab::OBJECT_ROOT_LARGE, color, alpha );
        return;
    }

    int treeType = EDITOR_MINI_TREE_TYPE_NONE;
    int treePlacement = EDITOR_MINI_TREE_PLACEMENT_NONE;
    bool treeSlope = false;
    bool treeShedding = false;

    if ( EditorMiniTreeVisualForType( type, treeType, treePlacement, treeSlope, treeShedding ) )
    {
        DrawEditorMiniTreeSilhouette( draw, bounds, treeType, treePlacement == EDITOR_MINI_TREE_PLACEMENT_ROOTED, treeSlope,
                                      treeShedding,

                                      color, alpha );
        return;
    }

    if ( IsEditorMiniHullType( type ) )
    {
        DrawEditorMiniHullSilhouette( draw, bounds, type, color, alpha );
        return;
    }

    if ( IsEditorMiniRockType( type ) )
    {
        DrawEditorMiniRockSilhouette( draw, bounds, type, color, alpha );
        return;
    }

    draw.Triangle( cx - r, cy + r, cx + r, cy + r, cx, cy - r, color.r, color.g, color.b, alpha );
}


void DrawEditorMiniGlyph( const UIDrawContext& draw, const UIRect& bounds, int objectType )
{
    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedRect( bounds.x, bounds.y, bounds.w, bounds.h, 6.0f, palette.control.r, palette.control.g, palette.control.b,
                      0.92f );

    draw.Outline( bounds.x, bounds.y, bounds.w, bounds.h, palette.border.r, palette.border.g, palette.border.b, 0.75f );
    DrawEditorMiniIcon( draw, bounds, objectType, palette.accentStrong, 0.92f );
}


void DrawEditorMiniVariantMarker( const UIDrawContext& draw, const UIRect& bounds, int variant, const Style::UIColor& color )
{
    const float x = bounds.x + bounds.w - 8.0f;
    const float y = bounds.y + bounds.h - 8.0f;

    if ( variant == EDITOR_MINI_TREE_PLACEMENT_SLEEPING )
    {
        draw.Rect( x - 3.0f, y - 3.0f, 2.0f, 6.0f, color.r, color.g, color.b, 0.92f );
        draw.Rect( x + 1.0f, y - 3.0f, 2.0f, 6.0f, color.r, color.g, color.b, 0.92f );
        return;
    }

    if ( variant == EDITOR_MINI_TREE_PLACEMENT_ROOTED )
    {
        draw.Rect( x - 1.0f, y - 5.0f, 2.0f, 7.0f, color.r, color.g, color.b, 0.92f );
        draw.Triangle( x, y, x - 5.0f, y + 4.0f, x - 1.0f, y + 1.0f, color.r, color.g, color.b, 0.92f );
        draw.Triangle( x, y, x + 5.0f, y + 4.0f, x + 1.0f, y + 1.0f, color.r, color.g, color.b, 0.92f );
        return;
    }

    draw.RoundedRect( x - 3.0f, y - 3.0f, 6.0f, 6.0f, 999.0f, color.r, color.g, color.b, 0.88f );
}


void DrawEditorMiniHoldMarker( const UIDrawContext& draw, const UIRect& bounds, const Style::UIColor& color, bool active )
{
    const float dot = (std::max)( 2.0f, std::floor( bounds.w * 0.075f ) );
    const float x = bounds.x + bounds.w - dot - 5.0f;
    const float y = bounds.y + 5.0f;
    const float gap = (std::max)( 1.0f, dot * 0.75f );
    const float alpha = active ? 0.95f : 0.52f;
    draw.RoundedRect( x, y, dot, dot, 999.0f, color.r, color.g, color.b, alpha );
    draw.RoundedRect( x, y + dot + gap, dot, dot, 999.0f, color.r, color.g, color.b, alpha );
    draw.RoundedRect( x, y + ( dot + gap ) * 2.0f, dot, dot, 999.0f, color.r, color.g, color.b, alpha );
}


void DrawEditorMiniPaletteButton( const UIDrawContext& draw, const UIRect& bounds, int objectType, bool selected, bool hot,
                                  int variantMarker, bool holdCapable, bool holdActive )
{
    const Style::UIPalette& palette = Style::Palette();
    Style::UIColor fill = hot ? palette.controlHover : palette.control;

    if ( selected )
    {
        fill = palette.windowRaised;
    }

    fill.a = selected ? 0.96f : 0.88f;

    draw.RoundedRect( bounds.x + 2.0f, bounds.y + 2.0f, bounds.w, bounds.h, Style::Radii().smallButton, 0.0f, 0.0f, 0.0f,
                      0.20f );

    draw.RoundedPanel( bounds, Style::Radii().smallButton, fill, selected ? palette.accentStrong : palette.border );
    const Style::UIColor icon = selected ? palette.accentStrong : palette.textSecondary;
    const UIRect iconBounds = { bounds.x + 3.0f, bounds.y + 3.0f, (std::max)( 4.0f, bounds.w - 6.0f ),
                                (std::max)( 4.0f, bounds.h - 6.0f ) };

    DrawEditorMiniIcon( draw, iconBounds, objectType, icon, selected || hot ? 0.98f : 0.86f );

    if ( variantMarker >= 0 )
    {
        DrawEditorMiniVariantMarker( draw, bounds, variantMarker, selected ? palette.accentStrong : palette.textMuted );
    }

    if ( holdCapable )
    {
        DrawEditorMiniHoldMarker( draw, bounds, holdActive ? palette.accentStrong : palette.textMuted, holdActive );
    }
}


void DrawEditorMiniTooltip( const UIDrawContext& draw, const UIRect& anchor, const char* label, int screenW, int screenH )
{

    if ( !label || label[0] == '\0' || screenW <= 0 || screenH <= 0 )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    const float textSize = 10.5f;
    const float padX = 8.0f;
    const float padY = 5.0f;
    const float margin = 6.0f;
    const float maxTextW = (std::max)( 32.0f,
                                       (std::min)( 220.0f, static_cast<float>( screenW ) - margin * 2.0f - padX * 2.0f ) );

    char tooltip[80] = {};
    snprintf( tooltip, sizeof( tooltip ), "%s", label );
    Chrome::FitTitleText( tooltip, sizeof( tooltip ), textSize, maxTextW );

    const float textW = UIFontMetrics::MeasureText( textSize, tooltip );
    const float w = std::ceil( textW + padX * 2.0f );
    const float h = std::ceil( textSize + padY * 2.0f + 2.0f );

    float x = anchor.x + anchor.w + 10.0f;

    if ( x + w > static_cast<float>( screenW ) - margin )
    {
        x = anchor.x - w - 10.0f;
    }

    const float maxX = (std::max)( margin, static_cast<float>( screenW ) - w - margin );
    x = std::clamp( x, margin, maxX );

    float y = anchor.y + anchor.h * 0.5f - h * 0.5f;
    const float maxY = (std::max)( margin, static_cast<float>( screenH ) - h - margin );
    y = std::clamp( y, margin, maxY );

    draw.RoundedRect( x + 2.0f, y + 2.0f, w, h, Style::Radii().smallButton, 0.0f, 0.0f, 0.0f, 0.28f );
    draw.RoundedPanel( { x, y, w, h }, Style::Radii().smallButton, palette.windowRaised, palette.border );
    draw.Text( x + padX, y + padY + 1.0f, textSize, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b,
               tooltip );
}


int EditorMiniRagdollObjectType( int mode )
{
    return mode == EDITOR_MINI_RAGDOLL_MODE_SLEEPING ? EditorTab::OBJECT_RAGDOLL_SLEEP : EditorTab::OBJECT_RAGDOLL;
}


bool EditorMiniSelectionRequestsStatic( int holdMode, int treePlacement, bool& outPlaceStatic )
{

    if ( holdMode == EDITOR_MINI_HOLD_MODE_TREE_TYPES )
    {
        outPlaceStatic = treePlacement != EDITOR_MINI_TREE_PLACEMENT_SLEEPING;
        return true;
    }

    if ( holdMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES )
    {
        outPlaceStatic = false;
        return true;
    }

    outPlaceStatic = false;
    return false;
}


const char* EditorMiniPaletteEntryLabel( const EditorMiniPaletteEntry& entry )
{

    if ( entry.holdMode == EDITOR_MINI_HOLD_MODE_TREE_TYPES )
    {

        switch ( entry.treePlacement )
        {
        case EDITOR_MINI_TREE_PLACEMENT_FIXED:
            return "Fixed tree";
        case EDITOR_MINI_TREE_PLACEMENT_SLEEPING:
            return "Sleeping tree";
        case EDITOR_MINI_TREE_PLACEMENT_ROOTED:
            return "Rooted tree";
        default:
            break;
        }
    }

    if ( entry.holdMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES )
    {
        return "Ragdoll";
    }

    return EditorTab::ObjectLabel( entry.objectType );
}


void DrawEditorMiniPalette( const UIDrawContext& draw, const EditorMiniPaletteLayout& layout, int editorObjectType,
                            bool editorPlaceStatic, int mouseX, int mouseY, int flyoutTreePlacement, int flyoutHoldMode,
                            int pressedEntry, int screenW, int screenH )
{

    // Invariant: Draw order matches hit priority. Flyout options render after
    // root buttons because input tests flyout containment before the compact
    // button strip when the palette is open.
    const Style::UIPalette& palette = Style::Palette();
    int currentTreePlacement = EDITOR_MINI_TREE_PLACEMENT_NONE;
    int currentTreeType = EDITOR_MINI_TREE_TYPE_NONE;
    const bool currentTreeState = EditorMiniPaletteTreeStateForType( editorObjectType, editorPlaceStatic,
                                                                     currentTreePlacement, currentTreeType );

    const char* tooltipLabel = nullptr;
    UIRect tooltipAnchor = {};

    for ( int i = 0; i < layout.buttonCount; ++i )
    {
        const EditorMiniPaletteEntry& entry = kEditorMiniPaletteEntries[i];
        const bool treeEntry = IsEditorMiniTreePlacementValid( entry.treePlacement );
        const bool ragdollEntry = entry.holdMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES;
        const bool selected = treeEntry ? ( currentTreeState && currentTreePlacement == entry.treePlacement )
                                        : ( ragdollEntry ? ( editorObjectType == EditorTab::OBJECT_RAGDOLL ||
                                                             editorObjectType == EditorTab::OBJECT_RAGDOLL_SLEEP )
                                                         : entry.objectType == editorObjectType );

        const bool hot = layout.buttons[i].Contains( mouseX, mouseY );
        const int marker = treeEntry ? entry.treePlacement
                                     : ( ragdollEntry && editorObjectType == EditorTab::OBJECT_RAGDOLL_SLEEP
                                             ? EDITOR_MINI_TREE_PLACEMENT_SLEEPING
                                             : -1 );

        const bool holdCapable = entry.holdMode != EDITOR_MINI_HOLD_MODE_NONE;
        const bool holdActive = holdCapable && i == pressedEntry && flyoutHoldMode != EDITOR_MINI_HOLD_MODE_NONE;
        DrawEditorMiniPaletteButton( draw, layout.buttons[i], entry.objectType, selected, hot, marker, holdCapable,
                                     holdActive );

        if ( hot )
        {
            tooltipLabel = EditorMiniPaletteEntryLabel( entry );
            tooltipAnchor = layout.buttons[i];
        }
    }

    if ( layout.flyoutVisible )
    {
        draw.RoundedRect( layout.flyoutBounds.x + 2.0f, layout.flyoutBounds.y + 2.0f, layout.flyoutBounds.w,
                          layout.flyoutBounds.h, Style::Radii().control, 0.0f, 0.0f, 0.0f, 0.24f );

        draw.RoundedPanel( layout.flyoutBounds, Style::Radii().control, palette.window, palette.border );

        for ( int option = 0; option < layout.flyoutOptionCount; ++option )
        {
            int marker = -1;
            int type = EditorTab::OBJECT_BOX;
            bool selected = false;

            if ( flyoutHoldMode == EDITOR_MINI_HOLD_MODE_TREE_TYPES )
            {
                marker = flyoutTreePlacement;
                type = EditorMiniTreeObjectType( option, flyoutTreePlacement );
                selected = currentTreeState && currentTreePlacement == flyoutTreePlacement && currentTreeType == option;
            }
            else if ( flyoutHoldMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES )
            {
                type = EditorMiniRagdollObjectType( option );
                marker = option == EDITOR_MINI_RAGDOLL_MODE_SLEEPING ? EDITOR_MINI_TREE_PLACEMENT_SLEEPING : -1;
                selected = type == editorObjectType;
            }

            const bool hot = layout.flyoutOptions[option].Contains( mouseX, mouseY );
            DrawEditorMiniPaletteButton( draw, layout.flyoutOptions[option], type, selected, hot, marker, false, false );

            if ( hot )
            {
                tooltipLabel = EditorTab::ObjectLabel( type );
                tooltipAnchor = layout.flyoutOptions[option];
            }
        }
    }

    if ( tooltipLabel )
    {
        DrawEditorMiniTooltip( draw, tooltipAnchor, tooltipLabel, screenW, screenH );
    }
}


void DrawEditorMinimizedWindow( const UIDrawContext& draw, const UIRect& minimized, const InGameUIFrameData& data,
                                int mouseX, int mouseY )
{
    const Style::UIPalette& palette = Style::Palette();
    const EditorMinimizedStatusLayout layout = BuildEditorMinimizedStatusLayout( minimized, data );
    draw.RoundedRect( minimized.x + 4.0f, minimized.y + 5.0f, minimized.w, minimized.h, Style::Radii().window, 0.0f, 0.0f,
                      0.0f, 0.26f );

    draw.RoundedPanel( minimized, Style::Radii().window, palette.window, palette.border );

    DrawEditorMiniGlyph( draw, layout.glyph, data.editorObjectType );

    const char* modeLabel = data.editorPlacementMode ? "Place" : "Gizmo";
    const char* bodyLabel = data.editorPlaceStatic ? "Static" : "Dynamic";
    const char* alignLabel = data.editorTerrainAlign ? "Align" : "Level";

    char shapeLabel[64] = {};
    snprintf( shapeLabel, sizeof( shapeLabel ), "%s", EditorTab::ObjectLabel( data.editorObjectType ) );
    Chrome::FitTitleText( shapeLabel, sizeof( shapeLabel ), 12.0f, layout.labelMaxW );
    draw.Text( layout.labelX, minimized.y + 13.0f, 12.0f, palette.textPrimary.r, palette.textPrimary.g,
               palette.textPrimary.b, shapeLabel );

    Style::UIColor modeFill = palette.accent;
    modeFill.a = 0.92f;
    Style::UIColor bodyFill = data.editorPlaceStatic ? palette.control : palette.warningAccent;
    bodyFill.a = 0.92f;
    Style::UIColor alignFill = data.editorTerrainAlign ? palette.accentStrong : palette.control;
    alignFill.a = 0.92f;
    DrawEditorMiniChip( draw, layout.modeChip.x, layout.modeChip.y, modeLabel, modeFill, palette.textPrimary,
                        layout.modeChip.Contains( mouseX, mouseY ) );

    DrawEditorMiniChip( draw, layout.bodyChip.x, layout.bodyChip.y, bodyLabel, bodyFill, palette.textPrimary,
                        layout.bodyChip.Contains( mouseX, mouseY ) );

    DrawEditorMiniChip( draw, layout.alignChip.x, layout.alignChip.y, alignLabel, alignFill, palette.textPrimary,
                        layout.alignChip.Contains( mouseX, mouseY ) );

    draw.RoundedPanel( layout.restoreButton, Style::Radii().smallButton, palette.control, palette.border );
    const float plusX = layout.restoreButton.x + layout.restoreButton.w * 0.5f;
    const float plusY = layout.restoreButton.y + layout.restoreButton.h * 0.5f;
    draw.Rect( plusX - 5.0f, plusY - 1.0f, 10.0f, 2.0f, palette.textSecondary.r, palette.textSecondary.g,
               palette.textSecondary.b, 0.96f );

    draw.Rect( plusX - 1.0f, plusY - 5.0f, 2.0f, 10.0f, palette.textSecondary.r, palette.textSecondary.g,
               palette.textSecondary.b, 0.96f );
}
} // namespace SkullbonezCore::UI::FrameComposition
