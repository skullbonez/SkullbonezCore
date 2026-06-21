/*
File: SkullbonezSource/PhysicsDebugVisualizer.h
Purpose:
  Draws physics contacts, axes, sleep state, and pipeline diagnostics.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  SkullScope: Queryable physics diagnostics workflow backed by bounded trace
  output and local queries.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/PhysicsDebugVisualizer.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>
#include <vector>
#include "Matrix4.h"
#include "Vector3.h"

namespace SkullbonezCore
{
namespace Geometry
{
class Terrain;
}

namespace GameObjects
{
class GameModelCollection;
}

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
    // The visualizer can show one stage at a time so a reader can inspect the
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

const char* PhysicsPipelineStageName( PhysicsPipelineStage stage );

struct PhysicsDebugContact
{
    // Solver contact row captured for drawing: point, push direction, friction
    // directions, overlap depth, and the normal impulse that was accumulated.
    int bodyA = -1;
    int bodyB = -1;
    uint32_t featureId = 0;
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent1 = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent2 = Math::Vector::ZERO_VECTOR;
    float penetration = 0.0f;
    float normalImpulse = 0.0f;
};

class PhysicsDebugVisualizer
{
  private:
    struct TrackedContact
    {
        // Contact visuals linger briefly after the solver row disappears so a
        // human can actually see a one-frame impact. This is display-only state.
        PhysicsDebugContact contact;
        float remainingSeconds = 0.0f;
        float lifetimeSeconds = 0.0f;
    };

    uint32_t m_flags = PHYSICS_DEBUG_NONE;
    int m_pipelineStageCursor = 0;
    float m_contactLingerSeconds = 0.45f;
    std::vector<float> m_lineData;
    std::vector<TrackedContact> m_trackedContacts;

    TrackedContact* FindTrackedContact( const PhysicsDebugContact& contact );
    float ContactFade( const TrackedContact& contact ) const;
    void EmitLine( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitCross( const Math::Vector::Vector3& p, float size, float r, float g, float bl );
    void EmitArrow( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitRingXZ( const Math::Vector::Vector3& center, float radius, float yOffset, float r, float g, float bl );
    void EmitObjectAxes( GameObjects::GameModelCollection& models );
    void EmitConvexHullWireframes( GameObjects::GameModelCollection& models );
    void EmitContacts( GameObjects::GameModelCollection& models );
    void EmitSleepState( GameObjects::GameModelCollection& models );
    void EmitPipelineStage( GameObjects::GameModelCollection& models );
    void EmitTerrainContactProbe( GameObjects::GameModelCollection& models, Geometry::Terrain* terrain );

  public:
    void SetFlags( uint32_t flags )
    {
        m_flags = flags & PHYSICS_DEBUG_ALL;
        if ( ( m_flags & PHYSICS_DEBUG_CONTACTS ) == 0 )
        {
            m_trackedContacts.clear();
        }
    }
    uint32_t GetFlags() const
    {
        return m_flags;
    }
    bool IsEnabled() const
    {
        return m_flags != PHYSICS_DEBUG_NONE;
    }
    void SetContactLingerSeconds( float seconds );
    void SetPipelineStageCursor( int cursor );
    void Update( float dt, GameObjects::GameModelCollection& models );
    void Render( GameObjects::GameModelCollection& models,
                 const Math::Transformation::Matrix4& viewProj,
                 Geometry::Terrain* terrain = nullptr );
};
} // namespace Physics
} // namespace SkullbonezCore
