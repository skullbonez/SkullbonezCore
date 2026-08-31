/*
File: SkullbonezTests/TestUIDrawValues.cpp
Purpose:
  Locks the backend-neutral UI draw-stream and font-metric contracts.

Summary:
  UI records ordered values into bounded storage. These tests inspect every
  value variant directly without a renderer device, including fixed-capacity
  failure behavior, preview fallbacks, clip nesting, immutable text metrics,
  detached memory rows, window interaction, and every production surface.

Glossary:
  Clip stack: Nested screen rectangles constraining later draw commands.
  Capacity row: Runtime-owned label and numeric values rendered without a live
    allocator borrow.

Invariants:
  - Command order and stored text are deterministic.
  - Capacity exhaustion reports a flag and never grows storage.
  - Missing previews carry an authored fill and label fallback.
  - Memory rows sort largest-resident-first without exhausting draw storage.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/UI/UIDrawList.h
  - SkullbonezSource/UI/UIFontMetrics.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/UI/UIDraw.h"
#include "../SkullbonezSource/UI/UIDrawList.h"
#include "../SkullbonezSource/UI/UIComboBox.h"
#include "../SkullbonezSource/UI/UIFontMetrics.h"
#include "../SkullbonezSource/UI/UICache.h"
#include "../SkullbonezSource/UI/UILayout.h"
#include "../SkullbonezSource/UI/UIWindowChrome.h"
#include "../SkullbonezSource/Runtime/Render/UIProfilerOverlayPresenter.h"
#include "../SkullbonezSource/UI/UIStyle.h"
#include "../SkullbonezSource/Runtime/UI/GameUI/GameUILayout.h"
#include "../SkullbonezSource/Runtime/UI/GameUI/UI.h"
#include "../SkullbonezSource/Runtime/UI/GameUI/UIFrameComposition.h"
#include "../SkullbonezSource/Runtime/UI/GameUI/UITabPhysics.h"
#include "../SkullbonezSource/Runtime/UI/GameUI/UITabScene.h"
#include "../SkullbonezSource/Runtime/UI/GameUI/UIWindowInteractionOwner.h"
#include "../SkullbonezSource/UI/UIDrawWidgets.h"

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
template <typename T>
concept HasOrdinaryRenderCapability = requires( const T& value ) { value.ordinaryRender; };

template <typename T>
concept HasCinematicConfigCapability = requires( const T& value ) { value.cinematic; };

template <typename T>
concept HasOperatorEditorCapability = requires( const T& value ) { value.operatorEditor; };

template <typename T>
concept HasForecastTickCapability = requires( const T& value ) { value.newestAbsoluteTick; };

template <typename T>
concept HasForecastRetainedBytesCapability = requires( const T& value ) { value.retainedBytes; };

static_assert( !HasOrdinaryRenderCapability<SkullbonezCore::UI::UIOptionsTabFrameView> );
static_assert( !HasCinematicConfigCapability<SkullbonezCore::UI::UIOptionsTabFrameView> );
static_assert( !HasOperatorEditorCapability<SkullbonezCore::UI::UISceneTabFrameView> );
static_assert( !HasForecastTickCapability<SkullbonezCore::UI::UISceneForecastFrameView> );
static_assert( !HasForecastRetainedBytesCapability<SkullbonezCore::UI::UISceneForecastFrameView> );

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
    using SkullbonezCore::UI::UIWindowInteractionOwner;
    using SkullbonezCore::UI::InputControl::UIInputSnapshot;

    UIWindowInteractionOwner owner;
    owner.SetVisible( true, 0.0 );
    owner.SetWindowBounds( 100, 120, 760, 520 );

    UIInputSnapshot input;
    input.mouseX = 100 + 760 - 25;
    input.mouseY = 120 + 22;
    input.leftDown = true;
    input.leftPressed = true;

    const SkullbonezCore::UI::SceneNavigationModel sceneNavigation;
    owner.UpdateInput( input, { 1920, 1080, 1.0 }, {}, { 0xffffffffu }, sceneNavigation );

    CHECK_FALSE( owner.IsVisible() );
    CHECK( owner.IsMinimized() );
}

TEST_CASE( "UI interaction anchors preserve a window-local point across layout sizes" )
{
    SkullbonezCore::UI::UIWindowInteractionOwner owner;
    owner.SetWindowBounds( 100, 120, 760, 520 );

    char anchor[64] = {};
    REQUIRE( owner.CaptureInteractionAnchor( 479, 379, anchor, sizeof( anchor ) ) );

    owner.SetWindowBounds( 25, 40, 380, 260 );
    int resolvedX = 0;
    int resolvedY = 0;
    REQUIRE( owner.ResolveInteractionAnchor( anchor, resolvedX, resolvedY ) );
    CHECK( resolvedX == 214 );
    CHECK( resolvedY == 169 );
}

TEST_CASE( "UI capture cancellation discards deferred replay memory previews" )
{
    using SkullbonezCore::UI::InGameUITab;
    using SkullbonezCore::UI::UIWindowInteractionOwner;

    UIWindowInteractionOwner owner;
    owner.SetVisible( true, 0.0 );
    owner.SetActiveTab( InGameUITab::Memory );

    const auto armPreview = [&owner]()
    {
        auto widgets = owner.Widgets();
        widgets.activeSlider = 901;
        widgets.memoryOverlay.previewRetentionSeconds = 93;
        widgets.memoryOverlay.previewBudgetMiB = 384;
    };
    const auto checkDiscarded = [&owner]()
    {
        auto widgets = owner.Widgets();
        CHECK( widgets.activeSlider == 0 );
        CHECK( widgets.memoryOverlay.previewRetentionSeconds == -1 );
        CHECK( widgets.memoryOverlay.previewBudgetMiB == -1 );
    };

    armPreview();
    owner.CancelInputCapture();
    checkDiscarded();

    armPreview();
    owner.SetActiveTab( InGameUITab::Scene );
    checkDiscarded();

    owner.SetVisible( true, 1.0 );
    armPreview();
    owner.SetMinimized( true, 2.0 );
    checkDiscarded();

    owner.SetVisible( true, 3.0 );
    armPreview();
    owner.SetVisible( false, 4.0 );
    checkDiscarded();
}

TEST_CASE( "UI programmatic scroll reveal starts at the next visible draw" )
{
    SkullbonezCore::UI::UIWindowInteractionOwner owner;

    owner.SetScrollY( 120.0f );
    owner.PrepareForDraw( 50.0 );
    CHECK( owner.Widgets().scrollbarVisibleUntil == doctest::Approx( 51.2 ) );

    owner.PrepareForDraw( 70.0 );
    CHECK( owner.Widgets().scrollbarVisibleUntil == doctest::Approx( 51.2 ) );

    owner.SetScrollY( 80.0f );
    owner.PrepareForDraw( 70.0 );
    CHECK( owner.Widgets().scrollbarVisibleUntil == doctest::Approx( 71.2 ) );
}

TEST_CASE( "UI narrow clients replace ordinary window minima with reachable bounds" )
{
    using SkullbonezCore::UI::UIRect;
    using SkullbonezCore::UI::UIWindowState;
    using SkullbonezCore::UI::Chrome::ClampWindowToScreen;
    using SkullbonezCore::UI::FrameComposition::BuildEditorMinimizedStatusLayout;
    using SkullbonezCore::UI::FrameComposition::DrawEditorMinimizedWindow;
    using SkullbonezCore::UI::FrameComposition::EditorMinimizedWidth;
    using SkullbonezCore::UI::FrameComposition::MinimizedCameraModeComboBounds;
    using SkullbonezCore::UI::FrameComposition::MinimizedWidthWithCameraModeCombo;

    UIWindowState window;
    window.x = 100;
    window.y = 120;
    window.width = 760;
    window.height = 520;
    ClampWindowToScreen( window, 320, 180, 520, 250, 10 );
    CHECK( window.x == 10 );
    CHECK( window.y == 10 );
    CHECK( window.width == 300 );
    CHECK( window.height == 160 );
    CHECK( window.x + window.width <= 310 );
    CHECK( window.y + window.height <= 170 );

    SkullbonezCore::UI::UIWindowInteractionOwner owner;
    owner.SetVisible( true, 0.0 );
    owner.SetWindowBounds( 10, 10, 300, 160 );
    {
        auto widgets = owner.Widgets();
        widgets.interaction.isResizing = true;
        widgets.interaction.resizeStartMouseX = 300;
        widgets.interaction.resizeStartMouseY = 160;
        widgets.interaction.resizeStartW = 300;
        widgets.interaction.resizeStartH = 160;
    }
    SkullbonezCore::UI::InputControl::UIInputSnapshot resizeInput;
    resizeInput.mouseX = 319;
    resizeInput.mouseY = 179;
    resizeInput.leftDown = true;
    const SkullbonezCore::UI::SceneNavigationModel sceneNavigation;
    owner.UpdateInput( resizeInput, { 320, 180, 1.0 }, {}, { 0xffffffffu }, sceneNavigation );
    CHECK( owner.Widgets().window.width <= 300 );
    CHECK( owner.Widgets().window.height <= 160 );

    SkullbonezCore::UI::UIEditorTabFrameView editor;
    const float minimizedWidth = EditorMinimizedWidth( editor, 320 );
    const UIRect minimized = SkullbonezCore::UI::Layout::MinimizedRect( 320, 180, minimizedWidth );
    CHECK( minimized.x >= 0.0f );
    CHECK( minimized.y >= 0.0f );
    CHECK( minimized.x + minimized.w <= 320.0f );
    CHECK( minimized.y + minimized.h <= 180.0f );

    const auto layout = BuildEditorMinimizedStatusLayout( minimized, editor );
    const auto insideMinimized = [&minimized]( const UIRect& bounds )
    {
        CHECK( bounds.x >= minimized.x );
        CHECK( bounds.y >= minimized.y );
        CHECK( bounds.x + bounds.w <= minimized.x + minimized.w );
        CHECK( bounds.y + bounds.h <= minimized.y + minimized.h );
    };
    insideMinimized( layout.modeChip );
    insideMinimized( layout.bodyChip );
    insideMinimized( layout.alignChip );
    insideMinimized( layout.restoreButton );
    CHECK( layout.modeChip.x + layout.modeChip.w <= layout.bodyChip.x );
    CHECK( layout.bodyChip.x + layout.bodyChip.w <= layout.alignChip.x );
    CHECK( layout.alignChip.x + layout.alignChip.w <= layout.restoreButton.x );

    const float compactEditorWidth = EditorMinimizedWidth( editor, 160 );
    const UIRect compactEditor = SkullbonezCore::UI::Layout::MinimizedRect( 160, 120, compactEditorWidth );
    UIDrawList compactEditorDrawList;
    const UIDrawContext compactEditorDraw( 160, 120, compactEditorDrawList );
    DrawEditorMinimizedWindow( compactEditorDraw, compactEditor, editor, 0, 0 );
    for ( const UIDrawList::Command& command : compactEditorDrawList.Commands() )
    {
        CHECK( command.type != UIDrawList::CommandType::Text );
    }

    const float runtimeWidth = MinimizedWidthWithCameraModeCombo( "Runtime diagnostic title", 160 );
    const UIRect runtimeMinimized = SkullbonezCore::UI::Layout::MinimizedRect( 160, 120, runtimeWidth );
    const UIRect cameraCombo = MinimizedCameraModeComboBounds( runtimeMinimized );
    CHECK( cameraCombo.w > 0.0f );
    CHECK( cameraCombo.x >= runtimeMinimized.x );
    CHECK( cameraCombo.y >= runtimeMinimized.y );
    CHECK( cameraCombo.x + cameraCombo.w <= runtimeMinimized.x + runtimeMinimized.w );
    CHECK( cameraCombo.y + cameraCombo.h <= runtimeMinimized.y + runtimeMinimized.h );
}

TEST_CASE( "UI position cache hashes pointer interaction in window-local coordinates" )
{
    using SkullbonezCore::UI::UI_DIRTY_POSITION;
    using SkullbonezCore::UI::UICacheFrameKey;
    using SkullbonezCore::UI::UICacheState;
    using SkullbonezCore::UI::UIRect;
    using SkullbonezCore::UI::FrameComposition::BuildUIInteractionSignature;

    const UIRect firstBounds { 100.0f, 120.0f, 760.0f, 520.0f };
    const UIRect movedBounds { 140.0f, 160.0f, 760.0f, 520.0f };
    const uint32_t firstSignature = BuildUIInteractionSignature( { { 130, 150 }, firstBounds, 0u, 0, 0 } );
    const uint32_t movedSignature = BuildUIInteractionSignature( { { 170, 190 }, movedBounds, 0u, 0, 0 } );
    const uint32_t localPointerMove = BuildUIInteractionSignature( { { 171, 190 }, movedBounds, 0u, 0, 0 } );
    CHECK( firstSignature == movedSignature );
    CHECK( movedSignature != localPointerMove );

    UICacheState cache;
    UICacheFrameKey firstKey;
    firstKey.screenW = 1920;
    firstKey.screenH = 1080;
    firstKey.windowBounds = firstBounds;
    firstKey.contentSignature = 11u;
    firstKey.interactionSignature = firstSignature;
    cache.BeginFrame( firstKey );
    cache.MutableDrawList().AddRect( { 100.0f, 120.0f, 20.0f, 20.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } );
    cache.StoreFrame( firstKey );

    UICacheFrameKey movedKey = firstKey;
    movedKey.windowBounds = movedBounds;
    movedKey.contentSignature = 12u;
    movedKey.interactionSignature = movedSignature;
    CHECK( cache.BeginFrame( movedKey ) == ( UI_DIRTY_POSITION | SkullbonezCore::UI::UI_DIRTY_CONTENT ) );
    CHECK_FALSE( cache.CanReplayPositionOnly( movedKey ) );
    CHECK( cache.CanReplayPositionOnly( movedKey, true ) );
    CHECK( cache.ReplayOffsetX( movedKey ) == doctest::Approx( 40.0f ) );
    CHECK( cache.ReplayOffsetY( movedKey ) == doctest::Approx( 40.0f ) );

    const UIRect secondMovedBounds { 180.0f, 200.0f, 760.0f, 520.0f };
    UICacheFrameKey secondMovedKey = movedKey;
    secondMovedKey.windowBounds = secondMovedBounds;
    secondMovedKey.contentSignature = 13u;
    secondMovedKey.interactionSignature = BuildUIInteractionSignature( { { 210, 230 }, secondMovedBounds, 0u, 0, 0 } );
    cache.BeginFrame( secondMovedKey );
    CHECK( cache.CanReplayPositionOnly( secondMovedKey, true ) );
    CHECK( cache.ReplayOffsetX( secondMovedKey ) == doctest::Approx( 80.0f ) );
    CHECK( cache.ReplayOffsetY( secondMovedKey ) == doctest::Approx( 80.0f ) );
}

TEST_CASE( "Scene header keeps save defaults reachable at the minimum ordinary window width" )
{
    using SkullbonezCore::UI::InGameUIInputResult;
    using SkullbonezCore::UI::GameLayout::ResolveSceneHeaderWidths;
    using SkullbonezCore::UI::SceneTab::HandleHeaderClick;
    using SkullbonezCore::UI::SceneTab::UISceneTabState;

    constexpr float contentX = 18.0f;
    constexpr float rowBase = 100.0f;
    constexpr float contentW = 476.0f;
    const auto widths = ResolveSceneHeaderWidths( contentW );
    const float occupied = widths.combo + widths.reset + widths.resetDefaults + widths.saveDefaults + widths.gap * 3.0f;
    CHECK( occupied == doctest::Approx( contentW ) );
    CHECK( widths.combo > 0.0f );
    CHECK( widths.saveDefaults > 0.0f );

    UISceneTabState state;
    InGameUIInputResult result;
    const int saveX = static_cast<int>( contentX + contentW - widths.saveDefaults * 0.5f );
    REQUIRE( HandleHeaderClick( state, result, saveX, static_cast<int>( rowBase + 12.0f ), contentX, rowBase, contentW ) );
    CHECK( result.commands.scene.saveSceneDefaults );
}

TEST_CASE( "UI rolling prediction checkbox publishes forecast toggle intent" )
{
    using SkullbonezCore::UI::InGameUI;
    using SkullbonezCore::UI::InGameUIFrameData;
    using SkullbonezCore::UI::InGameUIInputResult;
    using SkullbonezCore::UI::InGameUITab;
    using SkullbonezCore::UI::UIForecastCommandType;
    using SkullbonezCore::UI::InputControl::UIInputSnapshot;

    const char* sceneOptions[] = { "solar_system.scene.json" };
    auto data = std::make_unique<InGameUIFrameData>();
    data->screenW = 1920;
    data->screenH = 1080;
    data->sceneName = sceneOptions[0];
    data->sceneOptions = sceneOptions;
    data->sceneOptionCount = 1;
    data->selectedSceneOption = 0;

    auto ui = std::make_unique<InGameUI>();
    ui->SetVisible( true );
    ui->SetWindowBounds( 34, 56, 760, 520 );
    ui->SetActiveTab( InGameUITab::Scene );

    const UIDrawList& frame = ui->Draw( *data );
    const int labelIndex = FindDrawTextIndex( frame, "Rolling prediction" );
    REQUIRE( labelIndex >= 0 );
    const UIDrawList::Command& label = frame.Commands()[static_cast<std::size_t>( labelIndex )];

    UIInputSnapshot input;
    input.mouseX = static_cast<int>( label.x0 + 6.0f );
    input.mouseY = static_cast<int>( label.y0 + 6.0f );
    input.leftDown = true;
    input.leftPressed = true;
    ui->SceneNavigation().browser.names.emplace_back( sceneOptions[0] );
    ui->SceneNavigation().browser.namePtrs.push_back( ui->SceneNavigation().browser.names[0].c_str() );
    const InGameUIInputResult result = ui->UpdateInput( input, { data->screenW, data->screenH, 1.0 },
                                                        { false, false, true, false }, { 0xffffffffu } );

    CHECK( result.commands.forecast.type == UIForecastCommandType::ToggleContinuous );
    CHECK( result.commands.ui.userInteracted );
}

TEST_CASE( "Scene recording combo publishes the newest-first catalog index" )
{
    using SkullbonezCore::UI::InGameUIInputResult;
    using SkullbonezCore::UI::SceneTab::HandleClosedRecordingComboClick;
    using SkullbonezCore::UI::SceneTab::HandleOpenRecordingComboClick;
    using SkullbonezCore::UI::SceneTab::UISceneTabState;

    UISceneTabState state;
    constexpr float contentX = 20.0f;
    constexpr float rowBase = 42.0f;
    constexpr float contentW = 620.0f;
    REQUIRE( HandleClosedRecordingComboClick( state, 3, 0, 100, 78, contentX, rowBase, contentW ) );
    REQUIRE( state.recordingCombo.IsOpen() );

    const SkullbonezCore::UI::UIRect dropdown = state.recordingCombo.DropdownBounds( 3 );
    InGameUIInputResult result;
    REQUIRE( HandleOpenRecordingComboClick( state, result, 3, static_cast<int>( dropdown.x + 5.0f ),
                                            static_cast<int>( dropdown.y + dropdown.h - 5.0f ), contentX, rowBase,
                                            contentW ) );
    CHECK( result.commands.scene.requestedInteractionRecordingIndex == 2 );
    CHECK( result.commands.ui.userInteracted );
    CHECK_FALSE( state.recordingCombo.IsOpen() );
}

TEST_CASE( "UI draw values preserve primitive and text order" )
{
    UIDrawList list;
    list.Clear();
    list.AddRect( { 1, 2, 3, 4 }, { 0.1f, 0.2f, 0.3f, 0.4f } );
    list.AddRoundedRect( { 5, 6, 7, 8 }, 2, { 0.2f, 0.3f, 0.4f, 0.5f } );
    list.AddTriangle( { { 1, 2 }, { 3, 4 }, { 5, 6 } }, { 0.3f, 0.4f, 0.5f, 0.6f } );
    list.AddText( { 7, 8 }, 12, { 0.8f, 0.9f, 1.0f, 1.0f }, "ordered" );

    const auto commands = list.Commands();
    REQUIRE( commands.size() == 4 );
    CHECK( commands[0].type == UIDrawList::CommandType::Rect );
    CHECK( commands[1].type == UIDrawList::CommandType::RoundedRect );
    CHECK( commands[2].type == UIDrawList::CommandType::Triangle );
    CHECK( commands[3].type == UIDrawList::CommandType::Text );
    CHECK( std::string( list.TextAt( commands[3].textOffset ) ) == "ordered" );
}


TEST_CASE( "UI combo presentation keeps options selection and disabled state aligned" )
{
    const char* options[] = { "First", "Second", "Third" };
    SkullbonezCore::UI::UIComboPresentationView presentation { std::span<const char* const>( options ), 1, 1u << 1 };

    CHECK( presentation.OptionCount() == 3 );
    CHECK( std::strcmp( presentation.SelectedText(), "Second" ) == 0 );
    CHECK_FALSE( presentation.SelectedOptionEnabled() );

    presentation.selectedTextOverride = "Selected elsewhere";
    CHECK( std::strcmp( presentation.SelectedText(), "Selected elsewhere" ) == 0 );
    presentation.selectedIndex = -1;
    CHECK( presentation.SelectedOptionEnabled() );
}


TEST_CASE( "UI draw values retain nested clips and report imbalance" )
{
    UIDrawList list;
    list.Clear();
    list.PushClip( { 0, 0, 100, 100 } );
    list.PushClip( { 10, 10, 20, 20 } );
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
    list.AddPreviewImage( { 4, true }, { 10, 20, 300, 160 }, { 0.08f, 0.09f, 0.11f, 1.0f }, "Preview unavailable" );

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
        list.AddRect( { 0, 0, 1, 1 }, { 1, 1, 1, 1 } );
    }
    CHECK( list.Commands().size() == UIDrawList::MAX_COMMANDS );
    CHECK( list.GetStats().commandOverflow );

    list.Clear();
    std::array<char, UIDrawList::MAX_TEXT_BYTES + 1> oversizedText {};
    oversizedText.fill( 'x' );
    oversizedText.back() = '\0';
    list.AddText( { 0, 0 }, 10, { 1, 1, 1, 1 }, oversizedText.data() );
    CHECK( list.GetStats().textOverflow );
    REQUIRE( list.Commands().size() == 1 );
    CHECK( std::strlen( list.TextAt( list.Commands()[0].textOffset ) ) == UIDrawList::MAX_TEXT_BYTES - 1 );

    list.Clear();
    for ( int depth = 0; depth < UIDrawList::MAX_CLIP_DEPTH + 1; ++depth )
    {
        list.PushClip( { 0, 0, 1, 1 } );
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
    cached->AddText( { 10, 20 }, 12, { 1, 1, 1, 1 }, "cached label" );
    cached->PushClip( { 5, 6, 100, 80 } );
    cached->AddPreviewImage( { 2, true }, { 8, 9, 40, 30 }, { 0.1f, 0.2f, 0.3f, 1.0f }, "Preview unavailable" );
    cached->PopClip();

    auto frame = std::make_unique<UIDrawList>();
    frame->Clear();
    frame->AddRect( { 0, 0, 1, 1 }, { 1, 0, 0, 1 } );
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
        InGameUITab::Profiler, InGameUITab::Scene,     InGameUITab::Editor,  InGameUITab::Physics,
        InGameUITab::Options,  InGameUITab::Render,    InGameUITab::Targets, InGameUITab::Keys,
        InGameUITab::Sky,      InGameUITab::Cinematic, InGameUITab::Memory,
    };
    constexpr uint64_t expected[] = {

    // Why: the profiler fingerprint was refreshed when the per-marker Work column was added so
    // worker-thread time stopped being summed into the frame-thread rows.
    // Only this surface moved; the other ten fingerprints prove the column
    // did not disturb any other tab's stream.
#if defined( SKULLBONEZ_PORTABLE_CPU )
        // Why: the portable target links the non-instrumented UI library, matching
        // the standalone UI boundary executable rather than the Profile app.
        17282268762934632125ull,
#else
        16424379413615724563ull,
#endif
        // Invariant: the Scene stream includes the newest-first replay selector plus
        // the existing continuous-forecast controls and stability rows.
        2399826200700883422ull, // Scene: render-frame lockstep is named as a capture request.
        643319089294822447ull,
        9774020997193876338ull,
        16562541090565446015ull, // Options: the same request is labelled Capture lockstep.
        13838569643518502325ull,
        1186693958027131891ull,
        5057719176066529734ull,
        3243788985155815295ull,
        15645422141942934428ull,
        14809053394253860312ull, // Memory: prediction evidence bank current/peak rows added.
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

        if ( tabs[surface] == InGameUITab::Scene )
        {
            REQUIRE( FindDrawTextIndex( frame, "running / capture lockstep off" ) >= 0 );
            const int forecastTitleIndex = FindDrawTextIndex( frame, "Continuous orbital forecast" );
            const int forecastButtonIndex = FindDrawTextIndex( frame, "Rolling prediction" );
            REQUIRE( forecastTitleIndex >= 0 );
            REQUIRE( forecastButtonIndex >= 0 );
            const UIDrawList::Command& forecastTitle = frame.Commands()[static_cast<std::size_t>( forecastTitleIndex )];
            const UIDrawList::Command& forecastButton = frame.Commands()[static_cast<std::size_t>( forecastButtonIndex )];
            CHECK( forecastTitle.pxSize == doctest::Approx( 12.0f ) );
            CHECK( forecastButton.y0 - forecastTitle.y0 > 20.0f );
            CHECK( forecastButton.y0 - forecastTitle.y0 < 40.0f );
        }

        if ( tabs[surface] == InGameUITab::Options )
        {
            REQUIRE( FindDrawTextIndex( frame, "Capture lockstep" ) >= 0 );
        }

        CHECK( frame.Fingerprint() == expected[surface] );
        CHECK_FALSE( frame.GetStats().commandOverflow );
        CHECK_FALSE( frame.GetStats().textOverflow );
        CHECK_FALSE( frame.GetStats().clipOverflow );
    }
}

TEST_CASE( "GameUI projects focused tab views from the root frame" )
{
    auto data = std::make_unique<SkullbonezCore::UI::InGameUIFrameData>();
    data->modelCapacity = 321;
    data->rngSeed = 77u;
    data->editorObjectType = 9;
    data->selectedCineModeSceneOption = 3;
    data->ordinaryRender.shadow.enabled = false;
    data->cinematic.shadow.enabled = true;
    data->operatorEditor.forecast.available = true;
    data->operatorEditor.forecast.simulatedSeconds = 12.5;
    data->timeScale = 2.5f;
    data->worldGravity = -12.0f;
    data->profilerMarkerOptionCount = 4;
    data->reserveGrowthEventTotalCount = 19u;
    data->currentFrame = 42;

    const auto controls = data->ControlsTabFrame();
    const auto editor = data->EditorTabFrame();
    const auto cinematic = data->CinematicTabFrame();
    const auto options = data->OptionsTabFrame();
    const auto physics = data->PhysicsTabFrame();
    const auto profiler = data->ProfilerTabFrame();
    const auto memory = data->MemoryTabFrame();
    const auto scene = data->SceneTabFrame();

    CHECK( controls.modelCapacity == 321 );
    CHECK( controls.rngSeed == 77u );
    CHECK( editor.editorObjectType == 9 );
    CHECK( cinematic.selectedCineModeSceneOption == 3 );
    CHECK( &cinematic.cinematic == &data->cinematic );
    CHECK( options.timeScale == doctest::Approx( 2.5f ) );
    CHECK_FALSE( options.ordinaryShadowsEnabled );
    CHECK( options.cinematicShadowsEnabled );
    CHECK( physics.worldGravity == doctest::Approx( -12.0f ) );
    CHECK( &physics.physicsDebug == &data->physicsDebug );
    CHECK( profiler.markerOptions == data->profilerMarkerOptions );
    CHECK( profiler.markerOptionCount == 4 );
    CHECK( &memory.mainMemory == &data->mainMemory );
    CHECK( memory.reserveGrowthEventTotalCount == 19u );
    CHECK( scene.forecast.available );
    CHECK( scene.forecast.simulatedSeconds == doctest::Approx( 12.5 ) );
    CHECK( scene.currentFrame == 42 );
}

TEST_CASE( "GameUI gravity slider endpoints emit signed world acceleration from shared policy" )
{
    namespace PhysicsTab = SkullbonezCore::UI::PhysicsTab;
    namespace Policy = SkullbonezCore::UI::OperatorControlPolicy;
    namespace Widgets = SkullbonezCore::UI::Widgets;

    PhysicsTab::UIPhysicsTabState state;
    state.worldGravitySlider.SetBounds( 20.0f, 30.0f, 320.0f, 34.0f );
    const SkullbonezCore::UI::UIRect track = Widgets::SliderTrackBounds( state.worldGravitySlider.Bounds() );

    SkullbonezCore::UI::InGameUIInputResult minimumResult;
    REQUIRE( PhysicsTab::UpdateActiveSlider( state, PhysicsTab::SLIDER_WORLD_GRAVITY, static_cast<int>( track.x ),
                                             minimumResult ) );
    CHECK( minimumResult.commands.water.requestWorldGravity );
    CHECK( minimumResult.commands.water.requestedWorldGravity == doctest::Approx( -Policy::UI_WORLD_GRAVITY_MIN ) );

    SkullbonezCore::UI::InGameUIInputResult maximumResult;
    REQUIRE( PhysicsTab::UpdateActiveSlider( state, PhysicsTab::SLIDER_WORLD_GRAVITY, static_cast<int>( track.x + track.w ),
                                             maximumResult ) );
    CHECK( maximumResult.commands.water.requestWorldGravity );
    CHECK( maximumResult.commands.water.requestedWorldGravity == doctest::Approx( -Policy::UI_WORLD_GRAVITY_MAX ) );
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
    const SkullbonezCore::UI::UIMemoryTabFrameView memoryFrame = data->MemoryTabFrame();

    UIDrawList list;
    UIDrawContext draw( 1920, 1080, list );
    UIMemoryOverlayState state;
    SkullbonezCore::UI::MemoryTab::Draw( draw, state, memoryFrame, 20.0f, 0.0f, 720.0f, 260.0f, -450.0f, 0, 0, 0 );

    UIDrawList measuredList;
    UIDrawContext measuredDraw( 1920, 1080, measuredList );
    UIMemoryOverlayState measuredState;
    SkullbonezCore::Core::Allocation::ResetRuntimeAllocationCounters();
    SkullbonezCore::Core::Allocation::SetRuntimeAllocationGuardMode(
        SkullbonezCore::Core::Allocation::RuntimeAllocationGuardMode::Gameplay );
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope renderScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::Render );
        SkullbonezCore::UI::MemoryTab::Draw( measuredDraw, measuredState, memoryFrame, 20.0f, 0.0f, 720.0f, 260.0f, -450.0f,
                                             0, 0, 0 );
    }
    const uint64_t memoryDrawAllocationViolations = SkullbonezCore::Core::Allocation::RuntimeAllocationGuardViolationCount();
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
