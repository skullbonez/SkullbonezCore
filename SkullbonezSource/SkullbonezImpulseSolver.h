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

    // 1D elastic collision formula projected along the collision normal (momentum + restitution).
    // e combined via geometric mean sqrt(e1*e2) so a fully inelastic body dominates.
    static void SphereVsSphereLinear( GameModel& gameModel1, GameModel& gameModel2, const Vector3& collisionNormal );

    // Coulomb friction-based spin transfer at the sphere-sphere contact point.
    // For spheres r × n = 0, so normal impulses carry no torque; all spin change comes from tangential friction.
    // Δω = I⁻¹ * (r × J_friction)
    static void SphereVsSphereAngular( GameModel& gameModel1, GameModel& gameModel2, const Vector3& collisionNormal );

    // Returns unit vector from gameModel1's bounding volume centre to gameModel2's (the contact normal for sphere pairs).
    static Vector3 GetCollisionNormalSphereVsSphere( GameModel& gameModel1, GameModel& gameModel2 );

    // World-space bounding volume centre: body_position + R * local_offset
    static Vector3 GetCollidedObjectWorldPosition( GameModel& gameModel );

  public:
    static void SetLegacyPhysics( bool legacy ) { s_legacyPhysics = legacy; } // Called once from SkullbonezRun::Initialise()
    static bool IsLegacyPhysics() { return s_legacyPhysics; }

    static void RespondCollisionTerrain( GameModel& gameModel, float changeInTime );        // Unified sphere+box terrain response (sequential impulse solver)
    static void RespondCollisionGameModels( GameModel& gameModel1, GameModel& gameModel2 ); // Sphere-sphere (and mixed) game model response
};
} // namespace Physics
} // namespace SkullbonezCore
