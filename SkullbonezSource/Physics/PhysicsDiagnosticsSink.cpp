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

#ifdef _DEBUG
#include "PhysicsDiagnosticsModel.h"
#endif
#include "ColliderStore.h"
#include "PhysicsBodyStore.h"
#include "PhysicsModelAccess.h"
#include "PhysicsWorld.h"

#include <cmath>
#include <cstring>

using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;


#ifdef _DEBUG
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


void PhysicsDiagnosticsSink::EmitRegressionLog( PhysicsWorld& world,
                                                PhysicsModelAccess& modelAccess,
                                                const PhysicsBodyStore& bodyStore,
                                                const ColliderStore& colliderStore )
{
    const PhysicsDiagnosticsView diagnosticsView = world.GetDiagnosticsView();
    const auto& m_sleepSupportedThisFrame = diagnosticsView.sleepSupportedThisFrame;
    const auto& m_sleepState = diagnosticsView.sleepState;
    const auto& m_sleepInhibitedThisFrame = diagnosticsView.sleepInhibitedThisFrame;

    if ( m_physicsRegressionLogPath[0] == '\0' )
    {
        return;
    }

    const int modelCount = bodyStore.Count();
    if ( m_physicsRegressionLogFrame == 0 )
    {
        Log().Writef( m_physicsRegressionLogPath,
                      "frame,idx,name,posX,posY,posZ,velX,velY,velZ,speed,omegaX,omegaY,omegaZ,omegaMag,qX,qY,qZ,qW,"
                      "grounded,sleeping,sleepInhibited\n" );
    }
    for ( int i = 0; i < modelCount; ++i )
    {
        PhysicsDiagnosticsModelRecord model;
        if ( !modelAccess.TryGetPhysicsDiagnosticsModel( i, bodyStore, colliderStore, model ) )
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


void PhysicsDiagnosticsSink::EmitFrame( PhysicsModelAccess& modelAccess,
                                        const PhysicsBodyStore& bodyStore,
                                        const ColliderStore& colliderStore,
                                        float dt )
{
    m_skullScope.EmitFrame( modelAccess, bodyStore, colliderStore, dt );
}
#endif


void PhysicsDiagnosticsSink::EmitCollisionTime( PhysicsModelAccess& modelAccess,
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
    const char* modelNameA = "";
    const char* modelNameB = "";
    const char* nameA = modelAccess.TryGetPhysicsDiagnosticsModelName( bodyA, modelNameA ) ? modelNameA : "terrain";
    const char* nameB = modelAccess.TryGetPhysicsDiagnosticsModelName( bodyB, modelNameB ) ? modelNameB : "terrain";
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
    (void)modelAccess;
    (void)type;
    (void)bodyA;
    (void)bodyB;
    (void)collisionTime;
    (void)availableTime;
#endif
}
