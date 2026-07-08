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

// Debug CSV output boundary. The runtime composition edge supplies this writer
// when a launch enables byte-exact physics CSVs; physics formats rows but does
// not reach through the ambient Log() accessor.
struct PhysicsDiagnosticsCsvWriter
{
    using WriteVfFn = void ( * )( void* userData, const char* fileName, const char* fmt, va_list args );

    WriteVfFn writeVf = nullptr;
    void* userData = nullptr;

    // Caller contract: an empty writer is a no-op; validation launches bind a
    // writer before enabling the byte-exact CSV paths.
    void Writef( const char* fileName, const char* fmt, ... ) const;
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
    void EmitCollisionTime( const char* const* diagnosticNames,
                            int diagnosticNameCount,
                            const PhysicsDiagnosticsCsvWriter& csvWriter,
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
