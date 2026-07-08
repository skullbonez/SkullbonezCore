/*
File: SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp
Purpose:
  Streams bounded physics diagnostics to SkullScope trace files.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/PhysicsDiagnosticsSink.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "PhysicsDiagnosticsSink.h"
#include "../Core/Log.h"

#ifdef _DEBUG
#include "PhysicsDiagnosticsModel.h"
#endif
#include "ColliderStore.h"
#include "PhysicsBodyStore.h"
#include "PhysicsWorld.h"

#include <cmath>
#include <cstring>
#include <type_traits>
#include <variant>

using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;
namespace Math = SkullbonezCore::Math;


#ifdef _DEBUG
bool SkullbonezCore::Physics::TryBuildPhysicsDiagnosticsModelRecord( int index,
                                                                     const PhysicsBodyStore& bodyStore,
                                                                     const ColliderStore& colliderStore,
                                                                     const PhysicsDiagnosticsNameView& names,
                                                                     PhysicsDiagnosticsModelRecord& outRecord )
{
    if ( index < 0 || index >= bodyStore.Count() || index >= colliderStore.Count() )
    {
        return false;
    }

    const PhysicsBodyRecord& bodyRecord = bodyStore.Records()[static_cast<std::size_t>( index )];
    const ColliderRecord& colliderRecord = colliderStore.Records()[static_cast<std::size_t>( index )];
    outRecord = PhysicsDiagnosticsModelRecord{};
    outRecord.name = names.NameFor( index );
    outRecord.position = bodyRecord.position;
    outRecord.velocity = bodyRecord.linearVelocity;
    outRecord.angularVelocity = bodyRecord.angularVelocity;
    outRecord.rotationalInertia = bodyRecord.rotationalInertia;
    bodyRecord.orientation.GetComponents( outRecord.qx, outRecord.qy, outRecord.qz, outRecord.qw );
    outRecord.mass = bodyRecord.mass;
    outRecord.inverseMass = bodyRecord.invMass;

    // Why: regression CSV diagnostics are emitted after the solver, so body and
    // shape state must come from the stores just written by the step. The
    // scene/model edge contributes only the cold presentation name.
    std::visit(
        [&]( const auto& shape )
        {
            using ShapeT = std::decay_t<decltype( shape )>;
            if constexpr ( std::is_same_v<ShapeT, Math::CollisionDetection::BoundingSphere> )
            {
                outRecord.shapeName = "sphere";
                outRecord.radius = shape.GetRadius();
            }
            else if constexpr ( std::is_same_v<ShapeT, Math::CollisionDetection::BoundingBox> )
            {
                outRecord.shapeName = "box";
                outRecord.halfExtents = shape.GetHalfExtents();
            }
            else
            {
                outRecord.shapeName = "convex_hull";
                outRecord.radius = shape.GetBoundingRadius();
                outRecord.hullName = shape.GetName();
                outRecord.hullVertices = shape.GetVertexCount();
                outRecord.hullFaces = shape.GetFaceCount();
                outRecord.hullEdges = shape.GetEdgeCount();
            }
        },
        colliderRecord.shape );
    return true;
}


void PhysicsDiagnosticsSink::SetPhysicsRegressionLogPath( const char* path )
{
    strcpy_s( m_physicsRegressionLogPath, sizeof( m_physicsRegressionLogPath ), path );
    m_physicsRegressionLogFrame = 0;
}


void PhysicsDiagnosticsSink::SetPhysicsCollisionTimeLogPath( const char* path )
{
    strcpy_s( m_physicsCollisionTimeLogPath, sizeof( m_physicsCollisionTimeLogPath ), path );
    m_physicsCollisionTimeLogFrame = 0;
    m_physicsCollisionTimeHeaderWritten = false;
}


void PhysicsDiagnosticsSink::SetPhysicsDiagnosticsPath( const char* path )
{
    m_skullScope.SetPath( path );
}


void PhysicsDiagnosticsSink::SetPhysicsDiagnosticsRunId( const char* runId )
{
    m_skullScope.SetRunId( runId );
}


bool PhysicsDiagnosticsSink::IsCollisionTimeLogEnabled() const
{
    return m_physicsCollisionTimeLogPath[0] != '\0';
}


bool PhysicsDiagnosticsSink::IsRegressionLogEnabled() const
{
    return m_physicsRegressionLogPath[0] != '\0';
}


void PhysicsDiagnosticsSink::EmitRegressionLog( const PhysicsDiagnosticsFrameInput& frame )
{
    if ( !IsRegressionLogEnabled() )
    {
        return;
    }

    const PhysicsDiagnosticsView& diagnosticsView = frame.world;
    const auto& m_sleepSupportedThisFrame = diagnosticsView.sleepSupportedThisFrame;
    const auto& m_sleepState = diagnosticsView.sleepState;
    const auto& m_sleepInhibitedThisFrame = diagnosticsView.sleepInhibitedThisFrame;

    const int modelCount = frame.bodyStore.Count();
    if ( m_physicsRegressionLogFrame == 0 )
    {
        Log().Writef( m_physicsRegressionLogPath,
                      "frame,idx,name,posX,posY,posZ,velX,velY,velZ,speed,omegaX,omegaY,omegaZ,omegaMag,qX,qY,qZ,qW,"
                      "grounded,sleeping,sleepInhibited\n" );
    }
    for ( int i = 0; i < modelCount; ++i )
    {
        PhysicsDiagnosticsModelRecord model;
        if ( !TryBuildPhysicsDiagnosticsModelRecord( i, frame.bodyStore, frame.colliderStore, frame.names, model ) )
        {
            continue;
        }

        const Vector3& pos = model.position;
        const Vector3& vel = model.velocity;
        const Vector3& omega = model.angularVelocity;
        float speed = sqrtf( vel.x * vel.x + vel.y * vel.y + vel.z * vel.z );
        float omegaMag = sqrtf( omega.x * omega.x + omega.y * omega.y + omega.z * omega.z );
        int sleepSupported = m_sleepSupportedThisFrame[i];
        int sleeping = ( i < static_cast<int>( m_sleepState.size() ) ) ? m_sleepState[i] : 0;
        int sleepInhibited =
            ( i < static_cast<int>( m_sleepInhibitedThisFrame.size() ) ) ? m_sleepInhibitedThisFrame[i] : 0;
        Log().Writef( m_physicsRegressionLogPath,
                      "%d,%d,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d\n",
                      m_physicsRegressionLogFrame,
                      i,
                      model.name,
                      pos.x,
                      pos.y,
                      pos.z,
                      vel.x,
                      vel.y,
                      vel.z,
                      speed,
                      omega.x,
                      omega.y,
                      omega.z,
                      omegaMag,
                      model.qx,
                      model.qy,
                      model.qz,
                      model.qw,
                      sleepSupported,
                      sleeping,
                      sleepInhibited );
    }
    ++m_physicsRegressionLogFrame;
}


void PhysicsDiagnosticsSink::IncrementCollisionTimeFrameIfEnabled()
{
    if ( m_physicsCollisionTimeLogPath[0] != '\0' )
    {
        ++m_physicsCollisionTimeLogFrame;
    }
}


bool PhysicsDiagnosticsSink::IsFrameLogEnabled() const
{
    return m_skullScope.IsFrameEnabled();
}


void PhysicsDiagnosticsSink::EmitFrame( const PhysicsDiagnosticsFrameInput& frame )
{
    m_skullScope.EmitFrame( frame );
}
#endif


void PhysicsDiagnosticsSink::EmitCollisionTime( const char* const* diagnosticNames,
                                                int diagnosticNameCount,
                                                const char* type,
                                                int bodyA,
                                                int bodyB,
                                                float collisionTime,
                                                float availableTime )
{
#ifdef _DEBUG
    if ( m_physicsCollisionTimeLogPath[0] == '\0' )
    {
        return;
    }
    if ( !m_physicsCollisionTimeHeaderWritten )
    {
        Log().Writef( m_physicsCollisionTimeLogPath,
                      "frame,type,bodyA,bodyB,nameA,nameB,collisionTime,availableTime\n" );
        m_physicsCollisionTimeHeaderWritten = true;
    }
    const PhysicsDiagnosticsNameView names{ diagnosticNames, diagnosticNameCount };
    const auto collisionNameFor = [&]( int bodyIndex ) -> const char*
    { return ( bodyIndex >= 0 && bodyIndex < names.count ) ? names.NameFor( bodyIndex ) : "terrain"; };
    const char* nameA = collisionNameFor( bodyA );
    const char* nameB = collisionNameFor( bodyB );
    Log().Writef( m_physicsCollisionTimeLogPath,
                  "%d,%s,%d,%d,%s,%s,%.6f,%.6f\n",
                  m_physicsCollisionTimeLogFrame,
                  type,
                  bodyA,
                  bodyB,
                  nameA,
                  nameB,
                  collisionTime,
                  availableTime );
#else
    (void)diagnosticNames;
    (void)diagnosticNameCount;
    (void)type;
    (void)bodyA;
    (void)bodyB;
    (void)collisionTime;
    (void)availableTime;
#endif
}
