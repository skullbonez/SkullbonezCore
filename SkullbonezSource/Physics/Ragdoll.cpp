/*
File: SkullbonezSource/Physics/Ragdoll.cpp
Purpose:
  Builds simple humanoid ragdolls with named production constraints.

Mental model:
  Ragdoll owns prefab body layout. Runtime joint behavior is authored as
  PhysicsConstraintDescriptor data so humanoid joints share the production
  constraint solver with any scene-authored compatibility point joints.

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
#include "HumanoidRagdollPreset.h"
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
    HumanoidJointKind kind;
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
          HumanoidJointKind::Neck },
        { PART_TORSO,
          PART_LEFT_UPPER_ARM,
          Vector3( -2.2f, 2.25f, 0.0f ),
          Vector3( 0.0f, 2.2f, 0.0f ),
          0.35f,
          HumanoidJointKind::Shoulder },
        { PART_LEFT_UPPER_ARM,
          PART_LEFT_LOWER_ARM,
          Vector3( 0.0f, -2.2f, 0.0f ),
          Vector3( 0.0f, 2.2f, 0.0f ),
          0.30f,
          HumanoidJointKind::Elbow },
        { PART_TORSO,
          PART_RIGHT_UPPER_ARM,
          Vector3( 2.2f, 2.25f, 0.0f ),
          Vector3( 0.0f, 2.2f, 0.0f ),
          0.35f,
          HumanoidJointKind::Shoulder },
        { PART_RIGHT_UPPER_ARM,
          PART_RIGHT_LOWER_ARM,
          Vector3( 0.0f, -2.2f, 0.0f ),
          Vector3( 0.0f, 2.2f, 0.0f ),
          0.30f,
          HumanoidJointKind::Elbow },
        { PART_TORSO,
          PART_LEFT_UPPER_LEG,
          Vector3( -0.85f, -3.2f, 0.0f ),
          Vector3( 0.0f, 2.4f, 0.0f ),
          0.35f,
          HumanoidJointKind::Hip },
        { PART_LEFT_UPPER_LEG,
          PART_LEFT_LOWER_LEG,
          Vector3( 0.0f, -2.4f, 0.0f ),
          Vector3( 0.0f, 2.4f, 0.0f ),
          0.30f,
          HumanoidJointKind::Knee },
        { PART_TORSO,
          PART_RIGHT_UPPER_LEG,
          Vector3( 0.85f, -3.2f, 0.0f ),
          Vector3( 0.0f, 2.4f, 0.0f ),
          0.35f,
          HumanoidJointKind::Hip },
        { PART_RIGHT_UPPER_LEG,
          PART_RIGHT_LOWER_LEG,
          Vector3( 0.0f, -2.4f, 0.0f ),
          Vector3( 0.0f, 2.4f, 0.0f ),
          0.30f,
          HumanoidJointKind::Knee },
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
        model.SetFixed( options.fixed );

        collection.AddGameModel( std::move( model ) );
    }

    int jointCount = 0;
    const SimpleJointDef* joints = SimpleJoints( jointCount );
    const HumanoidRagdollPreset preset = HumanoidRagdollPresetFactory::Default();
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
        constraint.solverEnabled = false;
        collection.AddPointJointConstraint( constraint );
        PhysicsConstraintDescriptor descriptor = HumanoidRagdollPresetFactory::BuildJoint( joints[i].kind,
                                                                                           constraint.bodyA,
                                                                                           constraint.bodyB,
                                                                                           constraint.localAnchorA,
                                                                                           constraint.localAnchorB,
                                                                                           scale,
                                                                                           groupId,
                                                                                           preset );
        descriptor.stableId = ( groupId * 31u ) + static_cast<uint32_t>( i + 1 );
        sprintf_s( descriptor.debugName, sizeof( descriptor.debugName ), "humanoid_joint_%u_%02d", groupId, i );
        collection.AddPhysicsConstraint( descriptor );
    }

    if ( options.startsAsleep && !options.fixed )
    {
        for ( int i = 0; i < PART_COUNT; ++i )
        {
            collection.SeedModelAsleep( firstBody + i );
        }
    }
}
