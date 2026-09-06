// Simple ragdoll prefab geometry and placement names. Physics owns registered
// point joints separately; the neck helper supplies only an angular cone policy.
#pragma once
#include "PointJointConstraint.h"

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

    // correctionCross and rawDot must come from the same normalized vector pair;
    // false means the authored cone requires no orientation mutation.
    static bool TryBuildNeckSwingCorrection( float rawDot, const Math::Vector::Vector3& correctionCross,
                                             const Math::Vector::Vector3& fallbackAxis,
                                             Math::Vector::Vector3& outCorrectionAxis, float& outCorrectionAngle ) noexcept;

    // This separate angular cone policy runs once after the shared velocity solve.
    static bool ApplyNeckSwingLimits( PhysicsBodyStore& bodyStore, std::span<const PointJointConstraint> constraints,
                                      std::span<const uint8_t> sleepState );
};
} // namespace Physics
} // namespace SkullbonezCore
