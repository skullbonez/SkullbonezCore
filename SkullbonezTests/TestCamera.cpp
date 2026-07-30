//
// File: SkullbonezTests/TestCamera.cpp
// Purpose:
//   Locks caller-reachable camera basis fallbacks at the CameraCollection boundary.
//
// Summary:
//   Authored and replay-restored poses can temporarily contain coincident
//   eye/target points or a missing up basis. Camera owns deterministic movement
//   and tween fallbacks so those transient poses never publish NaNs.
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
//
// Related:
//   - SkullbonezSource/Runtime/Camera/Camera.cpp
//   - SkullbonezSource/Runtime/Camera/CameraCollection.cpp
//   - Agentic/Reports/2026-07-15/math-fatal-call-site-survey.md
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

TEST_CASE( "Camera: coincident eye and target move forward along world minus Z" )
{
    CameraCollection cameras;
    const Vector3 point( 4.0f, 5.0f, 6.0f );
    cameras.AddCamera( point, point, Vector3( 0.0f, 1.0f, 0.0f ), 0xCA02u );
    cameras.MovePrimary( Camera::TravelDirection::Forward, 2.0f );
    CHECK( cameras.GetPrimaryMovementBuffer() == Vector3( 0.0f, 0.0f, -2.0f ) );
}

TEST_CASE( "Camera: parallel view and up axes move right along world plus X" )
{
    CameraCollection cameras;
    cameras.AddCamera( SkullbonezCore::Math::Vector::ZERO_VECTOR,
                       Vector3( 0.0f, 1.0f, 0.0f ),
                       Vector3( 0.0f, 1.0f, 0.0f ),
                       0xCA03u );
    cameras.MovePrimary( Camera::TravelDirection::Right, 3.0f );
    CHECK( cameras.GetPrimaryMovementBuffer() == Vector3( 3.0f, 0.0f, 0.0f ) );
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

TEST_CASE( "CameraCollection: SetPrimaryUp repairs zero input to world up" )
{
    CameraCollection cameras;
    cameras.AddCamera( SkullbonezCore::Math::Vector::ZERO_VECTOR,
                       Vector3( 0.0f, 0.0f, -1.0f ),
                       Vector3( 0.0f, 1.0f, 0.0f ),
                       0xCA04u );
    cameras.SetPrimaryUp( SkullbonezCore::Math::Vector::ZERO_VECTOR );
    CHECK( cameras.GetCameraUp() == Vector3( 0.0f, 1.0f, 0.0f ) );
}

TEST_CASE( "CameraCollection: opposed tween up vectors cancel to world up" )
{
    EngineConfig config;
    Terrain terrain( 0.0f, 0.0f, 0.0f, config );
    CameraCollection cameras;
    const Vector3 position( 10.0f, 10.0f, 10.0f );
    const Vector3 view( 10.0f, 10.0f, 9.0f );
    cameras.AddCamera( position, view, Vector3( 0.0f, 1.0f, 0.0f ), 0xCA05u );
    cameras.SetCamera();
    cameras.SetTerrain( &terrain );
    cameras.SetTweenSpeed( 0.5f );

    cameras.TweenPrimaryToPose( position, view, Vector3( 0.0f, -1.0f, 0.0f ) );
    cameras.SetCamera();

    CHECK( cameras.IsCameraTweening() );
    CHECK( cameras.GetRenderCameraUp() == Vector3( 0.0f, 1.0f, 0.0f ) );
}

TEST_CASE( "CameraCollection: terrainless tween bypasses terrain collision" )
{
    CameraCollection cameras;
    const Vector3 startPosition( 10.0f, 20.0f, 30.0f );
    const Vector3 startView( 10.0f, 20.0f, 29.0f );
    const Vector3 destinationPosition( 250000.0f, 40.0f, -250000.0f );
    const Vector3 destinationView( 249999.0f, 40.0f, -250000.0f );
    cameras.AddCamera( startPosition, startView, Vector3( 0.0f, 1.0f, 0.0f ), 0xCA06u );
    cameras.SetCamera();
    cameras.SetTerrain( nullptr );
    cameras.SetTweenSpeed( 0.5f );

    // Invariant: a space-scene tween has no terrain provider and may cross
    // coordinates far outside any terrain grid without entering its fatal
    // collision query path.
    cameras.TweenPrimaryToPose( destinationPosition, destinationView, Vector3( 0.0f, 1.0f, 0.0f ) );
    cameras.SetCamera();

    CHECK( cameras.IsCameraTweening() );
    CHECK( cameras.GetRenderCameraTranslation().x > startPosition.x );
    CHECK( cameras.GetRenderCameraTranslation().z < startPosition.z );
}
