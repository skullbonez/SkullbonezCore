/*
File: UIFontMetrics.cpp
Purpose:
  Publishes baked glyph advances and performs renderer-free UI text measurement.

Summary:
  Accepts the baked table once at cold setup and provides allocation-free,
  deterministic measurement to UI layout and hit-geometry code.

Mental model:
  Font pixels remain a renderer resource. Their scalar layout facts cross the
  Runtime composition boundary once and become an immutable UI value table.

Glossary:
  Glyph advance: Horizontal distance added after laying out one character.
  Cold setup: Startup/device initialization phase before steady frame work.

Invariants:
  - Measurement is byte-for-byte the legacy Text2d loop and operation order.
  - No allocation occurs during installation or measurement.

Related:
  - UIFontMetrics.h
  - Rendering/Text.cpp
*/
#include "UIFontMetrics.h"

#include <cstring>

namespace SkullbonezCore::UI
{
bool UIFontMetrics::Install( const float* advances, int count )
{

    if ( !advances || count != GLYPH_COUNT )
    {
        return false;
    }

    if ( s_ready )
    {
        return std::memcmp( s_advances.data(), advances, sizeof( s_advances ) ) == 0;
    }

    std::memcpy( s_advances.data(), advances, sizeof( s_advances ) );
    s_ready = true;
    return true;
}


bool UIFontMetrics::Ready()
{
    return s_ready;
}


float UIFontMetrics::MeasureText( float size, const char* text )
{

    if ( !text )
    {
        return 0.0f;
    }

    float width = 0.0f;

    for ( const char* cursor = text; *cursor; ++cursor )
    {
        const unsigned char character = static_cast<unsigned char>( *cursor );
        width += character >= 32 && character <= 127 ? s_advances[character - 32] * size : size * 0.5f;
    }

    return width;
}


const std::array<float, UIFontMetrics::GLYPH_COUNT>& UIFontMetrics::Advances()
{
    return s_advances;
}
} // namespace SkullbonezCore::UI
