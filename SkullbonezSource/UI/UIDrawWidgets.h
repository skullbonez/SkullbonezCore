/*
File: SkullbonezSource/UI/UIDrawWidgets.h
Purpose:
  Implements UI DrawWidgets widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UIDrawWidgets.h implements UI DrawWidgets widgets, layout, drawing, or UI
  state for the in-engine controls.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIDrawWidgets.cpp
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UICheckBox;

namespace Widgets
{

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
