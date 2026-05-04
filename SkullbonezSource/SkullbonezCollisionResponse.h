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
/* -- CollisionResponse ------------------------------------------------------------------------------------------------------------------------------------------

    Contains a series of static methods to assist in response to collisions
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class CollisionResponse
{

  private:
    static void SphereVsSphereLinear( GameModel& gameModel1, GameModel& gameModel2, const Vector3& collisionNormal );  // Sphere and sphere response, linear component
    static void SphereVsSphereAngular( GameModel& gameModel1, GameModel& gameModel2, const Vector3& collisionNormal ); // Sphere and sphere response, angular component
    static Vector3 GetCollisionNormalSphereVsSphere( GameModel& gameModel1, GameModel& gameModel2 );                   // Calculate the collision normal for a sphere and a sphere
    static Vector3 GetCollidedObjectWorldPosition( GameModel& gameModel );                                             // Calculates the world position of the current collided dynamics object

  public:
    static void RespondCollisionTerrain( GameModel& gameModel, float changeInTime );        // Performs a response based on the game model and the terrain
    static void RespondCollisionGameModels( GameModel& gameModel1, GameModel& gameModel2 ); // Performs a response on the game models based on their current state
    static Ray CalculateRay( GameModel& gameModel, float changeInTime );                    // Returns a ray representing the path travelled by the target in the supplied time frame
    static void SetPhysicsLog( FILE* file );                                                // Enable/disable frame-by-frame physics logging
    static void SetPhysicsFrame( int frame );                                               // Set current frame for logging context
};
} // namespace Physics
} // namespace SkullbonezCore
