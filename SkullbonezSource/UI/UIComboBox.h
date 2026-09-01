/*
File: SkullbonezSource/UI/UIComboBox.h
Purpose:
  Declares the retained popup-state adapter for stateless combo operations.

Summary:
  Bounds, open state, popup direction, and label visibility form one persistent
  combo invariant; selection, hover, disabled options, and commands do not.

Invariants:
  - UIDrawWidgets exclusively derives field, popup, and option geometry.

Related:
  - SkullbonezSource/UI/UIComboBox.cpp
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"

#include <cstdint>
#include <span>

namespace SkullbonezCore
{
namespace UI
{

// Invariant: option storage and its count cannot disagree. Selection display
// and disabled-state lookup use this one synchronous view, so callers cannot
// pass a count from a different option collection.
struct UIComboPresentationView
{
    std::span<const char* const> options;
    int selectedIndex = -1;
    uint32_t disabledOptionMask = 0;
    const char* selectedTextOverride = nullptr;

    int OptionCount() const noexcept;
    const char* SelectedText() const noexcept;
    bool SelectedOptionEnabled() const noexcept;
};

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
    void Draw( const UIDrawContext& draw, const char* label, const UIComboPresentationView& presentation,
               UIPointerPosition pointer ) const;

  private:
    UIRect m_bounds;
    bool m_isOpen = false;
    bool m_dropUp = false;
    bool m_labelVisible = true;
};

} // namespace UI
} // namespace SkullbonezCore
