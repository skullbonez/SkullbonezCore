/*
File: SkullbonezSource/Physics/PhysicsRuntimeSettings.h
Purpose:
  Defines the Physics-owned immutable value snapshot consumed by fixed-step work.

Summary:
  Process configuration is converted once at the PhysicsEngine boundary. The
  solver and its stages then borrow these plain values without reaching back
  into Core configuration types during a fixed tick.

Glossary:
  Runtime settings snapshot: Physics-owned copy of every process-configured
    scalar or switch that can affect deterministic simulation.
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
  - SkullbonezSource/Physics/PhysicsEngine.cpp
*/
#pragma once

namespace SkullbonezCore
{
namespace Physics
{
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
} // namespace Physics
} // namespace SkullbonezCore
