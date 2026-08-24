/*
File: TestRecordedCursorPresentation.cpp
Purpose:
  Verifies recorded-manifest fake-cursor visibility and native-pointer
  non-interference without opening a window or rendering a frame.

Summary:
  The visibility cases freeze every approved playback and interaction
  disposition. Separate InputRouter observations prove that evaluating the pure
  product policy cannot change desired or committed native capture/visibility,
  and deliberate local mutations prove both observation channels can fail.

Glossary:
  False-pass control: Deliberate mutation that proves a negative assertion can
    detect the regression it claims to guard against.

Invariants:
  - Tests call no native cursor, capture, window, or renderer API.
  - Every InputRouter mutation is confined to the test case that owns it.
  - Ordinary UI capture and passive Replay inspection use CursorBearing.

Related:
  - SkullbonezSource/Runtime/UI/RecordedCursorPresentationPolicy.h
  - SkullbonezSource/Runtime/Input/InputRouter.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.h"
#include "../SkullbonezSource/Runtime/Automation/InteractionAutomationRecorder.h"
#include "../SkullbonezSource/Runtime/Input/Input.h"
#include "../SkullbonezSource/Runtime/Input/InputRouter.h"
#include "../SkullbonezSource/Runtime/UI/RecordedCursorPresentationPolicy.h"

#include <cstring>

using SkullbonezCore::Core::SbDiagnosticStore;
using SkullbonezCore::Hardware::Input;
using SkullbonezCore::Runtime::BuildRecordedFramePublication;
using SkullbonezCore::Runtime::ClassifyRecordedCursorPlaybackPhase;
using SkullbonezCore::Runtime::ClassifyRecordedCursorPointerDisposition;
using SkullbonezCore::Runtime::FilterRecordedCursorFrame;
using SkullbonezCore::Runtime::InputRouter;
using SkullbonezCore::Runtime::PointerPresentationState;
using SkullbonezCore::Runtime::RecordedCursorFrame;
using SkullbonezCore::Runtime::RecordedCursorPlaybackPhase;
using SkullbonezCore::Runtime::RecordedCursorPointerDisposition;
using SkullbonezCore::Runtime::RecordedInputFrame;
using SkullbonezCore::Runtime::ShouldPresentRecordedCursor;

namespace
{
bool IsCleared( const RecordedCursorFrame& frame )
{
    return frame.clientX == 0 && frame.clientY == 0 && !frame.publishedRealTurn && !frame.pointerResolved &&
           !frame.recordedAppFocused;
}
} // namespace

TEST_CASE( "Recorded cursor presentation: visibility follows replayed logical facts" )
{
    using Phase = RecordedCursorPlaybackPhase;
    using Disposition = RecordedCursorPointerDisposition;

    CHECK( ShouldPresentRecordedCursor( Phase::PublishedRealTurn, Disposition::CursorBearing, true, true, true ) );

    CHECK_FALSE( ShouldPresentRecordedCursor( Phase::Inactive, Disposition::CursorBearing, true, true, true ) );
    CHECK_FALSE( ShouldPresentRecordedCursor( Phase::NeutralBaselineFrame, Disposition::CursorBearing, true, true, true ) );
    CHECK_FALSE( ShouldPresentRecordedCursor( Phase::Failed, Disposition::CursorBearing, true, true, true ) );
    CHECK_FALSE( ShouldPresentRecordedCursor( Phase::Completed, Disposition::CursorBearing, true, true, true ) );
    CHECK_FALSE( ShouldPresentRecordedCursor( Phase::PublishedRealTurn, Disposition::CursorBearing, false, true, true ) );
    CHECK_FALSE( ShouldPresentRecordedCursor( Phase::PublishedRealTurn, Disposition::CursorBearing, true, false, true ) );
    CHECK_FALSE( ShouldPresentRecordedCursor( Phase::PublishedRealTurn, Disposition::CursorBearing, true, true, false ) );
    CHECK_FALSE( ShouldPresentRecordedCursor( Phase::PublishedRealTurn, Disposition::MouseLook, true, true, true ) );
    CHECK_FALSE(
        ShouldPresentRecordedCursor( Phase::PublishedRealTurn, Disposition::EditorViewportLook, true, true, true ) );
    CHECK_FALSE(
        ShouldPresentRecordedCursor( Phase::PublishedRealTurn, Disposition::ReplayInspectionLook, true, true, true ) );
    CHECK_FALSE( ShouldPresentRecordedCursor( Phase::PublishedRealTurn, Disposition::PlacementPreview, true, true, true ) );
    CHECK_FALSE( ShouldPresentRecordedCursor( Phase::PublishedRealTurn, Disposition::CameraLookCapture, true, true, true ) );
    CHECK_FALSE(
        ShouldPresentRecordedCursor( Phase::PublishedRealTurn, Disposition::ToolGestureCapture, true, true, true ) );
}

TEST_CASE( "Recorded cursor presentation: publication reuses normal recorded input mapping" )
{
    RecordedInputFrame recorded;
    recorded.normalizedX = 0.25f;
    recorded.normalizedY = 0.75f;
    recorded.rawMouseX = -17;
    recorded.rawMouseY = 29;
    recorded.wheelDelta = 120;
    recorded.hasPointer = true;
    recorded.appFocused = true;
    recorded.leftDown = true;
    recorded.rightDown = true;
    recorded.middleDown = true;
    recorded.keyWords[static_cast<std::size_t>( 'A' ) / 64u] |= uint64_t { 1 } << ( static_cast<unsigned int>( 'A' ) & 63u );

    Input::AutomationState inputState;
    const RecordedCursorFrame cursor = BuildRecordedFramePublication( recorded, inputState, 101, 81, true );
    CHECK( cursor.clientX == 25 );
    CHECK( cursor.clientY == 60 );
    CHECK( cursor.publishedRealTurn );
    CHECK( cursor.pointerResolved );
    CHECK( cursor.recordedAppFocused );

    CHECK( inputState.enabled );
    CHECK( inputState.overrideAppFocused );
    CHECK( inputState.mouseClientPosition.x == cursor.clientX );
    CHECK( inputState.mouseClientPosition.y == cursor.clientY );
    CHECK( inputState.hasMouseClientPosition == cursor.pointerResolved );
    CHECK( inputState.appFocused == cursor.recordedAppFocused );
    CHECK( inputState.leftMouseDown == recorded.leftDown );
    CHECK( inputState.rightMouseDown == recorded.rightDown );
    CHECK( inputState.middleMouseDown == recorded.middleDown );
    CHECK( inputState.mouseWheelDelta == recorded.wheelDelta );
    CHECK( inputState.rawMouseDeltaX == recorded.rawMouseX );
    CHECK( inputState.rawMouseDeltaY == recorded.rawMouseY );
    CHECK( inputState.keyWords == recorded.keyWords );
}

TEST_CASE( "Recorded cursor presentation: semantic anchors relocate only real pointers" )
{
    RecordedInputFrame recorded;
    recorded.normalizedX = 0.9f;
    recorded.normalizedY = 0.8f;
    recorded.hasPointer = true;
    recorded.appFocused = true;
    const POINT semanticPosition { 37, 19 };
    Input::AutomationState inputState;
    const RecordedCursorFrame semantic = BuildRecordedFramePublication( recorded, inputState, 101, 81, true,
                                                                        &semanticPosition );
    CHECK( semantic.clientX == semanticPosition.x );
    CHECK( semantic.clientY == semanticPosition.y );
    CHECK( semantic.pointerResolved );
    CHECK( inputState.hasMouseClientPosition );
    CHECK( inputState.mouseClientPosition.x == semantic.clientX );
    CHECK( inputState.mouseClientPosition.y == semantic.clientY );

    recorded.hasPointer = false;
    const RecordedCursorFrame absent = BuildRecordedFramePublication( recorded, inputState, 101, 81, true,
                                                                      &semanticPosition );
    CHECK( absent.publishedRealTurn );
    CHECK_FALSE( absent.pointerResolved );
    CHECK( absent.clientX == 0 );
    CHECK( absent.clientY == 0 );

    CHECK_FALSE( inputState.hasMouseClientPosition );
    CHECK( inputState.mouseClientPosition.x == 0 );
    CHECK( inputState.mouseClientPosition.y == 0 );
}

TEST_CASE( "Recorded cursor presentation: production classification and filter clear every hidden frame" )
{
    using Phase = RecordedCursorPlaybackPhase;
    using Disposition = RecordedCursorPointerDisposition;

    RecordedCursorFrame visible;
    visible.clientX = 63;
    visible.clientY = 47;
    visible.publishedRealTurn = true;
    visible.pointerResolved = true;
    visible.recordedAppFocused = true;

    CHECK( ClassifyRecordedCursorPlaybackPhase( true, true, false, false, true, true ) == Phase::PublishedRealTurn );
    CHECK( ClassifyRecordedCursorPlaybackPhase( true, true, false, false, true, false ) == Phase::NeutralBaselineFrame );
    CHECK( ClassifyRecordedCursorPlaybackPhase( false, true, false, false, true, true ) == Phase::Inactive );
    CHECK( ClassifyRecordedCursorPlaybackPhase( true, false, false, false, true, true ) == Phase::Inactive );
    CHECK( ClassifyRecordedCursorPlaybackPhase( true, true, true, true, true, true ) == Phase::Failed );
    CHECK( ClassifyRecordedCursorPlaybackPhase( true, true, false, true, true, true ) == Phase::Completed );

    CHECK( ClassifyRecordedCursorPointerDisposition( true, true, true, true, true, true ) ==
           Disposition::CameraLookCapture );
    CHECK( ClassifyRecordedCursorPointerDisposition( false, true, true, true, true, true ) ==
           Disposition::ToolGestureCapture );
    CHECK( ClassifyRecordedCursorPointerDisposition( false, false, true, true, true, true ) ==
           Disposition::EditorViewportLook );
    CHECK( ClassifyRecordedCursorPointerDisposition( false, false, false, true, true, true ) ==
           Disposition::ReplayInspectionLook );
    CHECK( ClassifyRecordedCursorPointerDisposition( false, false, false, false, true, true ) ==
           Disposition::PlacementPreview );
    CHECK( ClassifyRecordedCursorPointerDisposition( false, false, false, false, false, true ) == Disposition::MouseLook );
    CHECK( ClassifyRecordedCursorPointerDisposition( false, false, false, false, false, false ) ==
           Disposition::CursorBearing );

    CHECK_FALSE( IsCleared( FilterRecordedCursorFrame( visible, Phase::PublishedRealTurn, Disposition::CursorBearing, 128,
                                                       96, false, false ) ) );
    CHECK( IsCleared(
        FilterRecordedCursorFrame( visible, Phase::Inactive, Disposition::CursorBearing, 128, 96, false, false ) ) );
    CHECK( IsCleared( FilterRecordedCursorFrame( visible, Phase::NeutralBaselineFrame, Disposition::CursorBearing, 128, 96,
                                                 false, false ) ) );
    CHECK( IsCleared(
        FilterRecordedCursorFrame( visible, Phase::Failed, Disposition::CursorBearing, 128, 96, false, false ) ) );
    CHECK( IsCleared(
        FilterRecordedCursorFrame( visible, Phase::Completed, Disposition::CursorBearing, 128, 96, false, false ) ) );
    CHECK( IsCleared(
        FilterRecordedCursorFrame( visible, Phase::PublishedRealTurn, Disposition::CursorBearing, 128, 96, true, false ) ) );
    CHECK( IsCleared(
        FilterRecordedCursorFrame( visible, Phase::PublishedRealTurn, Disposition::CursorBearing, 128, 96, false, true ) ) );

    for ( const Disposition disposition :
          { Disposition::MouseLook, Disposition::EditorViewportLook, Disposition::ReplayInspectionLook,
            Disposition::PlacementPreview, Disposition::CameraLookCapture, Disposition::ToolGestureCapture } )
    {
        CHECK( IsCleared(
            FilterRecordedCursorFrame( visible, Phase::PublishedRealTurn, disposition, 128, 96, false, false ) ) );
    }

    RecordedCursorFrame hidden = visible;
    hidden.pointerResolved = false;
    CHECK( IsCleared(
        FilterRecordedCursorFrame( hidden, Phase::PublishedRealTurn, Disposition::CursorBearing, 128, 96, false, false ) ) );
    hidden = visible;
    hidden.recordedAppFocused = false;
    CHECK( IsCleared(
        FilterRecordedCursorFrame( hidden, Phase::PublishedRealTurn, Disposition::CursorBearing, 128, 96, false, false ) ) );
    hidden = visible;
    hidden.clientX = 128;
    CHECK( IsCleared(
        FilterRecordedCursorFrame( hidden, Phase::PublishedRealTurn, Disposition::CursorBearing, 128, 96, false, false ) ) );
    hidden = visible;
    hidden.clientY = -1;
    CHECK( IsCleared(
        FilterRecordedCursorFrame( hidden, Phase::PublishedRealTurn, Disposition::CursorBearing, 128, 96, false, false ) ) );
    CHECK( IsCleared(
        FilterRecordedCursorFrame( visible, Phase::PublishedRealTurn, Disposition::CursorBearing, 0, 96, false, false ) ) );
}

TEST_CASE( "Recorded cursor presentation: pure policy leaves native pointer state uncommitted" )
{
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
    PointerPresentationState committed;
    REQUIRE( router.ConsumePointerPresentationChange( committed ) );
    router.RequestNativeCapture();
    router.RequestCursorVisible( false );
    REQUIRE( router.ConsumePointerPresentationChange( committed ) );

    const bool captureBefore = router.NativeCaptureRequested();
    const bool cursorBefore = router.CursorVisibleRequested();
    const PointerPresentationState requestedBefore { captureBefore, cursorBefore };
    const PointerPresentationState committedBefore = committed;
    RecordedCursorFrame visible;
    visible.clientX = 10;
    visible.clientY = 20;
    visible.publishedRealTurn = true;
    visible.pointerResolved = true;
    visible.recordedAppFocused = true;
    CHECK_FALSE(
        IsCleared( FilterRecordedCursorFrame( visible, RecordedCursorPlaybackPhase::PublishedRealTurn,
                                              RecordedCursorPointerDisposition::CursorBearing, 64, 64, false, false ) ) );
    CHECK( router.NativeCaptureRequested() == captureBefore );
    CHECK( router.CursorVisibleRequested() == cursorBefore );
    CHECK_FALSE( router.ConsumePointerPresentationChange( committed ) );
    const PointerPresentationState requestedAfter { router.NativeCaptureRequested(), router.CursorVisibleRequested() };
    CHECK( std::memcmp( &requestedBefore, &requestedAfter, sizeof( requestedBefore ) ) == 0 );
    CHECK( std::memcmp( &committedBefore, &committed, sizeof( committedBefore ) ) == 0 );
}

TEST_CASE( "Recorded cursor presentation: false-pass controls detect both native mutation channels" )
{
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
    PointerPresentationState committed;
    REQUIRE( router.ConsumePointerPresentationChange( committed ) );

    const bool captureBefore = router.NativeCaptureRequested();
    router.RequestNativeCapture();
    CHECK( router.NativeCaptureRequested() != captureBefore );
    REQUIRE( router.ConsumePointerPresentationChange( committed ) );
    CHECK( committed.nativeCapture );

    const bool cursorBefore = router.CursorVisibleRequested();
    router.RequestCursorVisible( false );
    CHECK( router.CursorVisibleRequested() != cursorBefore );
    REQUIRE( router.ConsumePointerPresentationChange( committed ) );
    CHECK_FALSE( committed.cursorVisible );
}
