/*
File: SkullbonezSource/Physics/PhysicsDiagnosticsSink.h
Purpose:
  Streams bounded physics diagnostics to SkullScope trace files.

Summary:
  PhysicsDiagnosticsSink.h streams bounded physics diagnostics to SkullScope
  trace files. As a public header, keep edits anchored on deterministic
  physics, diagnostics, or world-state flow and on the glossary/invariants
  below.

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
  - SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Runtime/Scene/SceneCapacity.h"
#include <array>
#include <cstdarg>

#include "../Core/SkullScope.h"

#ifdef _DEBUG
#include "PhysicsDiagnosticsModel.h"
#endif

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct PhysicsDiagnosticsView;

inline constexpr int PHYSICS_COLLISION_TIME_EVENT_CAPACITY = SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * 5;

// Debug CSV output boundary. Physics formats rows after the solver pass and the
// concrete writer delegates to the process log sink; no callback, user pointer,
// or file-I/O dependency enters collision detection or solver loops.
struct PhysicsDiagnosticsCsvWriter
{
    // Cold concrete writer: no callback or retained user pointer can enter the
    // solver. Calls occur only after bounded physics events are committed.
    void Writef( const char* fileName, const char* fmt, ... ) const;
};

struct PhysicsCollisionTimeEvent
{
    const char* type = nullptr; // Static "object"/"terrain" token.
    int bodyA = -1;
    int bodyB = -1;
    float collisionTime = 0.0f;
    float availableTime = 0.0f;
};

#ifdef _DEBUG
// Immutable inputs for one diagnostics emission pass. Body/collider/world facts
// are already owned by physics; only names remain a presentation overlay.
struct PhysicsDiagnosticsFrameInput
{
    const PhysicsDiagnosticsView& world;
    const PhysicsBodyStore& bodyStore;
    const ColliderStore& colliderStore;
    PhysicsDiagnosticsNameView names;
    PhysicsDiagnosticsCsvWriter csvWriter;
    float deltaSeconds = 0.0f;
};
#endif

class PhysicsDiagnosticsSink
{
  public:
    // One object event per bounded candidate pair plus one terrain event per
    // body. PhysicsWorld reserves four candidate pairs per model, so five rows
    // per model is the exact upstream maximum for one fixed step.
    static constexpr int CollisionTimeEventCapacity()
    {
        return PHYSICS_COLLISION_TIME_EVENT_CAPACITY;
    }
#ifdef _DEBUG
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
    bool IsCollisionTimeLogEnabled() const;
    bool IsRegressionLogEnabled() const;
    void EmitRegressionLog( const PhysicsDiagnosticsFrameInput& frame );
    void IncrementCollisionTimeFrameIfEnabled();
    bool IsFrameLogEnabled() const;
    void EmitFrame( const PhysicsDiagnosticsFrameInput& frame );
#endif
    void BeginCollisionTimeFrame();
    void QueueCollisionTime( const char* type, int bodyA, int bodyB, float collisionTime, float availableTime );
    void FlushCollisionTimes( const char* const* diagnosticNames,
                              int diagnosticNameCount,
                              const PhysicsDiagnosticsCsvWriter& csvWriter );

  private:
#ifdef _DEBUG
    static constexpr int COLLISION_TIME_EVENT_CAPACITY = PHYSICS_COLLISION_TIME_EVENT_CAPACITY;
    char m_physicsRegressionLogPath[256] = {};
    int m_physicsRegressionLogFrame = 0;
    char m_physicsCollisionTimeLogPath[256] = {};
    int m_physicsCollisionTimeLogFrame = 0;
    bool m_physicsCollisionTimeHeaderWritten = false;
    std::array<PhysicsCollisionTimeEvent, COLLISION_TIME_EVENT_CAPACITY> m_collisionTimeEvents = {};
    int m_collisionTimeEventCount = 0;
    int m_collisionTimeEventHighWater = 0;
    GameObjects::SkullScope m_skullScope;
#endif
};
} // namespace Physics
} // namespace SkullbonezCore
