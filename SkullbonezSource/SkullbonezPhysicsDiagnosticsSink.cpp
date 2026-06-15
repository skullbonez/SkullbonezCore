#include "SkullbonezPhysicsDiagnosticsSink.h"

#include "SkullbonezGameModelCollection.h"
#include "SkullbonezPhysicsWorld.h"

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


void PhysicsDiagnosticsSink::EmitRegressionLog( PhysicsWorld& world, GameModelCollection& collection )
{
    auto& m_gameModels = collection.m_gameModels;
    auto& m_sleepSupportedThisFrame = world.m_sleepSupportedThisFrame;
    auto& m_sleepState = world.m_sleepState;
    auto& m_sleepInhibitedThisFrame = world.m_sleepInhibitedThisFrame;

    if ( m_physicsRegressionLogPath[0] == '\0' )
    {
        return;
    }

    const int modelCount = static_cast<int>( m_gameModels.size() );
    if ( m_physicsRegressionLogFrame == 0 )
    {
        Log().Writef( m_physicsRegressionLogPath, "frame,idx,name,posX,posY,posZ,velX,velY,velZ,speed,omegaX,omegaY,omegaZ,omegaMag,qX,qY,qZ,qW,grounded,sleeping,sleepInhibited\n" );
    }
    for ( int i = 0; i < modelCount; ++i )
    {
        const char* name = m_gameModels[i].GetName();
        const Vector3& pos = m_gameModels[i].GetPosition();
        const Vector3& vel = m_gameModels[i].GetVelocity();
        const Vector3& omega = m_gameModels[i].GetAngularVelocity();
        float qx = 0.0f;
        float qy = 0.0f;
        float qz = 0.0f;
        float qw = 1.0f;
        m_gameModels[i].GetOrientation().GetComponents( qx, qy, qz, qw );
        float speed = sqrtf( vel.x * vel.x + vel.y * vel.y + vel.z * vel.z );
        float omegaMag = sqrtf( omega.x * omega.x + omega.y * omega.y + omega.z * omega.z );
        int sleepSupported = m_sleepSupportedThisFrame[i];
        int sleeping = ( i < static_cast<int>( m_sleepState.size() ) ) ? m_sleepState[i] : 0;
        int sleepInhibited = ( i < static_cast<int>( m_sleepInhibitedThisFrame.size() ) ) ? m_sleepInhibitedThisFrame[i] : 0;
        Log().Writef( m_physicsRegressionLogPath, "%d,%d,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d\n", m_physicsRegressionLogFrame, i, name, pos.x, pos.y, pos.z, vel.x, vel.y, vel.z, speed, omega.x, omega.y, omega.z, omegaMag, qx, qy, qz, qw, sleepSupported, sleeping, sleepInhibited );
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


void PhysicsDiagnosticsSink::EmitFrame( GameModelCollection& collection, float dt )
{
    m_skullScope.EmitFrame( collection, dt );
}
#endif


void PhysicsDiagnosticsSink::EmitCollisionTime( GameModelCollection& collection, const char* type, int bodyA, int bodyB, float collisionTime, float availableTime )
{
#ifdef _DEBUG
    auto& m_gameModels = collection.m_gameModels;
    if ( m_physicsCollisionTimeLogPath[0] == '\0' )
    {
        return;
    }
    if ( !m_physicsCollisionTimeHeaderWritten )
    {
        Log().Writef( m_physicsCollisionTimeLogPath, "frame,type,bodyA,bodyB,nameA,nameB,collisionTime,availableTime\n" );
        m_physicsCollisionTimeHeaderWritten = true;
    }
    const char* nameA = ( bodyA >= 0 && bodyA < static_cast<int>( m_gameModels.size() ) ) ? m_gameModels[bodyA].GetName() : "terrain";
    const char* nameB = ( bodyB >= 0 && bodyB < static_cast<int>( m_gameModels.size() ) ) ? m_gameModels[bodyB].GetName() : "terrain";
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
    (void)collection;
    (void)type;
    (void)bodyA;
    (void)bodyB;
    (void)collisionTime;
    (void)availableTime;
#endif
}
