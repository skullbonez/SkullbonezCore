/*
File: Agentic/Tests/RuntimeInteractionPolicyTests/RuntimeInteractionPolicyTests.cpp
Purpose:
  Verifies CPU-side runtime interaction, picker, and fixed-capacity UI surface
  rules that should not require a renderer launch.

Summary:
  Covers interaction ownership, pointer capture, picking, and shared UI bounds
  through deterministic CPU-only policy tests.

Mental model:
  RuntimeInteractionPolicyTests.cpp verifies CPU-side runtime interaction and
  picker rules that should not require a renderer launch. As an implementation
  unit, keep edits anchored on the behavior under test and the regression
  signal and on the glossary/invariants below.

Glossary:
  Pointer capture: Exclusive owner for an in-progress mouse gesture.
  Pick ray: World-space line projected from a screen pointer into the scene.
  Collision shape: Authored sphere, oriented box, or convex hull used as the
    pickable geometry for a model.
  Runtime UI surface: Disposable ordered table that gives input and rendering
    one shared set of control bounds and states.

Invariants:
  Tests exercise RuntimeInteractionController policy without editor, replay,
  renderer, or physics launches.
  Gesture, owner, pointer capture, camera-look, and physics-advance transitions
  must remain mutually consistent.

Related:
  - AGENTS.md
  - Agentic/Reports/2026-07-11/interaction-state-machine-closure-review.md
  - SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h
  - SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.h
  - SkullbonezSource/Runtime/UI/RuntimeUiSurface.h
*/
#include "Runtime/Interaction/RuntimeInteractionController.h"
#include "Runtime/Interaction/RuntimeInteractionCommands.h"
#include "Runtime/Interaction/RuntimePickGeometry.h"
#include "Runtime/UI/RuntimeUiSurface.h"
#include "Core/SbDiagnosticStore.h"

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Vector;
using SkullbonezCore::Core::SbDiagnosticStore;
using SkullbonezCore::Physics::PhysicsBodyHandle;

namespace
{
struct TestFailure : public std::runtime_error
{
    explicit TestFailure( const std::string& message ) : std::runtime_error( message )
    {
    }
};


void Fail( const char* file, int line, const std::string& message )
{
    std::ostringstream out;
    out << file << "(" << line << "): " << message;
    throw TestFailure( out.str() );
}


void ExpectTrue( bool value, const char* expression, const char* file, int line )
{

    if ( !value )
    {
        Fail( file, line, std::string( "expected true: " ) + expression );
    }
}


void ExpectFloatNear( float actual, float expected, float tolerance, const char* actualExpression,
                      const char* expectedExpression, const char* file, int line )
{

    if ( fabsf( actual - expected ) > tolerance )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " near " << expectedExpression << ", actual " << actual << ", expected "
            << expected << ", tolerance " << tolerance;
        Fail( file, line, out.str() );
    }
}


template <typename T, typename U>
void ExpectEqualImpl( const T& actual, const U& expected, const char* actualExpression, const char* expectedExpression,
                      const char* file, int line )
{

    if ( !( actual == expected ) )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " == " << expectedExpression;
        Fail( file, line, out.str() );
    }
}


#define EXPECT_TRUE( expression ) ExpectTrue( !!( expression ), #expression, __FILE__, __LINE__ )
#define EXPECT_FALSE( expression ) ExpectTrue( !( expression ), "!(" #expression ")", __FILE__, __LINE__ )
#define EXPECT_EQ( actual, expected ) ExpectEqualImpl( ( actual ), ( expected ), #actual, #expected, __FILE__, __LINE__ )
#define EXPECT_NEAR( actual, expected, tolerance )                                                                          \
    ExpectFloatNear( ( actual ), ( expected ), ( tolerance ), #actual, #expected, __FILE__, __LINE__ )

struct TestCase
{
    const char* name = "";
    void ( *run )() = nullptr;
};


RuntimeInteractionFrameInput MakeDefaultFrameInput()
{
    RuntimeInteractionFrameInput input;
    input.scenePhysicsEnabled = true;
    input.stepHeld = false;
    input.sceneTimeScale = 1.0f;
    return input;
}


RuntimeInteractionGesture MakeMousePickupGesture()
{
    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::MousePickupDrag;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = 42;
    gesture.startY = 24;
    gesture.body = PhysicsBodyHandle { 7u, 3u };
    return gesture;
}


RuntimeInteractionGesture MakeCameraLookGesture()
{
    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::CameraLook;
    gesture.button = RuntimePointerButton::Right;
    gesture.startX = 11;
    gesture.startY = 13;
    return gesture;
}


RuntimeInteractionGesture MakeReplayGesture( RuntimeInteractionGestureKind kind )
{
    RuntimeInteractionGesture gesture;
    gesture.kind = kind;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = 30;
    gesture.startY = 50;

    if ( kind == RuntimeInteractionGestureKind::ReplayVelocityDrag )
    {
        gesture.body = PhysicsBodyHandle { 9u, 4u };
        gesture.axis = 2;
    }
    else if ( kind == RuntimeInteractionGestureKind::ReplayCauseTreeDrag )
    {
        gesture.axis = 0;
    }

    return gesture;
}


RuntimeInteractionGesture MakeGizmoGesture( bool angular )
{
    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::GizmoDrag;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = 64;
    gesture.startY = 96;
    gesture.body = PhysicsBodyHandle { 5u, 2u };
    gesture.axis = 1;
    gesture.angular = angular;
    gesture.gizmoKind = angular ? RuntimeGizmoDragKind::Rotate : RuntimeGizmoDragKind::Translate;
    return gesture;
}


RuntimeGestureEvent BeginGesture( RuntimeInteractionController& controller, const RuntimeInteractionGesture& gesture,
                                  RuntimePointerCaptureOwner captureOwner = RuntimePointerCaptureOwner::ToolGesture,
                                  InteractionExitReason reason = InteractionExitReason::BeginGesture )
{
    RuntimeGestureCommand command;
    command.gesture = gesture;
    command.captureOwner = captureOwner;
    command.reason = reason;
    RuntimeGestureEvent event;
    EXPECT_TRUE( controller.ApplyGestureCommand( command, event ) );
    return event;
}


RuntimeGestureEvent EndGesture( RuntimeInteractionController& controller, RuntimeInteractionGestureKind kind,
                                InteractionExitReason reason = InteractionExitReason::EndGesture )
{
    RuntimeGestureCommand command;
    command.action = RuntimeGestureCommandAction::End;
    command.gesture.kind = kind;
    command.reason = reason;
    RuntimeGestureEvent event;
    EXPECT_TRUE( controller.ApplyGestureCommand( command, event ) );
    return event;
}


RuntimePickShapeTransform MakePickTransform( const Vector3& position = ZERO_VECTOR,
                                             const Quaternion& orientation = IDENTITY_QUATERNION )
{
    RuntimePickShapeTransform transform;
    transform.position = position;
    transform.orientation = orientation;
    return transform;
}


Quaternion MakeYawQuarterTurn()
{
    Quaternion yaw;
    yaw.RotateAboutAxis( Vector3( 0.0f, 1.0f, 0.0f ), _HALF_PI );
    return yaw;
}


void TestExactBoxPickRejectsOldBoundingSphereEnvelope()
{
    const CollisionShape shape = BoundingBox( Vector3( 1.0f, 5.0f, 1.0f ), ZERO_VECTOR );
    const RuntimePickShapeTransform transform = MakePickTransform();
    float rayT = 0.0f;

    EXPECT_FALSE( TryIntersectRuntimePickShape( shape, transform, Vector3( 4.0f, 0.0f, -20.0f ), Vector3( 0.0f, 0.0f, 1.0f ), rayT ) );

    EXPECT_TRUE( TryIntersectRuntimePickShape( shape, transform, Vector3( 0.0f, 0.0f, -20.0f ), Vector3( 0.0f, 0.0f, 1.0f ), rayT ) );
    EXPECT_NEAR( rayT, 19.0f, 0.001f );
}


void TestTreeTrunkHullPickUsesConvexFaces()
{
    SbDiagnosticStore diagnostics;
    ConvexHullShape trunk;
    EXPECT_TRUE( ConvexHullShape::TryLoadFromFile( diagnostics, "SkullbonezData/hulls/tree_trunk_faceted.hull", trunk ).Ok() );

    const CollisionShape shape = trunk;
    const RuntimePickShapeTransform transform = MakePickTransform();
    float rayT = 0.0f;

    EXPECT_TRUE( TryIntersectRuntimePickShape( shape, transform, Vector3( 0.0f, 0.0f, -20.0f ), Vector3( 0.0f, 0.0f, 1.0f ), rayT ) );
    EXPECT_NEAR( rayT, 17.58f, 0.01f );

    EXPECT_FALSE( TryIntersectRuntimePickShape( shape, transform, Vector3( 4.0f, 0.0f, -20.0f ), Vector3( 0.0f, 0.0f, 1.0f ), rayT ) );
}


void TestRotatedShapePickReturnsNearestEntry()
{
    SbDiagnosticStore diagnostics;
    const RuntimePickShapeTransform rotated = MakePickTransform( ZERO_VECTOR, MakeYawQuarterTurn() );
    float rayT = 0.0f;

    const CollisionShape box = BoundingBox( Vector3( 1.0f, 2.0f, 3.0f ), ZERO_VECTOR );
    EXPECT_TRUE( TryIntersectRuntimePickShape( box, rotated, Vector3( 0.0f, 0.0f, -10.0f ), Vector3( 0.0f, 0.0f, 1.0f ), rayT ) );
    EXPECT_NEAR( rayT, 9.0f, 0.001f );

    ConvexHullShape loadedTrunk;
    EXPECT_TRUE(
        ConvexHullShape::TryLoadFromFile( diagnostics, "SkullbonezData/hulls/tree_trunk_faceted.hull", loadedTrunk ).Ok() );
    const CollisionShape trunk = loadedTrunk;

    EXPECT_TRUE( TryIntersectRuntimePickShape( trunk, rotated, Vector3( 0.0f, 0.0f, -20.0f ), Vector3( 0.0f, 0.0f, 1.0f ), rayT ) );
    EXPECT_NEAR( rayT, 17.2f, 0.02f );
}


void TestMousePickupDragRunsPhysicsWithoutStepHold()
{
    RuntimeInteractionController controller;
    const RuntimeInteractionTransition transition = controller.EnterManipulator();

    EXPECT_TRUE( transition.ownerChanged );
    EXPECT_EQ( controller.Workspace(), RuntimeWorkspace::Live );
    EXPECT_EQ( controller.Owner(), WorldInteractionOwner::Manipulator );

    const RuntimeGestureEvent beginEvent = BeginGesture( controller, MakeMousePickupGesture(),
                                                         RuntimePointerCaptureOwner::ToolGesture,
                                                         InteractionExitReason::EnterManipulator );

    EXPECT_EQ( beginEvent.type, RuntimeGestureEventType::Began );

    RuntimeInteractionFrameInput input = MakeDefaultFrameInput();
    input.stepHeld = false;

    const RuntimeInteractionFramePolicy policy = controller.BuildFramePolicy( input );

    EXPECT_TRUE( policy.manipulatorActive );
    EXPECT_EQ( policy.physicsAdvance, PhysicsAdvanceState::Running );
    EXPECT_EQ( policy.physicsTimeScale, 1.0f );
}


void TestToolGestureSuppressesCameraLook()
{
    RuntimeInteractionController controller;
    controller.EnterManipulator();

    const RuntimeGestureEvent beginEvent = BeginGesture( controller, MakeMousePickupGesture(),
                                                         RuntimePointerCaptureOwner::ToolGesture,
                                                         InteractionExitReason::EnterManipulator );

    EXPECT_EQ( beginEvent.type, RuntimeGestureEventType::Began );
    EXPECT_EQ( beginEvent.previousPointerCapture, RuntimePointerCaptureOwner::None );
    EXPECT_EQ( beginEvent.pointerCapture, RuntimePointerCaptureOwner::ToolGesture );
    EXPECT_EQ( beginEvent.gesture.kind, RuntimeInteractionGestureKind::MousePickupDrag );
    EXPECT_EQ( beginEvent.gesture.body.index, 7u );
    EXPECT_EQ( beginEvent.gesture.body.generation, 3u );

    RuntimeInteractionFrameInput input = MakeDefaultFrameInput();
    input.rightMouseLookHeld = true;
    input.editorViewportLookActive = true;
    input.replayInspectionLookActive = true;

    const RuntimeInteractionFramePolicy policy = controller.BuildFramePolicy( input );

    EXPECT_EQ( policy.pointerCapture, RuntimePointerCaptureOwner::ToolGesture );
    EXPECT_EQ( policy.gesture, RuntimeInteractionGestureKind::MousePickupDrag );
    EXPECT_EQ( policy.cameraLook, CameraLookState::Passive );
    EXPECT_FALSE( policy.cameraMouseLookActive );
}


void TestGestureCommandsEmitOnlyAfterSuccessfulMutation()
{
    RuntimeInteractionController controller;
    controller.EnterManipulator();

    RuntimeGestureCommand begin;
    begin.gesture = MakeMousePickupGesture();
    begin.reason = InteractionExitReason::EnterManipulator;
    RuntimeGestureEvent event;
    EXPECT_TRUE( controller.ApplyGestureCommand( begin, event ) );
    EXPECT_EQ( event.type, RuntimeGestureEventType::Began );
    EXPECT_EQ( event.previousGesture.kind, RuntimeInteractionGestureKind::None );
    EXPECT_EQ( event.gesture.kind, RuntimeInteractionGestureKind::MousePickupDrag );
    EXPECT_EQ( event.pointerCapture, RuntimePointerCaptureOwner::ToolGesture );

    RuntimeGestureCommand wrongEnd;
    wrongEnd.action = RuntimeGestureCommandAction::End;
    wrongEnd.gesture.kind = RuntimeInteractionGestureKind::GizmoDrag;
    wrongEnd.reason = InteractionExitReason::EndGesture;
    EXPECT_TRUE( !controller.ApplyGestureCommand( wrongEnd, event ) );
    EXPECT_EQ( event.type, RuntimeGestureEventType::None );
    EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::MousePickupDrag );

    RuntimeGestureCommand end;
    end.action = RuntimeGestureCommandAction::End;
    end.gesture.kind = RuntimeInteractionGestureKind::MousePickupDrag;
    end.reason = InteractionExitReason::EndGesture;
    EXPECT_TRUE( controller.ApplyGestureCommand( end, event ) );
    EXPECT_EQ( event.type, RuntimeGestureEventType::Ended );
    EXPECT_EQ( event.previousGesture.kind, RuntimeInteractionGestureKind::MousePickupDrag );
    EXPECT_EQ( event.gesture.kind, RuntimeInteractionGestureKind::None );
    EXPECT_EQ( event.pointerCapture, RuntimePointerCaptureOwner::None );
}


void TestEditorPlacementScaleUsesTypedCapture()
{
    RuntimeInteractionController controller;
    controller.EnterEdit();
    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::EditorPlacementScaleDrag;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = 120;
    gesture.startY = 80;

    const RuntimeGestureEvent beginEvent = BeginGesture( controller, gesture );
    EXPECT_EQ( beginEvent.type, RuntimeGestureEventType::Began );
    EXPECT_EQ( controller.Owner(), WorldInteractionOwner::EditorPlacement );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::ToolGesture );

    const RuntimeGestureEvent endEvent = EndGesture( controller, RuntimeInteractionGestureKind::EditorPlacementScaleDrag );
    EXPECT_EQ( endEvent.type, RuntimeGestureEventType::Ended );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );
}


void TestFocusLossCancelsCameraLookGesture()
{
    RuntimeInteractionController controller;
    controller.EnterInspect();
    RuntimeInteractionFrameInput frameInput = MakeDefaultFrameInput();
    frameInput.rightMouseLookHeld = true;
    RuntimeInteractionFramePolicy policy = controller.BuildFramePolicy( frameInput );
    RuntimeInputSnapshot snapshot;
    snapshot.appFocused = true;
    snapshot.pointer.rightDown = true;
    controller.SyncCameraLookGesture( snapshot, policy, true );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::CameraLook );

    snapshot.appFocused = false;
    controller.SyncCameraLookGesture( snapshot, policy, true );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );
    EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::None );
}


void TestCameraLookGestureCapturesPointer()
{
    RuntimeInteractionController controller;
    RuntimeInteractionFrameInput input = MakeDefaultFrameInput();
    input.rightMouseLookHeld = true;
    const RuntimeInteractionFramePolicy policy = controller.BuildFramePolicy( input );
    RuntimeInputSnapshot snapshot;
    snapshot.appFocused = true;
    snapshot.pointer.rightDown = true;
    snapshot.pointer.clientX = 321;
    snapshot.pointer.clientY = 654;
    controller.SyncCameraLookGesture( snapshot, policy, true );

    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::CameraLook );
    EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::CameraLook );
    EXPECT_EQ( controller.Gesture().button, RuntimePointerButton::Right );
    EXPECT_EQ( controller.Gesture().startX, 321 );
    EXPECT_EQ( controller.Gesture().startY, 654 );
    EXPECT_EQ( policy.cameraLook, CameraLookState::RightMouseLook );
    EXPECT_TRUE( policy.cameraMouseLookActive );
}


void TestCameraLookGestureEndsCleanly()
{
    RuntimeInteractionController controller;
    BeginGesture( controller, MakeCameraLookGesture(), RuntimePointerCaptureOwner::CameraLook );

    controller.CancelCameraLookGesture();

    EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::None );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );
}


void TestCameraLookReleaseAllowsToolGesture()
{
    RuntimeInteractionController controller;
    BeginGesture( controller, MakeCameraLookGesture(), RuntimePointerCaptureOwner::CameraLook );

    const RuntimeGestureEvent endEvent = EndGesture( controller, RuntimeInteractionGestureKind::CameraLook );

    EXPECT_EQ( endEvent.pointerCapture, RuntimePointerCaptureOwner::None );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );

    controller.EnterManipulator();
    const RuntimeGestureEvent beginToolEvent = BeginGesture( controller, MakeMousePickupGesture(),
                                                             RuntimePointerCaptureOwner::ToolGesture,
                                                             InteractionExitReason::EnterManipulator );

    EXPECT_EQ( beginToolEvent.type, RuntimeGestureEventType::Began );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::ToolGesture );
    EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::MousePickupDrag );
}


void TestEndGesturePublishesCleanupMetadata()
{
    RuntimeInteractionController controller;
    controller.EnterManipulator();
    BeginGesture( controller, MakeMousePickupGesture(), RuntimePointerCaptureOwner::ToolGesture,
                  InteractionExitReason::EnterManipulator );

    const RuntimeGestureEvent endEvent = EndGesture( controller, RuntimeInteractionGestureKind::MousePickupDrag );

    EXPECT_EQ( endEvent.type, RuntimeGestureEventType::Ended );
    EXPECT_EQ( endEvent.previousGesture.kind, RuntimeInteractionGestureKind::MousePickupDrag );
    EXPECT_EQ( endEvent.gesture.kind, RuntimeInteractionGestureKind::None );
    EXPECT_EQ( endEvent.previousPointerCapture, RuntimePointerCaptureOwner::ToolGesture );
    EXPECT_EQ( endEvent.pointerCapture, RuntimePointerCaptureOwner::None );
    EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::None );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );
}


void TestWorkspaceTransitionClearsCapturedGesture()
{
    RuntimeInteractionController controller;
    controller.EnterManipulator();
    BeginGesture( controller, MakeMousePickupGesture(), RuntimePointerCaptureOwner::ToolGesture,
                  InteractionExitReason::EnterManipulator );

    const RuntimeInteractionTransition transition = controller.EnterLive();

    EXPECT_TRUE( transition.ownerChanged );
    EXPECT_TRUE( transition.gestureChanged );
    EXPECT_TRUE( transition.pointerCaptureChanged );
    EXPECT_EQ( transition.previousGesture.kind, RuntimeInteractionGestureKind::MousePickupDrag );
    EXPECT_EQ( transition.gesture.kind, RuntimeInteractionGestureKind::None );
    EXPECT_EQ( transition.previousPointerCapture, RuntimePointerCaptureOwner::ToolGesture );
    EXPECT_EQ( transition.pointerCapture, RuntimePointerCaptureOwner::None );
    EXPECT_EQ( controller.Owner(), WorldInteractionOwner::None );
    EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::None );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );
}


void TestCameraModeCommandsMapToInteractionOwners()
{
    struct CameraModeCase
    {
        RunCameraMode mode;
        RuntimeWorkspace workspace;
        WorldInteractionOwner owner;
        InteractionExitReason reason;
    };

    const CameraModeCase cases[] = {
        { RunCameraMode::Demo, RuntimeWorkspace::Live, WorldInteractionOwner::None, InteractionExitReason::EnterLive },
        { RunCameraMode::Scene, RuntimeWorkspace::Live, WorldInteractionOwner::None, InteractionExitReason::EnterLive },
        { RunCameraMode::Inspect, RuntimeWorkspace::Inspect, WorldInteractionOwner::None,
          InteractionExitReason::EnterInspect },
        { RunCameraMode::Attach, RuntimeWorkspace::Inspect, WorldInteractionOwner::None,
          InteractionExitReason::EnterInspect },
        { RunCameraMode::Launcher, RuntimeWorkspace::Live, WorldInteractionOwner::Launcher,
          InteractionExitReason::EnterLauncher },
        { RunCameraMode::Manipulator, RuntimeWorkspace::Live, WorldInteractionOwner::Manipulator,
          InteractionExitReason::EnterManipulator },
        { RunCameraMode::Count, RuntimeWorkspace::Live, WorldInteractionOwner::None, InteractionExitReason::EnterLive },
    };

    for ( const CameraModeCase& modeCase : cases )
    {
        RuntimeInteractionController controller;
        controller.EnterManipulator();
        BeginGesture( controller, MakeMousePickupGesture(), RuntimePointerCaptureOwner::ToolGesture,
                      InteractionExitReason::EnterManipulator );

        const RuntimeInteractionTransition transition = controller.EnterCameraMode( modeCase.mode );

        EXPECT_EQ( transition.workspace, modeCase.workspace );
        EXPECT_EQ( transition.owner, modeCase.owner );
        EXPECT_EQ( transition.reason, modeCase.reason );
        EXPECT_TRUE( transition.gestureChanged );
        EXPECT_TRUE( transition.pointerCaptureChanged );
        EXPECT_EQ( transition.gesture.kind, RuntimeInteractionGestureKind::None );
        EXPECT_EQ( transition.pointerCapture, RuntimePointerCaptureOwner::None );
        EXPECT_EQ( controller.Workspace(), modeCase.workspace );
        EXPECT_EQ( controller.Owner(), modeCase.owner );
    }
}


void TestWorkspaceOwnerTransitionKeepsExactReplayOwner()
{
    RuntimeInteractionController controller;
    controller.EnterInspect();

    const RuntimeInteractionTransition
        transition = controller.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                     WorldInteractionOwner::ReplayCauseTree,
                                                                     InteractionExitReason::EnterReplay );

    EXPECT_TRUE( transition.workspaceChanged );
    EXPECT_TRUE( transition.ownerChanged );
    EXPECT_EQ( transition.previousWorkspace, RuntimeWorkspace::Inspect );
    EXPECT_EQ( transition.workspace, RuntimeWorkspace::Replay );
    EXPECT_EQ( transition.previousOwner, WorldInteractionOwner::None );
    EXPECT_EQ( transition.owner, WorldInteractionOwner::ReplayCauseTree );
    EXPECT_EQ( controller.Workspace(), RuntimeWorkspace::Replay );
    EXPECT_EQ( controller.Owner(), WorldInteractionOwner::ReplayCauseTree );
}


void TestReplayToolGesturesCapturePointer()
{
    struct ReplayGestureCase
    {
        WorldInteractionOwner owner;
        RuntimeInteractionGestureKind kind;
    };

    const ReplayGestureCase cases[] = {
        { WorldInteractionOwner::ReplayScrub, RuntimeInteractionGestureKind::ReplayScrubDrag },
        { WorldInteractionOwner::ReplayVelocityEdit, RuntimeInteractionGestureKind::ReplayVelocityDrag },
        { WorldInteractionOwner::ReplayPrediction, RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag },
        { WorldInteractionOwner::ReplayCauseTree, RuntimeInteractionGestureKind::ReplayCauseTreeDrag },
    };

    for ( const ReplayGestureCase& replayCase : cases )
    {
        RuntimeInteractionController controller;
        controller.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, replayCase.owner,
                                                        InteractionExitReason::EnterReplay );

        const RuntimeGestureEvent beginEvent = BeginGesture( controller, MakeReplayGesture( replayCase.kind ) );

        EXPECT_EQ( beginEvent.type, RuntimeGestureEventType::Began );
        EXPECT_EQ( controller.Workspace(), RuntimeWorkspace::Replay );
        EXPECT_EQ( controller.Owner(), replayCase.owner );
        EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::ToolGesture );
        EXPECT_EQ( controller.Gesture().kind, replayCase.kind );

        RuntimeInteractionFrameInput input = MakeDefaultFrameInput();
        input.rightMouseLookHeld = true;
        input.replayInspectionLookActive = true;

        const RuntimeInteractionFramePolicy policy = controller.BuildFramePolicy( input );

        EXPECT_EQ( policy.pointerCapture, RuntimePointerCaptureOwner::ToolGesture );
        EXPECT_EQ( policy.gesture, replayCase.kind );
        EXPECT_EQ( policy.cameraLook, CameraLookState::Passive );
        EXPECT_FALSE( policy.cameraMouseLookActive );

        const RuntimeGestureEvent endEvent = EndGesture( controller, replayCase.kind );
        EXPECT_EQ( endEvent.type, RuntimeGestureEventType::Ended );
        EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::None );
        EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );
    }
}


void TestReplayGestureSceneResetCancelsCapture()
{
    RuntimeInteractionController controller;
    controller.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayScrub,
                                                    InteractionExitReason::EnterReplay );

    BeginGesture( controller, MakeReplayGesture( RuntimeInteractionGestureKind::ReplayScrubDrag ) );

    const RuntimeInteractionTransition transition = controller.ResetForScene( InteractionExitReason::ResetScene );

    EXPECT_TRUE( transition.workspaceChanged );
    EXPECT_TRUE( transition.ownerChanged );
    EXPECT_TRUE( transition.gestureChanged );
    EXPECT_TRUE( transition.pointerCaptureChanged );
    EXPECT_EQ( transition.previousGesture.kind, RuntimeInteractionGestureKind::ReplayScrubDrag );
    EXPECT_EQ( transition.gesture.kind, RuntimeInteractionGestureKind::None );
    EXPECT_EQ( transition.previousPointerCapture, RuntimePointerCaptureOwner::ToolGesture );
    EXPECT_EQ( transition.pointerCapture, RuntimePointerCaptureOwner::None );
    EXPECT_EQ( controller.Workspace(), RuntimeWorkspace::Live );
    EXPECT_EQ( controller.Owner(), WorldInteractionOwner::None );
}


void TestGizmoDragCapturesPointerForEditorAndInspect()
{
    struct GizmoCase
    {
        RuntimeWorkspace workspace;
        WorldInteractionOwner owner;
        bool angular;
    };

    const GizmoCase cases[] = {
        { RuntimeWorkspace::Edit, WorldInteractionOwner::EditorGizmo, false },
        { RuntimeWorkspace::Inspect, WorldInteractionOwner::InspectGizmo, true },
    };

    for ( const GizmoCase& gizmoCase : cases )
    {
        RuntimeInteractionController controller;
        controller.SetWorldInteractionOwnerInWorkspace( gizmoCase.workspace, gizmoCase.owner,
                                                        InteractionExitReason::EnterEdit );

        const RuntimeGestureEvent beginEvent = BeginGesture( controller, MakeGizmoGesture( gizmoCase.angular ) );

        EXPECT_EQ( beginEvent.type, RuntimeGestureEventType::Began );
        EXPECT_EQ( controller.Workspace(), gizmoCase.workspace );
        EXPECT_EQ( controller.Owner(), gizmoCase.owner );
        EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::ToolGesture );
        EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::GizmoDrag );
        EXPECT_EQ( controller.Gesture().axis, 1 );
        EXPECT_EQ( controller.Gesture().angular, gizmoCase.angular );

        RuntimeInteractionFrameInput input = MakeDefaultFrameInput();
        input.rightMouseLookHeld = true;
        input.editorViewportLookActive = true;

        const RuntimeInteractionFramePolicy policy = controller.BuildFramePolicy( input );

        EXPECT_EQ( policy.pointerCapture, RuntimePointerCaptureOwner::ToolGesture );
        EXPECT_EQ( policy.gesture, RuntimeInteractionGestureKind::GizmoDrag );
        EXPECT_EQ( policy.cameraLook, CameraLookState::Passive );
        EXPECT_FALSE( policy.cameraMouseLookActive );

        const RuntimeGestureEvent endEvent = EndGesture( controller, RuntimeInteractionGestureKind::GizmoDrag );
        EXPECT_EQ( endEvent.type, RuntimeGestureEventType::Ended );
        EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::None );
        EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );
    }
}


void TestInvalidToolGestureWithoutCaptureIsRejected()
{
    RuntimeInteractionController controller;
    controller.EnterManipulator();

    RuntimeGestureCommand command;
    command.gesture = MakeMousePickupGesture();
    command.captureOwner = RuntimePointerCaptureOwner::None;
    command.reason = InteractionExitReason::EnterManipulator;
    RuntimeGestureEvent rejectedEvent;
    EXPECT_FALSE( controller.ApplyGestureCommand( command, rejectedEvent ) );

    EXPECT_EQ( rejectedEvent.type, RuntimeGestureEventType::None );
    EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::None );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );

    RuntimeInteractionFrameInput input = MakeDefaultFrameInput();
    input.rightMouseLookHeld = true;

    const RuntimeInteractionFramePolicy policy = controller.BuildFramePolicy( input );
    EXPECT_EQ( policy.gesture, RuntimeInteractionGestureKind::None );
    EXPECT_EQ( policy.pointerCapture, RuntimePointerCaptureOwner::None );
    EXPECT_EQ( policy.cameraLook, CameraLookState::RightMouseLook );
    EXPECT_TRUE( policy.cameraMouseLookActive );
}


RuntimeUiControl MakeUiControl( uint32_t id, RuntimeUiControlKind kind, float x, float y, float width, float height )
{
    RuntimeUiControl control;
    control.id = RuntimeUiControlId { id };
    control.kind = kind;
    control.action = RuntimeUiActionId { id + 100u };
    control.drawRect = { x, y, width, height };
    control.hitRect = control.drawRect;
    return control;
}


void TestRuntimeUiSurfaceRepresentsEveryControlKind()
{
    constexpr RuntimeUiControlKind kinds[] = { RuntimeUiControlKind::Panel,  RuntimeUiControlKind::HotZone,
                                               RuntimeUiControlKind::Button, RuntimeUiControlKind::Toggle,
                                               RuntimeUiControlKind::Slider, RuntimeUiControlKind::Track,
                                               RuntimeUiControlKind::Tab,    RuntimeUiControlKind::ToolHandle };

    RuntimeUiSurface<8> surface;

    for ( std::size_t index = 0; index < 8; ++index )
    {
        EXPECT_TRUE( surface.TryAdd( MakeUiControl( static_cast<uint32_t>( index + 1 ), kinds[index], 0.0f, 0.0f, 10.0f, 10.0f ) ) );
    }

    EXPECT_EQ( surface.controlCount, std::size_t { 8 } );

    for ( std::size_t index = 0; index < surface.controlCount; ++index )
    {
        EXPECT_EQ( surface.controls[index].kind, kinds[index] );
        EXPECT_TRUE( static_cast<bool>( surface.controls[index].action ) );
    }
}


void TestRuntimeUiSurfaceRejectsCapacityAndIdentityViolations()
{
    RuntimeUiSurface<2> surface;
    EXPECT_FALSE( surface.TryAdd( MakeUiControl( 0u, RuntimeUiControlKind::Button, 0.0f, 0.0f, 5.0f, 5.0f ) ) );
    EXPECT_TRUE( surface.TryAdd( MakeUiControl( 1u, RuntimeUiControlKind::Button, 0.0f, 0.0f, 5.0f, 5.0f ) ) );
    EXPECT_FALSE( surface.TryAdd( MakeUiControl( 1u, RuntimeUiControlKind::Toggle, 5.0f, 0.0f, 5.0f, 5.0f ) ) );
    EXPECT_TRUE( surface.TryAdd( MakeUiControl( 2u, RuntimeUiControlKind::Slider, 10.0f, 0.0f, 5.0f, 5.0f ) ) );
    EXPECT_FALSE( surface.TryAdd( MakeUiControl( 3u, RuntimeUiControlKind::Tab, 15.0f, 0.0f, 5.0f, 5.0f ) ) );
    EXPECT_EQ( surface.controlCount, std::size_t { 2 } );
}


void TestRuntimeUiSurfaceResolvesOneOrderedEligibleHit()
{
    RuntimeUiSurface<4> surface;
    RuntimeUiControl hidden = MakeUiControl( 1u, RuntimeUiControlKind::Button, 0.0f, 0.0f, 20.0f, 20.0f );
    hidden.visible = false;
    RuntimeUiControl disabled = MakeUiControl( 2u, RuntimeUiControlKind::Toggle, 0.0f, 0.0f, 20.0f, 20.0f );
    disabled.enabled = false;
    disabled.visible = false;
    EXPECT_TRUE( surface.TryAdd( hidden ) );
    EXPECT_TRUE( surface.TryAdd( disabled ) );
    EXPECT_TRUE( surface.TryAdd( MakeUiControl( 3u, RuntimeUiControlKind::Slider, 0.0f, 0.0f, 20.0f, 20.0f ) ) );
    EXPECT_TRUE( surface.TryAdd( MakeUiControl( 4u, RuntimeUiControlKind::Panel, 0.0f, 0.0f, 20.0f, 20.0f ) ) );

    surface.ResolvePointer( 10, 10 );

    EXPECT_TRUE( surface.hasHotControl );
    EXPECT_TRUE( surface.hasPointerControl );
    EXPECT_TRUE( surface.consumesPointer );
    EXPECT_EQ( surface.hotControl, RuntimeUiControlId { 3u } );
    EXPECT_EQ( surface.pointerControl, RuntimeUiControlId { 3u } );
    EXPECT_FALSE( surface.controls[0].hovered );
    EXPECT_FALSE( surface.controls[1].hovered );
    EXPECT_TRUE( surface.controls[2].hovered );
    EXPECT_FALSE( surface.controls[3].hovered );

    surface.ResolvePointer( 30, 30 );
    EXPECT_FALSE( surface.hasHotControl );
    EXPECT_FALSE( surface.hasPointerControl );
    EXPECT_FALSE( surface.consumesPointer );
    EXPECT_FALSE( surface.controls[2].hovered );
}


void TestRuntimeUiSurfaceDisabledControlPreventsClickThrough()
{
    RuntimeUiSurface<2> surface;
    RuntimeUiControl disabled = MakeUiControl( 1u, RuntimeUiControlKind::Button, 0.0f, 0.0f, 20.0f, 20.0f );
    disabled.enabled = false;
    EXPECT_TRUE( surface.TryAdd( disabled ) );
    EXPECT_TRUE( surface.TryAdd( MakeUiControl( 2u, RuntimeUiControlKind::Panel, 0.0f, 0.0f, 20.0f, 20.0f ) ) );

    surface.ResolvePointer( 10, 10 );

    EXPECT_TRUE( surface.hasPointerControl );
    EXPECT_EQ( surface.pointerControl, RuntimeUiControlId { 1u } );
    EXPECT_FALSE( surface.hasHotControl );
    EXPECT_TRUE( surface.consumesPointer );
    EXPECT_FALSE( surface.controls[0].hovered );
    EXPECT_FALSE( surface.controls[1].hovered );
}


void TestRuntimeUiSurfaceBlockedPointerClearsHover()
{
    RuntimeUiSurface<1> surface;
    EXPECT_TRUE( surface.TryAdd( MakeUiControl( 1u, RuntimeUiControlKind::Button, 0.0f, 0.0f, 20.0f, 20.0f ) ) );
    surface.ResolvePointer( 10, 10 );
    EXPECT_TRUE( surface.controls[0].hovered );

    surface.ResolvePointer( 10, 10, true );

    EXPECT_FALSE( surface.hasHotControl );
    EXPECT_FALSE( surface.hasPointerControl );
    EXPECT_FALSE( surface.consumesPointer );
    EXPECT_FALSE( surface.controls[0].hovered );
}


void TestRuntimeUiSurfacePublishesHitStateWithDrawGeometry()
{
    RuntimeUiSurface<1> surface;
    EXPECT_TRUE( surface.TryAdd( MakeUiControl( 9u, RuntimeUiControlKind::Track, 12.0f, 18.0f, 80.0f, 10.0f ) ) );

    surface.ResolvePointer( 40, 22 );

    const RuntimeUiControl* renderRow = surface.Find( surface.hotControl );
    EXPECT_TRUE( renderRow != nullptr );
    EXPECT_TRUE( renderRow->hovered );
    EXPECT_NEAR( renderRow->drawRect.x, 12.0f, 0.0001f );
    EXPECT_NEAR( renderRow->drawRect.y, 18.0f, 0.0001f );
    EXPECT_NEAR( renderRow->drawRect.w, 80.0f, 0.0001f );
    EXPECT_NEAR( renderRow->drawRect.h, 10.0f, 0.0001f );
    EXPECT_NEAR( renderRow->hitRect.x, renderRow->drawRect.x, 0.0001f );
    EXPECT_NEAR( renderRow->hitRect.y, renderRow->drawRect.y, 0.0001f );
    EXPECT_NEAR( renderRow->hitRect.w, renderRow->drawRect.w, 0.0001f );
    EXPECT_NEAR( renderRow->hitRect.h, renderRow->drawRect.h, 0.0001f );
}


void TestRuntimeUiSurfaceResetClearsDisposableFrameState()
{
    RuntimeUiSurface<1> surface;
    EXPECT_TRUE( surface.TryAdd( MakeUiControl( 8u, RuntimeUiControlKind::HotZone, 0.0f, 0.0f, 5.0f, 5.0f ) ) );
    surface.ResolvePointer( 2, 2 );
    surface.activeControl = RuntimeUiControlId { 8u };
    surface.hasActiveControl = true;

    surface.Reset();

    EXPECT_EQ( surface.controlCount, std::size_t { 0 } );
    EXPECT_FALSE( surface.hasHotControl );
    EXPECT_FALSE( surface.hasPointerControl );
    EXPECT_FALSE( surface.hasActiveControl );
    EXPECT_FALSE( surface.consumesPointer );
    EXPECT_FALSE( static_cast<bool>( surface.hotControl ) );
    EXPECT_FALSE( static_cast<bool>( surface.pointerControl ) );
    EXPECT_FALSE( static_cast<bool>( surface.activeControl ) );
}


void RunTest( const TestCase& test )
{
    std::cout << "[ RUN      ] " << test.name << "\n";
    test.run();
    std::cout << "[       OK ] " << test.name << "\n";
}
} // namespace


int main()
{
    const TestCase tests[] = {
        { "MousePickupDragRunsPhysicsWithoutStepHold", &TestMousePickupDragRunsPhysicsWithoutStepHold },
        { "ToolGestureSuppressesCameraLook", &TestToolGestureSuppressesCameraLook },
        { "GestureCommandsEmitOnlyAfterSuccessfulMutation", &TestGestureCommandsEmitOnlyAfterSuccessfulMutation },
        { "EditorPlacementScaleUsesTypedCapture", &TestEditorPlacementScaleUsesTypedCapture },
        { "FocusLossCancelsCameraLookGesture", &TestFocusLossCancelsCameraLookGesture },
        { "CameraLookGestureCapturesPointer", &TestCameraLookGestureCapturesPointer },
        { "CameraLookGestureEndsCleanly", &TestCameraLookGestureEndsCleanly },
        { "CameraLookReleaseAllowsToolGesture", &TestCameraLookReleaseAllowsToolGesture },
        { "EndGesturePublishesCleanupMetadata", &TestEndGesturePublishesCleanupMetadata },
        { "WorkspaceTransitionClearsCapturedGesture", &TestWorkspaceTransitionClearsCapturedGesture },
        { "CameraModeCommandsMapToInteractionOwners", &TestCameraModeCommandsMapToInteractionOwners },
        { "WorkspaceOwnerTransitionKeepsExactReplayOwner", &TestWorkspaceOwnerTransitionKeepsExactReplayOwner },
        { "ReplayToolGesturesCapturePointer", &TestReplayToolGesturesCapturePointer },
        { "ReplayGestureSceneResetCancelsCapture", &TestReplayGestureSceneResetCancelsCapture },
        { "GizmoDragCapturesPointerForEditorAndInspect", &TestGizmoDragCapturesPointerForEditorAndInspect },
        { "ExactBoxPickRejectsOldBoundingSphereEnvelope", &TestExactBoxPickRejectsOldBoundingSphereEnvelope },
        { "TreeTrunkHullPickUsesConvexFaces", &TestTreeTrunkHullPickUsesConvexFaces },
        { "RotatedShapePickReturnsNearestEntry", &TestRotatedShapePickReturnsNearestEntry },
        { "InvalidToolGestureWithoutCaptureIsRejected", &TestInvalidToolGestureWithoutCaptureIsRejected },
        { "RuntimeUiSurfaceRepresentsEveryControlKind", &TestRuntimeUiSurfaceRepresentsEveryControlKind },
        { "RuntimeUiSurfaceRejectsCapacityAndIdentityViolations",
          &TestRuntimeUiSurfaceRejectsCapacityAndIdentityViolations },
        { "RuntimeUiSurfaceResolvesOneOrderedEligibleHit", &TestRuntimeUiSurfaceResolvesOneOrderedEligibleHit },
        { "RuntimeUiSurfaceDisabledControlPreventsClickThrough", &TestRuntimeUiSurfaceDisabledControlPreventsClickThrough },
        { "RuntimeUiSurfaceBlockedPointerClearsHover", &TestRuntimeUiSurfaceBlockedPointerClearsHover },
        { "RuntimeUiSurfacePublishesHitStateWithDrawGeometry", &TestRuntimeUiSurfacePublishesHitStateWithDrawGeometry },
        { "RuntimeUiSurfaceResetClearsDisposableFrameState", &TestRuntimeUiSurfaceResetClearsDisposableFrameState },
    };

    try
    {

        for ( const TestCase& test : tests )
        {
            RunTest( test );
        }
    }
    catch ( const std::exception& exception )
    {
        std::cerr << exception.what() << "\n";

        return 1;
    }

    std::cout << "PASS: runtime interaction policy tests passed.\n";
    return 0;
}
