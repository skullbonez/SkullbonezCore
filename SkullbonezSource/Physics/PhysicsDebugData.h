/*
File: SkullbonezSource/Physics/PhysicsDebugData.h
Purpose:
  Names the physics-owned debug records emitted by solver and diagnostics code.

Summary:
  Solver diagnostics are plain data snapshots. Physics writes these records
  during deterministic fixed-step work, while runtime/rendering code decides
  later whether and how to visualize them.

Invariants:
  - These records must stay render-API-free so the physics library can emit
    diagnostics without depending on debug overlay ownership.
  - Field order and units are validation-sensitive because probes, replay, and
    SkullScope summaries read these values.

Related:
  - SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstdint>

#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Physics
{

// Debug flags select which physics overlays are drawn. These are visualization
// layers only; toggling them must never alter collision response, sleep policy,
// or solver ordering.
enum PhysicsDebugFlags : uint32_t
{
    PHYSICS_DEBUG_NONE = 0u,
    PHYSICS_DEBUG_AXES = 1u << 0,
    PHYSICS_DEBUG_CONTACTS = 1u << 1,
    PHYSICS_DEBUG_SLEEP = 1u << 2,
    PHYSICS_DEBUG_PIPELINE = 1u << 3,
    PHYSICS_DEBUG_TERRAIN_CONTACT = 1u << 4,
    PHYSICS_DEBUG_ALL = PHYSICS_DEBUG_AXES | PHYSICS_DEBUG_CONTACTS | PHYSICS_DEBUG_SLEEP | PHYSICS_DEBUG_PIPELINE |
                        PHYSICS_DEBUG_TERRAIN_CONTACT,
};

enum class PhysicsPipelineStage : uint8_t
{

    // Ordered list of major physics pipeline events recorded during a tick.
    // Runtime overlays can show one stage at a time so a reader can inspect the
    // broadphase, manifold, warm-start, solve, writeback, and sleep decisions.
    BroadphaseCandidate,
    SleepPrunedPair,
    WakeDecision,
    SweptObjectHit,
    SweptObjectMiss,
    TerrainHit,
    TerrainManifold,
    TerrainRow,
    ManifoldRow,
    WarmStart,
    SolverIteration,
    VelocityWriteback,
    PositionCorrection,
    CacheStore,
    SleepSupportEdge,
    SleepIslandDecision,
    Count
};

struct PhysicsPipelineRecord
{

    // One compact breadcrumb from a physics tick. scalarA/B/C intentionally mean
    // different things per stage; see emit sites for exact meaning. Keeping the
    // payload small makes debug drawing and SkullScope summaries cheap.
    PhysicsPipelineStage stage = PhysicsPipelineStage::BroadphaseCandidate;
    int bodyA = -1;
    int bodyB = -1;
    int iteration = -1;
    uint32_t featureId = 0;
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    float scalarA = 0.0f;
    float scalarB = 0.0f;
    float scalarC = 0.0f;
};

// Caller contract: this is a stable diagnostics label for runtime overlays,
// replay, and SkullScope output. It must not allocate or depend on debug
// visualizer state.
inline const char* PhysicsPipelineStageName( PhysicsPipelineStage stage )
{

    switch ( stage )
    {
    case PhysicsPipelineStage::BroadphaseCandidate:
        return "broadphase_candidate";
    case PhysicsPipelineStage::SleepPrunedPair:
        return "sleep_pruned_pair";
    case PhysicsPipelineStage::WakeDecision:
        return "wake_decision";
    case PhysicsPipelineStage::SweptObjectHit:
        return "swept_object_hit";
    case PhysicsPipelineStage::SweptObjectMiss:
        return "swept_object_miss";
    case PhysicsPipelineStage::TerrainHit:
        return "terrain_hit";
    case PhysicsPipelineStage::TerrainManifold:
        return "terrain_manifold";
    case PhysicsPipelineStage::TerrainRow:
        return "terrain_row";
    case PhysicsPipelineStage::ManifoldRow:
        return "manifold_row";
    case PhysicsPipelineStage::WarmStart:
        return "warm_start";
    case PhysicsPipelineStage::SolverIteration:
        return "solver_iteration";
    case PhysicsPipelineStage::VelocityWriteback:
        return "velocity_writeback";
    case PhysicsPipelineStage::PositionCorrection:
        return "position_correction";
    case PhysicsPipelineStage::CacheStore:
        return "cache_store";
    case PhysicsPipelineStage::SleepSupportEdge:
        return "sleep_support_edge";
    case PhysicsPipelineStage::SleepIslandDecision:
        return "sleep_island_decision";
    default:
        return "unknown";
    }
}

struct PhysicsDebugContact
{

    // Solver contact row captured for drawing and presentation sinks. The
    // pre-solve speeds are measured before the row applies impulses, so
    // diagnostics distinguish real relative motion from support-force transfer.
    int bodyA = -1;
    int bodyB = -1;
    uint32_t featureId = 0;
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent1 = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent2 = Math::Vector::ZERO_VECTOR;
    float penetration = 0.0f;
    float normalImpulse = 0.0f;

    // Velocity target attributable only to overlap separation. Restitution
    // leaves this zero so energy audits cannot disguise bounce as repair work.
    float separationBias = 0.0f;
    float preSolveNormalSpeed = 0.0f;
    float preSolveClosingSpeed = 0.0f;
    float preSolveSlipSpeed = 0.0f;
};
} // namespace Physics
} // namespace SkullbonezCore
