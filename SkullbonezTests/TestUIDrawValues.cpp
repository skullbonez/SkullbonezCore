/*
File: TestUIDrawValues.cpp
Purpose:
  Locks the backend-neutral Legacy UI draw-stream and font-metric contracts.

Summary:
  Exercises every UR1 value variant, fixed-capacity failure behavior, preview
  fallback data, clip nesting, immutable text measurement, the profiler
  presenter, detached memory-capacity rows, window-chrome interaction semantics,
  and all production UI surfaces without a GPU.

Mental model:
  UI records ordered values into bounded storage. These tests inspect those
  values directly, without a renderer device, and prove overflow/fallback
  behavior at the ownership boundary.

Glossary:
  Preview identity: UI catalog row resolved to a texture only during submission.
  Clip stack: Nested screen rectangles constraining later draw commands.
  Capacity row: Runtime-owned label and numeric values rendered without a live
    allocator borrow.

Invariants:
  - Command order and stored text are deterministic.
  - Capacity exhaustion reports a flag and never grows storage.
  - Missing previews carry an authored fill and label fallback.
  - Memory rows sort largest-resident-first without exhausting draw storage.

Related:
  - SkullbonezSource/UI/UIDrawList.h
  - SkullbonezSource/UI/UIFontMetrics.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/UI/UIDraw.h"
#include "../SkullbonezSource/UI/UIDrawList.h"
#include "../SkullbonezSource/UI/UIFontMetrics.h"
#include "../SkullbonezSource/UI/UIProfilerOverlayPresenter.h"
#include "../SkullbonezSource/UI/UIStyle.h"
#include "../SkullbonezSource/UI/UI.h"
#include "../SkullbonezSource/UI/UIWindowInteractionOwner.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>

using SkullbonezCore::UI::UIDrawContext;
using SkullbonezCore::UI::UIDrawList;
using SkullbonezCore::UI::UIFontMetrics;

namespace
{
int FindDrawTextIndex( const UIDrawList& list, const char* expected )
{
    const std::span<const UIDrawList::Command> commands = list.Commands();

    for ( int index = 0; index < static_cast<int>( commands.size() ); ++index )
    {
        if ( commands[static_cast<std::size_t>( index )].type == UIDrawList::CommandType::Text &&
             std::strcmp( list.TextAt( commands[static_cast<std::size_t>( index )].textOffset ), expected ) == 0 )
        {
            return index;
        }
    }

    return -1;
}
} // namespace

TEST_CASE( "UI window close hides the panel instead of minimizing it" )
{
    using SkullbonezCore::UI::InputControl::UIInputSnapshot;
    using SkullbonezCore::UI::UIWindowInteractionOwner;

    UIWindowInteractionOwner owner;
    owner.SetVisible( true, 0.0 );
    owner.SetWindowBounds( 100, 120, 760, 520 );

    UIInputSnapshot input;
    input.mouseX = 100 + 760 - 25;
    input.mouseY = 120 + 22;
    input.leftDown = true;
    input.leftPressed = true;

    owner.UpdateInput( input, 1920, 1080, 1.0, false, false, false, false, 0, 0xffffffffu, {}, 0 );

    CHECK_FALSE( owner.IsVisible() );
    CHECK( owner.IsMinimized() );
}

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
    list.PopClip(); // Matches the rejected push and must not pop a retained clip.
    CHECK( list.Commands().size() == UIDrawList::MAX_CLIP_DEPTH );
    for ( int depth = 0; depth < UIDrawList::MAX_CLIP_DEPTH; ++depth )
    {
        list.PopClip();
    }
    CHECK( list.Commands().size() == UIDrawList::MAX_CLIP_DEPTH * 2 );
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


TEST_CASE( "Production UI frame streams retain committed fingerprints" )
{
    using SkullbonezCore::UI::InGameUI;
    using SkullbonezCore::UI::InGameUIFrameData;
    using SkullbonezCore::UI::InGameUITab;

    const char* sceneOptions[] = { "ui_scene", "ui_scene_alt" };
    auto data = std::make_unique<InGameUIFrameData>();
    data->screenW = 1920;
    data->screenH = 1080;
    data->rendererName = "DirectX 12";
    data->sceneName = "UI production fingerprint";
    data->sceneOptions = sceneOptions;
    data->sceneOptionCount = static_cast<int>( std::size( sceneOptions ) );
    data->selectedSceneOption = 0;
    data->currentSceneIndex = 0;
    data->sceneCount = data->sceneOptionCount;
    data->sceneMode = true;
    data->currentFrame = 20;
    data->targetFrameCount = 120;
    data->runtimeInputModeLabel = "Inspect";
    data->fps = 60.0f;
    data->renderMs = 4.0f;
    data->physicsMs = 2.0f;
    data->cpuFrameMs = 6.0f;
    data->gpuFrameMs = 3.0f;
    data->modelCount = 32;
    data->workerThreadCount = 4;
    data->maxWorkerThreadCount = 8;
    data->renderTargetPreviewCount = 1;
    data->renderTargetPreviews[0] = { "Scene HDR", 1920, 1080, false, false, true };

    constexpr InGameUITab tabs[] = {
        InGameUITab::Profiler, InGameUITab::Scene,   InGameUITab::Editor, InGameUITab::Physics,
        InGameUITab::Options,  InGameUITab::Render,  InGameUITab::Targets, InGameUITab::Keys,
        InGameUITab::Sky,      InGameUITab::Cinematic, InGameUITab::Memory,
    };
    constexpr uint64_t expected[] = {
        1167260169752999469ull,
        5621374501062094743ull,
        643319089294822447ull,
        9774020997193876338ull,
        3787874871094680490ull,
        13838569643518502325ull,
        1186693958027131891ull,
        5057719176066529734ull,
        3243788985155815295ull,
        15645422141942934428ull,
        5868520363750485546ull,
    };
    static_assert( std::size( tabs ) == std::size( expected ) );

    auto ui = std::make_unique<InGameUI>();
    ui->SetVisible( true );
    ui->SetWindowBounds( 54, 72, 760, 520 );
    ui->SetMouseOverride( true, 12, 12 );
    for ( int surface = 0; surface < static_cast<int>( std::size( tabs ) ); ++surface )
    {
        INFO( "surface=" << surface );
        ui->SetActiveTab( tabs[surface] );
        ui->ResetPresentationState();
        const UIDrawList& frame = ui->Draw( *data );
        CHECK( frame.Fingerprint() == expected[surface] );
        CHECK_FALSE( frame.GetStats().commandOverflow );
        CHECK_FALSE( frame.GetStats().textOverflow );
        CHECK_FALSE( frame.GetStats().clipOverflow );
    }
}

TEST_CASE( "Memory capacity table sorts detached owner rows by resident bytes without draw overflow" )
{
    using SkullbonezCore::UI::InGameUIFrameData;
    using SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState;

    auto data = std::make_unique<InGameUIFrameData>();
    SkullbonezCore::UI::UIRuntimeReserveCapacityRow capacityRows[2] = {};
    data->reserveCapacityRows = capacityRows;
    data->reserveCapacityRowCount = 2;
    strcpy_s( capacityRows[0].ownerName, "PhysicsBodyStore.bodies" );
    strcpy_s( capacityRows[0].capacityReason, "one row per loaded body" );
    strcpy_s( capacityRows[0].subsystemName, "physics" );
    capacityRows[0].elementSizeBytes = 72;
    capacityRows[0].currentCapacity = 100;
    capacityRows[0].liveCount = 30;
    capacityRows[0].sessionHighWater = 40;
    capacityRows[0].residentBytes = 7200;
    strcpy_s( capacityRows[1].ownerName, "ColliderStore.colliders" );
    strcpy_s( capacityRows[1].capacityReason, "one row per loaded collider" );
    strcpy_s( capacityRows[1].subsystemName, "physics" );
    capacityRows[1].elementSizeBytes = 80;
    capacityRows[1].currentCapacity = 200;
    capacityRows[1].liveCount = 80;
    capacityRows[1].sessionHighWater = 125;
    capacityRows[1].residentBytes = 16000;

    UIDrawList list;
    UIDrawContext draw( 1920, 1080, list );
    UIMemoryOverlayState state;
    SkullbonezCore::UI::MemoryTab::Draw( draw, state, *data, 20.0f, 0.0f, 720.0f, 260.0f, -450.0f, 0, 0, 0 );

    UIDrawList measuredList;
    UIDrawContext measuredDraw( 1920, 1080, measuredList );
    UIMemoryOverlayState measuredState;
    SkullbonezCore::Core::Allocation::ResetRuntimeAllocationCounters();
    SkullbonezCore::Core::Allocation::SetRuntimeAllocationGuardMode(
        SkullbonezCore::Core::Allocation::RuntimeAllocationGuardMode::Gameplay );
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope renderScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::Render );
        SkullbonezCore::UI::MemoryTab::Draw( measuredDraw, measuredState, *data, 20.0f, 0.0f, 720.0f, 260.0f, -450.0f, 0,
                                            0, 0 );
    }
    const uint64_t memoryDrawAllocationViolations =
        SkullbonezCore::Core::Allocation::RuntimeAllocationGuardViolationCount();
    SkullbonezCore::Core::Allocation::SetRuntimeAllocationGuardMode(
        SkullbonezCore::Core::Allocation::RuntimeAllocationGuardMode::Off );

    CHECK( memoryDrawAllocationViolations == 0u );
    const int colliderRow = FindDrawTextIndex( measuredList, "ColliderStore.colliders" );
    const int bodyRow = FindDrawTextIndex( measuredList, "PhysicsBodyStore.bodies" );
    REQUIRE( colliderRow >= 0 );
    REQUIRE( bodyRow >= 0 );
    CHECK( colliderRow < bodyRow );
    CHECK( FindDrawTextIndex( measuredList, "62.5%" ) >= 0 );
    const UIDrawList::Stats stats = measuredList.GetStats();
    CHECK( stats.commandCount > 0 );
    CHECK( stats.commandCount < UIDrawList::MAX_COMMANDS );
    CHECK( stats.textBytes < UIDrawList::MAX_TEXT_BYTES );
    CHECK_FALSE( stats.commandOverflow );
    CHECK_FALSE( stats.textOverflow );
    CHECK_FALSE( stats.clipOverflow );
}


TEST_CASE( "Profiler overlay presenter records detached UI values without a renderer" )
{
    SkullbonezCore::Core::Profiler::ProfilerFrameView emptyFrame;
    UIDrawList overlay;
    UIDrawContext draw( 1920, 1080, overlay );
    SkullbonezCore::UI::UIProfilerOverlayPresenter presenter;

    presenter.RecordOverlay( emptyFrame, draw, -800.0f, -480.0f, 18.0f, 12.0f, 60.0f );
    presenter.RecordBarOverlay( emptyFrame, draw, -800.0f, -300.0f, 640.0f, 160.0f, true );

    CHECK_FALSE( overlay.Commands().empty() );
    CHECK( overlay.Fingerprint() != 0 );
    CHECK_FALSE( overlay.GetStats().commandOverflow );
    CHECK_FALSE( overlay.GetStats().textOverflow );
    CHECK_FALSE( overlay.GetStats().clipOverflow );
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
