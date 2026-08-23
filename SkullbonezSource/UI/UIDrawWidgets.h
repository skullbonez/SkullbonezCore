/*
File: SkullbonezSource/UI/UIDrawWidgets.h
Purpose:
  Declares stateless component geometry, value, hit-testing, and drawing
  contracts shared by UI presenters.

Summary:
  Presenters resolve disposable visual state and pass it with one geometry
  value. The helpers measure text through UIFontMetrics and append only bounded
  draw values; they retain no input, product, Runtime, or renderer authority.

Invariants:
  - Component draw and hit geometry derives from the same caller-supplied
    bounds model.
  - Disabled controls may block a pointer but cannot activate.
  - Hidden controls append no commands.
  - Appearance profiles own presentation only and retain no caller state.

Related:
  - SkullbonezSource/UI/UIDrawWidgets.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{

class UICheckBox;

namespace Widgets
{
// Selects a component-neutral presentation profile without changing caller-
// resolved state or authority. Adaptive emphasizes the full state vocabulary;
// Established preserves retained-wrapper command streams; Footer selects the
// existing compact footer-toggle layout. All use the same stateless operations.
enum class ComponentAppearance : unsigned char
{
    Adaptive,
    Established,
    Footer
};

enum class ComponentIcon
{
    ChevronLeft,
    ChevronRight,
    ChevronUp,
    ChevronDown,
    Minus,
    Plus,
    Minimize,
    Maximize,
    Restore,
    Close
};

// Returns whether a visible component owns this point. Disabled controls keep
// this result so an interaction owner can prevent click-through.
bool ContainsComponent( const UIRect& bounds, UIVisualState state, int pointerX, int pointerY );

// Returns whether the same point may produce an action. This is the only
// generic hit helper that applies the Enabled flag.
bool CanActivateComponent( const UIRect& bounds, UIVisualState state, int pointerX, int pointerY );

void DrawPanel( const UIDrawContext& draw, const UIRect& bounds, UIVisualState state );
void DrawLabelValueRow( const UIDrawContext& draw, const UIRect& bounds, const char* label, const char* value,
                        const Style::UIColor& valueColor, UIVisualState state );
void DrawButton( const UIDrawContext& draw, const UIRect& bounds, const char* label, UIVisualState state,
                 ComponentAppearance appearance = ComponentAppearance::Adaptive );
void DrawToggle( const UIDrawContext& draw, const UIRect& bounds, const char* label, const Style::UIColor& accent,
                 UIVisualState state, ComponentAppearance appearance = ComponentAppearance::Adaptive );

float SliderValueFromPointer( const UIRect& bounds, int pointerX, float minValue, float maxValue, float step );
UIRect SliderTrackBounds( const UIRect& bounds );
UIRect SliderThumbBounds( const UIRect& bounds, float value, float minValue, float maxValue );
void DrawSlider( const UIDrawContext& draw, const UIRect& bounds, const char* label, const char* valueText, float value,
                 float minValue, float maxValue, UIVisualState state,
                 ComponentAppearance appearance = ComponentAppearance::Adaptive );

// Carries the complete tab geometry decision. Adaptive tabs use one rectangle
// for both fields. Established tabs preserve the retained strip's full pointer
// partition while drawing the same inset pill; keeping both sub-rectangles in
// one value makes that compatibility distinction explicit and testable.
struct TabLayout
{
    UIRect interactionBounds;
    UIRect visualBounds;
};

TabLayout ResolveTabLayout( const UIRect& stripBounds, int tabIndex, int tabCount,
                            ComponentAppearance appearance = ComponentAppearance::Adaptive );
UIRect TabBounds( const UIRect& stripBounds, int tabIndex, int tabCount,
                  ComponentAppearance appearance = ComponentAppearance::Adaptive );
int HitTestTab( const UIRect& stripBounds, UIVisualState state, int pointerX, int pointerY, int tabCount,
                ComponentAppearance appearance = ComponentAppearance::Adaptive );
void DrawTab( const UIDrawContext& draw, const UIRect& bounds, const char* label, UIVisualState state,
              ComponentAppearance appearance = ComponentAppearance::Adaptive );

UIRect ScrollThumbBounds( const UIRect& trackBounds, float contentHeight, float viewportHeight, float scrollOffset,
                          ComponentAppearance appearance = ComponentAppearance::Adaptive );
void DrawScrollBar( const UIDrawContext& draw, const UIRect& trackBounds, float contentHeight, float viewportHeight,
                    float scrollOffset, float alpha, UIVisualState state,
                    ComponentAppearance appearance = ComponentAppearance::Adaptive );

UIRect ComboFieldBounds( const UIRect& bounds, bool labelVisible );
UIRect ComboPopupBounds( const UIRect& bounds, bool labelVisible, bool dropUp, int optionCount );

// Returns the geometric row even when the popup or row is disabled. The
// interaction owner uses IsComboOptionEnabled before producing an action.
int ComboOptionAtPointer( const UIRect& popupBounds, UIVisualState state, int pointerX, int pointerY, int optionCount );
bool IsComboOptionEnabled( uint32_t disabledOptionMask, int optionIndex );
void DrawComboField( const UIDrawContext& draw, const UIRect& bounds, const char* label, const char* selectedText,
                     bool labelVisible, bool open, UIVisualState state, bool selectedEnabled = true,
                     ComponentAppearance appearance = ComponentAppearance::Adaptive );
void DrawComboPopup( const UIDrawContext& draw, const UIRect& popupBounds, const char* const* options, int optionCount,
                     int selectedIndex, int hoveredIndex, uint32_t disabledOptionMask, UIVisualState state,
                     ComponentAppearance appearance = ComponentAppearance::Adaptive );

void DrawIconButton( const UIDrawContext& draw, const UIRect& bounds, ComponentIcon icon, UIVisualState state,
                     ComponentAppearance appearance = ComponentAppearance::Adaptive );

enum class TitleButtonIcon
{
    Minimize,
    Maximize,
    Restore,
    Close
};

bool IsRowVisible( float contentY, float contentH, float rowY, float rowH );

void DrawTitleButton( const UIDrawContext& draw, const UIRect& bounds, TitleButtonIcon icon, bool hot, bool active );
void DrawPipelineStepButton( const UIDrawContext& draw, const UIRect& bounds, bool previous, bool hot );
void DrawFooterToggle( const UIDrawContext& draw, const UIRect& bounds, const char* label, bool checked );

void DrawLabelValueAt( const UIDrawContext& draw, float contentY, float contentH, float tx, float rowY, const char* label,
                       const char* value, float vr, float vg, float vb );
void DrawSectionTitle( const UIDrawContext& draw, float contentX, float contentY, float contentH, float rowY, float textSize,
                       const char* text );
void DrawContentToggle( const UIDrawContext& draw, float contentY, float contentH, UICheckBox& toggle, float tx, float rowY,
                        float controlW, const char* label, bool checked );

void DrawFooterStatCell( const UIDrawContext& draw, float tx, float bottomY, const char* name, const char* value, float r,
                         float g, float b );
void DrawCompactFooterStat( const UIDrawContext& draw, float statsX, float ty, const char* name, const char* value, float r,
                            float g, float b );
void DrawFooterStatDivider( const UIDrawContext& draw, float x, float bottomY );

} // namespace Widgets
} // namespace UI
} // namespace SkullbonezCore
