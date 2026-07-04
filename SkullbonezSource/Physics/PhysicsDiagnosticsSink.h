/*
File: SkullbonezSource/Physics/PhysicsDiagnosticsSink.h
Purpose:
  Streams bounded physics diagnostics to SkullScope trace files.

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
  - SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/SkullScope.h"

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
class PhysicsModelAccess;
struct PhysicsDiagnosticsView;

// Cold presentation names for diagnostics rows. The table is supplied by the
// scene/model edge; the sink treats missing names as empty and never indexes a
// GameModel range.
struct PhysicsDiagnosticsNameView
{
    const char* const* names = nullptr;
    int count = 0;

    const char* NameFor( int index ) const
    {
        return ( names && index >= 0 && index < count && names[index] ) ? names[index] : "";
    }
};

// Immutable inputs for one diagnostics emission pass. Body/collider/world facts
// are already owned by physics; only names remain a presentation overlay.
struct PhysicsDiagnosticsFrameInput
{
    const PhysicsDiagnosticsView& world;
    const PhysicsBodyStore& bodyStore;
    const ColliderStore& colliderStore;
    PhysicsDiagnosticsNameView names;
    float deltaSeconds = 0.0f;
};

class PhysicsDiagnosticsSink
{
  public:
#ifdef _DEBUG
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
    bool IsRegressionLogEnabled() const;
    void EmitRegressionLog( const PhysicsDiagnosticsFrameInput& frame );
    void IncrementCollisionTimeFrameIfEnabled();
    void EmitFrame( PhysicsModelAccess& modelAccess,
                    const PhysicsBodyStore& bodyStore,
                    const ColliderStore& colliderStore,
                    float dt );
#endif
    void EmitCollisionTime( PhysicsModelAccess& modelAccess,
                            const char* type,
                            int bodyA,
                            int bodyB,
                            float collisionTime,
                            float availableTime );

  private:
#ifdef _DEBUG
    char m_physicsRegressionLogPath[256] = {};
    int m_physicsRegressionLogFrame = 0;
    char m_physicsCollisionTimeLogPath[256] = {};
    int m_physicsCollisionTimeLogFrame = 0;
    bool m_physicsCollisionTimeHeaderWritten = false;
    GameObjects::SkullScope m_skullScope;
#endif
};
} // namespace Physics
} // namespace SkullbonezCore
