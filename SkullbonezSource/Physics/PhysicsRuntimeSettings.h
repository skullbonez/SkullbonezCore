/*
File: SkullbonezSource/Physics/PhysicsRuntimeSettings.h
Purpose:
  Defines the Physics-owned immutable value snapshot consumed by fixed-step work.

Summary:
  Process configuration is converted once at the PhysicsEngine boundary. The
  solver and its stages then borrow these plain values without reaching back
  into Core configuration types during a fixed tick.

Glossary:
  Stamp boundary: Cold ApplyRuntimeConfig operation that replaces the snapshot
    before authored values or fixed-step work consume it.

Invariants:
  - Defaults mirror the authored process-configuration defaults so a newly
    constructed PhysicsEngine remains deterministic before its first stamp.
  - The snapshot contains values only; it owns no services, callbacks, dynamic
    storage, or reference back to Core configuration.
  - Fixed-step stages may borrow this snapshot synchronously but never retain
    pointers into caller-owned process configuration.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/Physics/PhysicsEngine.cpp
*/
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace SkullbonezCore
{
namespace Physics
{
enum class InteractivePhysicsSetting
{
    None = -1,
    Iterations,
    Slop,
    Bias,
    PositionCorrection,
    TerrainSlop,
    TerrainBias,
    TerrainMaxBias,
    SpinFriction,
    RestitutionThreshold,
    SleepLinear,
    SleepAngular,
    SleepFrames,
    WarmStart,
    Count
};
struct InteractivePhysicsSettingRange
{
    float minimum, maximum;
    bool integer;
};
inline constexpr std::array<InteractivePhysicsSettingRange, 13> INTERACTIVE_PHYSICS_RANGES { { { 1, 32, true },
                                                                                               { 0, .1f, false },
                                                                                               { 0, 1, false },
                                                                                               { 0, 1, false },
                                                                                               { 0, .1f, false },
                                                                                               { 0, 1, false },
                                                                                               { 0, 20, false },
                                                                                               { 0, 2, false },
                                                                                               { 0, 20, false },
                                                                                               { 0, 5, false },
                                                                                               { 0, 5, false },
                                                                                               { 1, 600, true },
                                                                                               { 0, 1, true } } };
inline bool NormalizeInteractivePhysicsSetting( InteractivePhysicsSetting setting, float requested, float& normalized ) noexcept
{
    const int index = static_cast<int>( setting );
    if ( index < 0 || index >= static_cast<int>( INTERACTIVE_PHYSICS_RANGES.size() ) || !std::isfinite( requested ) )
    {
        return false;
    }
    const auto range = INTERACTIVE_PHYSICS_RANGES[static_cast<std::size_t>( index )];
    normalized = std::clamp( requested, range.minimum, range.maximum );
    if ( range.integer )
    {
        normalized = std::round( normalized );
    }
    return true;
}

struct PhysicsMaterialSettings
{
    float sphereDragCoefficient = 0.4f;
    float terrainFrictionCoefficient = 0.1f;
    float objectFrictionCoefficient = 0.1f;
    float rollingFrictionCoefficient = 0.02f;
    float spinFrictionCoefficient = 0.3f; // Effective contact-patch length; angular impulse = length * normal impulse.
};

struct BodySimulationSettings
{
    float angularVelocityLimit = 5.0f;
    float contactRestitutionThreshold = 2.0f;
    float contactEpsilon = 0.05f;
};

struct ContactSolverSettings
{
    float slop = 0.005f;
    float baumgarteBeta = 0.2f;
    float positionCorrectionPercent = 0.35f;
    int iterations = 12;
    bool warmStart = true;
};

struct TerrainContactSettings
{
    float threshold = 0.15f;
    float slop = 0.005f;
    float baumgarteBeta = 0.3f;
    float maxBaumgarteBias = 2.0f;
};

struct SleepSettings
{
    float linearSpeed = 0.5f;
    float angularSpeed = 0.3f;
    int frames = 30;
};

struct BroadphaseSettings
{
    float cellSize = 24.0f;
};

struct PhysicsExecutionSettings
{
    bool parallel = true;
    bool parallelApplyForces = true;
    bool parallelMutualGravity = true;
    bool parallelNarrowphase = false;
    bool parallelTerrainDetect = true;
    bool parallelIntegrate = true;
};

struct WorldForceSettings
{
    float gravity = -30.0f;
};

struct PhysicsRuntimeSettings
{
    PhysicsMaterialSettings material;
    BodySimulationSettings body;
    ContactSolverSettings solver;
    TerrainContactSettings terrain;
    SleepSettings sleep;
    BroadphaseSettings broadphase;
    PhysicsExecutionSettings execution;
    WorldForceSettings worldForces;
};
inline std::array<float, 13> InteractivePhysicsValues( const PhysicsRuntimeSettings& settings ) noexcept
{
    return { static_cast<float>( settings.solver.iterations ),
             settings.solver.slop,
             settings.solver.baumgarteBeta,
             settings.solver.positionCorrectionPercent,
             settings.terrain.slop,
             settings.terrain.baumgarteBeta,
             settings.terrain.maxBaumgarteBias,
             settings.material.spinFrictionCoefficient,
             settings.body.contactRestitutionThreshold,
             settings.sleep.linearSpeed,
             settings.sleep.angularSpeed,
             static_cast<float>( settings.sleep.frames ),
             settings.solver.warmStart ? 1.0f : 0.0f };
}
// Fixed wire order for solver snapshot v10. Explicit scalars exclude padding
// and preserve effective per-world settings when replay resumes on another run.
using PhysicsSettingsPacket = std::array<float, 28>;
inline PhysicsSettingsPacket EncodePhysicsSettings( const PhysicsRuntimeSettings& settings ) noexcept
{
    return { settings.material.sphereDragCoefficient,
             settings.material.terrainFrictionCoefficient,
             settings.material.objectFrictionCoefficient,
             settings.material.rollingFrictionCoefficient,
             settings.material.spinFrictionCoefficient,
             settings.body.angularVelocityLimit,
             settings.body.contactRestitutionThreshold,
             settings.body.contactEpsilon,
             settings.solver.slop,
             settings.solver.baumgarteBeta,
             settings.solver.positionCorrectionPercent,
             static_cast<float>( settings.solver.iterations ),
             settings.solver.warmStart ? 1.0f : 0.0f,
             settings.terrain.threshold,
             settings.terrain.slop,
             settings.terrain.baumgarteBeta,
             settings.terrain.maxBaumgarteBias,
             settings.sleep.linearSpeed,
             settings.sleep.angularSpeed,
             static_cast<float>( settings.sleep.frames ),
             settings.broadphase.cellSize,
             settings.execution.parallel ? 1.0f : 0.0f,
             settings.execution.parallelApplyForces ? 1.0f : 0.0f,
             settings.execution.parallelMutualGravity ? 1.0f : 0.0f,
             settings.execution.parallelNarrowphase ? 1.0f : 0.0f,
             settings.execution.parallelTerrainDetect ? 1.0f : 0.0f,
             settings.execution.parallelIntegrate ? 1.0f : 0.0f,
             settings.worldForces.gravity, };
}
inline bool DecodePhysicsSettings( const PhysicsSettingsPacket& values, PhysicsRuntimeSettings& settings ) noexcept
{
    for ( float value : values )
    {
        if ( !std::isfinite( value ) )
        {
            return false;
        }
    }
    for ( int index : { 12, 21, 22, 23, 24, 25, 26 } )
    {
        if ( values[index] != 0 && values[index] != 1 )
        {
            return false;
        }
    }
    // The retained format accepts the full startup-config range, including
    // zero sleep ticks. Interactive sliders intentionally use narrower limits.
    for ( std::size_t index = 0; index <= 20; ++index )
    {
        if ( values[index] < 0 || values[index] > 1000000 )
        {
            return false;
        }
    }
    for ( int index : { 9, 10, 15 } )
    {
        if ( values[index] > 1 )
        {
            return false;
        }
    }
    for ( int index : { 11, 19 } )
    {
        if ( values[index] != std::floor( values[index] ) )
        {
            return false;
        }
    }
    if ( values[11] < 1 || values[20] < .0001f || std::abs( values[27] ) > 1000000 )
    {
        return false;
    }
    settings.material.sphereDragCoefficient = values[0];
    settings.material.terrainFrictionCoefficient = values[1];
    settings.material.objectFrictionCoefficient = values[2];
    settings.material.rollingFrictionCoefficient = values[3];
    settings.material.spinFrictionCoefficient = values[4];
    settings.body.angularVelocityLimit = values[5];
    settings.body.contactRestitutionThreshold = values[6];
    settings.body.contactEpsilon = values[7];
    settings.solver.slop = values[8];
    settings.solver.baumgarteBeta = values[9];
    settings.solver.positionCorrectionPercent = values[10];
    settings.solver.iterations = static_cast<int>( values[11] );
    settings.solver.warmStart = values[12] != 0;
    settings.terrain.threshold = values[13];
    settings.terrain.slop = values[14];
    settings.terrain.baumgarteBeta = values[15];
    settings.terrain.maxBaumgarteBias = values[16];
    settings.sleep.linearSpeed = values[17];
    settings.sleep.angularSpeed = values[18];
    settings.sleep.frames = static_cast<int>( values[19] );
    settings.broadphase.cellSize = values[20];
    settings.execution.parallel = values[21] != 0;
    settings.execution.parallelApplyForces = values[22] != 0;
    settings.execution.parallelMutualGravity = values[23] != 0;
    settings.execution.parallelNarrowphase = values[24] != 0;
    settings.execution.parallelTerrainDetect = values[25] != 0;
    settings.execution.parallelIntegrate = values[26] != 0;
    settings.worldForces.gravity = values[27];
    return true;
}
} // namespace Physics
} // namespace SkullbonezCore
