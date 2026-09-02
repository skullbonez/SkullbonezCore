//
// File: SkullbonezTests/TestCamera.cpp
// Purpose:
//   Locks caller-reachable camera basis fallbacks at the CameraCollection boundary.
//
// Summary:
//   Authored and replay-restored poses can temporarily contain coincident
//   eye/target points or a missing up basis. Camera owns deterministic movement
//   and tween fallbacks so those transient poses never publish NaNs. Published
//   causal progress tests pin spherical direction travel, independent look
//   distance, antipodal stability, and exact endpoint preservation.
//   Scene-slot and camera-transition tests pin causal detail registration,
//   cycling exclusion, visible-pose orbit seeding, and return behavior.
//
// Glossary:
//   Movement buffer: Camera-local translation staged before bounds are applied.
//   Render pose: Eye/view/up triple selected after tween interpolation.
//
// Invariants:
//   - Coincident eye and target move along conventional world -Z.
//   - Parallel view/up axes use world +X as the right basis.
//   - Missing or cancelling up vectors resolve according to their documented owner.
//   - A null terrain binding is valid and leaves space-scene tweens unconstrained.
//   - External progress is consumed once and cannot introduce a recursive
//     frame-rate-dependent tween.
//
// Related:
//   - SkullbonezSource/Runtime/Camera/Camera.cpp
//   - SkullbonezSource/Runtime/Camera/CameraCollection.cpp
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Assets/AssetSystem.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Maths/RotationMatrix.h"
#include "../SkullbonezSource/Runtime/Camera/AttachedCameraController.InspectionPolicy.h"
#include "../SkullbonezSource/Runtime/Camera/CameraCollection.h"
#include "../SkullbonezSource/Runtime/Camera/CameraControlState.h"
#include "../SkullbonezSource/Runtime/Input/InputController.h"
#include "../SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.CameraSlots.h"

#include <algorithm>
#include <cmath>
#include <limits>

using SkullbonezCore::Assets::AssetSystem;
using SkullbonezCore::Core::EngineConfig;
using SkullbonezCore::Environment::Camera;
using SkullbonezCore::Environment::CameraCollection;
using SkullbonezCore::Environment::CameraMovementSettings;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Math::Vector::Vector3;
using namespace SkullbonezCore::Runtime;

TEST_CASE( "Camera: movement configuration is accepted atomically by the camera owner" )
{
    EngineConfig config;
    config.camera.mouseSensitivity = 0.35f;
    config.camera.keySpeed = 240.0f;
    config.camera.minCameraHeight = 2.0f;
    config.camera.maxCameraHeight = 125.0f;

    CameraControlState camera;
    REQUIRE( camera.ConfigureMovement( config.camera ) );
    const float acceptedRadiansPerPixel = camera.mouseRadiansPerPixel;
    CHECK( acceptedRadiansPerPixel == doctest::Approx( ( 1.0f / 60.0f ) * 0.35f ) );

    config.camera.maxCameraHeight = 1.0f;
    CHECK_FALSE( camera.ConfigureMovement( config.camera ) );
    CHECK( camera.mouseRadiansPerPixel == acceptedRadiansPerPixel );

    config.camera.maxCameraHeight = 125.0f;
    config.camera.keySpeed = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE( camera.ConfigureMovement( config.camera ) );
    CHECK( camera.mouseRadiansPerPixel == acceptedRadiansPerPixel );
}

TEST_CASE( "Camera: generated demo cycling follows the supplied frame delta" )
{
    CameraControlState camera;
    camera.selectedCamera = 1;
    camera.AdvanceDemoCameraCycleClock( 2.75f, true );
    CHECK( camera.selectedCamera == 1 );
    CHECK( camera.cameraTime == doctest::Approx( 2.75f ) );

    camera.AdvanceDemoCameraCycleClock( 2.26f, true );
    CHECK( camera.selectedCamera == 2 );
    CHECK( camera.cameraTime == doctest::Approx( 0.0f ) );

    camera.AdvanceDemoCameraCycleClock( 1.0f, false );
    CHECK( camera.selectedCamera == 2 );
    CHECK( camera.cameraTime == doctest::Approx( 0.0f ) );
}

TEST_CASE( "Camera: authored zero up remains the SetAll sentinel" )
{
    CameraCollection cameras;
    cameras.AddCamera( Vector3( 1.0f, 2.0f, 3.0f ), Vector3( 1.0f, 2.0f, 2.0f ), SkullbonezCore::Math::Vector::ZERO_VECTOR,
                       0xCA01u );
    CHECK( cameras.GetCameraUp() == SkullbonezCore::Math::Vector::ZERO_VECTOR );
}
TEST_CASE( "Camera: rounded pole dot remains finite and engages the pitch cap" )
{
    CameraCollection cameras;
    const Vector3 pole( -398.8823547363281f, -559.8487548828125f, -648.941162109375f );
    CameraMovementSettings settings;
    settings.cameraCollisionThreshold = 0.01f;
    cameras.ApplyMovementSettings( settings );
    cameras.AddCamera( SkullbonezCore::Math::Vector::ZERO_VECTOR, -pole, pole, 0xCA07u );

    // Hazard: after independent float normalization, this pole's self-dot is
    // 1.000000119f. Without ClampUnit, acosf returns NaN and the cap returns
    // the raw 0.25-radian request.
    cameras.RotatePrimary( 0.0f, 0.25f );

    const Vector3& view = cameras.GetCameraView();
    CHECK( std::isfinite( view.x ) );
    CHECK( std::isfinite( view.y ) );
    CHECK( std::isfinite( view.z ) );

    Vector3 cappedPole = -view;
    Vector3 normalizedUp = pole;
    REQUIRE( cappedPole.TryNormalise() );
    REQUIRE( normalizedUp.TryNormalise() );
    CHECK( SkullbonezCore::Math::Vector::Dot( cappedPole, normalizedUp ) > 0.999f );
}

TEST_CASE( "Camera: zero authored up uses the deterministic world basis for pitch caps" )
{
    CameraCollection cameras;
    CameraMovementSettings settings;
    settings.cameraCollisionThreshold = 0.01f;
    cameras.ApplyMovementSettings( settings );
    cameras.AddCamera( SkullbonezCore::Math::Vector::ZERO_VECTOR, Vector3( 0.0f, -1.0f, 0.0f ),
                       SkullbonezCore::Math::Vector::ZERO_VECTOR, 0xCA08u );

    cameras.RotatePrimary( 0.0f, 0.25f );

    const Vector3& view = cameras.GetCameraView();
    CHECK( std::isfinite( view.x ) );
    CHECK( std::isfinite( view.y ) );
    CHECK( std::isfinite( view.z ) );

    Vector3 cappedPole = -view;
    REQUIRE( cappedPole.TryNormalise() );
    CHECK( cappedPole.y > 0.999f );
}


TEST_CASE( "Camera input: right and up mouse motion turns free and launcher cameras right and up" )
{
    CameraMovementSettings settings;
    const float yaw = InputController::ResolveMouseLookRadians( 10, 0.01f );
    const float pitch = InputController::ResolveMouseLookRadians( -10, 0.01f );
    REQUIRE( yaw < 0.0f );
    REQUIRE( pitch > 0.0f );

    CameraCollection freeCameras;
    freeCameras.ApplyMovementSettings( settings );
    freeCameras.AddCamera( SkullbonezCore::Math::Vector::ZERO_VECTOR, Vector3( 0.0f, 0.0f, -1.0f ),
                           Vector3( 0.0f, 1.0f, 0.0f ), 0xCA24u );
    freeCameras.RotatePrimary( yaw, pitch );
    CHECK( freeCameras.GetCameraView().x > 0.0f );
    CHECK( freeCameras.GetCameraView().y > 0.0f );

    CameraCollection launcherCameras;
    launcherCameras.ApplyMovementSettings( settings );
    launcherCameras.AddCamera( Vector3( 0.0f, 0.0f, 10.0f ), SkullbonezCore::Math::Vector::ZERO_VECTOR,
                               Vector3( 0.0f, 1.0f, 0.0f ), 0xCA25u );
    launcherCameras.SetLockedMode( true );
    launcherCameras.RotatePrimary( yaw, pitch );
    launcherCameras.ApplyPrimaryMovementBuffer();
    const Vector3 launcherViewDirection = launcherCameras.GetCameraView() - launcherCameras.GetCameraTranslation();
    CHECK( launcherViewDirection.x > 0.0f );
    CHECK( launcherViewDirection.y > 0.0f );
}

TEST_CASE( "Camera: locked dolly clamps the requested endpoint to orbit limits" )
{
    CameraCollection cameras;
    CameraMovementSettings settings;
    settings.minViewMag = 2.0f;
    settings.maxViewMag = 12.0f;
    cameras.ApplyMovementSettings( settings );
    cameras.AddCamera( Vector3( 0.0f, 0.0f, 10.0f ), SkullbonezCore::Math::Vector::ZERO_VECTOR, Vector3( 0.0f, 1.0f, 0.0f ),
                       0xCA09u );
    cameras.SetLockedMode( true );

    cameras.MovePrimary( Camera::TravelDirection::Forward, 5.0f );
    cameras.MovePrimary( Camera::TravelDirection::Forward, 5.0f );
    cameras.ApplyPrimaryMovementBuffer();
    CHECK( cameras.GetCameraTranslation().z == doctest::Approx( 2.0f ) );
    CHECK( cameras.GetCameraView() == SkullbonezCore::Math::Vector::ZERO_VECTOR );

    cameras.MovePrimary( Camera::TravelDirection::Forward, -20.0f );
    cameras.ApplyPrimaryMovementBuffer();
    CHECK( cameras.GetCameraTranslation().z == doctest::Approx( 12.0f ) );
    CHECK( cameras.GetCameraView() == SkullbonezCore::Math::Vector::ZERO_VECTOR );

    CameraCollection recoveryCameras;
    recoveryCameras.ApplyMovementSettings( settings );
    recoveryCameras.AddCamera( Vector3( 0.0f, 0.0f, 1.0f ), SkullbonezCore::Math::Vector::ZERO_VECTOR,
                               Vector3( 0.0f, 1.0f, 0.0f ), 0xCA0Bu );
    recoveryCameras.SetLockedMode( true );
    recoveryCameras.MovePrimary( Camera::TravelDirection::Forward, 0.0f );
    recoveryCameras.ApplyPrimaryMovementBuffer();
    CHECK( recoveryCameras.GetCameraTranslation().z == doctest::Approx( 2.0f ) );
}

TEST_CASE( "Camera: locked yaw and pitch compose without moving the orbit target" )
{
    CameraCollection cameras;
    CameraMovementSettings settings;
    cameras.ApplyMovementSettings( settings );
    const Vector3 eye( 0.0f, 0.0f, 10.0f );
    const Vector3 target = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    const Vector3 up( 0.0f, 1.0f, 0.0f );
    cameras.AddCamera( eye, target, up, 0xCA0Au );
    cameras.SetLockedMode( true );

    const float yaw = 0.4f;
    const float pitch = 0.3f;
    Vector3 expectedEye = SkullbonezCore::Math::Transformation::RotatePointAboutArbitrary( yaw, up, eye );
    Vector3 expectedViewDirection = -expectedEye;
    REQUIRE( expectedViewDirection.TryNormalise() );
    Vector3 expectedRight = SkullbonezCore::Math::Vector::CrossProduct( expectedViewDirection, up );
    REQUIRE( expectedRight.TryNormalise() );
    expectedEye = SkullbonezCore::Math::Transformation::RotatePointAboutArbitrary( pitch, expectedRight, expectedEye );
    Vector3 expectedDollyDirection = -expectedEye;
    REQUIRE( expectedDollyDirection.TryNormalise() );
    expectedEye += expectedDollyDirection * 3.0f;

    cameras.RotatePrimary( yaw, pitch );
    cameras.MovePrimary( Camera::TravelDirection::Forward, 3.0f );
    cameras.ApplyPrimaryMovementBuffer();

    const Vector3 actualEye = cameras.GetCameraTranslation();
    CHECK( actualEye.x == doctest::Approx( expectedEye.x ) );
    CHECK( actualEye.y == doctest::Approx( expectedEye.y ) );
    CHECK( actualEye.z == doctest::Approx( expectedEye.z ) );
    CHECK( actualEye.x > 0.0f );
    CHECK( actualEye.y < 0.0f );
    CHECK( cameras.GetCameraView() == target );
    CHECK( SkullbonezCore::Math::Vector::Distance( actualEye, target ) == doctest::Approx( 7.0f ) );

    CameraCollection zeroUpCameras;
    zeroUpCameras.ApplyMovementSettings( settings );
    zeroUpCameras.AddCamera( eye, target, SkullbonezCore::Math::Vector::ZERO_VECTOR, 0xCA0Cu );
    zeroUpCameras.SetLockedMode( true );
    zeroUpCameras.RotatePrimary( yaw, pitch );
    zeroUpCameras.ApplyPrimaryMovementBuffer();
    CHECK( zeroUpCameras.GetCameraView() == target );
    CHECK( SkullbonezCore::Math::Vector::Distance( zeroUpCameras.GetCameraTranslation(), target ) ==
           doctest::Approx( 10.0f ) );
}

TEST_CASE( "Camera: repeated locked rotation recovery never rewrites the target" )
{
    CameraCollection cameras;
    CameraMovementSettings settings;
    cameras.ApplyMovementSettings( settings );
    const Vector3 eye( 3.0f, 4.0f, 12.0f );
    const Vector3 target( 1.0f, -2.0f, 0.0f );
    const float retainedDistance = SkullbonezCore::Math::Vector::Distance( eye, target );
    cameras.AddCamera( eye, target, Vector3( 0.0f, 1.0f, 0.0f ), 0xCA0Du );
    cameras.SetLockedMode( true );

    for ( int turn = 0; turn < 128; ++turn )
    {
        const float pitch = ( turn & 1 ) ? 0.071f : -0.053f;
        cameras.RotatePrimary( 0.137f, pitch );
        cameras.ApplyPrimaryMovementBuffer();
        CHECK( cameras.GetCameraView() == target );
        CHECK( SkullbonezCore::Math::Vector::Distance( cameras.GetCameraTranslation(), target ) ==
               doctest::Approx( retainedDistance ).epsilon( 0.0001f ) );
    }
}

TEST_CASE( "Camera: published tween progress slerps direction and keeps look distance nonzero" )
{
    CameraCollection cameras;
    cameras.AddCamera( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 2.0f ), Vector3( 0.0f, 1.0f, 0.0f ), 0xCA10u );
    cameras.AddCamera( Vector3( 10.0f, 0.0f, 0.0f ), Vector3( 14.0f, 0.0f, 0.0f ), Vector3( 0.0f, 1.0f, 0.0f ), 0xCA11u );
    cameras.SetCamera();
    cameras.SelectCamera( 0xCA11u, true );
    cameras.SetTweenProgress( 0.5f );
    cameras.SetCamera();

    const Vector3 eye = cameras.GetRenderCameraTranslation();
    const Vector3 look = cameras.GetRenderCameraView() - eye;
    CHECK( eye.x == doctest::Approx( 5.0f ) );
    CHECK( sqrtf( SkullbonezCore::Math::Vector::Dot( look, look ) ) == doctest::Approx( 3.0f ) );

    Vector3 direction = look;
    REQUIRE( direction.TryNormalise() );
    CHECK( direction.x == doctest::Approx( 0.7071067f ).epsilon( 0.0001f ) );
    CHECK( direction.z == doctest::Approx( 0.7071067f ).epsilon( 0.0001f ) );
}

TEST_CASE( "Camera: antipodal slerp is finite and exact endpoints preserve authored poses" )
{
    CameraCollection cameras;
    const Vector3 sourceEye( 1.0f, 2.0f, 3.0f );
    const Vector3 sourceView = sourceEye + Vector3( 0.0f, 0.0f, 5.0f );
    const Vector3 destinationEye( 9.0f, 4.0f, -2.0f );
    const Vector3 destinationView = destinationEye + Vector3( 0.0f, 0.0f, -7.0f );
    const Vector3 destinationUp( 0.0f, 1.0f, 0.0f );
    cameras.AddCamera( sourceEye, sourceView, Vector3( 0.0f, 1.0f, 0.0f ), 0xCA20u );
    cameras.AddCamera( destinationEye, destinationView, destinationUp, 0xCA21u );
    cameras.SetCamera();
    cameras.SelectCamera( 0xCA21u, true );
    cameras.SetTweenProgress( 0.5f );
    cameras.SetCamera();

    const Vector3 midpointLook = cameras.GetRenderCameraView() - cameras.GetRenderCameraTranslation();
    CHECK( std::isfinite( midpointLook.x ) );
    CHECK( std::isfinite( midpointLook.y ) );
    CHECK( std::isfinite( midpointLook.z ) );
    CHECK( SkullbonezCore::Math::Vector::Dot( midpointLook, midpointLook ) > 1.0f );

    cameras.SetTweenProgress( 1.0f );
    cameras.SetCamera();
    CHECK( cameras.GetRenderCameraTranslation() == destinationEye );
    CHECK( cameras.GetRenderCameraView() == destinationView );
    CHECK( cameras.GetRenderCameraUp() == destinationUp );
}

TEST_CASE( "Camera: completed terrain-clamped tween retains its published endpoint" )
{
    EngineConfig config;
    Terrain terrain( 10.0f, 0.0f, 0.0f, config );
    CameraCollection cameras;
    CameraMovementSettings settings;
    settings.minCameraHeight = 1.5f;
    cameras.ApplyMovementSettings( settings );
    cameras.SetTerrain( &terrain );
    cameras.AddCamera( Vector3( 0.0f, 20.0f, 10.0f ), Vector3( 0.0f, 20.0f, 0.0f ), Vector3( 0.0f, 1.0f, 0.0f ), 0xCA22u );
    cameras.AddCamera( Vector3( 20.0f, 20.0f, 10.0f ), Vector3( 20.0f, 20.0f, 0.0f ), Vector3( 0.0f, 1.0f, 0.0f ), 0xCA23u );
    cameras.SetCamera();

    cameras.TweenPrimaryToPose( Vector3( 0.0f, 0.0f, 10.0f ), SkullbonezCore::Math::Vector::ZERO_VECTOR,
                                Vector3( 0.0f, 1.0f, 0.0f ) );
    cameras.SetTweenProgress( 1.0f );
    cameras.SetCamera();
    CHECK( cameras.GetRenderCameraTranslation().y == doctest::Approx( 11.5f ) );
    CHECK( cameras.GetCameraTranslation().y == doctest::Approx( 11.5f ) );

    cameras.SetCamera();
    CHECK( cameras.GetRenderCameraTranslation().y == doctest::Approx( 11.5f ) );

    const Vector3 correctedEye = cameras.GetCameraTranslation();
    const Vector3 correctedView = cameras.GetCameraView();
    const float correctedDistance = SkullbonezCore::Math::Vector::Distance( correctedEye, correctedView );
    cameras.SelectCamera( 0xCA23u, false );
    cameras.SelectCamera( 0xCA22u, false );
    cameras.SetLockedMode( true );
    cameras.ApplyPrimaryMovementBuffer();
    CHECK( cameras.GetCameraView() == correctedView );
    CHECK( SkullbonezCore::Math::Vector::Distance( cameras.GetCameraTranslation(), correctedView ) ==
           doctest::Approx( correctedDistance ) );
}

TEST_CASE( "Scene camera slots: causal detail registration preserves main selection and stays outside demo cycling" )
{
    CameraCollection cameras;
    const Vector3 eye( 1.0f, 2.0f, 3.0f );
    const Vector3 view( 4.0f, 5.0f, 6.0f );
    const Vector3 up( 0.0f, 1.0f, 0.0f );
    cameras.AddCamera( eye, view, up, CAMERA_SCENE_OBJECT_1 );
    cameras.AddCamera( eye, view, up, CAMERA_SCENE_OBJECT_2 );
    cameras.AddCamera( eye, view, up, CAMERA_FREE );
    const uint32_t selectedMain = cameras.GetSelectedCameraName();

    RegisterCausalDetailCamera( cameras, eye, view, up );

    CHECK( cameras.HasCamera( CAMERA_CAUSAL_DETAIL ) );
    CHECK( cameras.GetSelectedCameraName() == selectedMain );
    CHECK( std::find( DEMO_CAMERA_CYCLE_SLOTS.begin(), DEMO_CAMERA_CYCLE_SLOTS.end(), CAMERA_CAUSAL_DETAIL ) ==
           DEMO_CAMERA_CYCLE_SLOTS.end() );
}

TEST_CASE( "Camera inspection: saved main slot and visible pose restore after dedicated inspection" )
{
    CameraCollection cameras;
    const Vector3 mainEye( 2.0f, 3.0f, 4.0f );
    const Vector3 mainView( 2.0f, 3.0f, 9.0f );
    const Vector3 up( 0.0f, 1.0f, 0.0f );
    cameras.AddCamera( mainEye, mainView, up, CAMERA_FREE );
    RegisterCausalDetailCamera( cameras, Vector3( 20.0f, 30.0f, 40.0f ), Vector3( 20.0f, 30.0f, 41.0f ), up );
    cameras.SetCamera();

    const uint32_t savedMainHash = cameras.GetSelectedCameraName();
    const Vector3 savedEye = cameras.GetRenderCameraTranslation();
    const Vector3 savedView = cameras.GetRenderCameraView();
    const Vector3 savedUp = cameras.GetRenderCameraUp();
    cameras.SelectCamera( CAMERA_CAUSAL_DETAIL, false );
    cameras.SetPrimaryPose( Vector3( 50.0f, 60.0f, 70.0f ), Vector3( 51.0f, 60.0f, 70.0f ), up );
    cameras.SelectCamera( savedMainHash, false );
    cameras.TweenPrimaryToPose( savedEye, savedView, savedUp );

    CHECK( cameras.GetSelectedCameraName() == CAMERA_FREE );
    cameras.SetTweenProgress( 1.0f );
    cameras.SetCamera();
    CHECK( cameras.GetRenderCameraTranslation() == mainEye );
    CHECK( cameras.GetRenderCameraView() == mainView );
}

TEST_CASE( "Attached camera inspection policy: visible-pose seed retargets a moving body without restarting entry" )
{
    CameraCollection cameras;
    const Vector3 up( 0.0f, 1.0f, 0.0f );
    cameras.AddCamera( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 2.0f ), up, CAMERA_FREE );
    cameras.AddCamera( Vector3( 10.0f, 0.0f, 0.0f ), Vector3( 10.0f, 0.0f, 2.0f ), up, CAMERA_SCENE_OBJECT_1 );
    RegisterCausalDetailCamera( cameras, Vector3( 50.0f, 0.0f, 0.0f ), Vector3( 50.0f, 0.0f, 2.0f ), up );
    cameras.SetCamera();
    cameras.SelectCamera( CAMERA_SCENE_OBJECT_1, true );
    cameras.SetTweenProgress( 0.25f );
    cameras.SetCamera();
    const Vector3 visibleEye = cameras.GetRenderCameraTranslation();
    const Vector3 visibleView = cameras.GetRenderCameraView();

    AttachedCameraState follow;
    follow.submode = AttachedCameraSubmode::FixedRelative;
    AttachedCameraPhysicsTarget target;
    target.position = Vector3( 10.0f, 0.0f, 0.0f );
    target.radius = 1.0f;
    const AttachedCameraPose visiblePose { visibleEye, visibleView, up };
    SeedAttachedCameraFixedRelative( follow, visiblePose, target );
    follow.needsEntryTween = true;

    AttachedCameraPoseCommand firstCommand;
    REQUIRE( BuildAttachedCameraOrbitPose( follow, target, visiblePose, 0.0f, 0.0f, firstCommand ) );
    CHECK( firstCommand.startEntryTween );
    CHECK( firstCommand.pose.eye.x == doctest::Approx( visibleEye.x ) );
    CHECK( firstCommand.pose.view == target.position );

    target.position.x += 4.0f;
    AttachedCameraPoseCommand movedCommand;
    REQUIRE( BuildAttachedCameraOrbitPose( follow, target, firstCommand.pose, 0.0f, 0.0f, movedCommand ) );
    CHECK_FALSE( movedCommand.startEntryTween );
    CHECK( movedCommand.pose.eye.x == doctest::Approx( firstCommand.pose.eye.x + 4.0f ) );
    CHECK( movedCommand.pose.view == target.position );

    // Production TickFollow applies the first command as a tween and later
    // commands as live destination updates through these CameraCollection seams.
    cameras.SelectCamera( CAMERA_CAUSAL_DETAIL, false );
    cameras.TweenPrimaryToPose( firstCommand.pose.eye, firstCommand.pose.view, firstCommand.pose.up );
    cameras.SetTweenProgress( 0.0f );
    cameras.SetCamera();
    CHECK( cameras.GetRenderCameraTranslation() == visibleEye );
    CHECK( cameras.GetRenderCameraView() == visibleView );

    cameras.SetTweenProgress( 0.5f );
    cameras.SetPrimaryPose( movedCommand.pose.eye, movedCommand.pose.view, movedCommand.pose.up );
    cameras.SetCamera();
    CHECK( cameras.GetRenderCameraTranslation().x == doctest::Approx( ( visibleEye.x + movedCommand.pose.eye.x ) * 0.5f ) );
    CHECK( cameras.GetRenderCameraView().x > visibleView.x );
    CHECK( cameras.GetSelectedCameraName() == CAMERA_CAUSAL_DETAIL );
}
