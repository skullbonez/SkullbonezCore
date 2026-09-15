#pragma once

// Shared character mapping keeps UI measurement and text submission in agreement.
// The atlas stores printable ASCII followed by the Greek capital delta.
namespace SkullbonezCore::Core::TextGlyphs
{
inline constexpr int ASCII_COUNT = 95;
inline constexpr int DELTA_INDEX = ASCII_COUNT;
inline constexpr int COUNT = ASCII_COUNT + 1;

// Consumes one supported glyph, or one unsupported byte with the legacy fallback
// advance. Checking the lead byte first makes a truncated delta safe to consume.
inline int Next( const char*& cursor )
{
    const auto first = static_cast<unsigned char>( *cursor );
    if ( first == 0 )
    {
        return -1;
    }
    ++cursor;
    if ( first >= 32 && first <= 126 )
    {
        return first - 32;
    }
    if ( first == 0xCE && static_cast<unsigned char>( *cursor ) == 0x94 )
    {
        ++cursor;
        return DELTA_INDEX;
    }
    return -1;
}
} // namespace SkullbonezCore::Core::TextGlyphs
