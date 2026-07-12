/*
File: SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp
Purpose:
  Streams bounded physics diagnostics to SkullScope trace files.

Mental model:
  PhysicsDiagnosticsSink.cpp streams bounded physics diagnostics to SkullScope
  trace files. As an implementation unit, keep edits anchored on deterministic
  physics, diagnostics, or world-state flow and on the glossary/invariants
  below.

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

#ifdef _DEBUG
#include "PhysicsDiagnosticsModel.h"
#endif
#include "ColliderStore.h"
#include "../Core/FatalError.h"
#include "../Core/Log.h"
#include "PhysicsBodyStore.h"
#include "PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>
#include <variant>

using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;
namespace Math = SkullbonezCore::Math;


void PhysicsDiagnosticsCsvWriter::Writef( const char* fileName, const char* fmt, ... ) const
{
#ifdef _DEBUG
    if ( fileName == nullptr || fmt == nullptr )
    {
        return;
    }

    va_list args;
    va_start( args, fmt );
    Log().WriteVf( fileName, fmt, args );
    va_end( args );
#else
    (void)fileName;
    (void)fmt;
#endif
}


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
        frame.csvWriter.Writef( m_physicsRegressionLogPath,
                                "frame,idx,name,posX,posY,posZ,velX,velY,velZ,speed,omegaX,omegaY,omegaZ,omegaMag,qX,"
                                "qY,qZ,qW,grounded,sleeping,sleepInhibited\n" );
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
        frame.csvWriter.Writef(
            m_physicsRegressionLogPath,
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


void PhysicsDiagnosticsSink::BeginCollisionTimeFrame()
{
#ifdef _DEBUG
    m_collisionTimeEventCount = 0;
#endif
}

void PhysicsDiagnosticsSink::QueueCollisionTime( const char* type,
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
    if ( m_collisionTimeEventCount >= COLLISION_TIME_EVENT_CAPACITY )
    {
        SB_FATAL( "PhysicsDiagnosticsSink",
                  "Collision-time event capacity exhausted. owner=PhysicsDiagnosticsSink capacity=%d high_water=%d "
                  "phase=fixed_step_collision_commit",
                  COLLISION_TIME_EVENT_CAPACITY,
                  m_collisionTimeEventHighWater );
    }
    m_collisionTimeEvents[static_cast<std::size_t>( m_collisionTimeEventCount++ )] =
        PhysicsCollisionTimeEvent{ type, bodyA, bodyB, collisionTime, availableTime };
    m_collisionTimeEventHighWater = (std::max)( m_collisionTimeEventHighWater, m_collisionTimeEventCount );
#else
    (void)type;
    (void)bodyA;
    (void)bodyB;
    (void)collisionTime;
    (void)availableTime;
#endif
}

void PhysicsDiagnosticsSink::FlushCollisionTimes( const char* const* diagnosticNames,
                                                  int diagnosticNameCount,
                                                  const PhysicsDiagnosticsCsvWriter& csvWriter )
{
#ifdef _DEBUG
    if ( m_physicsCollisionTimeLogPath[0] == '\0' || m_collisionTimeEventCount == 0 )
    {
        return;
    }
    if ( !m_physicsCollisionTimeHeaderWritten )
    {
        csvWriter.Writef( m_physicsCollisionTimeLogPath,
                          "frame,type,bodyA,bodyB,nameA,nameB,collisionTime,availableTime\n" );
        m_physicsCollisionTimeHeaderWritten = true;
    }
    const PhysicsDiagnosticsNameView names{ diagnosticNames, diagnosticNameCount };
    const auto collisionNameFor = [&]( int bodyIndex ) -> const char*
    { return ( bodyIndex >= 0 && bodyIndex < names.count ) ? names.NameFor( bodyIndex ) : "terrain"; };
    for ( int eventIndex = 0; eventIndex < m_collisionTimeEventCount; ++eventIndex )
    {
        const PhysicsCollisionTimeEvent& event = m_collisionTimeEvents[static_cast<std::size_t>( eventIndex )];
        csvWriter.Writef( m_physicsCollisionTimeLogPath,
                          "%d,%s,%d,%d,%s,%s,%.6f,%.6f\n",
                          m_physicsCollisionTimeLogFrame,
                          event.type,
                          event.bodyA,
                          event.bodyB,
                          collisionNameFor( event.bodyA ),
                          collisionNameFor( event.bodyB ),
                          event.collisionTime,
                          event.availableTime );
    }
#else
    (void)diagnosticNames;
    (void)diagnosticNameCount;
    (void)csvWriter;
#endif
}
