/*
File: SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.h
Purpose:
  Draws physics contacts, axes, sleep state, and pipeline diagnostics.

Summary:
  PhysicsDebugVisualizer.h draws physics contacts, axes, sleep state, and
  pipeline diagnostics. As a public header, keep edits anchored on
  deterministic physics, diagnostics, or world-state flow and on the
  glossary/invariants below.

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
  - SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.cpp
  - SkullbonezSource/Physics/PhysicsDebugData.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <span>
#include <vector>
#include "../../Maths/Matrix4.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsDebugData.h"

namespace SkullbonezCore
{
namespace Geometry
{
class Terrain;
}

namespace Rendering
{
class Dx12GeometryOwner;
}

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;

struct PhysicsDebugFrameView
{
    const PhysicsBodyStore& bodies;
    const ColliderStore& colliders;
    std::span<const uint8_t> sleepStates;
    std::span<const uint8_t> sleepSupportedStates;
    std::span<const uint8_t> sleepInhibitedStates;
    const std::vector<PhysicsDebugContact>& debugContacts;
    const std::vector<PhysicsPipelineRecord>& pipelineTrace;
    int modelCount = 0;
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
    void EmitObjectAxes( const PhysicsDebugFrameView& view );
    void EmitConvexHullWireframes( const PhysicsDebugFrameView& view );
    void EmitContacts( const PhysicsDebugFrameView& view );
    void EmitSleepState( const PhysicsDebugFrameView& view );
    void EmitPipelineStage( const PhysicsDebugFrameView& view );
    void EmitTerrainContactProbe( const PhysicsDebugFrameView& view, Geometry::Terrain* terrain );

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
    void Update( float dt, const PhysicsDebugFrameView& view );
    // The caller owns renderer readiness and debug-line capability for the frame.
    void Render( const PhysicsDebugFrameView& view,
                 const Math::Transformation::Matrix4& viewProj,
                 Rendering::Dx12GeometryOwner& renderCommands,
                 bool supportsDebugLines,
                 Geometry::Terrain* terrain = nullptr );
};
} // namespace Physics
} // namespace SkullbonezCore
