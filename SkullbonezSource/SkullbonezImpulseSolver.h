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
    Handles both spheres and boxes against terrain, and sphere-sphere collisions
    with friction-based spin transfer.

    When s_legacyPhysics is true, terrain and game-model collision is delegated
    to the original CollisionResponse routines (sphere-only, ad-hoc solver).
    Used for side-by-side comparison via --legacy-physics flag.  To be removed
    once legacy parity is verified.

-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class ImpulseSolver
{

  private:
    static bool s_legacyPhysics; // Set once at startup via --legacy-physics; routes to CollisionResponse

    static void SphereVsSphereLinear( GameModel& gameModel1, GameModel& gameModel2, const Vector3& collisionNormal );  // Sphere-sphere linear velocity exchange (improved geometric-mean restitution)
    static void SphereVsSphereAngular( GameModel& gameModel1, GameModel& gameModel2, const Vector3& collisionNormal ); // Sphere-sphere angular impulse (Coulomb friction-based spin transfer)
    static Vector3 GetCollisionNormalSphereVsSphere( GameModel& gameModel1, GameModel& gameModel2 );                   // Collision normal from center-to-center
    static Vector3 GetCollidedObjectWorldPosition( GameModel& gameModel );                                             // World position of bounding volume center

  public:
    static void SetLegacyPhysics( bool legacy ) { s_legacyPhysics = legacy; } // Called once from SkullbonezRun::Initialise()
    static bool IsLegacyPhysics() { return s_legacyPhysics; }

    static void RespondCollisionTerrain( GameModel& gameModel, float changeInTime );        // Unified sphere+box terrain response (sequential impulse solver)
    static void RespondCollisionGameModels( GameModel& gameModel1, GameModel& gameModel2 ); // Sphere-sphere (and mixed) game model response
};
} // namespace Physics
} // namespace SkullbonezCore
