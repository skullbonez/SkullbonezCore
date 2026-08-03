/*
File: SkullbonezSource/UI/UIInput.cpp
Purpose:
  Queries detached UI keyboard levels and derives scene-filter key edges.

Summary:
  Runtime has already copied one immutable input turn into UIInputSnapshot.
  These helpers read its keyboard words and maintain widget-local prior levels
  without reaching back into Runtime or polling hardware.

Glossary:
  Prior level: Widget-owned bit recording whether that key was down on the
    preceding UI interaction turn.

Invariants:
  - Key indexing matches Runtime's four-word, 256-key capture layout.
  - UI derives press edges only from the copied current level and its own prior
    level; pointer edges remain the Runtime-produced values in the snapshot.

Related:
  - SkullbonezSource/UI/UIInput.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UIInput.h"

namespace SkullbonezCore
{
namespace UI
{
namespace InputControl
{

void CaptureKeyStates( bool keyWasDown[UIInputSnapshot::VIRTUAL_KEY_COUNT], const UIInputSnapshot& input )
{

    if ( !keyWasDown )
    {
        return;
    }

    for ( int key = 0; key < UIInputSnapshot::VIRTUAL_KEY_COUNT; ++key )
    {
        keyWasDown[key] = IsVirtualKeyDown( input, key );
    }
}


bool ConsumeKeyPress( bool keyWasDown[UIInputSnapshot::VIRTUAL_KEY_COUNT], const UIInputSnapshot& input, int virtualKey )
{

    if ( !keyWasDown || virtualKey < 0 || virtualKey >= UIInputSnapshot::VIRTUAL_KEY_COUNT )
    {
        return false;
    }

    const bool isDown = IsVirtualKeyDown( input, virtualKey );
    const bool wasPressed = isDown && !keyWasDown[virtualKey];
    keyWasDown[virtualKey] = isDown;
    return wasPressed;
}


bool IsVirtualKeyDown( const UIInputSnapshot& input, int virtualKey )
{

    if ( virtualKey < 0 || virtualKey >= UIInputSnapshot::VIRTUAL_KEY_COUNT )
    {
        return false;
    }

    const std::size_t word = static_cast<std::size_t>( virtualKey ) / 64u;
    const uint64_t bit = uint64_t { 1 } << ( static_cast<unsigned int>( virtualKey ) & 63u );
    return ( input.keyWords[word] & bit ) != 0u;
}


} // namespace InputControl
} // namespace UI
} // namespace SkullbonezCore
