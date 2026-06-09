// --- Includes ---
#include "SkullbonezInput.h"
#include "SkullbonezWindow.h"


// --- Usings ---
using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Basics;

namespace
{
int g_mouseWheelDelta = 0;

void EnsureShowCursorVisible()
{
    int counter = ShowCursor( TRUE );
    while ( counter < 0 )
    {
        counter = ShowCursor( TRUE );
    }
    if ( counter > 0 )
    {
        ShowCursor( FALSE );
    }
}


void EnsureShowCursorHidden()
{
    int counter = ShowCursor( FALSE );
    while ( counter >= 0 )
    {
        counter = ShowCursor( FALSE );
    }
    if ( counter < -1 )
    {
        ShowCursor( TRUE );
    }
}
} // namespace


/*
   0x8000 = (16^3)*8 + (16^2)*0 + (16^1)*0 + (16^0)*0
          = 32,768 (decimal)
          = 1000 0000 0000 0000 (binary)
   This is the highest order bit of a 16 bit piece of data (SHORT)
 */
#define HIGHEST_ORDER_BIT_16 0x8000

/*
   0x1 = 0x0001 = (16^3)*0 + (16^2)*0 + (16^1)*0 + (16^0)*1
       = 1 (decimal)
       = 1 (binary)
       = 0000 0000 0000 0001 (binary)
   This is the lowest order bit of any sized piece of binary data
   */
#define LOWEST_ORDER_BIT_16 0x1

bool Input::IsAppFocused()
{
    SkullbonezWindow* window = SkullbonezWindow::Instance();
    if ( !window || !window->m_sWindow )
    {
        return false;
    }

    return GetForegroundWindow() == window->m_sWindow;
}


void Input::SetSystemCursorVisible( bool visible )
{
    if ( visible )
    {
        SetCursor( LoadCursor( nullptr, IDC_ARROW ) );
        EnsureShowCursorVisible();
    }
    else
    {
        SetCursor( nullptr );
        EnsureShowCursorHidden();
    }
}


bool Input::IsKeyDown( const char cKey )
{
    if ( !IsAppFocused() )
    {
        return false;
    }

    /*
        recall that HIGHEST_ORDER_BIT_16 = 1000 0000 0000 0000 (binary)
        recall that a conditional statement in C++ simply checks if the value is
        nonzero, and if it is nonzero returns true, otherwise false.
        GetKeyState(cKey) will return a 16 bit SHORT and if the key is pressed,
        the SHORT returned will have the highest order bit set to 1.
        the binary AND operator '&' will do the comparision on the two SHORTs
        1000 0000 0000 0000 & 1000 0000 0000 0000 (if the key is pressed)
           = 1000 0000 0000 0000 != 0
        1000 0000 0000 0000 & 0000 0000 0000 0000 (if the key is not pressed)
           = 0000 0000 0000 0000 == 0
    */
    return ( ( GetKeyState( cKey ) & HIGHEST_ORDER_BIT_16 ) != 0 );
}


bool Input::IsKeyToggled( const char cKey )
{
    if ( !IsAppFocused() )
    {
        return false;
    }

    // lowest order bit is set to 1 if key is toggled, see Input::IsKeyDown
    // for an explanation on the conditional statement below
    return ( ( GetKeyState( cKey ) & LOWEST_ORDER_BIT_16 ) != 0 );
}


POINT Input::GetMouseCoordinates()
{
    POINT mousePos;
    if ( !GetCursorPos( &mousePos ) ) // attempt to get the mouse m_position
    {
        throw std::runtime_error( "Getting mouse coordinates failed (Input::GetMouseCoordinates)." );
    }

    return mousePos;
}


POINT Input::GetClientMouseCoordinates()
{
    POINT mousePos = GetMouseCoordinates();
    SkullbonezWindow* m_cWindow = SkullbonezWindow::Instance();
    if ( !ScreenToClient( m_cWindow->m_sWindow, &mousePos ) )
    {
        throw std::runtime_error( "Converting mouse coordinates failed (Input::GetClientMouseCoordinates)." );
    }

    return mousePos;
}


void Input::SetMouseCoordinates( const POINT& pNewCoordinates )
{
    if ( !IsAppFocused() )
    {
        return;
    }

    // attempt to set the mouse m_position
    if ( !SetCursorPos( pNewCoordinates.x, pNewCoordinates.y ) )
    {
        throw std::runtime_error( "Setting mouse m_position failed (Input::SetMouseCoordinates)." );
    }
}


bool Input::IsLeftMouseDown()
{
    if ( !IsAppFocused() )
    {
        return false;
    }

    return ( ( GetKeyState( VK_LBUTTON ) & HIGHEST_ORDER_BIT_16 ) != 0 );
}


int Input::ConsumeMouseWheelDelta()
{
    if ( !IsAppFocused() )
    {
        g_mouseWheelDelta = 0;
        return 0;
    }

    const int delta = g_mouseWheelDelta;
    g_mouseWheelDelta = 0;
    return delta;
}


void Input::AccumulateMouseWheelDelta( int delta )
{
    if ( !IsAppFocused() )
    {
        return;
    }

    g_mouseWheelDelta += delta;
}


void Input::CentreMouseCoordinates()
{
    if ( !IsAppFocused() )
    {
        return;
    }

    SkullbonezWindow* m_cWindow = SkullbonezWindow::Instance();
    POINT clientCenter = { m_cWindow->m_sWindowDimensions.x >> 1,
                           m_cWindow->m_sWindowDimensions.y >> 1 };
    if ( !ClientToScreen( m_cWindow->m_sWindow, &clientCenter ) )
    {
        throw std::runtime_error( "Converting mouse center failed (Input::CentreMouseCoordinates)." );
    }

    if ( !SetCursorPos( clientCenter.x, clientCenter.y ) )
    {
        throw std::runtime_error( "Setting mouse center failed (Input::CentreMouseCoordinates)." );
    }
}
