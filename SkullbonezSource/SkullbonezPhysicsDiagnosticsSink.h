#pragma once

#include "SkullbonezSkullScope.h"

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
    void EmitCollisionTime( GameObjects::GameModelCollection& collection, const char* type, int bodyA, int bodyB, float collisionTime, float availableTime );

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
