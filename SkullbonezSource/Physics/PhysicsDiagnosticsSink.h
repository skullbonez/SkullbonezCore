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
namespace GameObjects
{
class GameModelCollection;
}

namespace Physics
{
class PhysicsWorld;

class PhysicsDiagnosticsSink
{
  public:
#ifdef _DEBUG
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
    void EmitRegressionLog( PhysicsWorld& world, GameObjects::GameModelCollection& collection );
    void IncrementCollisionTimeFrameIfEnabled();
    void EmitFrame( GameObjects::GameModelCollection& collection, float dt );
#endif
    void EmitCollisionTime( GameObjects::GameModelCollection& collection,
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
