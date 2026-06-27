/*
File: SkullbonezSource/Physics/Ragdoll.cpp
Purpose:
  Builds simple humanoid ragdolls and solves their first point-joint constraints.

Mental model:
  The body layout is prefab code; the joint rows are intentionally generic
  point-to-point constraint data. This keeps the hacky ragdoll feature isolated
  and leaves a clear migration path to a full constraint solver.

Glossary:
  Point joint: Constraint that keeps two local anchors near each other.
  Slack: Allowed anchor separation before the solver pushes the bodies back
  toward the constraint.
  Neck swing limit: Special angular clamp applied to the head/torso joint.

Invariants:
  - Body and constraint creation order must stay deterministic.
  - Constraint solving must not allocate per row while physics is stepping.

Related:
  - SkullbonezSource/Physics/Ragdoll.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#include "Ragdoll.h"

#include "../GameObjects/GameModel.h"
#include "../GameObjects/GameModelCollection.h"
#include "ContactSolverCommon.h"
#include "PhysicsBodyStore.h"
#include "PhysicsEngine.h"
#include "PhysicsMass.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::GameObjects;
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

struct SimplePartDef
{
    const char* suffix;
    Vector3 localCenter;
    Vector3 halfExtents;
    float restitution;
    float tintR;
    float tintG;
    float tintB;
};

struct SimpleJointDef
{
    int bodyA;
    int bodyB;
    Vector3 localAnchorA;
    Vector3 localAnchorB;
    float slack;
    uint8_t flags;
};

Vector3 ScaleVector( const Vector3& value, float scale )
{
    return Vector3( value.x * scale, value.y * scale, value.z * scale );
}

float ClampRagdollScale( float scale )
{
    if ( !std::isfinite( scale ) )
    {
        return RAGDOLL_DEFAULT_SCALE;
    }
    return std::clamp( scale, RAGDOLL_MIN_SCALE, RAGDOLL_MAX_SCALE );
}

const SimplePartDef* SimpleParts()
{
    // Invariant: this table order is the prefab body index order and is paired
    // with SimpleJoints plus SIMPLE_PART_COUNT.
    static const SimplePartDef parts[PART_COUNT] = {
        { "torso", Vector3( 0.0f, 12.8f, 0.0f ), Vector3( 2.2f, 3.2f, 1.1f ), 0.18f, 0.62f, 0.72f, 1.0f },
        { "head", Vector3( 0.0f, 17.25f, 0.0f ), Vector3( 1.2f, 1.2f, 1.2f ), 0.15f, 0.95f, 0.82f, 0.58f },
        { "upper_arm_l", Vector3( -3.0f, 13.8f, 0.0f ), Vector3( 0.65f, 2.2f, 0.65f ), 0.14f, 0.42f, 0.50f, 0.90f },
        { "lower_arm_l", Vector3( -3.0f, 9.4f, 0.0f ), Vector3( 0.58f, 2.2f, 0.58f ), 0.14f, 0.42f, 0.50f, 0.90f },
        { "upper_arm_r", Vector3( 3.0f, 13.8f, 0.0f ), Vector3( 0.65f, 2.2f, 0.65f ), 0.14f, 0.42f, 0.50f, 0.90f },
        { "lower_arm_r", Vector3( 3.0f, 9.4f, 0.0f ), Vector3( 0.58f, 2.2f, 0.58f ), 0.14f, 0.42f, 0.50f, 0.90f },
        { "upper_leg_l", Vector3( -0.85f, 7.2f, 0.0f ), Vector3( 0.8f, 2.4f, 0.75f ), 0.12f, 0.36f, 0.42f, 0.80f },
        { "lower_leg_l", Vector3( -0.85f, 2.4f, 0.0f ), Vector3( 0.72f, 2.4f, 0.72f ), 0.12f, 0.36f, 0.42f, 0.80f },
        { "upper_leg_r", Vector3( 0.85f, 7.2f, 0.0f ), Vector3( 0.8f, 2.4f, 0.75f ), 0.12f, 0.36f, 0.42f, 0.80f },
        { "lower_leg_r", Vector3( 0.85f, 2.4f, 0.0f ), Vector3( 0.72f, 2.4f, 0.72f ), 0.12f, 0.36f, 0.42f, 0.80f },
    };
    return parts;
}

const SimpleJointDef* SimpleJoints( int& outCount )
{
    static const SimpleJointDef joints[] = {
        { PART_TORSO,
          PART_HEAD,
          Vector3( 0.0f, 3.2f, 0.0f ),
          Vector3( 0.0f, -1.2f, 0.0f ),
          0.28f,
          PointJointConstraint::FLAG_LIMIT_NECK_SWING },
        { PART_TORSO, PART_LEFT_UPPER_ARM, Vector3( -2.2f, 2.25f, 0.0f ), Vector3( 0.0f, 2.2f, 0.0f ), 0.35f, 0 },
        { PART_LEFT_UPPER_ARM,
          PART_LEFT_LOWER_ARM,
          Vector3( 0.0f, -2.2f, 0.0f ),
          Vector3( 0.0f, 2.2f, 0.0f ),
          0.30f,
          0 },
        { PART_TORSO, PART_RIGHT_UPPER_ARM, Vector3( 2.2f, 2.25f, 0.0f ), Vector3( 0.0f, 2.2f, 0.0f ), 0.35f, 0 },
        { PART_RIGHT_UPPER_ARM,
          PART_RIGHT_LOWER_ARM,
          Vector3( 0.0f, -2.2f, 0.0f ),
          Vector3( 0.0f, 2.2f, 0.0f ),
          0.30f,
          0 },
        { PART_TORSO, PART_LEFT_UPPER_LEG, Vector3( -0.85f, -3.2f, 0.0f ), Vector3( 0.0f, 2.4f, 0.0f ), 0.35f, 0 },
        { PART_LEFT_UPPER_LEG,
          PART_LEFT_LOWER_LEG,
          Vector3( 0.0f, -2.4f, 0.0f ),
          Vector3( 0.0f, 2.4f, 0.0f ),
          0.30f,
          0 },
        { PART_TORSO, PART_RIGHT_UPPER_LEG, Vector3( 0.85f, -3.2f, 0.0f ), Vector3( 0.0f, 2.4f, 0.0f ), 0.35f, 0 },
        { PART_RIGHT_UPPER_LEG,
          PART_RIGHT_LOWER_LEG,
          Vector3( 0.0f, -2.4f, 0.0f ),
          Vector3( 0.0f, 2.4f, 0.0f ),
          0.30f,
          0 },
    };
    outCount = static_cast<int>( sizeof( joints ) / sizeof( joints[0] ) );
    return joints;
}

void AppendPreviewLine( std::vector<float>& lineData, const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    lineData.insert( lineData.end(), { a.x, a.y, a.z, r, g, bl, b.x, b.y, b.z, r, g, bl } );
}

void AppendPreviewBox( std::vector<float>& lineData,
                       const Vector3& center,
                       const RotationMatrix& rotation,
                       const Vector3& halfExtents,
                       float r,
                       float g,
                       float b )
{
    const Vector3 xAxis = rotation * Vector3( halfExtents.x, 0.0f, 0.0f );
    const Vector3 yAxis = rotation * Vector3( 0.0f, halfExtents.y, 0.0f );
    const Vector3 zAxis = rotation * Vector3( 0.0f, 0.0f, halfExtents.z );
    const Vector3 corners[8] = {
        center - xAxis - yAxis - zAxis,
        center + xAxis - yAxis - zAxis,
        center + xAxis + yAxis - zAxis,
        center - xAxis + yAxis - zAxis,
        center - xAxis - yAxis + zAxis,
        center + xAxis - yAxis + zAxis,
        center + xAxis + yAxis + zAxis,
        center - xAxis + yAxis + zAxis,
    };
    constexpr int edges[12][2] = {
        { 0, 1 },
        { 1, 2 },
        { 2, 3 },
        { 3, 0 },
        { 4, 5 },
        { 5, 6 },
        { 6, 7 },
        { 7, 4 },
        { 0, 4 },
        { 1, 5 },
        { 2, 6 },
        { 3, 7 },
    };
    for ( const auto& edge : edges )
    {
        AppendPreviewLine( lineData, corners[edge[0]], corners[edge[1]], r, g, b );
    }
}

RotationMatrix BodyRotation( const PhysicsBodyRecord& record )
{
    Quaternion q = record.orientation;
    return q.GetOrientationMatrix();
}

Vector3 ApplyRecordInvInertia( const GameModel& model, const PhysicsBodyRecord& record, const Vector3& value )
{
    if ( !model.UsesWorldInertia() )
    {
        return VectorMultiply( record.invRotationalInertia, value );
    }

    const RotationMatrix rotation = BodyRotation( record );
    const Vector3 local = rotation.TransposeMultiply( value );
    return rotation * VectorMultiply( record.invRotationalInertia, local );
}

Vector3 ClampVectorMagnitude( const Vector3& value, float limit )
{
    if ( !std::isfinite( value.x ) || !std::isfinite( value.y ) || !std::isfinite( value.z ) )
    {
        return ZERO_VECTOR;
    }

    const float limitSq = limit * limit;
    const float magSq = value * value;
    if ( magSq <= limitSq || magSq <= TOLERANCE )
    {
        return value;
    }

    return value * ( limit / sqrtf( magSq ) );
}

void ClampRagdollBodyVelocity( PhysicsBodyRecord& record )
{
    record.linearVelocity = ClampVectorMagnitude( record.linearVelocity, RAGDOLL_JOINT_MAX_LINEAR_SPEED );
    record.angularVelocity = ClampVectorMagnitude( record.angularVelocity, RAGDOLL_JOINT_MAX_ANGULAR_SPEED );
}

void ApplyConstraintImpulse( const GameModel& aModel,
                             const GameModel& bModel,
                             PhysicsBodyRecord& a,
                             PhysicsBodyRecord& b,
                             const Vector3& rA,
                             const Vector3& rB,
                             const Vector3& impulse,
                             float invMassA,
                             float invMassB )
{
    if ( invMassA > 0.0f )
    {
        a.linearVelocity += impulse * invMassA;
        a.angularVelocity += ApplyRecordInvInertia( aModel, a, CrossProduct( rA, impulse ) );
    }
    if ( invMassB > 0.0f )
    {
        b.linearVelocity -= impulse * invMassB;
        b.angularVelocity -= ApplyRecordInvInertia( bModel, b, CrossProduct( rB, impulse ) );
    }
    if ( invMassA > 0.0f )
    {
        ClampRagdollBodyVelocity( a );
    }
    if ( invMassB > 0.0f )
    {
        ClampRagdollBodyVelocity( b );
    }
}


bool IsBodySleeping( int bodyIndex, const std::vector<uint8_t>& sleepState )
{
    return bodyIndex >= 0 && bodyIndex < static_cast<int>( sleepState.size() ) && sleepState[bodyIndex] != 0;
}


bool ApplyNeckSwingLimits( std::vector<GameModel>& models,
                           PhysicsBodyStore& bodyStore,
                           const std::vector<PointJointConstraint>& constraints,
                           const std::vector<uint8_t>& sleepState )
{
    const int modelCount = static_cast<int>( models.size() );
    std::vector<PhysicsBodyRecord>& bodyRecords = bodyStore.MutableRecords();
    bool changed = false;
    for ( const PointJointConstraint& constraint : constraints )
    {
        if ( ( constraint.flags & PointJointConstraint::FLAG_LIMIT_NECK_SWING ) == 0 )
        {
            continue;
        }
        if ( constraint.bodyA < 0 || constraint.bodyB < 0 || constraint.bodyA >= modelCount ||
             constraint.bodyB >= modelCount )
        {
            continue;
        }

        PhysicsBodyRecord& headRecord = bodyRecords[static_cast<size_t>( constraint.bodyB )];
        if ( headRecord.isFixed || IsBodySleeping( constraint.bodyB, sleepState ) )
        {
            continue;
        }

        const RotationMatrix torsoRot = BodyRotation( bodyRecords[static_cast<size_t>( constraint.bodyA )] );
        const RotationMatrix headRot = BodyRotation( headRecord );
        Vector3 torsoUp = torsoRot * Vector3( 0.0f, 1.0f, 0.0f );
        Vector3 headUp = headRot * Vector3( 0.0f, 1.0f, 0.0f );
        torsoUp.Normalise();
        headUp.Normalise();

        const float dot = std::clamp( headUp * torsoUp, -1.0f, 1.0f );
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

        const float correctionAngle =
            (std::min)( acosf( dot ) - RAGDOLL_NECK_MAX_SWING_RADIANS, RAGDOLL_NECK_MAX_CORRECTION_RADIANS );
        Quaternion orientation = headRecord.orientation;
        orientation.RotateAboutAxis( correctionAxis, correctionAngle );
        headRecord.orientation = orientation;
        headRecord.angularVelocity = headRecord.angularVelocity * RAGDOLL_NECK_ANGULAR_DAMPING;
        bodyStore.WriteBackToModelAt( models, constraint.bodyB );
        changed = true;
    }
    return changed;
}

} // namespace

float Ragdoll::DefaultEditorScale()
{
    return RAGDOLL_DEFAULT_SCALE;
}

Vector3 Ragdoll::DefaultPreviewCenter( const Vector3& terrainPoint, float scale, const Quaternion& orientation )
{
    const SimplePartDef* parts = SimpleParts();
    float minY = FLT_MAX;
    float maxY = -FLT_MAX;
    for ( int i = 0; i < PART_COUNT; ++i )
    {
        minY = (std::min)( minY, parts[i].localCenter.y - parts[i].halfExtents.y );
        maxY = (std::max)( maxY, parts[i].localCenter.y + parts[i].halfExtents.y );
    }

    Quaternion q = orientation;
    const RotationMatrix rotation = q.GetOrientationMatrix();
    const float clampedScale = ClampRagdollScale( scale );
    return terrainPoint + rotation * Vector3( 0.0f, ( minY + maxY ) * 0.5f * clampedScale, 0.0f );
}

void Ragdoll::AddPreviewLines( std::vector<float>& lineData,
                               const Vector3& terrainPoint,
                               float scale,
                               const Quaternion& orientation,
                               float r,
                               float g,
                               float b )
{
    const SimplePartDef* parts = SimpleParts();
    Quaternion q = orientation;
    const RotationMatrix rotation = q.GetOrientationMatrix();
    const float clampedScale = ClampRagdollScale( scale );
    const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, RAGDOLL_SURFACE_EPSILON, 0.0f );
    for ( int i = 0; i < PART_COUNT; ++i )
    {
        AppendPreviewBox( lineData,
                          base + rotation * ScaleVector( parts[i].localCenter, clampedScale ),
                          rotation,
                          ScaleVector( parts[i].halfExtents, clampedScale ),
                          r,
                          g,
                          b );
    }
}

void Ragdoll::AddSimpleHumanoid( GameModelCollection& collection,
                                 PhysicsEngine& physics,
                                 WorldEnvironment& worldEnvironment,
                                 SkullbonezCore::Geometry::Terrain* terrain,
                                 const RagdollBuildOptions& options )
{
    const int firstBody = collection.GetModelCount();
    const uint32_t groupId = static_cast<uint32_t>( firstBody + 1 );
    const float scale = ClampRagdollScale( options.scale );
    Quaternion orientation = options.orientation;
    const RotationMatrix rotation = orientation.GetOrientationMatrix();
    const Vector3 base = options.terrainPoint + rotation * Vector3( 0.0f, RAGDOLL_SURFACE_EPSILON, 0.0f );
    const char* prefix = options.namePrefix && options.namePrefix[0] ? options.namePrefix : "ragdoll";
    const SimplePartDef* parts = SimpleParts();

    for ( int i = 0; i < PART_COUNT; ++i )
    {
        const Vector3 halfExtents = ScaleVector( parts[i].halfExtents, scale );
        const float mass = CalculateBoxMass( halfExtents );
        GameModel model( &worldEnvironment,
                         base + rotation * ScaleVector( parts[i].localCenter, scale ),
                         CalculateBoxInertiaForHalfExtents( halfExtents, mass ),
                         mass );
        model.SetTerrain( terrain );
        model.SetCoefficientRestitution( parts[i].restitution );
        model.AddBoundingBox( halfExtents );
        model.SetOrientation( orientation );
        model.SetRenderTint( parts[i].tintR, parts[i].tintG, parts[i].tintB, 1.0f );
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_%s", prefix, parts[i].suffix );
        model.SetName( name );
        model.SetRuntimeCollection( GameModelCollectionKind::SimpleRagdoll, firstBody + PART_TORSO, i );
        model.SetFixed( options.fixed );

        collection.AddGameModel( std::move( model ) );
    }

    int jointCount = 0;
    const SimpleJointDef* joints = SimpleJoints( jointCount );
    for ( int i = 0; i < jointCount; ++i )
    {
        PointJointConstraint constraint;
        constraint.bodyA = firstBody + joints[i].bodyA;
        constraint.bodyB = firstBody + joints[i].bodyB;
        constraint.localAnchorA = ScaleVector( joints[i].localAnchorA, scale );
        constraint.localAnchorB = ScaleVector( joints[i].localAnchorB, scale );
        constraint.slack = joints[i].slack * scale;
        constraint.stiffness = 0.22f;
        constraint.damping = 0.35f;
        constraint.groupId = groupId;
        constraint.flags = joints[i].flags;
        physics.AddPointJointConstraint( constraint );
    }

    if ( options.startsAsleep && !options.fixed )
    {
        for ( int i = 0; i < PART_COUNT; ++i )
        {
            physics.SeedBodyAsleep( collection, firstBody + i );
        }
    }
}

void Ragdoll::SolvePointJoints( GameModelCollection& collection,
                                PhysicsBodyStore& bodyStore,
                                const std::vector<PointJointConstraint>& constraints,
                                const std::vector<uint8_t>& sleepState,
                                float dt )
{
    if ( constraints.empty() || dt <= TOLERANCE )
    {
        return;
    }

    std::vector<GameModel>& models = collection.PhysicsModels();
    std::vector<PhysicsBodyRecord>& bodyRecords = bodyStore.MutableRecords();
    const int modelCount = static_cast<int>( models.size() );
    const float invDt = 1.0f / dt;
    for ( int iteration = 0; iteration < RAGDOLL_SOLVER_ITERATIONS; ++iteration )
    {
        for ( const PointJointConstraint& constraint : constraints )
        {
            if ( constraint.bodyA < 0 || constraint.bodyB < 0 || constraint.bodyA >= modelCount ||
                 constraint.bodyB >= modelCount )
            {
                continue;
            }

            GameModel& a = models[static_cast<size_t>( constraint.bodyA )];
            GameModel& b = models[static_cast<size_t>( constraint.bodyB )];
            PhysicsBodyRecord& bodyA = bodyRecords[static_cast<size_t>( constraint.bodyA )];
            PhysicsBodyRecord& bodyB = bodyRecords[static_cast<size_t>( constraint.bodyB )];
            const bool aSleeping =
                constraint.bodyA < static_cast<int>( sleepState.size() ) && sleepState[constraint.bodyA] != 0;
            const bool bSleeping =
                constraint.bodyB < static_cast<int>( sleepState.size() ) && sleepState[constraint.bodyB] != 0;
            const float invMassA = ( bodyA.isFixed || aSleeping ) ? 0.0f : bodyA.invMass;
            const float invMassB = ( bodyB.isFixed || bSleeping ) ? 0.0f : bodyB.invMass;
            const float totalInvMass = invMassA + invMassB;
            if ( totalInvMass <= TOLERANCE )
            {
                continue;
            }

            const RotationMatrix rotA = BodyRotation( bodyA );
            const RotationMatrix rotB = BodyRotation( bodyB );
            const Vector3 rA = rotA * constraint.localAnchorA;
            const Vector3 rB = rotB * constraint.localAnchorB;
            const Vector3 anchorA = bodyA.position + rA;
            const Vector3 anchorB = bodyB.position + rB;
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

            const Vector3 velA = bodyA.linearVelocity + CrossProduct( bodyA.angularVelocity, rA );
            const Vector3 velB = bodyB.linearVelocity + CrossProduct( bodyB.angularVelocity, rB );
            const float relVel = ( velB - velA ) * axis;
            const float distanceError = (std::max)( 0.0f, distance - constraint.slack );
            const float biasSpeed =
                std::clamp( distanceError * constraint.stiffness * invDt, 0.0f, RAGDOLL_JOINT_MAX_BIAS_SPEED );
            const float velocityTarget = std::clamp( ( relVel + biasSpeed ) * ( 1.0f + constraint.damping ),
                                                     -RAGDOLL_JOINT_MAX_BIAS_SPEED,
                                                     RAGDOLL_JOINT_MAX_BIAS_SPEED );
            const float effectiveMass = ContactSolver::ComputeTwoBodyEffectiveMass(
                invMassA,
                invMassB,
                axis,
                rA,
                rB,
                [&]( const Vector3& v )
                { return invMassA > 0.0f ? ApplyRecordInvInertia( a, bodyA, v ) : ZERO_VECTOR; },
                [&]( const Vector3& v )
                { return invMassB > 0.0f ? ApplyRecordInvInertia( b, bodyB, v ) : ZERO_VECTOR; } );
            if ( effectiveMass > 0.0f )
            {
                ApplyConstraintImpulse( a,
                                        b,
                                        bodyA,
                                        bodyB,
                                        rA,
                                        rB,
                                        axis * ( effectiveMass * velocityTarget ),
                                        invMassA,
                                        invMassB );
            }

            if ( distanceError > TOLERANCE )
            {
                const float correctionAmount =
                    (std::min)( distanceError * constraint.stiffness, RAGDOLL_JOINT_MAX_POSITION_CORRECTION );
                const Vector3 correction = axis * ( correctionAmount / totalInvMass );
                if ( invMassA > 0.0f )
                {
                    bodyA.position += correction * invMassA;
                    bodyStore.WriteBackToModelAt( models, constraint.bodyA );
                }
                if ( invMassB > 0.0f )
                {
                    bodyB.position -= correction * invMassB;
                    bodyStore.WriteBackToModelAt( models, constraint.bodyB );
                }
            }
        }
    }

    ApplyNeckSwingLimits( models, bodyStore, constraints, sleepState );
    bodyStore.WriteBackToModels( models );
    collection.InvalidatePhysicsStreams();
}
