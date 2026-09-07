// Simple humanoid prefab geometry, placement previews and the separate neck
// angular cone policy. Linear point joints are solved by the shared constraint
// transaction; this file does not own its velocity iteration or warm cache.
#include "Ragdoll.h"

#include "../Core/Common.h"
#include "../Maths/DeterministicMath.h"
#include "ContactSolverCommon.h"
#include "PhysicsBodyStore.h"

#include <algorithm>
#include <cmath>
#include <cfloat>

using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;

namespace
{
constexpr float RAGDOLL_SURFACE_EPSILON = 0.08f;
constexpr float RAGDOLL_DEFAULT_SCALE = 1.0f;
constexpr float RAGDOLL_MIN_SCALE = 0.25f;
constexpr float RAGDOLL_MAX_SCALE = 8.0f;
constexpr float RAGDOLL_NECK_MAX_SWING_RADIANS = 0.52359878f;
constexpr float RAGDOLL_NECK_MAX_SWING_COSINE = 0.86602539f;
constexpr float RAGDOLL_NECK_MAX_CORRECTION_RADIANS = 0.20f;
constexpr float RAGDOLL_NECK_ANGULAR_DAMPING = 0.45f;

enum SimplePart
{
    PART_TORSO,
    PART_HEAD,
    PART_LEFT_UPPER_ARM,
    PART_LEFT_LOWER_ARM,
    PART_RIGHT_UPPER_ARM,
    PART_RIGHT_LOWER_ARM,
    PART_LEFT_UPPER_LEG,
    PART_LEFT_LOWER_LEG,
    PART_RIGHT_UPPER_LEG,
    PART_RIGHT_LOWER_LEG,
    PART_COUNT
};

static_assert( PART_COUNT == Ragdoll::SIMPLE_PART_COUNT, "simple ragdoll part count mismatch" );

Vector3 ScaleVector( const Vector3& value, float scale )
{
    return Vector3( value.x * scale, value.y * scale, value.z * scale );
}

void AppendPreviewLine( std::vector<float>& lineData, const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    lineData.insert( lineData.end(), { a.x, a.y, a.z, r, g, bl, b.x, b.y, b.z, r, g, bl } );
}

void AppendPreviewBox( std::vector<float>& lineData, const Vector3& center, const RotationMatrix& rotation,
                       const Vector3& halfExtents, float r, float g, float b )
{
    const Vector3 xAxis = rotation * Vector3( halfExtents.x, 0.0f, 0.0f );
    const Vector3 yAxis = rotation * Vector3( 0.0f, halfExtents.y, 0.0f );
    const Vector3 zAxis = rotation * Vector3( 0.0f, 0.0f, halfExtents.z );
    const Vector3 corners[8] = {
        center - xAxis - yAxis - zAxis, center + xAxis - yAxis - zAxis, center + xAxis + yAxis - zAxis,
        center - xAxis + yAxis - zAxis, center - xAxis - yAxis + zAxis, center + xAxis - yAxis + zAxis,
        center + xAxis + yAxis + zAxis, center - xAxis + yAxis + zAxis,
    };

    constexpr int edges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
        { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    };

    for ( const auto& edge : edges )
    {
        AppendPreviewLine( lineData, corners[edge[0]], corners[edge[1]], r, g, b );
    }
}

RotationMatrix BodyRotation( const PhysicsBodyHotState& hot )
{
    Quaternion q = hot.orientation;
    return q.GetOrientationMatrix();
}

bool IsBodySleeping( int bodyIndex, std::span<const uint8_t> sleepState )
{
    return bodyIndex >= 0 && bodyIndex < static_cast<int>( sleepState.size() ) && sleepState[bodyIndex] != 0;
}


} // namespace

float Ragdoll::ClampScale( float scale )
{
    if ( !std::isfinite( scale ) )
    {
        return RAGDOLL_DEFAULT_SCALE;
    }

    return std::clamp( scale, RAGDOLL_MIN_SCALE, RAGDOLL_MAX_SCALE );
}


float Ragdoll::SurfaceEpsilon()
{
    return RAGDOLL_SURFACE_EPSILON;
}


bool Ragdoll::TryBuildNeckSwingCorrection( float rawDot, const Vector3& correctionCross, const Vector3& fallbackAxis,
                                           Vector3& outCorrectionAxis, float& outCorrectionAngle ) noexcept
{
    // Invariant: roundoff may move a dot just outside [-1, 1]. Clamp before
    // deterministic Atan2 so both endpoint domains remain finite and explicit.
    const float dot = SkullbonezCore::Math::ClampUnit( rawDot );

    if ( dot >= RAGDOLL_NECK_MAX_SWING_COSINE )
    {
        return false;
    }

    // Concept: vector angle is atan2(|cross|, dot). The same cross vector owns
    // both the sine-like magnitude and correction axis, avoiding acos domain
    // failure and keeping the geometric inputs paired.
    const float crossMagnitude = VectorMag( correctionCross );
    const float swingAngle = SkullbonezCore::Math::Deterministic::Atan2( crossMagnitude, dot );

    if ( swingAngle <= RAGDOLL_NECK_MAX_SWING_RADIANS )
    {
        return false;
    }

    outCorrectionAxis = correctionCross;

    if ( crossMagnitude <= TOLERANCE )
    {
        // Invariant: the torso's world X axis is the established deterministic
        // choice when parallel or anti-parallel up vectors provide no axis.
        outCorrectionAxis = fallbackAxis;
    }

    outCorrectionAxis.Normalise();
    outCorrectionAngle = (std::min)( swingAngle - RAGDOLL_NECK_MAX_SWING_RADIANS, RAGDOLL_NECK_MAX_CORRECTION_RADIANS );
    return true;
}


const RagdollPartDesc* Ragdoll::SimpleParts()
{
    // Invariant: this table order is the prefab body index order and is paired
    // with SimpleJoints plus SIMPLE_PART_COUNT.
    static const RagdollPartDesc parts[PART_COUNT] = {
        { SIMPLE_PART_SUFFIXES[PART_TORSO], Vector3( 0.0f, 12.8f, 0.0f ), Vector3( 2.2f, 3.2f, 1.1f ), 0.18f, 0.62f, 0.72f,
          1.0f },
        { SIMPLE_PART_SUFFIXES[PART_HEAD], Vector3( 0.0f, 17.25f, 0.0f ), Vector3( 1.2f, 1.2f, 1.2f ), 0.15f, 0.95f, 0.82f,
          0.58f },
        { SIMPLE_PART_SUFFIXES[PART_LEFT_UPPER_ARM], Vector3( -3.0f, 13.8f, 0.0f ), Vector3( 0.65f, 2.2f, 0.65f ), 0.14f,
          0.42f, 0.50f, 0.90f },
        { SIMPLE_PART_SUFFIXES[PART_LEFT_LOWER_ARM], Vector3( -3.0f, 9.4f, 0.0f ), Vector3( 0.58f, 2.2f, 0.58f ), 0.14f,
          0.42f, 0.50f, 0.90f },
        { SIMPLE_PART_SUFFIXES[PART_RIGHT_UPPER_ARM], Vector3( 3.0f, 13.8f, 0.0f ), Vector3( 0.65f, 2.2f, 0.65f ), 0.14f,
          0.42f, 0.50f, 0.90f },
        { SIMPLE_PART_SUFFIXES[PART_RIGHT_LOWER_ARM], Vector3( 3.0f, 9.4f, 0.0f ), Vector3( 0.58f, 2.2f, 0.58f ), 0.14f,
          0.42f, 0.50f, 0.90f },
        { SIMPLE_PART_SUFFIXES[PART_LEFT_UPPER_LEG], Vector3( -0.85f, 7.2f, 0.0f ), Vector3( 0.8f, 2.4f, 0.75f ), 0.12f,
          0.36f, 0.42f, 0.80f },
        { SIMPLE_PART_SUFFIXES[PART_LEFT_LOWER_LEG], Vector3( -0.85f, 2.4f, 0.0f ), Vector3( 0.72f, 2.4f, 0.72f ), 0.12f,
          0.36f, 0.42f, 0.80f },
        { SIMPLE_PART_SUFFIXES[PART_RIGHT_UPPER_LEG], Vector3( 0.85f, 7.2f, 0.0f ), Vector3( 0.8f, 2.4f, 0.75f ), 0.12f,
          0.36f, 0.42f, 0.80f },
        { SIMPLE_PART_SUFFIXES[PART_RIGHT_LOWER_LEG], Vector3( 0.85f, 2.4f, 0.0f ), Vector3( 0.72f, 2.4f, 0.72f ), 0.12f,
          0.36f, 0.42f, 0.80f },
    };

    return parts;
}


const RagdollJointDesc* Ragdoll::SimpleJoints( int& outCount )
{
    static const RagdollJointDesc joints[] = {
        { PART_TORSO, PART_HEAD, Vector3( 0.0f, 3.2f, 0.0f ), Vector3( 0.0f, -1.2f, 0.0f ), 0.28f,
          PointJointConstraint::FLAG_LIMIT_NECK_SWING },
        { PART_TORSO, PART_LEFT_UPPER_ARM, Vector3( -2.2f, 2.25f, 0.0f ), Vector3( 0.0f, 2.2f, 0.0f ), 0.35f, 0 },
        { PART_LEFT_UPPER_ARM, PART_LEFT_LOWER_ARM, Vector3( 0.0f, -2.2f, 0.0f ), Vector3( 0.0f, 2.2f, 0.0f ), 0.30f, 0 },
        { PART_TORSO, PART_RIGHT_UPPER_ARM, Vector3( 2.2f, 2.25f, 0.0f ), Vector3( 0.0f, 2.2f, 0.0f ), 0.35f, 0 },
        { PART_RIGHT_UPPER_ARM, PART_RIGHT_LOWER_ARM, Vector3( 0.0f, -2.2f, 0.0f ), Vector3( 0.0f, 2.2f, 0.0f ), 0.30f, 0 },
        { PART_TORSO, PART_LEFT_UPPER_LEG, Vector3( -0.85f, -3.2f, 0.0f ), Vector3( 0.0f, 2.4f, 0.0f ), 0.35f, 0 },
        { PART_LEFT_UPPER_LEG, PART_LEFT_LOWER_LEG, Vector3( 0.0f, -2.4f, 0.0f ), Vector3( 0.0f, 2.4f, 0.0f ), 0.30f, 0 },
        { PART_TORSO, PART_RIGHT_UPPER_LEG, Vector3( 0.85f, -3.2f, 0.0f ), Vector3( 0.0f, 2.4f, 0.0f ), 0.35f, 0 },
        { PART_RIGHT_UPPER_LEG, PART_RIGHT_LOWER_LEG, Vector3( 0.0f, -2.4f, 0.0f ), Vector3( 0.0f, 2.4f, 0.0f ), 0.30f, 0 },
    };

    outCount = static_cast<int>( sizeof( joints ) / sizeof( joints[0] ) );
    return joints;
}


Vector3 Ragdoll::DefaultPreviewCenter( const Vector3& terrainPoint, float scale, const Quaternion& orientation )
{
    const RagdollPartDesc* parts = SimpleParts();
    float minY = FLT_MAX;
    float maxY = -FLT_MAX;

    for ( int i = 0; i < PART_COUNT; ++i )
    {
        minY = (std::min)( minY, parts[i].localCenter.y - parts[i].halfExtents.y );
        maxY = (std::max)( maxY, parts[i].localCenter.y + parts[i].halfExtents.y );
    }

    Quaternion q = orientation;
    const RotationMatrix rotation = q.GetOrientationMatrix();
    const float clampedScale = ClampScale( scale );
    return terrainPoint + rotation * Vector3( 0.0f, ( minY + maxY ) * 0.5f * clampedScale, 0.0f );
}

void Ragdoll::AddPreviewLines( std::vector<float>& lineData, const Vector3& terrainPoint, float scale,
                               const Quaternion& orientation, float r, float g, float b )
{
    const RagdollPartDesc* parts = SimpleParts();
    Quaternion q = orientation;
    const RotationMatrix rotation = q.GetOrientationMatrix();
    const float clampedScale = ClampScale( scale );
    const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, RAGDOLL_SURFACE_EPSILON, 0.0f );

    for ( int i = 0; i < PART_COUNT; ++i )
    {
        AppendPreviewBox( lineData, base + rotation * ScaleVector( parts[i].localCenter, clampedScale ), rotation,
                          ScaleVector( parts[i].halfExtents, clampedScale ), r, g, b );
    }
}


bool Ragdoll::ApplyNeckSwingLimits( PhysicsBodyStore& bodyStore, std::span<const PointJointConstraint> constraints,
                                    std::span<const uint8_t> sleepState )
{
    const PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();
    const int modelCount = bodyStore.Count();
    bool changed = false;

    for ( const PointJointConstraint& constraint : constraints )
    {
        if ( ( constraint.flags & PointJointConstraint::FLAG_LIMIT_NECK_SWING ) == 0 )
        {
            continue;
        }

        const int bodyAIndex = constraint.BodyAIndex( bodyStore );
        const int bodyBIndex = constraint.BodyBIndex( bodyStore );

        if ( bodyAIndex < 0 || bodyBIndex < 0 || bodyAIndex >= modelCount || bodyBIndex >= modelCount )
        {
            continue;
        }

        const size_t headIndex = static_cast<size_t>( bodyBIndex );

        if ( hotFields.fixed[headIndex] != 0u || IsBodySleeping( bodyBIndex, sleepState ) )
        {
            continue;
        }

        const PhysicsBodyHotState torsoHot = LoadPhysicsBodyHotState( hotFields, static_cast<size_t>( bodyAIndex ) );
        PhysicsBodyHotState headHot = LoadPhysicsBodyHotState( hotFields, headIndex );
        const RotationMatrix torsoRot = BodyRotation( torsoHot );
        const RotationMatrix headRot = BodyRotation( headHot );
        Vector3 torsoUp = torsoRot * Vector3( 0.0f, 1.0f, 0.0f );
        Vector3 headUp = headRot * Vector3( 0.0f, 1.0f, 0.0f );
        torsoUp.Normalise();
        headUp.Normalise();

        const float rawDot = Dot( headUp, torsoUp );
        const Vector3 correctionCross = CrossProduct( headUp, torsoUp );
        const Vector3 fallbackAxis = torsoRot * Vector3( 1.0f, 0.0f, 0.0f );
        Vector3 correctionAxis;
        float correctionAngle = 0.0f;

        if ( !Ragdoll::TryBuildNeckSwingCorrection( rawDot, correctionCross, fallbackAxis, correctionAxis,
                                                    correctionAngle ) )
        {
            continue;
        }

        Quaternion orientation = headHot.orientation;
        orientation.RotateAboutAxis( correctionAxis, correctionAngle );
        headHot.orientation = orientation;
        headHot.angularVelocity = headHot.angularVelocity * RAGDOLL_NECK_ANGULAR_DAMPING;
        StorePhysicsBodyHotState( hotFields, headIndex, headHot );
        changed = true;
    }

    return changed;
}
