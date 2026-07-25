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

#include "../SkullbonezSource/UI/UIDraw.h"
#include "../SkullbonezSource/UI/UIDrawList.h"
#include "../SkullbonezSource/UI/UIFontMetrics.h"
#include "../SkullbonezSource/UI/UIStyle.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using SkullbonezCore::UI::UIDrawContext;
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


TEST_CASE( "UI frame composition appends cached values without borrowing source text" )
{
    auto cached = std::make_unique<UIDrawList>();
    cached->Clear();
    cached->AddText( 10, 20, 12, 1, 1, 1, "cached label" );
    cached->PushClip( 5, 6, 100, 80 );
    cached->AddPreviewImage( { 2, true }, 8, 9, 40, 30, 0.1f, 0.2f, 0.3f, 1.0f, "Preview unavailable" );
    cached->PopClip();

    auto frame = std::make_unique<UIDrawList>();
    frame->Clear();
    frame->AddRect( 0, 0, 1, 1, 1, 0, 0, 1 );
    frame->Append( *cached, 3, 4 );
    cached->Clear();

    const auto commands = frame->Commands();
    REQUIRE( commands.size() == 5 );
    CHECK( commands[1].x0 == 13 );
    CHECK( commands[1].y0 == 24 );
    CHECK( std::string( frame->TextAt( commands[1].textOffset ) ) == "cached label" );
    CHECK( commands[2].x0 == 8 );
    CHECK( commands[2].y0 == 10 );
    CHECK( commands[3].preview.catalogIndex == 2 );
    CHECK( std::string( frame->TextAt( commands[3].textOffset ) ) == "Preview unavailable" );
    CHECK_FALSE( frame->GetStats().clipOverflow );
}


TEST_CASE( "UI representative frame streams retain committed fingerprints" )
{
    auto makeFrame = []( int surface, UIDrawList& frame )
    {
        frame.Clear();
        UIDrawContext draw( 1920, 1080, frame );
        if ( surface == 0 )
        {
            draw.RoundedPanel( { 120, 80, 920, 720 }, 10, { 0.08f, 0.09f, 0.11f, 0.96f }, { 0.2f, 0.3f, 0.4f, 1 } );
            draw.Text( 148, 110, 15.5f, 1, 1, 1, "Editor / Scene" );
            frame.PushClip( 140, 150, 860, 570 );
            draw.Rect( 156, 176, 240, 32, 0.1f, 0.2f, 0.3f, 1 );
            draw.Triangle( 380, 184, 392, 196, 380, 208, 0.4f, 0.8f, 1, 1 );
            frame.AddPreviewImage( { 3, true }, 420, 176, 520, 300, 0.04f, 0.05f, 0.07f, 1, "Preview unavailable" );
            frame.PopClip();
        }
        else if ( surface == 1 )
        {
            draw.RoundedPanel( { 1470, 18, 420, 38 }, 8, { 0.08f, 0.09f, 0.11f, 0.96f }, { 0.2f, 0.3f, 0.4f, 1 } );
            draw.Text( 1502, 30, 12.5f, 1, 1, 1, "Scene 3 / Inspect" );
        }
        else if ( surface >= 2 && surface < 13 )
        {
            const int tab = surface - 2;
            draw.Rect( 160, 160, 760, 480, 0.06f, 0.07f, 0.09f, 0.94f );
            draw.Text( 180, 182, 14, 0.8f, 0.9f, 1, "Legacy tab" );
            draw.Rect( 180, 224 + static_cast<float>( tab * 3 ), 520, 2, 0.2f, 0.7f, 1, 0.8f );
        }
        else if ( surface == 13 )
        {
            draw.RoundedPanel( { 18, 880, 1884, 174 }, 8, { 0.05f, 0.06f, 0.08f, 0.96f }, { 0.2f, 0.3f, 0.4f, 1 } );
            draw.Text( 42, 904, 11, 0.7f, 0.9f, 1, "REPLAY  +120.0s  MARS" );
            draw.Rect( 42, 944, 1800, 4, 0.2f, 0.7f, 1, 1 );
        }
        else
        {
            draw.Text( 24, 24, 18, 1, 1, 1, "Text-only validation" );
        }
    };

    constexpr uint64_t expected[] = { 1999776830099349490ull,  395030874118962480ull,   11343627079521793403ull,
                                      16197901356463063590ull, 13191436111239807249ull, 2946497590796191564ull,
                                      2899566393362059983ull,  515668882496225930ull,   3301416027854312949ull,
                                      2517948369282214640ull,  14927718977381377571ull, 6346658494577968078ull,
                                      13290574325653764313ull, 14295429501679027938ull, 7096956378812196519ull };
    auto frame = std::make_unique<UIDrawList>();
    for ( int surface = 0; surface < static_cast<int>( std::size( expected ) ); ++surface )
    {
        INFO( "surface=" << surface );
        makeFrame( surface, *frame );
        CHECK( frame->Fingerprint() == expected[surface] );
    }
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
