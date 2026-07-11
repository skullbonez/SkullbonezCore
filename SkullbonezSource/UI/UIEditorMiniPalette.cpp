/*
File: UIEditorMiniPalette.cpp
Purpose:
  Owns editor mini-palette mapping, layout, hit testing, and shared fitted-text policy.

Mental model:
  The palette is a value-driven UI surface. Layout and selection policy produce
  records that both input and drawing consume without reaching into InGameUI
  retained state.

Glossary:
  Mini palette: Compact editor placement surface shown while UI is minimized.
  Hold mode: Press-duration gesture that opens tree or ragdoll variants.
  Flyout: Secondary variant row anchored to one palette entry.

Invariants:
  - Hit testing and drawing use the same EditorMiniPaletteLayout geometry.
  - Functions retain no frame or owner reference after returning.
  - Object-type mappings remain stable authored editor vocabulary.

Related:
  - UIFrameComposition.h owns shared layout records and helper contracts.
  - UI.cpp owns the surrounding UI frame.
*/
#include "UIFrameComposition.h"

namespace SkullbonezCore::UI::FrameComposition
{
bool IsEditorMiniTreePlacementValid( int placement )
{
    return placement >= EDITOR_MINI_TREE_PLACEMENT_FIXED && placement <= EDITOR_MINI_TREE_PLACEMENT_ROOTED;
}

int EditorMiniPaletteFlyoutOptionCount( int holdMode )
{
    if ( holdMode == EDITOR_MINI_HOLD_MODE_TREE_TYPES )
    {
        return EDITOR_MINI_TREE_TYPE_COUNT;
    }
    if ( holdMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES )
    {
        return EDITOR_MINI_RAGDOLL_MODE_COUNT;
    }
    return 0;
}

bool EditorMiniTreeTypeForType( int objectType, int& outTreeType, int& outPlacement )
{
    switch ( objectType )
    {
    case EditorTab::OBJECT_TREE_SMALL:
        outTreeType = EDITOR_MINI_TREE_TYPE_SMALL;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_FIXED;
        return true;
    case EditorTab::OBJECT_TREE_SMALL_SLEEP:
        outTreeType = EDITOR_MINI_TREE_TYPE_SMALL;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_SLEEPING;
        return true;
    case EditorTab::OBJECT_TREE_SMALL_ROOTED:
        outTreeType = EDITOR_MINI_TREE_TYPE_SMALL;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_ROOTED;
        return true;
    case EditorTab::OBJECT_TREE_BIG:
        outTreeType = EDITOR_MINI_TREE_TYPE_PINE;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_FIXED;
        return true;
    case EditorTab::OBJECT_TREE_BIG_SLEEP:
        outTreeType = EDITOR_MINI_TREE_TYPE_PINE;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_SLEEPING;
        return true;
    case EditorTab::OBJECT_TREE_BIG_ROOTED:
        outTreeType = EDITOR_MINI_TREE_TYPE_PINE;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_ROOTED;
        return true;
    case EditorTab::OBJECT_TREE_CEDAR:
        outTreeType = EDITOR_MINI_TREE_TYPE_CEDAR;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_FIXED;
        return true;
    case EditorTab::OBJECT_TREE_CEDAR_SLEEP:
        outTreeType = EDITOR_MINI_TREE_TYPE_CEDAR;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_SLEEPING;
        return true;
    case EditorTab::OBJECT_TREE_CEDAR_ROOTED:
        outTreeType = EDITOR_MINI_TREE_TYPE_CEDAR;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_ROOTED;
        return true;
    default:
        outTreeType = EDITOR_MINI_TREE_TYPE_NONE;
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_NONE;
        return false;
    }
}

bool EditorMiniPaletteTreeStateForType( int objectType, bool editorPlaceStatic, int& outPlacement, int& outTreeType )
{
    int treeType = EDITOR_MINI_TREE_TYPE_NONE;
    int placement = EDITOR_MINI_TREE_PLACEMENT_NONE;
    if ( !EditorMiniTreeTypeForType( objectType, treeType, placement ) )
    {
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_NONE;
        outTreeType = EDITOR_MINI_TREE_TYPE_NONE;
        return false;
    }
    if ( placement == EDITOR_MINI_TREE_PLACEMENT_FIXED && !editorPlaceStatic )
    {
        outPlacement = EDITOR_MINI_TREE_PLACEMENT_NONE;
        outTreeType = treeType;
        return false;
    }
    outPlacement = placement;
    outTreeType = treeType;
    return true;
}

int EditorMiniTreeObjectType( int treeType, int placement )
{
    if ( treeType == EDITOR_MINI_TREE_TYPE_SMALL )
    {
        if ( placement == EDITOR_MINI_TREE_PLACEMENT_SLEEPING )
        {
            return EditorTab::OBJECT_TREE_SMALL_SLEEP;
        }
        if ( placement == EDITOR_MINI_TREE_PLACEMENT_ROOTED )
        {
            return EditorTab::OBJECT_TREE_SMALL_ROOTED;
        }
        return EditorTab::OBJECT_TREE_SMALL;
    }
    if ( treeType == EDITOR_MINI_TREE_TYPE_PINE )
    {
        if ( placement == EDITOR_MINI_TREE_PLACEMENT_SLEEPING )
        {
            return EditorTab::OBJECT_TREE_BIG_SLEEP;
        }
        if ( placement == EDITOR_MINI_TREE_PLACEMENT_ROOTED )
        {
            return EditorTab::OBJECT_TREE_BIG_ROOTED;
        }
        return EditorTab::OBJECT_TREE_BIG;
    }
    if ( treeType == EDITOR_MINI_TREE_TYPE_CEDAR )
    {
        if ( placement == EDITOR_MINI_TREE_PLACEMENT_SLEEPING )
        {
            return EditorTab::OBJECT_TREE_CEDAR_SLEEP;
        }
        if ( placement == EDITOR_MINI_TREE_PLACEMENT_ROOTED )
        {
            return EditorTab::OBJECT_TREE_CEDAR_ROOTED;
        }
        return EditorTab::OBJECT_TREE_CEDAR;
    }
    return EditorTab::OBJECT_TREE_SMALL;
}

EditorMiniPaletteLayout BuildEditorMiniPaletteLayout( int screenW,
                                                      int screenH,
                                                      const UIRect& minimized,
                                                      int flyoutAnchorEntry,
                                                      bool flyoutOpen )
{
    // Concept: The mini palette is the minimized editor's primary command
    // surface. One layout object drives drawing, hit boxes, flyout containment,
    // and tooltip placement so visual and input geometry cannot drift apart.
    EditorMiniPaletteLayout layout;
    layout.buttonCount = EDITOR_MINI_PALETTE_ENTRY_COUNT;

    constexpr float margin = 14.0f;
    const float topY = margin;
    const float bottomLimit = (std::max)( margin + 84.0f, minimized.y - 10.0f );
    const float availableH = (std::max)( 80.0f, bottomLimit - topY );
    float gap = 4.0f;
    float buttonSize = 32.0f;
    float requiredH =
        static_cast<float>( layout.buttonCount ) * buttonSize + static_cast<float>( layout.buttonCount - 1 ) * gap;
    if ( requiredH > availableH )
    {
        gap = 2.0f;
        buttonSize = std::floor( ( availableH - static_cast<float>( layout.buttonCount - 1 ) * gap ) /
                                 static_cast<float>( layout.buttonCount ) );
        buttonSize = std::clamp( buttonSize, 10.0f, 32.0f );
        requiredH =
            static_cast<float>( layout.buttonCount ) * buttonSize + static_cast<float>( layout.buttonCount - 1 ) * gap;
    }

    layout.buttonSize = buttonSize;
    const float x = margin;
    for ( int i = 0; i < layout.buttonCount; ++i )
    {
        layout.buttons[i] = { x, topY + static_cast<float>( i ) * ( buttonSize + gap ), buttonSize, buttonSize };
    }
    layout.bounds = { x, topY, buttonSize, requiredH };

    if ( flyoutOpen && flyoutAnchorEntry >= 0 && flyoutAnchorEntry < layout.buttonCount )
    {
        const int optionCount =
            EditorMiniPaletteFlyoutOptionCount( kEditorMiniPaletteEntries[flyoutAnchorEntry].holdMode );
        if ( optionCount <= 0 )
        {
            return layout;
        }
        const UIRect anchor = layout.buttons[flyoutAnchorEntry];
        const float optionSize = buttonSize;
        const float optionGap = (std::max)( 2.0f, std::floor( buttonSize * 0.12f ) );
        const float padding = 4.0f;
        const float flyoutW = padding * 2.0f + optionSize * static_cast<float>( optionCount ) +
                              optionGap * static_cast<float>( optionCount - 1 );
        const float flyoutH = padding * 2.0f + optionSize;
        float flyoutX = anchor.x + anchor.w + 8.0f;
        if ( flyoutX + flyoutW > static_cast<float>( screenW ) - margin )
        {
            flyoutX = anchor.x + anchor.w + 4.0f;
        }
        const float maxY = (std::max)( margin, static_cast<float>( screenH ) - margin - flyoutH );
        const float flyoutY = std::clamp( anchor.y + ( anchor.h - flyoutH ) * 0.5f, margin, maxY );
        layout.flyoutBounds = { flyoutX, flyoutY, flyoutW, flyoutH };
        layout.flyoutOptionCount = optionCount;
        for ( int i = 0; i < optionCount; ++i )
        {
            layout.flyoutOptions[i] = { flyoutX + padding + static_cast<float>( i ) * ( optionSize + optionGap ),
                                        flyoutY + padding,
                                        optionSize,
                                        optionSize };
        }
        layout.flyoutVisible = true;
    }

    return layout;
}

int HitEditorMiniPaletteButton( const EditorMiniPaletteLayout& layout, int mouseX, int mouseY )
{
    for ( int i = 0; i < layout.buttonCount; ++i )
    {
        if ( layout.buttons[i].Contains( mouseX, mouseY ) )
        {
            return i;
        }
    }
    return -1;
}

int HitEditorMiniPaletteFlyoutOption( const EditorMiniPaletteLayout& layout, int mouseX, int mouseY )
{
    if ( !layout.flyoutVisible )
    {
        return -1;
    }

    for ( int i = 0; i < layout.flyoutOptionCount; ++i )
    {
        if ( layout.flyoutOptions[i].Contains( mouseX, mouseY ) )
        {
            return i;
        }
    }
    return -1;
}

bool EditorMiniPaletteContains( const EditorMiniPaletteLayout& layout, int mouseX, int mouseY )
{
    return HitEditorMiniPaletteButton( layout, mouseX, mouseY ) >= 0 ||
           HitEditorMiniPaletteFlyoutOption( layout, mouseX, mouseY ) >= 0 ||
           ( layout.flyoutVisible && layout.flyoutBounds.Contains( mouseX, mouseY ) );
}

bool IsBlockVisible( float contentY, float contentH, float blockY, float blockH )
{
    return blockY + blockH >= contentY && blockY <= contentY + contentH;
}

void DrawHitboxRect( const UIDrawContext& draw,
                     const UIRect& bounds,
                     float r,
                     float g,
                     float b,
                     float fillA,
                     float outlineA )
{
    if ( bounds.w <= 0.0f || bounds.h <= 0.0f )
    {
        return;
    }

    draw.Rect( bounds.x, bounds.y, bounds.w, bounds.h, r, g, b, fillA );
    draw.Outline( bounds.x, bounds.y, bounds.w, bounds.h, r, g, b, outlineA );
    if ( bounds.w > 4.0f && bounds.h > 4.0f )
    {
        draw.Outline( bounds.x + 1.0f, bounds.y + 1.0f, bounds.w - 2.0f, bounds.h - 2.0f, r, g, b, outlineA * 0.42f );
    }
}

void DrawComboHitboxes( const UIDrawContext& draw, const UIComboBox& combo, int optionCount, float r, float g, float b )
{
    DrawHitboxRect( draw, combo.Bounds(), r, g, b );
    if ( combo.IsOpen() )
    {
        DrawHitboxRect( draw, combo.DropdownBounds( optionCount ), 0.18f, 0.58f, 1.0f, 0.078f, 0.96f );
    }
}

void DrawTabHitboxes( const UIDrawContext& draw, const UITabBar& tabBar, int tabCount )
{
    const UIRect tabs = tabBar.Bounds();
    if ( tabCount <= 0 || tabs.w <= 0.0f || tabs.h <= 0.0f )
    {
        return;
    }

    const float tabW = tabs.w / static_cast<float>( tabCount );
    for ( int i = 0; i < tabCount; ++i )
    {
        DrawHitboxRect( draw,
                        { tabs.x + static_cast<float>( i ) * tabW, tabs.y, tabW, tabs.h },
                        1.0f,
                        0.80f,
                        0.18f,
                        0.052f,
                        0.84f );
    }
}

int SceneDropdownHitboxOptionCount( const SceneTab::UISceneTabState& state, const InGameUIFrameData& data )
{
    const int filteredSceneCount =
        SceneTab::CountFilteredOptions( data.sceneOptions, data.sceneOptionCount, state.filter );
    const int sceneVisibleCount = SceneComboVisibleCount( filteredSceneCount );
    return sceneVisibleCount == 0 && state.filter[0] != '\0' ? 1 : sceneVisibleCount;
}

void EllipsizeToWidth( char* text, size_t textSize, float pxSize, float maxWidth )
{
    if ( !text || textSize == 0 || Text2d::MeasureText( pxSize, text ) <= maxWidth )
    {
        return;
    }

    size_t len = strlen( text );
    while ( len > 3 && Text2d::MeasureText( pxSize, text ) > maxWidth )
    {
        text[len - 3] = '.';
        text[len - 2] = '.';
        text[len - 1] = '.';
        text[len] = '\0';
        --len;
    }
}

void DrawFittedText( const UIDrawContext& draw,
                     float x,
                     float y,
                     float pxSize,
                     const Style::UIColor& color,
                     const char* value,
                     float maxWidth )
{
    char text[192] = {};
    snprintf( text, sizeof( text ), "%s", value ? value : "" );
    EllipsizeToWidth( text, sizeof( text ), pxSize, maxWidth );
    draw.Text( x, y, pxSize, color.r, color.g, color.b, text );
}

int RenderSliderIndexFromActiveSlider( int activeSlider )
{
    const int index = activeSlider - UI_RENDER_SLIDER_BASE;
    return ( index >= 0 && index < static_cast<int>( UIRenderParam::Count ) ) ? index : -1;
}

float RenderSliderY( int index, float baseY )
{
    float y = baseY;
    for ( int i = 0; i <= index; ++i )
    {
        if ( kRenderSliderSpecs[i].section )
        {
            y += UI_RENDER_SECTION_H;
        }
        if ( i == index )
        {
            return y;
        }
        y += UI_RENDER_ROW_H;
    }
    return y;
}

int RenderContentHeight()
{
    float height = UI_RENDER_START_Y;
    for ( int i = 0; i < static_cast<int>( UIRenderParam::Count ); ++i )
    {
        if ( kRenderSliderSpecs[i].section )
        {
            height += UI_RENDER_SECTION_H;
        }
        height += UI_RENDER_ROW_H;
    }
    return static_cast<int>( height + 18.0f );
}

int RenderTargetsContentHeight()
{
    return static_cast<int>( UI_TARGETS_CONTENT_H );
}

float RenderValueForParam( const OrdinaryRenderConfig& ordinary, UIRenderParam param )
{
    switch ( param )
    {
    case UIRenderParam::SunIntensity:
        return ordinary.sunIntensity;
    case UIRenderParam::SunRed:
        return ordinary.sunColorR;
    case UIRenderParam::SunGreen:
        return ordinary.sunColorG;
    case UIRenderParam::SunBlue:
        return ordinary.sunColorB;
    case UIRenderParam::AmbientStrength:
        return ordinary.ambientStrength;
    case UIRenderParam::SkyRed:
        return ordinary.skyAmbientR;
    case UIRenderParam::SkyGreen:
        return ordinary.skyAmbientG;
    case UIRenderParam::SkyBlue:
        return ordinary.skyAmbientB;
    case UIRenderParam::GroundRed:
        return ordinary.groundAmbientR;
    case UIRenderParam::GroundGreen:
        return ordinary.groundAmbientG;
    case UIRenderParam::GroundBlue:
        return ordinary.groundAmbientB;
    case UIRenderParam::ShadowStrength:
        return ordinary.shadow.strength;
    case UIRenderParam::ShadowSoftness:
        return ordinary.shadow.softness;
    case UIRenderParam::ShadowDepthBias:
        return ordinary.shadow.depthBias;
    case UIRenderParam::ShadowSlopeBias:
        return ordinary.shadow.slopeBias;
    case UIRenderParam::WaterRed:
        return ordinary.waterTintR;
    case UIRenderParam::WaterGreen:
        return ordinary.waterTintG;
    case UIRenderParam::WaterBlue:
        return ordinary.waterTintB;
    case UIRenderParam::WaterAlpha:
        return ordinary.waterAlpha;
    case UIRenderParam::WaterReflection:
        return ordinary.waterReflectionStrength;
    case UIRenderParam::WaterFresnel:
        return ordinary.waterFresnelF0;
    case UIRenderParam::BallRoughness:
        return ordinary.ballRoughnessScale;
    case UIRenderParam::BallSpecular:
        return ordinary.ballSpecularScale;
    case UIRenderParam::BoxRoughness:
        return ordinary.boxRoughnessScale;
    case UIRenderParam::BoxSpecular:
        return ordinary.boxSpecularScale;
    default:
        return 0.0f;
    }
}

void SetRenderSliderResult( InGameUIInputResult& result,
                            const UISlider& slider,
                            int mouseX,
                            const RenderSliderSpec& spec )
{
    result.commands.renderTuning.requestedParam = spec.param;
    result.commands.renderTuning.requestedValue =
        slider.ValueFromMouse( mouseX, spec.minValue, spec.maxValue, spec.step );
}

float EditorMiniChipWidth( const char* label )
{
    return Text2d::MeasureText( 10.5f, label ? label : "" ) + 18.0f;
}


EditorMinimizedStatusLayout BuildEditorMinimizedStatusLayout( const UIRect& minimized,
                                                              bool editorPlacementMode,
                                                              bool editorPlaceStatic,
                                                              bool editorTerrainAlign )
{
    EditorMinimizedStatusLayout layout;
    layout.restoreButton = { minimized.x + minimized.w - 36.0f, minimized.y + 7.0f, 26.0f, 22.0f };
    layout.glyph = { minimized.x + 11.0f, minimized.y + 7.0f, 24.0f, 24.0f };

    const char* modeLabel = editorPlacementMode ? "Place" : "Gizmo";
    const char* bodyLabel = editorPlaceStatic ? "Static" : "Dynamic";
    const char* alignLabel = editorTerrainAlign ? "Align" : "Level";
    const float alignW = EditorMiniChipWidth( alignLabel );
    const float bodyW = EditorMiniChipWidth( bodyLabel );
    const float modeW = EditorMiniChipWidth( modeLabel );
    const float chipY = minimized.y + 9.0f;
    const float alignX = layout.restoreButton.x - 10.0f - alignW;
    const float bodyX = alignX - 8.0f - bodyW;
    const float modeX = bodyX - 8.0f - modeW;
    layout.modeChip = { modeX, chipY, modeW, 20.0f };
    layout.bodyChip = { bodyX, chipY, bodyW, 20.0f };
    layout.alignChip = { alignX, chipY, alignW, 20.0f };
    layout.labelX = layout.glyph.x + layout.glyph.w + 10.0f;
    layout.labelMaxW = (std::max)( 42.0f, modeX - layout.labelX - 10.0f );
    return layout;
}


EditorMinimizedStatusLayout BuildEditorMinimizedStatusLayout( const UIRect& minimized, const InGameUIFrameData& data )
{
    return BuildEditorMinimizedStatusLayout( minimized,
                                             data.editorPlacementMode,
                                             data.editorPlaceStatic,
                                             data.editorTerrainAlign );
}


float EditorMinimizedWidth( const InGameUIFrameData& data, int screenW )
{
    constexpr float margin = 14.0f;
    const float maxW = (std::max)( 154.0f, static_cast<float>( screenW ) - margin * 2.0f );
    const char* shapeLabel = EditorTab::ObjectLabel( data.editorObjectType );
    const char* modeLabel = data.editorPlacementMode ? "Place" : "Gizmo";
    const char* bodyLabel = data.editorPlaceStatic ? "Static" : "Dynamic";
    const char* alignLabel = data.editorTerrainAlign ? "Align" : "Level";
    const float desiredW = 140.0f + Text2d::MeasureText( 12.0f, shapeLabel ) + EditorMiniChipWidth( modeLabel ) +
                           EditorMiniChipWidth( bodyLabel ) + EditorMiniChipWidth( alignLabel );
    return std::clamp( desiredW, 376.0f, maxW );
}


void DrawEditorMiniChip( const UIDrawContext& draw,
                         float x,
                         float y,
                         const char* label,
                         const Style::UIColor& fill,
                         const Style::UIColor& text,
                         bool hot )
{
    const Style::UIPalette& palette = Style::Palette();
    const float w = EditorMiniChipWidth( label );
    Style::UIColor chipFill = fill;
    chipFill.a = hot ? (std::min)( 1.0f, chipFill.a + 0.08f ) : chipFill.a;
    draw.RoundedRect( x, y, w, 20.0f, Style::Radii().smallButton, chipFill.r, chipFill.g, chipFill.b, chipFill.a );
    if ( hot )
    {
        draw.Outline( x, y, w, 20.0f, palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b, 0.72f );
    }
    draw.Text( x + 9.0f, y + 5.0f, 10.5f, text.r, text.g, text.b, label );
}


bool IsEditorMiniRootType( int objectType )
{
    return objectType == EditorTab::OBJECT_ROOT_SMALL || objectType == EditorTab::OBJECT_ROOT_LARGE;
}


bool IsEditorMiniRockType( int objectType )
{
    return objectType >= EditorTab::OBJECT_ROCK_SLAB && objectType <= EditorTab::OBJECT_ROCK_CHIPPED;
}


bool IsEditorMiniHullType( int objectType )
{
    return objectType >= EditorTab::OBJECT_HULL_WEDGE && objectType <= EditorTab::OBJECT_HULL_DIAMOND;
}


bool EditorMiniTreeVisualForType( int objectType,
                                  int& outTreeType,
                                  int& outPlacement,
                                  bool& outSlope,
                                  bool& outShedding )
{
    outSlope = false;
    outShedding = false;
    if ( EditorMiniTreeTypeForType( objectType, outTreeType, outPlacement ) )
    {
        return true;
    }

    outPlacement = EDITOR_MINI_TREE_PLACEMENT_FIXED;
    switch ( objectType )
    {
    case EditorTab::OBJECT_TREE_SMALL_SLOPE:
        outTreeType = EDITOR_MINI_TREE_TYPE_SMALL;
        outSlope = true;
        return true;
    case EditorTab::OBJECT_TREE_BIG_SLOPE:

        outTreeType = EDITOR_MINI_TREE_TYPE_PINE;
        outSlope = true;
        return true;
    case EditorTab::OBJECT_TREE_CEDAR_SLOPE:
        outTreeType = EDITOR_MINI_TREE_TYPE_CEDAR;
        outSlope = true;
        return true;
    case EditorTab::OBJECT_TREE_PINE_SHEDDING:
        outTreeType = EDITOR_MINI_TREE_TYPE_PINE;
        outShedding = true;
        return true;
    default:
        outTreeType = EDITOR_MINI_TREE_TYPE_NONE;
        return false;
    }
}
} // namespace SkullbonezCore::UI::FrameComposition
