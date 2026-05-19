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

    Legacy (single-pass, non-iterative) collision response system.
    Handles sphere-vs-terrain and sphere-vs-sphere collisions via one-shot impulse calculations.

    Replaced by ImpulseSolver for new scenes, but retained for side-by-side comparison via
    the --legacy-physics flag.  SkullbonezRun routes to CollisionResponse or ImpulseSolver
    at runtime depending on ImpulseSolver::IsLegacyPhysics().

    --- Design note ---
    All methods are static. The class has no instance state — it is effectively a namespace
    with access control (private helper methods), matching the original 2005 architecture.

    --- Sphere-Terrain Response (single pass, not iterated) ---
      1. Contact point: r = -radius * surface_normal  (bottom of sphere)
      2. Contact velocity: v_c = v + ω × r
      3. Normal impulse:
             j_n = -(1+e) * v_n / K_n
             where  v_n = v_c · n  (approach speed along normal)
                    K_n = 1/m + n · ((r×n)/I × r)  (effective mass at contact)
      4. Friction impulse (Coulomb): j_t ≤ μ * j_n, opposing tangential slide
      5. Spin damping, rolling friction, no-slip enforcement

    --- Sphere-Sphere Response ---
      Angular response (SphereVsSphereAngular) computed first using the rigid-body
      impulse formula with an empirical moment arm (normalised relative velocity).
      Linear response (SphereVsSphereLinear) uses the 1D elastic collision formula
      projected along the collision normal.  See those functions for full derivations.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class CollisionResponse
{

  private:
    // Applies the 1D elastic collision formula along the collision normal (momentum + restitution).
    static void SphereVsSphereLinear( GameModel& gameModel1, GameModel& gameModel2, const Vector3& collisionNormal );

    // Transfers spin between spheres using the rigid-body impulse formula with an empirical moment arm.
    // NOTE: the moment arm used (normalised relative linear velocity) is non-standard — see implementation.
    static void SphereVsSphereAngular( GameModel& gameModel1, GameModel& gameModel2, const Vector3& collisionNormal );

    // Returns the unit vector from gameModel1's bounding volume centre to gameModel2's.
    // For spheres this is always the correct contact normal (contact lies on the centre-to-centre line).
    static Vector3 GetCollisionNormalSphereVsSphere( GameModel& gameModel1, GameModel& gameModel2 );

    // Returns the world-space position of the bounding volume centre:
    // body_position + orientation_matrix * local_offset
    static Vector3 GetCollidedObjectWorldPosition( GameModel& gameModel );

  public:
    static void RespondCollisionTerrain( GameModel& gameModel, float changeInTime );        // Performs a response based on the game model and the terrain
    static void RespondCollisionGameModels( GameModel& gameModel1, GameModel& gameModel2 ); // Performs a response on the game models based on their current state
    static Ray CalculateRay( GameModel& gameModel, float changeInTime );                    // Returns a ray representing the path travelled by the target in the supplied time frame
};
} // namespace Physics
} // namespace SkullbonezCore
