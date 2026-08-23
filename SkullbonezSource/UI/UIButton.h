/*
File: SkullbonezSource/UI/UIButton.h
Purpose:
  Declares a retained-bounds adapter for the stateless button contract.

Summary:
  Layout callers may keep one stable bounds value while UIDrawWidgets owns hit
  policy, text measurement, style selection, and command recording.

Invariants:
  - Bounds are the only retained value; hover and commands remain frame-local.

Related:
  - SkullbonezSource/UI/UIButton.cpp
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UIButton
{
  public:
    void SetBounds( float x, float y, float w, float h );
    UIRect Bounds() const;
    bool HitTest( int mouseX, int mouseY ) const;
    void Draw( const UIDrawContext& draw, const char* label, int mouseX, int mouseY ) const;

  private:
    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
