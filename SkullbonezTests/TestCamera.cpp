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
//   cycling exclusion, prepared causal endpoints, upright blending, and return behavior.
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
    cameras.AddCamera( Vector3( 1.0f, 2.0f, 3.0f ), Vector3( 1.0f, 2.0f, 2.0f ), SkullbonezCore::Math::Vector::ZERO_VECTOR, 0xCA01u );
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
    cameras.AddCamera( SkullbonezCore::Math::Vector::ZERO_VECTOR, Vector3( 0.0f, -1.0f, 0.0f ), SkullbonezCore::Math::Vector::ZERO_VECTOR, 0xCA08u );

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
    freeCameras.AddCamera( SkullbonezCore::Math::Vector::ZERO_VECTOR, Vector3( 0.0f, 0.0f, -1.0f ), Vector3( 0.0f, 1.0f, 0.0f ), 0xCA24u );
    freeCameras.RotatePrimary( yaw, pitch );
    CHECK( freeCameras.GetCameraView().x > 0.0f );
    CHECK( freeCameras.GetCameraView().y > 0.0f );

    CameraCollection launcherCameras;
    launcherCameras.ApplyMovementSettings( settings );
    launcherCameras.AddCamera( Vector3( 0.0f, 0.0f, 10.0f ), SkullbonezCore::Math::Vector::ZERO_VECTOR, Vector3( 0.0f, 1.0f, 0.0f ), 0xCA25u );
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
    cameras.AddCamera( Vector3( 0.0f, 0.0f, 10.0f ), SkullbonezCore::Math::Vector::ZERO_VECTOR, Vector3( 0.0f, 1.0f, 0.0f ), 0xCA09u );
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
    recoveryCameras.AddCamera( Vector3( 0.0f, 0.0f, 1.0f ), SkullbonezCore::Math::Vector::ZERO_VECTOR, Vector3( 0.0f, 1.0f, 0.0f ), 0xCA0Bu );
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
    CHECK( SkullbonezCore::Math::Vector::Distance( zeroUpCameras.GetCameraTranslation(), target ) == doctest::Approx( 10.0f ) );
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
        CHECK( SkullbonezCore::Math::Vector::Distance( cameras.GetCameraTranslation(), target ) == doctest::Approx( retainedDistance ).epsilon( 0.0001f ) );
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

    cameras.TweenPrimaryToPose( Vector3( 0.0f, 0.0f, 10.0f ), SkullbonezCore::Math::Vector::ZERO_VECTOR, Vector3( 0.0f, 1.0f, 0.0f ) );
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
    CHECK( SkullbonezCore::Math::Vector::Distance( cameras.GetCameraTranslation(), correctedView ) == doctest::Approx( correctedDistance ) );
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
    CHECK( std::find( DEMO_CAMERA_CYCLE_SLOTS.begin(), DEMO_CAMERA_CYCLE_SLOTS.end(), CAMERA_CAUSAL_DETAIL ) == DEMO_CAMERA_CYCLE_SLOTS.end() );
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
    cameras.TweenPrimaryToUprightPose( Vector3( 50.0f, 60.0f, 70.0f ), Vector3( 51.0f, 60.0f, 70.0f ), up );
    cameras.SetTweenProgress( 0.4f );
    cameras.SetCamera();
    const Vector3 visibleCausalEye = cameras.GetRenderCameraTranslation();
    const Vector3 visibleCausalView = cameras.GetRenderCameraView();
    cameras.SelectCamera( savedMainHash, false );
    cameras.TweenPrimaryToUprightPose( savedEye, savedView, savedUp );

    CHECK( cameras.GetSelectedCameraName() == CAMERA_FREE );
    cameras.SetTweenProgress( 0.0f );
    cameras.SetCamera();
    CHECK( cameras.GetRenderCameraTranslation() == visibleCausalEye );
    CHECK( cameras.GetRenderCameraView() == visibleCausalView );
    cameras.SetTweenProgress( 1.0f );
    cameras.SetCamera();
    CHECK( cameras.GetRenderCameraTranslation() == mainEye );
    CHECK( cameras.GetRenderCameraView() == mainView );
}

TEST_CASE( "Causal camera: destination is prepared before an upright tween starts from the visible main pose" )
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
    target.position = Vector3( 100.0f, 5.0f, 0.0f );
    target.radius = 1.0f;
    const AttachedCameraPose visiblePose { visibleEye, visibleView, up };
    SeedAttachedCameraFixedRelative( follow, visiblePose, target );
    follow.needsEntryTween = false;

    AttachedCameraPoseCommand prepared;
    REQUIRE( BuildAttachedCameraOrbitPose( follow, target, visiblePose, 0.0f, 0.0f, prepared ) );
    CHECK_FALSE( prepared.startEntryTween );
    CHECK( prepared.pose.view == target.position );
    CHECK( SkullbonezCore::Math::Vector::Distance( prepared.pose.eye, target.position ) == doctest::Approx( 40.0f ) );
    CHECK( prepared.pose.up == up );

    // The selected causal slot owns the endpoint immediately. Render remains at
    // the main pose until externally synchronized cause transport advances it.
    cameras.SelectCamera( CAMERA_CAUSAL_DETAIL, false );
    cameras.SetPrimaryPose( visiblePose.eye, visiblePose.view, visiblePose.up );
    cameras.TweenPrimaryToUprightPose( prepared.pose.eye, prepared.pose.view, prepared.pose.up );
    CHECK( cameras.GetCameraTranslation() == prepared.pose.eye );
    CHECK( cameras.GetCameraView() == prepared.pose.view );
    cameras.SetTweenProgress( 0.0f );
    cameras.SetCamera();
    CHECK( cameras.GetRenderCameraTranslation() == visibleEye );
    CHECK( cameras.GetRenderCameraView() == visibleView );

    cameras.SetTweenProgress( 0.5f );
    cameras.SetCamera();
    Vector3 direction = cameras.GetRenderCameraView() - cameras.GetRenderCameraTranslation();
    REQUIRE( direction.TryNormalise() );
    Vector3 expectedUp = up - direction * direction.y;
    REQUIRE( expectedUp.TryNormalise() );
    CHECK( cameras.GetRenderCameraUp().x == doctest::Approx( expectedUp.x ) );
    CHECK( cameras.GetRenderCameraUp().y == doctest::Approx( expectedUp.y ) );
    CHECK( cameras.GetRenderCameraUp().z == doctest::Approx( expectedUp.z ) );
    CHECK( cameras.GetSelectedCameraName() == CAMERA_CAUSAL_DETAIL );

    const float retainedDistance = SkullbonezCore::Math::Vector::Distance( prepared.pose.eye, target.position );
    AttachedCameraPoseCommand orbited;
    REQUIRE( BuildAttachedCameraOrbitPose( follow, target, prepared.pose, 0.55f, -0.2f, orbited ) );
    CHECK( orbited.pose.view == target.position );
    CHECK( orbited.pose.up == up );
    CHECK( SkullbonezCore::Math::Vector::Distance( orbited.pose.eye, target.position ) == doctest::Approx( retainedDistance ).epsilon( 0.0001f ) );
    CHECK( SkullbonezCore::Math::Vector::Distance( orbited.pose.eye, prepared.pose.eye ) > 1.0f );
}

TEST_CASE( "Editor camera views: fixed axes allow only bounded zoom and restore perspective" )
{
    CameraCollection cameras;
    const Vector3 eye( 10, 40, 70 ), focus( 1, 2, 3 ), up( 0, 1, 0 );
    cameras.AddCamera( eye, focus, up, CAMERA_FREE );
    cameras.SetCamera();
    for ( int axis = 1; axis <= 3; ++axis )
    {
        cameras.SelectEditorView( cameras.GetCameraView(), 100.0f, axis );
        cameras.SetTweenDeltaSeconds( 0.2f );
        cameras.SetCamera();
        CHECK( cameras.EditorView() == axis );
        const Vector3 direction = axis == 1 ? Vector3( 0, 1, 0 ) : axis == 2 ? Vector3( 1, 0, 0 ) : Vector3( 0, 0, 1 );
        CHECK( cameras.GetRenderCameraTranslation() == focus + direction * 100.0f );
        CHECK( cameras.GetRenderCameraView() == focus );
        CHECK( cameras.GetRenderCameraUp() == ( axis == 1 ? Vector3( 0, 0, -1 ) : up ) );
        cameras.RotatePrimary( 1, 1 );
        cameras.MovePrimary( Camera::TravelDirection::Left, 500 );
        cameras.ApplyPrimaryMovementBuffer();
        cameras.TweenPrimaryToPose( Vector3( 99, 99, 99 ), Vector3( 4, 4, 4 ), up );
        cameras.SetCamera();
        CHECK_FALSE( cameras.IsTweening() );
        CHECK( cameras.GetRenderCameraTranslation() == focus + direction * 100.0f );
        cameras.ZoomEditorView( -0.5f );
        CHECK( SkullbonezCore::Math::Vector::Distance( cameras.GetRenderCameraTranslation(), focus ) < 100.0f );
        CHECK( cameras.GetRenderCameraView() == focus );
        const Vector3 zoomed = cameras.GetRenderCameraTranslation();
        cameras.SelectEditorView( cameras.GetCameraView(), 100.0f, 99 );
        CHECK( cameras.GetRenderCameraTranslation() == zoomed );
        cameras.SelectEditorView( cameras.GetCameraView(), 100.0f, 0 );
        cameras.SetCamera();
        CHECK( cameras.GetRenderCameraTranslation() == eye );
        CHECK( cameras.GetRenderCameraView() == focus );
        CHECK( cameras.GetRenderCameraUp() == up );
    }
}

TEST_CASE( "Editor camera views: workspace axis and zoom are independent and reset with the scene" )
{
    CameraCollection cameras;
    cameras.AddCamera( Vector3( 0, 30, 80 ), Vector3( 0, 0, 0 ), Vector3( 0, 1, 0 ), CAMERA_FREE );
    cameras.SetCamera();
    cameras.SelectEditorView( cameras.GetCameraView(), 100.0f, 1 );
    cameras.SetTweenDeltaSeconds( 0.2f );
    cameras.SetCamera();
    cameras.ZoomEditorView( 0.5f );
    const Vector3 sceneEye = cameras.GetCameraTranslation();
    cameras.SetEditorViewWorkspace( true );
    CHECK( cameras.EditorView() == 0 );
    cameras.SetPrimaryPose( Vector3( 100, 100, 100 ), Vector3( 0, 0, 0 ), Vector3( 0, 1, 0 ) );
    cameras.SetCamera();
    cameras.SelectEditorView( cameras.GetCameraView(), 100.0f, 2 );
    cameras.SetCamera();
    const Vector3 labEye = cameras.GetCameraTranslation();
    cameras.SetEditorViewWorkspace( false );
    cameras.SetCamera();
    CHECK( cameras.EditorView() == 1 );
    CHECK( cameras.GetRenderCameraTranslation() == sceneEye );
    cameras.SetEditorViewWorkspace( true );
    cameras.SetCamera();
    CHECK( cameras.EditorView() == 2 );
    CHECK( cameras.GetRenderCameraTranslation() == labEye );
    cameras.Reset();
    CHECK( cameras.EditorView() == 0 );
}

TEST_CASE( "Editor camera panes preserve full-screen pose and independent plane pan and zoom" )
{
    CameraCollection cameras;
    const Vector3 eye( 10, 40, 70 ), focus( 1, 2, 3 ), up( 0, 1, 0 );
    cameras.AddCamera( eye, focus, up, CAMERA_FREE );
    cameras.SetCamera();
    cameras.ToggleFourViews( focus, 200 );
    REQUIRE( cameras.FourViews() );
    CHECK( cameras.GetSelectedCameraName() == CAMERA_FREE );
    const auto perspective = SkullbonezCore::Math::Transformation::Matrix4::PerspectiveZeroToOne( 45, 1.5f, 0.1f, 10000 );
    for ( int pane = 0; pane < 3; ++pane )
    {
        cameras.SelectEditorPane( pane );
        const auto before = cameras.EditorPane( pane );
        cameras.RotatePrimary( 0.2f, 0.1f );
        cameras.MovePrimary( Camera::TravelDirection::Forward, 20 );
        cameras.SetCamera();
        CHECK( cameras.EditorPane( pane ).eye == before.eye );
        cameras.PanEditorView( 12, -8 );
        const auto panned = cameras.EditorPane( pane );
        CHECK( panned.eye - before.eye == panned.focus - before.focus );
        CHECK( panned.up == before.up );
        CHECK( panned.eye != before.eye );
        const Vector3 normal = pane == 0 ? Vector3( 0, 1, 0 ) : pane == 1 ? Vector3( 1, 0, 0 ) : Vector3( 0, 0, 1 );
        CHECK( SkullbonezCore::Math::Vector::Dot( panned.eye - before.eye, normal ) == 0 );
        cameras.ZoomEditorView( -0.3f );
        const auto zoomed = cameras.EditorPane( pane );
        CHECK( zoomed.focus == panned.focus );
        CHECK( SkullbonezCore::Math::Vector::Distance( zoomed.eye, zoomed.focus ) < 200 );
        const auto projection = cameras.EditorPaneProjection( pane, perspective );
        CHECK( projection.m[15] == 1 );
        CHECK( projection.m[11] == 0 );
        // An elevated object can be behind the zoomed eye and still belongs
        // to the editor's fitted scene volume. Zoom must preserve its depth.
        const auto view = SkullbonezCore::Math::Transformation::Matrix4::LookAt( zoomed.eye, zoomed.focus, zoomed.up );
        const auto clip = projection * view;
        const auto elevated = zoomed.focus + normal * 190;
        const float depth = clip.m[2] * elevated.x + clip.m[6] * elevated.y + clip.m[10] * elevated.z + clip.m[14];
        CHECK( depth > 0 );
        CHECK( depth < 1 );
        cameras.ZoomEditorView( -2 );
        const auto close = cameras.EditorPane( pane );
        const auto closeClip = cameras.EditorPaneProjection( pane, perspective ) * SkullbonezCore::Math::Transformation::Matrix4::LookAt( close.eye, close.focus, close.up );
        CHECK( closeClip.m[2] * elevated.x + closeClip.m[6] * elevated.y + closeClip.m[10] * elevated.z + closeClip.m[14] == doctest::Approx( depth ) );
        cameras.SelectEditorPane( 3 );
        CHECK( cameras.GetCameraTranslation() == eye );
        cameras.SelectEditorPane( pane );
        CHECK( cameras.GetCameraTranslation() == close.eye );
    }
    cameras.SelectEditorPane( 3 );
    cameras.SetPrimaryPose( Vector3( 40, 50, 60 ), focus, up );
    cameras.SetCamera();
    cameras.ToggleFourViews( focus, 200 );
    CHECK_FALSE( cameras.FourViews() );
    CHECK( cameras.GetRenderCameraTranslation() == eye );
    CHECK( cameras.GetRenderCameraView() == focus );
    cameras.SelectEditorView( focus, 200, 1 );
    cameras.SetTweenDeltaSeconds( 0.2f );
    cameras.SetCamera();
    const auto fixed = cameras.GetRenderCameraTranslation();
    cameras.ToggleFourViews( focus, 200 );
    cameras.SelectEditorPane( 2 );
    cameras.ZoomEditorView( -1 );
    cameras.ToggleFourViews( focus, 200 );
    CHECK( cameras.EditorView() == 1 );
    CHECK( cameras.GetRenderCameraTranslation() == fixed );
    cameras.SelectEditorView( focus, 200, 0 );
    cameras.SetCamera();
    CHECK( cameras.GetRenderCameraTranslation() == eye );
    cameras.ToggleFourViews( focus, 200 );
    cameras.Reset();
    CHECK_FALSE( cameras.FourViews() );
}

TEST_CASE( "Editor camera views: eased transitions finish in 200ms and retarget from the visible pose" )
{
    CameraCollection cameras;
    const Vector3 eye( 10, 40, 70 ), focus( 1, 2, 3 ), up( 0, 1, 0 );
    cameras.AddCamera( eye, focus, up, CAMERA_FREE );
    cameras.SetCamera();
    cameras.SelectEditorView( focus, 100, 1 );
    CHECK( cameras.GetSelectedCameraName() == CAMERA_FREE );
    REQUIRE( cameras.IsTweening() );
    CHECK( cameras.GetRenderCameraTranslation() == eye );
    cameras.SetTweenDeltaSeconds( 0.1f );
    cameras.SetCamera();
    CHECK( cameras.TweenProgress() == doctest::Approx( 0.875f ) );
    CHECK( cameras.GetRenderCameraTranslation().y == doctest::Approx( eye.y + ( focus.y + 100 - eye.y ) * 0.875f ) );
    const Vector3 visible = cameras.GetRenderCameraTranslation();
    const Vector3 visibleUp = cameras.GetRenderCameraUp();
    cameras.SelectEditorView( focus, 100, 2 );
    CHECK( cameras.GetRenderCameraTranslation() == visible );
    CHECK( cameras.GetRenderCameraUp() == visibleUp );
    cameras.SetTweenDeltaSeconds( 0.199f );
    cameras.SetCamera();
    REQUIRE( cameras.IsTweening() );
    CHECK( SkullbonezCore::Math::Vector::Distance( cameras.GetRenderCameraUp(), up ) < 0.001f );
    cameras.SetTweenDeltaSeconds( 0.00101f );
    cameras.SetCamera();
    CHECK_FALSE( cameras.IsTweening() );
    CHECK( cameras.GetRenderCameraTranslation() == focus + Vector3( 100, 0, 0 ) );
    cameras.SelectEditorView( focus, 100, 0 );
    cameras.SetTweenDeltaSeconds( 0.1f );
    cameras.SetCamera();
    const Vector3 returning = cameras.GetRenderCameraTranslation();
    cameras.SelectEditorView( focus, 100, 3 );
    CHECK( cameras.GetRenderCameraTranslation() == returning );
    cameras.SetTweenDeltaSeconds( 0.2f );
    cameras.SetCamera();
    cameras.SelectEditorView( focus, 100, 0 );
    cameras.SetCamera();
    CHECK_FALSE( cameras.IsTweening() );
    CHECK( cameras.GetRenderCameraTranslation() == eye );
    CHECK( cameras.GetRenderCameraUp() == up );
    // Ordinary scene-camera transitions retain their existing 1.5-second clock.
    cameras.TweenPrimaryToPose( Vector3( 200, 100, 100 ), focus, up );
    cameras.SetCamera();
    CHECK( cameras.IsTweening() );
    cameras.SetTweenDeltaSeconds( 1.3f );
    cameras.SetCamera();
    CHECK_FALSE( cameras.IsTweening() );
    const Vector3 beforeSelection = cameras.GetRenderCameraTranslation();
    cameras.AddCamera( Vector3( 500, 100, 500 ), focus, up, CAMERA_SCENE_OBJECT_1 );
    cameras.SelectCamera( CAMERA_SCENE_OBJECT_1, true );
    cameras.SelectEditorView( focus, 100, 0 );
    CHECK( cameras.GetRenderCameraTranslation() == beforeSelection );
    CHECK( cameras.GetRenderCameraView() == focus );
}
