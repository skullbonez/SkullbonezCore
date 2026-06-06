#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UIComboBox
{
  public:
    void SetBounds( float x, float y, float w, float h );
    bool HitBox( int mouseX, int mouseY ) const;
    int HitOption( int mouseX, int mouseY, int optionCount ) const;
    bool IsOpen() const;
    void SetOpen( bool open );
    void SetDropUp( bool dropUp );
    void ToggleOpen();
    void Close();
    void Draw( const UIDrawContext& draw, const char* label, const char* const* options, int optionCount, int selectedIndex, int mouseX, int mouseY ) const;
    void Draw( const UIDrawContext& draw, const char* label, const char* selectedText, const char* const* options, int optionCount, int selectedIndex, int mouseX, int mouseY ) const;

  private:
    UIRect FieldRect() const;
    UIRect DropdownRect( int optionCount ) const;

    UIRect m_bounds;
    bool m_isOpen = false;
    bool m_dropUp = false;
};

} // namespace UI
} // namespace SkullbonezCore
