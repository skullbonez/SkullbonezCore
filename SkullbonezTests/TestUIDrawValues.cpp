/*
File: TestUIDrawValues.cpp
Purpose:
  Locks the backend-neutral Legacy UI draw-stream and font-metric contracts.

Summary:
  Exercises every UR1 value variant, fixed-capacity failure behavior, preview
  fallback data, clip nesting, and immutable text measurement without a GPU.

Mental model:
  UI records ordered values into bounded storage. These tests inspect those
  values directly, without a renderer device, and prove overflow/fallback
  behavior at the ownership boundary.

Glossary:
  Preview identity: UI catalog row resolved to a texture only during submission.
  Clip stack: Nested screen rectangles constraining later draw commands.

Invariants:
  - Command order and stored text are deterministic.
  - Capacity exhaustion reports a flag and never grows storage.
  - Missing previews carry an authored fill and label fallback.

Related:
  - SkullbonezSource/UI/UIDrawList.h
  - SkullbonezSource/UI/UIFontMetrics.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/UI/UIDrawList.h"
#include "../SkullbonezSource/UI/UIFontMetrics.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

using SkullbonezCore::UI::UIDrawList;
using SkullbonezCore::UI::UIFontMetrics;

TEST_CASE( "UI draw values preserve primitive and text order" )
{
    UIDrawList list;
    list.Clear();
    list.AddRect( 1, 2, 3, 4, 0.1f, 0.2f, 0.3f, 0.4f );
    list.AddRoundedRect( 5, 6, 7, 8, 2, 0.2f, 0.3f, 0.4f, 0.5f );
    list.AddTriangle( 1, 2, 3, 4, 5, 6, 0.3f, 0.4f, 0.5f, 0.6f );
    list.AddText( 7, 8, 12, 0.8f, 0.9f, 1.0f, "ordered" );

    const auto commands = list.Commands();
    REQUIRE( commands.size() == 4 );
    CHECK( commands[0].type == UIDrawList::CommandType::Rect );
    CHECK( commands[1].type == UIDrawList::CommandType::RoundedRect );
    CHECK( commands[2].type == UIDrawList::CommandType::Triangle );
    CHECK( commands[3].type == UIDrawList::CommandType::Text );
    CHECK( std::string( list.TextAt( commands[3].textOffset ) ) == "ordered" );
}


TEST_CASE( "UI draw values retain nested clips and report imbalance" )
{
    UIDrawList list;
    list.Clear();
    list.PushClip( 0, 0, 100, 100 );
    list.PushClip( 10, 10, 20, 20 );
    list.PopClip();
    list.PopClip();
    list.PopClip();

    const auto commands = list.Commands();
    REQUIRE( commands.size() == 4 );
    CHECK( commands[0].type == UIDrawList::CommandType::PushClip );
    CHECK( commands[1].type == UIDrawList::CommandType::PushClip );
    CHECK( commands[2].type == UIDrawList::CommandType::PopClip );
    CHECK( commands[3].type == UIDrawList::CommandType::PopClip );
    CHECK( list.GetStats().maxClipDepth == 2 );
    CHECK( list.GetStats().clipOverflow );
}


TEST_CASE( "UI preview values define unavailable fallback presentation" )
{
    UIDrawList list;
    list.Clear();
    list.AddPreviewImage( { 4, true }, 10, 20, 300, 160, 0.08f, 0.09f, 0.11f, 1.0f, "Preview unavailable" );

    const auto commands = list.Commands();
    REQUIRE( commands.size() == 1 );
    CHECK( commands[0].type == UIDrawList::CommandType::PreviewImage );
    CHECK( commands[0].preview.valid );
    CHECK( commands[0].preview.catalogIndex == 4 );
    CHECK( commands[0].r == doctest::Approx( 0.08f ) );
    CHECK( std::string( list.TextAt( commands[0].textOffset ) ) == "Preview unavailable" );
}


TEST_CASE( "UI draw storage fails closed at fixed capacities" )
{
    UIDrawList list;
    list.Clear();
    for ( int index = 0; index < UIDrawList::MAX_COMMANDS + 1; ++index )
    {
        list.AddRect( 0, 0, 1, 1, 1, 1, 1, 1 );
    }
    CHECK( list.Commands().size() == UIDrawList::MAX_COMMANDS );
    CHECK( list.GetStats().commandOverflow );

    list.Clear();
    std::array<char, UIDrawList::MAX_TEXT_BYTES + 1> oversizedText {};
    oversizedText.fill( 'x' );
    oversizedText.back() = '\0';
    list.AddText( 0, 0, 10, 1, 1, 1, oversizedText.data() );
    CHECK( list.GetStats().textOverflow );
    REQUIRE( list.Commands().size() == 1 );
    CHECK( std::strlen( list.TextAt( list.Commands()[0].textOffset ) ) == UIDrawList::MAX_TEXT_BYTES - 1 );

    list.Clear();
    for ( int depth = 0; depth < UIDrawList::MAX_CLIP_DEPTH + 1; ++depth )
    {
        list.PushClip( 0, 0, 1, 1 );
    }
    CHECK( list.GetStats().maxClipDepth == UIDrawList::MAX_CLIP_DEPTH );
    CHECK( list.GetStats().clipOverflow );
}


TEST_CASE( "UI font metrics are immutable and preserve legacy operation order" )
{
    struct BakedFontHeader
    {
        char magic[8];
        uint32_t version;
        uint32_t atlasWidth;
        uint32_t atlasHeight;
        uint32_t fontSize;
        uint32_t cellWidth;
        uint32_t cellHeight;
        float advances[UIFontMetrics::GLYPH_COUNT];
    };
    static_assert( sizeof( BakedFontHeader ) == 416 );

    FILE* fontFile = nullptr;
    REQUIRE( fopen_s( &fontFile, "SkullbonezData/font_atlas.sdf", "rb" ) == 0 );
    REQUIRE( fontFile != nullptr );
    BakedFontHeader header {};
    const size_t headersRead = std::fread( &header, sizeof( header ), 1, fontFile );
    std::fclose( fontFile );
    REQUIRE( headersRead == 1 );
    REQUIRE( std::memcmp( header.magic, "SBSDF001", 8 ) == 0 );

    std::array<float, UIFontMetrics::GLYPH_COUNT> advances {};
    for ( int index = 0; index < UIFontMetrics::GLYPH_COUNT; ++index )
    {
        advances[static_cast<size_t>( index )] = header.advances[index];
    }
    REQUIRE( UIFontMetrics::Install( advances.data(), static_cast<int>( advances.size() ) ) );
    CHECK( UIFontMetrics::Install( advances.data(), static_cast<int>( advances.size() ) ) );

    char allGlyphs[UIFontMetrics::GLYPH_COUNT + 1] = {};
    for ( int index = 0; index < UIFontMetrics::GLYPH_COUNT; ++index )
    {
        allGlyphs[index] = static_cast<char>( index + 32 );
    }
    const char mixed[] = { 'M', 'a', 'r', 's', ' ', '1', '2', '0', 's', '\n', '\0' };
    const char* corpus[] = { "", "Frame/UI 16.67 ms", "Preview unavailable", mixed, allGlyphs };
    const float sizes[] = { 8.5f, 8.8f, 9.6f, 10.0f, 10.5f, 12.0f, 14.0f, 18.0f, 10.5f * 1.25f, 10.5f * 1.5f };

    auto legacyMeasure = [&]( float size, const char* text )
    {
        float width = 0.0f;
        for ( const char* cursor = text; *cursor; ++cursor )
        {
            const unsigned char character = static_cast<unsigned char>( *cursor );
            width += character >= 32 && character <= 127 ? advances[character - 32] * size : size * 0.5f;
        }
        return width;
    };
    for ( float size : sizes )
    {
        for ( const char* text : corpus )
        {
            CHECK( UIFontMetrics::MeasureText( size, text ) == legacyMeasure( size, text ) );
        }
    }
    CHECK( UIFontMetrics::MeasureText( 12.5f, nullptr ) == 0.0f );

    advances[0] += 1.0f;
    CHECK_FALSE( UIFontMetrics::Install( advances.data(), static_cast<int>( advances.size() ) ) );
}
