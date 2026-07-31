/*
File: SkullbonezSource/UI/UIComboBox.h
Purpose:
  Implements UI ComboBox widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UIComboBox.h implements UI ComboBox widgets, layout, drawing, or UI state

  for the in-engine controls.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIComboBox.cpp
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{

class UIComboBox
{
  public:
    void SetBounds( float x, float y, float w, float h );
    UIRect Bounds() const;
    UIRect DropdownBounds( int optionCount ) const;
    bool HitBox( int mouseX, int mouseY ) const;
    int HitOption( int mouseX, int mouseY, int optionCount ) const;
    bool IsOpen() const;
    void SetOpen( bool open );
    void SetDropUp( bool dropUp );
    void SetLabelVisible( bool visible );
    void ToggleOpen();
    void Close();
    void Draw( const UIDrawContext& draw, const char* label, const char* const* options, int optionCount, int selectedIndex,
               int mouseX, int mouseY, uint32_t disabledOptionMask = 0 ) const;
    void Draw( const UIDrawContext& draw, const char* label, const char* selectedText, const char* const* options,
               int optionCount, int selectedIndex, int mouseX, int mouseY, uint32_t disabledOptionMask = 0 ) const;

  private:
    UIRect FieldRect() const;
    UIRect DropdownRect( int optionCount ) const;

    UIRect m_bounds;
    bool m_isOpen = false;
    bool m_dropUp = false;
    bool m_labelVisible = true;
};

} // namespace UI
} // namespace SkullbonezCore
