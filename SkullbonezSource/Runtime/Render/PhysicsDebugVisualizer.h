/*
File: SkullbonezSource/Runtime/Render/PhysicsDebugVisualizer.h
Purpose:
  Draws physics contacts, axes, sleep state, and pipeline diagnostics.

Summary:
  Render converts copied solver records into line primitives and owns the
  short-lived contact cache. Detached values never give Scene or Diagnostics
  access to cached or GPU state.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Detached contact packets are consumed synchronously and never retained.

Related:
  - SkullbonezSource/Runtime/Render/PhysicsDebugVisualizer.cpp
  - SkullbonezSource/Physics/PhysicsDebugData.h
  - SkullbonezSource/Rendering/ContactManifoldPresentation.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstddef>
#include <span>
#include <vector>
#include "../../Core/SceneCapacity.h"
#include "../../Maths/Matrix4.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsDebugData.h"
#include "../../Rendering/ContactManifoldPresentation.h"

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
} // namespace Physics

namespace Runtime
{

struct PhysicsDebugBodyView
{
    const Physics::PhysicsBodyStore& bodies;
    const Physics::ColliderStore& colliders;
    int modelCount = 0;
};

struct PhysicsDebugContactView
{
    const Physics::PhysicsBodyStore& bodies;
    std::span<const Physics::PhysicsDebugContact> contacts;
};

struct PhysicsDebugSleepView
{
    PhysicsDebugBodyView bodies;
    std::span<const uint8_t> sleepStates;
    std::span<const uint8_t> sleepSupportedStates;
    std::span<const uint8_t> sleepInhibitedStates;
};

struct PhysicsDebugPipelineView
{
    const Physics::PhysicsBodyStore& bodies;
    std::span<const Physics::PhysicsPipelineRecord> pipelineTrace;
};

struct PhysicsDebugFrameView
{
    // Concept: this is composition for the flag router only. Each overlay below
    // receives its exact detached rows and cannot reach sibling diagnostics.
    PhysicsDebugBodyView bodies;
    PhysicsDebugContactView contacts;
    PhysicsDebugSleepView sleep;
    PhysicsDebugPipelineView pipeline;
};

class PhysicsDebugVisualizer
{
  private:
    // The line buffer is deliberately bounded at the complete scene-body axes
    // footprint. Other enabled layers share this staging and truncate only
    // visual output after it fills; they never grow storage during Render.
    static constexpr std::size_t LINE_FLOATS_PER_BODY_AXES = 3u * 3u * 12u;
    static constexpr std::size_t LINE_FLOAT_CAPACITY = static_cast<std::size_t>( Scene::Capacity::MAX_SCENE_OBJECTS ) *
                                                       LINE_FLOATS_PER_BODY_AXES;
    static constexpr std::size_t TRACKED_CONTACT_CAPACITY = Scene::Capacity::MAX_SCENE_OBJECTS;

    struct TrackedContact
    {
        // Contact visuals linger briefly after the solver row disappears so a
        // human can actually see a one-frame impact. This is display-only state.
        Physics::PhysicsDebugContact contact;
        float remainingSeconds = 0.0f;
        float lifetimeSeconds = 0.0f;
    };

    uint32_t m_flags = Physics::PHYSICS_DEBUG_NONE;
    int m_pipelineStageCursor = 0;
    float m_contactLingerSeconds = 0.45f;
    std::vector<float> m_lineData;
    std::vector<TrackedContact> m_trackedContacts;

    TrackedContact* FindTrackedContact( const Physics::PhysicsDebugContact& contact );
    float ContactFade( const TrackedContact& contact ) const;
    void EmitLine( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitCross( const Math::Vector::Vector3& p, float size, float r, float g, float bl );
    void EmitArrow( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitContactGlyph( const Rendering::ContactPointPresentation& point, float normalImpulse, float fade );
    void EmitRingXZ( const Math::Vector::Vector3& center, float radius, float yOffset, float r, float g, float bl );
    void EmitObjectAxes( const PhysicsDebugBodyView& view );
    void EmitConvexHullWireframes( const PhysicsDebugBodyView& view );
    void EmitContacts( const PhysicsDebugContactView& view );
    void EmitSleepState( const PhysicsDebugSleepView& view );
    void EmitPipelineStage( const PhysicsDebugPipelineView& view );
    void EmitTerrainContactProbe( const PhysicsDebugBodyView& view, Geometry::Terrain* terrain );

  public:
    PhysicsDebugVisualizer();

    void ResetTransientState();

    void SetFlags( uint32_t flags )
    {
        m_flags = flags & Physics::PHYSICS_DEBUG_ALL;

        if ( ( m_flags & Physics::PHYSICS_DEBUG_CONTACTS ) == 0 )
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
        return m_flags != Physics::PHYSICS_DEBUG_NONE;
    }
    void SetContactLingerSeconds( float seconds );
    void SetPipelineStageCursor( int cursor );
    void Update( float dt, std::span<const Physics::PhysicsDebugContact> contacts );

    std::size_t DiagnosticLineFloatCapacity() const
    {
        return m_lineData.capacity();
    }
    std::size_t DiagnosticTrackedContactCount() const
    {
        return m_trackedContacts.size();
    }
    static constexpr std::size_t DiagnosticRequiredLineFloatCapacity()
    {
        return LINE_FLOAT_CAPACITY;
    }
    static constexpr std::size_t DiagnosticTrackedContactCapacity()
    {
        return TRACKED_CONTACT_CAPACITY;
    }

    // The caller owns renderer readiness and debug-line capability for the frame.
    void Render( const PhysicsDebugFrameView& view, const Math::Transformation::Matrix4& viewProj,
                 Rendering::Dx12GeometryOwner& renderCommands, bool supportsDebugLines,
                 Geometry::Terrain* terrain = nullptr );

    // Reuses the contact glyph path for an upper-layer-owned detached patch.
    // The packet is consumed synchronously and never enters the linger cache.
    void RenderContactManifold( const Rendering::ContactManifoldPresentation& presentation,
                                const Math::Transformation::Matrix4& viewProj, Rendering::Dx12GeometryOwner& renderCommands,
                                bool supportsDebugLines );
};
} // namespace Runtime
} // namespace SkullbonezCore
