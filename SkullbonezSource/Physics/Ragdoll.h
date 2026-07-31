/*
File: SkullbonezSource/Physics/Ragdoll.h
Purpose:
  Describes simple ragdoll prefab geometry and solves point-joint constraints.

Summary:
  This is deliberately a small bridge toward a future generic constraint
  system. Ragdoll prefab data is value metadata; scene/authored owners decide
  how those values become renderable objects while physics only keeps handle-
  keyed point-joint descriptors and solver math.

Glossary:
  Preview lines: Editor-only visualization geometry for placement feedback.
  Part display name: Deterministic `<prefix>_<suffix>` identity shared by
    parser collision checks and runtime construction.

Invariants:
  - Constraint order is deterministic and scene-authored.
  - Constraint bodies refer to PhysicsBodyHandle values; only owners with a live
    PhysicsBodyStore may resolve those handles to current model-order rows.
  - The joint solver mutates PhysicsBodyStore records only; later presentation
    mirrors are owner-side side effects, not ragdoll solver state.
  - Every simple-ragdoll part name is preflighted against the engine's 64-byte
    display-name field before the first part is appended.

Related:
  - SkullbonezSource/Physics/Ragdoll.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "PhysicsHandles.h"
#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Physics
{
class PhysicsBodyStore;

struct RagdollPartDesc
{
    const char* suffix;
    Math::Vector::Vector3 localCenter = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 halfExtents = Math::Vector::ZERO_VECTOR;
    float restitution = 0.0f;
    float tintR = 1.0f;
    float tintG = 1.0f;
    float tintB = 1.0f;
};

struct RagdollJointDesc
{
    int bodyA = 0;
    int bodyB = 0;
    Math::Vector::Vector3 localAnchorA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localAnchorB = Math::Vector::ZERO_VECTOR;
    float slack = 0.25f;
    uint8_t flags = 0;
};

struct PointJointConstraint
{
    static constexpr uint8_t FLAG_LIMIT_NECK_SWING = 1u << 0;

    PhysicsConstraintHandle handle;          // Stable identity; dense solver-row movement never retargets it.
    PhysicsBodyHandle bodyA;
    PhysicsBodyHandle bodyB;
    Math::Vector::Vector3 localAnchorA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localAnchorB = Math::Vector::ZERO_VECTOR;
    float slack = 0.25f;
    float stiffness = 0.22f;
    float damping = 0.35f;
    uint32_t groupId = 0;
    uint8_t flags = 0;

    void SetBodies( PhysicsBodyHandle bodyAHandle, PhysicsBodyHandle bodyBHandle )
    {
        bodyA = bodyAHandle;
        bodyB = bodyBHandle;
    }

    int BodyAIndex( const PhysicsBodyStore& bodyStore ) const;
    int BodyBIndex( const PhysicsBodyStore& bodyStore ) const;

    bool HasValidBodies() const
    {
        return bodyA.IsValid() && bodyB.IsValid() && bodyA != bodyB;
    }
};

struct RagdollBuildOptions
{
    const char* namePrefix = "ragdoll";
    Math::Vector::Vector3 terrainPoint = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    float scale = 1.0f;
    bool fixed = false;
    bool startsAsleep = false;
    PhysicsSceneObjectId firstSceneObjectId; // First id in caller-owned contiguous body range.
};

class Ragdoll
{
  public:
    static constexpr int SIMPLE_PART_COUNT = 10;
    static inline constexpr const char* SIMPLE_PART_SUFFIXES[SIMPLE_PART_COUNT] = {
        "torso",       "head",        "upper_arm_l", "lower_arm_l", "upper_arm_r",
        "lower_arm_r", "upper_leg_l", "lower_leg_l", "upper_leg_r", "lower_leg_r",
    };

    static bool TryBuildSimplePartName( const char* prefix, int partIndex, char ( &outName )[64] )
    {

        if ( partIndex < 0 || partIndex >= SIMPLE_PART_COUNT )
        {
            outName[0] = '\0';
            return false;
        }

        const char* resolvedPrefix = prefix && prefix[0] ? prefix : "ragdoll";
        const char* suffix = SIMPLE_PART_SUFFIXES[partIndex];
        const std::size_t prefixLength = std::strlen( resolvedPrefix );
        const std::size_t suffixLength = std::strlen( suffix );

        if ( prefixLength + 1u + suffixLength >= sizeof( outName ) )
        {
            outName[0] = '\0';
            return false;
        }

        // Invariant: both lengths were bounded against the fixed destination,
        // so these copies cannot overrun or silently truncate; the scene owner
        // remains responsible for rejecting duplicate complete names.
        std::memcpy( outName, resolvedPrefix, prefixLength );
        outName[prefixLength] = '_';
        std::memcpy( outName + prefixLength + 1u, suffix, suffixLength );
        outName[prefixLength + 1u + suffixLength] = '\0';
        return true;
    }

    static float ClampScale( float scale );
    static float SurfaceEpsilon();
    static const RagdollPartDesc* SimpleParts();
    static const RagdollJointDesc* SimpleJoints( int& outCount );
    static Math::Vector::Vector3 DefaultPreviewCenter( const Math::Vector::Vector3& terrainPoint, float scale,
                                                       const Math::Orientation::Quaternion& orientation );
    static void AddPreviewLines( std::vector<float>& lineData, const Math::Vector::Vector3& terrainPoint, float scale,
                                 const Math::Orientation::Quaternion& orientation, float r, float g, float b );
    static bool SolvePointJoints( PhysicsBodyStore& bodyStore, std::span<const PointJointConstraint> constraints,
                                  std::span<const uint8_t> sleepState, float dt );
};
} // namespace Physics
} // namespace SkullbonezCore
