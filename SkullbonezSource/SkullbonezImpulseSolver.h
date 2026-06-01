#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezGeometricStructures.h"


// --- Usings ---
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Geometry;


namespace SkullbonezCore
{
namespace Physics
{
/* -- ImpulseSolver -----------------------------------------------------------------------------------------------------------------------------------------------

    Unified sequential impulse solver (Erin Catto / Box2D / Bullet style).
    Handles both spheres and boxes against terrain, plus object-object contacts
    through the shared contact-point impulse path.

-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class ImpulseSolver
{

  private:
    static void SphereVsSphereLinear( GameModel& gameModel1, GameModel& gameModel2, const Vector3& collisionNormal );
    static void SphereVsSphereAngular( GameModel& gameModel1, GameModel& gameModel2, const Vector3& collisionNormal );
    static Vector3 GetCollisionNormalSphereVsSphere( GameModel& gameModel1, GameModel& gameModel2 );
    static Vector3 GetCollidedObjectWorldPosition( GameModel& gameModel );

  public:
    static bool RespondCollisionTerrain( GameModel& gameModel, float changeInTime );        // Unified sphere+box terrain response; returns true when contact can sleep
    static void RespondCollisionGameModels( GameModel& gameModel1, GameModel& gameModel2 ); // Object-object response through contact normal and tangent friction impulses
};
} // namespace Physics
} // namespace SkullbonezCore
