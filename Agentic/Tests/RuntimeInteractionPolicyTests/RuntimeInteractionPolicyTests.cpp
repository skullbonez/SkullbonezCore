/*
File: Agentic/Tests/RuntimeInteractionPolicyTests/RuntimeInteractionPolicyTests.cpp
Purpose:
  Verifies CPU-side runtime interaction and picker rules that should not require
  a renderer launch.

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

Invariants:
  Tests exercise RuntimeInteractionController policy without editor, replay,
  renderer, or physics launches.
  Gesture, owner, pointer capture, camera-look, and physics-advance transitions
  must remain mutually consistent.

Related:
  - AGENTS.md
  - Agentic/Plans/TODO/interaction-state-machine.md
  - SkullbonezSource/Runtime/RuntimeInteractionController.h
  - SkullbonezSource/Runtime/RuntimePickGeometry.h
*/
#include "Runtime/RuntimeInteractionController.h"
#include "Runtime/RuntimePickGeometry.h"

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Vector;

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


void ExpectFloatNear( float actual,
                      float expected,
                      float tolerance,
                      const char* actualExpression,
                      const char* expectedExpression,
                      const char* file,
                      int line )
{
    if ( fabsf( actual - expected ) > tolerance )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " near " << expectedExpression << ", actual " << actual
            << ", expected " << expected << ", tolerance " << tolerance;
        Fail( file, line, out.str() );
    }
}


template <typename T, typename U>
void ExpectEqualImpl( const T& actual,
                      const U& expected,
                      const char* actualExpression,
                      const char* expectedExpression,
                      const char* file,
                      int line )
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
#define EXPECT_EQ( actual, expected )                                                                                  \
    ExpectEqualImpl( ( actual ), ( expected ), #actual, #expected, __FILE__, __LINE__ )
#define EXPECT_NEAR( actual, expected, tolerance )                                                                     \
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
    gesture.modelIndex = 7;
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
    return gesture;
}


RuntimeInteractionGesture MakeGizmoGesture( bool angular )
{
    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::GizmoDrag;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = 64;
    gesture.startY = 96;
    gesture.modelIndex = 5;
    gesture.axis = 1;
    gesture.angular = angular;
    return gesture;
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

    EXPECT_FALSE( TryIntersectRuntimePickShape( shape,
                                                transform,
                                                Vector3( 4.0f, 0.0f, -20.0f ),
                                                Vector3( 0.0f, 0.0f, 1.0f ),
                                                rayT ) );

    EXPECT_TRUE( TryIntersectRuntimePickShape( shape,
                                               transform,
                                               Vector3( 0.0f, 0.0f, -20.0f ),
                                               Vector3( 0.0f, 0.0f, 1.0f ),
                                               rayT ) );
    EXPECT_NEAR( rayT, 19.0f, 0.001f );
}


void TestTreeTrunkHullPickUsesConvexFaces()
{
    const ConvexHullShape trunk = ConvexHullShape::LoadFromFile( "SkullbonezData/hulls/tree_trunk_faceted.hull" );
    const CollisionShape shape = trunk;
    const RuntimePickShapeTransform transform = MakePickTransform();
    float rayT = 0.0f;

    EXPECT_TRUE( TryIntersectRuntimePickShape( shape,
                                               transform,
                                               Vector3( 0.0f, 0.0f, -20.0f ),
                                               Vector3( 0.0f, 0.0f, 1.0f ),
                                               rayT ) );
    EXPECT_NEAR( rayT, 17.58f, 0.01f );

    EXPECT_FALSE( TryIntersectRuntimePickShape( shape,
                                                transform,
                                                Vector3( 4.0f, 0.0f, -20.0f ),
                                                Vector3( 0.0f, 0.0f, 1.0f ),
                                                rayT ) );
}


void TestRotatedShapePickReturnsNearestEntry()
{
    const RuntimePickShapeTransform rotated = MakePickTransform( ZERO_VECTOR, MakeYawQuarterTurn() );
    float rayT = 0.0f;

    const CollisionShape box = BoundingBox( Vector3( 1.0f, 2.0f, 3.0f ), ZERO_VECTOR );
    EXPECT_TRUE( TryIntersectRuntimePickShape( box,
                                               rotated,
                                               Vector3( 0.0f, 0.0f, -10.0f ),
                                               Vector3( 0.0f, 0.0f, 1.0f ),
                                               rayT ) );
    EXPECT_NEAR( rayT, 9.0f, 0.001f );

    const CollisionShape trunk = ConvexHullShape::LoadFromFile( "SkullbonezData/hulls/tree_trunk_faceted.hull" );
    EXPECT_TRUE( TryIntersectRuntimePickShape( trunk,
                                               rotated,
                                               Vector3( 0.0f, 0.0f, -20.0f ),
                                               Vector3( 0.0f, 0.0f, 1.0f ),
                                               rayT ) );
    EXPECT_NEAR( rayT, 17.2f, 0.02f );
}


void TestMousePickupDragRunsPhysicsWithoutStepHold()
{
    RuntimeInteractionController controller;
    const RuntimeInteractionTransition transition = controller.EnterManipulator();

    EXPECT_TRUE( transition.ownerChanged );
    EXPECT_EQ( controller.Workspace(), RuntimeWorkspace::Live );
    EXPECT_EQ( controller.Owner(), WorldInteractionOwner::Manipulator );

    const RuntimeInteractionTransition beginTransition =
        controller.BeginGesture( MakeMousePickupGesture(),
                                 RuntimePointerCaptureOwner::ToolGesture,
                                 InteractionExitReason::EnterManipulator );
    EXPECT_TRUE( beginTransition.gestureChanged );
    EXPECT_TRUE( beginTransition.pointerCaptureChanged );

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

    const RuntimeInteractionTransition beginTransition =
        controller.BeginGesture( MakeMousePickupGesture(),
                                 RuntimePointerCaptureOwner::ToolGesture,
                                 InteractionExitReason::EnterManipulator );

    EXPECT_TRUE( beginTransition.gestureChanged );
    EXPECT_TRUE( beginTransition.pointerCaptureChanged );
    EXPECT_EQ( beginTransition.previousPointerCapture, RuntimePointerCaptureOwner::None );
    EXPECT_EQ( beginTransition.pointerCapture, RuntimePointerCaptureOwner::ToolGesture );
    EXPECT_EQ( beginTransition.gesture.kind, RuntimeInteractionGestureKind::MousePickupDrag );
    EXPECT_EQ( beginTransition.gesture.modelIndex, 7 );

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
    controller.BeginGesture( MakeCameraLookGesture(),
                             RuntimePointerCaptureOwner::CameraLook,
                             InteractionExitReason::BeginGesture );

    controller.CancelCameraLookGesture();

    EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::None );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );
}


void TestCameraLookReleaseAllowsToolGesture()
{
    RuntimeInteractionController controller;
    controller.BeginGesture( MakeCameraLookGesture(),
                             RuntimePointerCaptureOwner::CameraLook,
                             InteractionExitReason::BeginGesture );

    const RuntimeInteractionTransition endTransition = controller.EndGesture( InteractionExitReason::EndGesture );

    EXPECT_EQ( endTransition.pointerCapture, RuntimePointerCaptureOwner::None );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );

    controller.EnterManipulator();
    const RuntimeInteractionTransition beginToolTransition =
        controller.BeginGesture( MakeMousePickupGesture(),
                                 RuntimePointerCaptureOwner::ToolGesture,
                                 InteractionExitReason::EnterManipulator );

    EXPECT_TRUE( beginToolTransition.gestureChanged );
    EXPECT_TRUE( beginToolTransition.pointerCaptureChanged );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::ToolGesture );
    EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::MousePickupDrag );
}


void TestEndGesturePublishesCleanupMetadata()
{
    RuntimeInteractionController controller;
    controller.EnterManipulator();
    controller.BeginGesture( MakeMousePickupGesture(),
                             RuntimePointerCaptureOwner::ToolGesture,
                             InteractionExitReason::EnterManipulator );

    const RuntimeInteractionTransition endTransition = controller.EndGesture( InteractionExitReason::EndGesture );

    EXPECT_TRUE( endTransition.gestureChanged );
    EXPECT_TRUE( endTransition.pointerCaptureChanged );
    EXPECT_EQ( endTransition.previousGesture.kind, RuntimeInteractionGestureKind::MousePickupDrag );
    EXPECT_EQ( endTransition.gesture.kind, RuntimeInteractionGestureKind::None );
    EXPECT_EQ( endTransition.previousPointerCapture, RuntimePointerCaptureOwner::ToolGesture );
    EXPECT_EQ( endTransition.pointerCapture, RuntimePointerCaptureOwner::None );
    EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::None );
    EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );
}


void TestWorkspaceTransitionClearsCapturedGesture()
{
    RuntimeInteractionController controller;
    controller.EnterManipulator();
    controller.BeginGesture( MakeMousePickupGesture(),
                             RuntimePointerCaptureOwner::ToolGesture,
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
        { RunCameraMode::Inspect,
          RuntimeWorkspace::Inspect,
          WorldInteractionOwner::None,
          InteractionExitReason::EnterInspect },
        { RunCameraMode::Attach,
          RuntimeWorkspace::Inspect,
          WorldInteractionOwner::None,
          InteractionExitReason::EnterInspect },
        { RunCameraMode::Launcher,
          RuntimeWorkspace::Live,
          WorldInteractionOwner::Launcher,
          InteractionExitReason::EnterLauncher },
        { RunCameraMode::Manipulator,
          RuntimeWorkspace::Live,
          WorldInteractionOwner::Manipulator,
          InteractionExitReason::EnterManipulator },
        { RunCameraMode::Count, RuntimeWorkspace::Live, WorldInteractionOwner::None, InteractionExitReason::EnterLive },
    };

    for ( const CameraModeCase& modeCase : cases )
    {
        RuntimeInteractionController controller;
        controller.EnterManipulator();
        controller.BeginGesture( MakeMousePickupGesture(),
                                 RuntimePointerCaptureOwner::ToolGesture,
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

    const RuntimeInteractionTransition transition =
        controller.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
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
        controller.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                        replayCase.owner,
                                                        InteractionExitReason::EnterReplay );

        const RuntimeInteractionTransition beginTransition =
            controller.BeginGesture( MakeReplayGesture( replayCase.kind ),
                                     RuntimePointerCaptureOwner::ToolGesture,
                                     InteractionExitReason::BeginGesture );

        EXPECT_TRUE( beginTransition.gestureChanged );
        EXPECT_TRUE( beginTransition.pointerCaptureChanged );
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

        const RuntimeInteractionTransition endTransition = controller.EndGesture( InteractionExitReason::EndGesture );
        EXPECT_TRUE( endTransition.gestureChanged );
        EXPECT_TRUE( endTransition.pointerCaptureChanged );
        EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::None );
        EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );
    }
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
        controller.SetWorldInteractionOwnerInWorkspace( gizmoCase.workspace,
                                                        gizmoCase.owner,
                                                        InteractionExitReason::EnterEdit );

        const RuntimeInteractionTransition beginTransition =
            controller.BeginGesture( MakeGizmoGesture( gizmoCase.angular ),
                                     RuntimePointerCaptureOwner::ToolGesture,
                                     InteractionExitReason::BeginGesture );

        EXPECT_TRUE( beginTransition.gestureChanged );
        EXPECT_TRUE( beginTransition.pointerCaptureChanged );
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

        const RuntimeInteractionTransition endTransition = controller.EndGesture( InteractionExitReason::EndGesture );
        EXPECT_TRUE( endTransition.gestureChanged );
        EXPECT_TRUE( endTransition.pointerCaptureChanged );
        EXPECT_EQ( controller.Gesture().kind, RuntimeInteractionGestureKind::None );
        EXPECT_EQ( controller.PointerCapture(), RuntimePointerCaptureOwner::None );
    }
}


#ifndef _DEBUG
void TestInvalidToolGestureWithoutCaptureIsRejected()
{
    RuntimeInteractionController controller;
    controller.EnterManipulator();

    const RuntimeInteractionTransition rejectedTransition =
        controller.BeginGesture( MakeMousePickupGesture(),
                                 RuntimePointerCaptureOwner::None,
                                 InteractionExitReason::EnterManipulator );

    EXPECT_FALSE( rejectedTransition.gestureChanged );
    EXPECT_FALSE( rejectedTransition.pointerCaptureChanged );
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
#endif


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
        { "CameraLookGestureCapturesPointer", &TestCameraLookGestureCapturesPointer },
        { "CameraLookGestureEndsCleanly", &TestCameraLookGestureEndsCleanly },
        { "CameraLookReleaseAllowsToolGesture", &TestCameraLookReleaseAllowsToolGesture },
        { "EndGesturePublishesCleanupMetadata", &TestEndGesturePublishesCleanupMetadata },
        { "WorkspaceTransitionClearsCapturedGesture", &TestWorkspaceTransitionClearsCapturedGesture },
        { "CameraModeCommandsMapToInteractionOwners", &TestCameraModeCommandsMapToInteractionOwners },
        { "WorkspaceOwnerTransitionKeepsExactReplayOwner", &TestWorkspaceOwnerTransitionKeepsExactReplayOwner },
        { "ReplayToolGesturesCapturePointer", &TestReplayToolGesturesCapturePointer },
        { "GizmoDragCapturesPointerForEditorAndInspect", &TestGizmoDragCapturesPointerForEditorAndInspect },
        { "ExactBoxPickRejectsOldBoundingSphereEnvelope", &TestExactBoxPickRejectsOldBoundingSphereEnvelope },
        { "TreeTrunkHullPickUsesConvexFaces", &TestTreeTrunkHullPickUsesConvexFaces },
        { "RotatedShapePickReturnsNearestEntry", &TestRotatedShapePickReturnsNearestEntry },
#ifndef _DEBUG
        { "InvalidToolGestureWithoutCaptureIsRejected", &TestInvalidToolGestureWithoutCaptureIsRejected },
#endif
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
