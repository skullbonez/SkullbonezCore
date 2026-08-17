/*
File: SkullbonezSource/Runtime/Camera/AttachedCameraController.InspectionPolicy.cpp
Purpose:
  Implements the stateless attached-camera orbit and focused-inspection policy.

Summary:
  The policy seeds orbit state from the currently visible pose, clamps every
  operator delta, and emits one pose command. Retained target identity, camera
  slot selection, suspension, and application remain in AttachedCameraController
  and CameraCollection rather than migrating into this calculation seam.

Invariants:
  - Invalid or degenerate pose math fails closed without publishing a command.
  - Orbit distance remains proportional to the resolved target radius.
  - needsEntryTween is consumed only after a valid command is produced.

Related:
  - SkullbonezSource/Runtime/Camera/AttachedCameraController.InspectionPolicy.h
  - SkullbonezSource/Runtime/Camera/AttachedCameraController.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "AttachedCameraController.InspectionPolicy.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr int ATTACHED_CAMERA_WHEEL_DELTA = 120;
constexpr float ATTACHED_CAMERA_ORBIT_DEFAULT_PITCH = 0.30f;
constexpr float ATTACHED_CAMERA_ORBIT_MOUSE_PITCH_MIN = -1.35f;
constexpr float ATTACHED_CAMERA_ORBIT_MOUSE_PITCH_MAX = 1.35f;
constexpr float ATTACHED_CAMERA_ORBIT_MIN_DISTANCE_RADIUS = 1.25f;
constexpr float ATTACHED_CAMERA_ORBIT_MAX_DISTANCE_RADIUS = 40.0f;
constexpr float ATTACHED_CAMERA_ORBIT_WHEEL_FACTOR = 0.88f;

bool IsFiniteVector( const Vector3& value )
{
    return std::isfinite( value.x ) && std::isfinite( value.y ) && std::isfinite( value.z );
}


bool TryNormalizeVector( Vector3& value )
{
    if ( !IsFiniteVector( value ) )
    {
        return false;
    }

    const float lengthSquared = VectorMagSquared( value );

    if ( lengthSquared <= TOLERANCE * TOLERANCE )
    {
        return false;
    }

    value *= 1.0f / sqrtf( lengthSquared );
    return true;
}


Vector3 NormalizedOr( Vector3 value, const Vector3& fallback )
{
    if ( TryNormalizeVector( value ) )
    {
        return value;
    }

    Vector3 safeFallback = fallback;
    return TryNormalizeVector( safeFallback ) ? safeFallback : Vector3( 0.0f, 1.0f, 0.0f );
}


float OrbitMinDistance( float targetRadius )
{
    return (std::max)( 1.0f, targetRadius * ATTACHED_CAMERA_ORBIT_MIN_DISTANCE_RADIUS );
}


float ClampOrbitDistance( float targetRadius, float distance )
{
    if ( !std::isfinite( distance ) )
    {
        distance = targetRadius * 8.0f;
    }

    const float minDistance = OrbitMinDistance( targetRadius );
    const float maxDistance = (std::max)( minDistance + 1.0f, targetRadius * ATTACHED_CAMERA_ORBIT_MAX_DISTANCE_RADIUS );
    return std::clamp( distance, minDistance, maxDistance );
}


float ClampOrbitPitch( float pitch )
{
    if ( !std::isfinite( pitch ) )
    {
        return ATTACHED_CAMERA_ORBIT_DEFAULT_PITCH;
    }

    return std::clamp( pitch, ATTACHED_CAMERA_ORBIT_MOUSE_PITCH_MIN, ATTACHED_CAMERA_ORBIT_MOUSE_PITCH_MAX );
}


float WrapOrbitYaw( float yaw )
{
    constexpr float PI = 3.14159265358979323846f;
    constexpr float TWO_PI = 2.0f * PI;

    while ( yaw > PI )
    {
        yaw -= TWO_PI;
    }

    while ( yaw < -PI )
    {
        yaw += TWO_PI;
    }

    return yaw;
}


Vector3 OrbitOffset( float yaw, float pitch, float distance )
{
    const float horizontal = cosf( pitch ) * distance;
    return Vector3( sinf( yaw ) * horizontal, sinf( pitch ) * distance, cosf( yaw ) * horizontal );
}
} // namespace


void CaptureAttachedCameraOrbit( AttachedCameraState& state, const AttachedCameraPose& currentPose,
                                 const AttachedCameraPhysicsTarget& target )
{
    Vector3 offset = currentPose.eye - target.position;
    float distance = sqrtf( VectorMagSquared( offset ) );

    if ( !std::isfinite( distance ) || distance < OrbitMinDistance( target.radius ) )
    {
        Vector3 look = currentPose.view - currentPose.eye;

        if ( !TryNormalizeVector( look ) )
        {
            look = Vector3( 0.0f, 0.0f, 1.0f );
        }

        distance = target.radius * 8.0f;
        offset = -look * distance;
    }

    const float normalizedY = SkullbonezCore::Math::ClampUnit( offset.y / (std::max)( distance, 0.001f ) );
    state.orbitDistance = ClampOrbitDistance( target.radius, distance );
    state.orbitPitchRadians = ClampOrbitPitch( asinf( normalizedY ) );
    state.orbitYawRadians = WrapOrbitYaw( atan2f( offset.x, offset.z ) );
    state.hasOrbit = true;
}


void SeedAttachedCameraFixedRelative( AttachedCameraState& state, const AttachedCameraPose& currentPose,
                                      const AttachedCameraPhysicsTarget& target )
{
    state.localEyeOffset = target.rotation.TransposeMultiply( currentPose.eye - target.position );
    state.localViewOffset = target.rotation.TransposeMultiply( currentPose.view - target.position );
    state.localUp = NormalizedOr( target.rotation.TransposeMultiply( currentPose.up ), Vector3( 0.0f, 1.0f, 0.0f ) );
    Vector3 look = currentPose.view - currentPose.eye;

    if ( TryNormalizeVector( look ) )
    {
        state.lastLookDirection = look;
        state.hasLastLookDirection = true;
    }

    state.hasFixedOffset = true;
    CaptureAttachedCameraOrbit( state, currentPose, target );
}


bool ApplyAttachedCameraOrbitWheel( AttachedCameraState& state, const AttachedCameraPhysicsTarget& target,
                                    int unhandledWheelDelta )
{
    const int wheelSteps = unhandledWheelDelta / ATTACHED_CAMERA_WHEEL_DELTA;

    if ( wheelSteps == 0 )
    {
        return false;
    }

    const float nextDistance = state.orbitDistance *
                               powf( ATTACHED_CAMERA_ORBIT_WHEEL_FACTOR, static_cast<float>( wheelSteps ) );
    state.orbitDistance = ClampOrbitDistance( target.radius, nextDistance );
    state.hasOrbit = true;
    return true;
}


bool BuildAttachedCameraOrbitPose( AttachedCameraState& state, const AttachedCameraPhysicsTarget& target,
                                   const AttachedCameraPose& currentPose, float orbitYawDelta, float orbitPitchDelta,
                                   AttachedCameraPoseCommand& outCommand )
{
    if ( !state.hasOrbit )
    {
        CaptureAttachedCameraOrbit( state, currentPose, target );
    }

    if ( orbitYawDelta != 0.0f || orbitPitchDelta != 0.0f )
    {
        state.orbitYawRadians = WrapOrbitYaw( state.orbitYawRadians + orbitYawDelta );
        state.orbitPitchRadians = ClampOrbitPitch( state.orbitPitchRadians + orbitPitchDelta );
    }

    state.orbitDistance = ClampOrbitDistance( target.radius, state.orbitDistance );
    const Vector3 eye = target.position + OrbitOffset( state.orbitYawRadians, state.orbitPitchRadians, state.orbitDistance );
    Vector3 view = target.position;
    const Vector3 up( 0.0f, 1.0f, 0.0f );

    if ( state.submode == AttachedCameraSubmode::VelocityForward )
    {
        Vector3 direction = target.linearVelocity;

        if ( !TryNormalizeVector( direction ) )
        {
            direction = state.hasLastLookDirection ? state.lastLookDirection : currentPose.view - currentPose.eye;

            if ( !TryNormalizeVector( direction ) )
            {
                direction = NormalizedOr( view - eye, Vector3( 0.0f, 0.0f, 1.0f ) );
            }
        }

        view = target.position + direction * (std::max)( target.radius, state.orbitDistance * 0.25f );
        state.lastLookDirection = direction;
        state.hasLastLookDirection = true;
    }

    if ( !IsFiniteVector( eye ) || !IsFiniteVector( view ) || VectorMagSquared( view - eye ) <= TOLERANCE * TOLERANCE )
    {
        return false;
    }

    outCommand.pose.eye = eye;
    outCommand.pose.view = view;
    outCommand.pose.up = up;
    outCommand.startEntryTween = state.needsEntryTween;
    state.needsEntryTween = false;
    return true;
}
} // namespace SkullbonezCore::Runtime
