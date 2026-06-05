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
    void SetDropdownBounds( float x, float y, float w, float h );
    bool HitBox( int mouseX, int mouseY ) const;
    int HitOption( int mouseX, int mouseY, int optionCount ) const;
    bool IsOpen() const;
    void SetOpen( bool open );
    void ToggleOpen();
    void Close();
    void Draw( const UiDrawContext& draw, const char* label, const char* const* options, int optionCount, int selectedIndex ) const;

  private:
    UiRect m_bounds;
    UiRect m_dropdownBounds;
    bool m_isOpen = false;
};

} // namespace Ui
} // namespace SkullbonezCore
