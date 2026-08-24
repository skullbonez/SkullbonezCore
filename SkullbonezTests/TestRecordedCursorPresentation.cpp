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
#include "../SkullbonezSource/Runtime/Input/InputRouter.h"
#include "../SkullbonezSource/Runtime/UI/RecordedCursorPresentationPolicy.h"

using SkullbonezCore::Core::SbDiagnosticStore;
using SkullbonezCore::Runtime::InputRouter;
using SkullbonezCore::Runtime::PointerPresentationState;
using SkullbonezCore::Runtime::RecordedCursorPlaybackPhase;
using SkullbonezCore::Runtime::RecordedCursorPointerDisposition;
using SkullbonezCore::Runtime::ShouldPresentRecordedCursor;

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
    CHECK( ShouldPresentRecordedCursor( RecordedCursorPlaybackPhase::PublishedRealTurn,
                                        RecordedCursorPointerDisposition::CursorBearing, true, true, true ) );
    CHECK( router.NativeCaptureRequested() == captureBefore );
    CHECK( router.CursorVisibleRequested() == cursorBefore );
    CHECK_FALSE( router.ConsumePointerPresentationChange( committed ) );
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
