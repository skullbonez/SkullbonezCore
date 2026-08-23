/*
File: SkullbonezSource/UI/UIIconButton.h
Purpose:
  Declares a retained-bounds adapter for stateless icon-button operations.

Summary:
  The wrapper keeps layout bounds while expanded state and glyph selection are
  disposable caller values consumed by UIDrawWidgets.

Invariants:
  - Expanded state, input, and commands are never retained here.

Related:
  - SkullbonezSource/UI/UIIconButton.cpp
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UIIconButton
{
  public:
    void SetBounds( float x, float y, float w, float h );
    bool HitTest( int mouseX, int mouseY ) const;
    void DrawExpander( const UIDrawContext& draw, bool expanded ) const;

  private:
    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
