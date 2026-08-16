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
#include "../SkullbonezSource/Runtime/Camera/CameraCollection.h"

#include <cmath>

using SkullbonezCore::Assets::AssetSystem;
using SkullbonezCore::Core::EngineConfig;
using SkullbonezCore::Environment::Camera;
using SkullbonezCore::Environment::CameraCollection;
using SkullbonezCore::Environment::CameraMovementSettings;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Math::Vector::Vector3;

TEST_CASE( "Camera: authored zero up remains the SetAll sentinel" )
{
    CameraCollection cameras;
    cameras.AddCamera( Vector3( 1.0f, 2.0f, 3.0f ),
                       Vector3( 1.0f, 2.0f, 2.0f ),
                       SkullbonezCore::Math::Vector::ZERO_VECTOR,
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
    cameras.AddCamera( SkullbonezCore::Math::Vector::ZERO_VECTOR,
                       Vector3( 0.0f, -1.0f, 0.0f ),
                       SkullbonezCore::Math::Vector::ZERO_VECTOR,
                       0xCA08u );

    cameras.RotatePrimary( 0.0f, 0.25f );

    const Vector3& view = cameras.GetCameraView();
    CHECK( std::isfinite( view.x ) );
    CHECK( std::isfinite( view.y ) );
    CHECK( std::isfinite( view.z ) );

    Vector3 cappedPole = -view;
    REQUIRE( cappedPole.TryNormalise() );
    CHECK( cappedPole.y > 0.999f );
}

TEST_CASE( "Camera: published tween progress slerps direction and keeps look distance nonzero" )
{
    CameraCollection cameras;
    cameras.AddCamera( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 2.0f ),
                       Vector3( 0.0f, 1.0f, 0.0f ), 0xCA10u );
    cameras.AddCamera( Vector3( 10.0f, 0.0f, 0.0f ), Vector3( 14.0f, 0.0f, 0.0f ),
                       Vector3( 0.0f, 1.0f, 0.0f ), 0xCA11u );
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
