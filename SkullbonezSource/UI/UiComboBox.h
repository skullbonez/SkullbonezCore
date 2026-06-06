#pragma once

#include "UiDraw.h"

namespace SkullbonezCore
{
namespace Ui
{

class UiComboBox
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
    void Draw( const UiDrawContext& draw, const char* label, const char* const* options, int optionCount, int selectedIndex, int mouseX, int mouseY ) const;
    void Draw( const UiDrawContext& draw, const char* label, const char* selectedText, const char* const* options, int optionCount, int selectedIndex, int mouseX, int mouseY ) const;

  private:
    UiRect FieldRect() const;
    UiRect DropdownRect( int optionCount ) const;

    UiRect m_bounds;
    bool m_isOpen = false;
    bool m_dropUp = false;
};

} // namespace Ui
} // namespace SkullbonezCore
