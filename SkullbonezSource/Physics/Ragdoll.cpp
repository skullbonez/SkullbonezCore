/*
File: SkullbonezSource/Physics/Ragdoll.cpp
Purpose:
  Defines simple humanoid ragdoll descriptors and point-joint solving.

Summary:
  The body layout is prefab value data. Scene owners turn the descriptors into
  authored objects; Physics keeps only handle-keyed point-joint descriptors and
  solver rows. This keeps the ragdoll feature isolated and leaves a clear
  migration path to a full constraint solver.

Glossary:
  Neck swing limit: Special angular clamp applied to the head/torso joint.
  Prefab descriptor: Immutable local part or joint facts consumed by authored
    scene setup.

Invariants:
  - Body and constraint creation order must stay deterministic.
  - Constraint solving must not allocate per row while physics is stepping.
  - This file does not construct scene objects; callers build renderable bodies
    from descriptors before registering point joints by handle.

Related:
  - SkullbonezSource/Physics/Ragdoll.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "Ragdoll.h"

#include "../Core/Common.h"
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
constexpr float RAGDOLL_JOINT_MAX_BIAS_SPEED = 28.0f;
constexpr float RAGDOLL_JOINT_MAX_POSITION_CORRECTION = 0.35f;
constexpr float RAGDOLL_JOINT_MAX_LINEAR_SPEED = 70.0f;
constexpr float RAGDOLL_JOINT_MAX_ANGULAR_SPEED = 18.0f;
constexpr float RAGDOLL_NECK_MAX_SWING_RADIANS = 0.52359878f;
constexpr float RAGDOLL_NECK_MAX_SWING_COSINE = 0.86602539f;
constexpr float RAGDOLL_NECK_MAX_CORRECTION_RADIANS = 0.20f;
constexpr float RAGDOLL_NECK_ANGULAR_DAMPING = 0.45f;
constexpr int RAGDOLL_SOLVER_ITERATIONS = 4;

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

Vector3 ApplyRecordInvInertia( const PhysicsBodyRecord& record, const PhysicsBodyHotState& hot, const Vector3& value )
{

    if ( !record.usesWorldInertia )
    {
        return VectorMultiply( hot.inverseRotationalInertia, value );
    }

    const RotationMatrix rotation = BodyRotation( hot );
    const Vector3 local = rotation.TransposeMultiply( value );
    return rotation * VectorMultiply( hot.inverseRotationalInertia, local );
}

Vector3 ClampVectorMagnitude( const Vector3& value, float limit )
{

    if ( !std::isfinite( value.x ) || !std::isfinite( value.y ) || !std::isfinite( value.z ) )
    {
        return ZERO_VECTOR;
    }

    const float limitSq = limit * limit;
    const float magSq = Dot( value, value );

    if ( magSq <= limitSq || magSq <= TOLERANCE )
    {
        return value;
    }

    return value * ( limit / sqrtf( magSq ) );
}

void ClampRagdollBodyVelocity( PhysicsBodyHotState& hot )
{
    hot.linearVelocity = ClampVectorMagnitude( hot.linearVelocity, RAGDOLL_JOINT_MAX_LINEAR_SPEED );
    hot.angularVelocity = ClampVectorMagnitude( hot.angularVelocity, RAGDOLL_JOINT_MAX_ANGULAR_SPEED );
}

void ApplyConstraintImpulse( PhysicsBodyRecord& a, PhysicsBodyRecord& b, PhysicsBodyHotState& hotA,
                             PhysicsBodyHotState& hotB, const Vector3& rA, const Vector3& rB, const Vector3& impulse,
                             float invMassA, float invMassB )
{

    if ( invMassA > 0.0f )
    {
        hotA.linearVelocity += impulse * invMassA;
        hotA.angularVelocity += ApplyRecordInvInertia( a, hotA, CrossProduct( rA, impulse ) );
    }

    if ( invMassB > 0.0f )
    {
        hotB.linearVelocity -= impulse * invMassB;
        hotB.angularVelocity -= ApplyRecordInvInertia( b, hotB, CrossProduct( rB, impulse ) );
    }

    if ( invMassA > 0.0f )
    {
        ClampRagdollBodyVelocity( hotA );
    }

    if ( invMassB > 0.0f )
    {
        ClampRagdollBodyVelocity( hotB );
    }
}


bool IsBodySleeping( int bodyIndex, std::span<const uint8_t> sleepState )
{
    return bodyIndex >= 0 && bodyIndex < static_cast<int>( sleepState.size() ) && sleepState[bodyIndex] != 0;
}


bool ApplyNeckSwingLimits( PhysicsBodyStore& bodyStore, std::span<const PointJointConstraint> constraints,
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

        // Invariant: this shared spelling preserves the former std::clamp
        // bounds exactly; ordinary solver inputs must remain byte-identical.
        const float dot = SkullbonezCore::Math::ClampUnit( Dot( headUp, torsoUp ) );

        if ( dot >= RAGDOLL_NECK_MAX_SWING_COSINE )
        {
            continue;
        }

        Vector3 correctionAxis = CrossProduct( headUp, torsoUp );

        if ( VectorMag( correctionAxis ) <= TOLERANCE )
        {
            correctionAxis = torsoRot * Vector3( 1.0f, 0.0f, 0.0f );
        }

        correctionAxis.Normalise();

        const float correctionAngle = (std::min)( acosf( dot ) - RAGDOLL_NECK_MAX_SWING_RADIANS,
                                                  RAGDOLL_NECK_MAX_CORRECTION_RADIANS );

        Quaternion orientation = headHot.orientation;
        orientation.RotateAboutAxis( correctionAxis, correctionAngle );
        headHot.orientation = orientation;
        headHot.angularVelocity = headHot.angularVelocity * RAGDOLL_NECK_ANGULAR_DAMPING;
        StorePhysicsBodyHotState( hotFields, headIndex, headHot );
        changed = true;
    }

    return changed;
}

} // namespace

int PointJointConstraint::BodyAIndex( const PhysicsBodyStore& bodyStore ) const
{
    return bodyStore.ModelIndexForHandle( bodyA );
}


int PointJointConstraint::BodyBIndex( const PhysicsBodyStore& bodyStore ) const
{
    return bodyStore.ModelIndexForHandle( bodyB );
}


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

bool Ragdoll::SolvePointJoints( PhysicsBodyStore& bodyStore, std::span<const PointJointConstraint> constraints,
                                std::span<const uint8_t> sleepState, float dt )
{

    if ( constraints.empty() || dt <= TOLERANCE )
    {
        return false;
    }

    const auto bodyRecords = bodyStore.MutableRecords();
    const PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();
    const int modelCount = bodyStore.Count();
    const float invDt = 1.0f / dt;

    for ( int iteration = 0; iteration < RAGDOLL_SOLVER_ITERATIONS; ++iteration )
    {

        for ( const PointJointConstraint& constraint : constraints )
        {
            const int bodyAIndex = constraint.BodyAIndex( bodyStore );
            const int bodyBIndex = constraint.BodyBIndex( bodyStore );

            if ( bodyAIndex < 0 || bodyBIndex < 0 || bodyAIndex >= modelCount || bodyBIndex >= modelCount )
            {
                continue;
            }

            PhysicsBodyRecord& bodyA = bodyRecords[static_cast<size_t>( bodyAIndex )];
            PhysicsBodyRecord& bodyB = bodyRecords[static_cast<size_t>( bodyBIndex )];
            const size_t hotAIndex = static_cast<size_t>( bodyAIndex );
            const size_t hotBIndex = static_cast<size_t>( bodyBIndex );
            PhysicsBodyHotState hotA = LoadPhysicsBodyHotState( hotFields, hotAIndex );
            PhysicsBodyHotState hotB = LoadPhysicsBodyHotState( hotFields, hotBIndex );
            const bool aSleeping = bodyAIndex < static_cast<int>( sleepState.size() ) && sleepState[bodyAIndex] != 0;
            const bool bSleeping = bodyBIndex < static_cast<int>( sleepState.size() ) && sleepState[bodyBIndex] != 0;
            const float invMassA = ( hotA.fixed || aSleeping ) ? 0.0f : hotA.inverseMass;
            const float invMassB = ( hotB.fixed || bSleeping ) ? 0.0f : hotB.inverseMass;
            const float totalInvMass = invMassA + invMassB;

            if ( totalInvMass <= TOLERANCE )
            {
                continue;
            }

            const RotationMatrix rotA = BodyRotation( hotA );
            const RotationMatrix rotB = BodyRotation( hotB );
            const Vector3 rA = rotA * constraint.localAnchorA;
            const Vector3 rB = rotB * constraint.localAnchorB;
            const Vector3 anchorA = hotA.position + rA;
            const Vector3 anchorB = hotB.position + rB;
            Vector3 error = anchorB - anchorA;
            float distance = VectorMag( error );

            if ( distance <= constraint.slack && iteration > 0 )
            {
                continue;
            }

            Vector3 axis( 1.0f, 0.0f, 0.0f );

            if ( distance > TOLERANCE )
            {
                axis = error / distance;
            }

            const Vector3 velA = hotA.linearVelocity + CrossProduct( hotA.angularVelocity, rA );
            const Vector3 velB = hotB.linearVelocity + CrossProduct( hotB.angularVelocity, rB );
            const float relVel = Dot( ( velB - velA ), axis );
            const float distanceError = (std::max)( 0.0f, distance - constraint.slack );
            const float biasSpeed = std::clamp( distanceError * constraint.stiffness * invDt, 0.0f,
                                                RAGDOLL_JOINT_MAX_BIAS_SPEED );

            const float velocityTarget = std::clamp( ( relVel + biasSpeed ) * ( 1.0f + constraint.damping ),
                                                     -RAGDOLL_JOINT_MAX_BIAS_SPEED, RAGDOLL_JOINT_MAX_BIAS_SPEED );

            const float effectiveMass = ContactSolver::ComputeTwoBodyEffectiveMass( invMassA, invMassB, axis, rA, rB, [&]( const Vector3& v )
                                                                                    { return invMassA > 0.0f ? ApplyRecordInvInertia( bodyA, hotA, v ) : ZERO_VECTOR; }, [&]( const Vector3& v )
                                                                                    { return invMassB > 0.0f ? ApplyRecordInvInertia( bodyB, hotB, v ) : ZERO_VECTOR; } );

            if ( effectiveMass > 0.0f )
            {
                ApplyConstraintImpulse( bodyA, bodyB, hotA, hotB, rA, rB, axis * ( effectiveMass * velocityTarget ),
                                        invMassA, invMassB );
            }

            if ( distanceError > TOLERANCE )
            {
                const float correctionAmount = (std::min)( distanceError * constraint.stiffness,
                                                           RAGDOLL_JOINT_MAX_POSITION_CORRECTION );

                const Vector3 correction = axis * ( correctionAmount / totalInvMass );

                if ( invMassA > 0.0f )
                {
                    hotA.position += correction * invMassA;
                }

                if ( invMassB > 0.0f )
                {
                    hotB.position -= correction * invMassB;
                }
            }

            StorePhysicsBodyHotState( hotFields, hotAIndex, hotA );
            StorePhysicsBodyHotState( hotFields, hotBIndex, hotB );
        }
    }

    (void)ApplyNeckSwingLimits( bodyStore, constraints, sleepState );
    return true;
}
