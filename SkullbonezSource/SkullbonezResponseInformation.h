#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezGeometricStructures.h"


// --- Usings ---
using namespace SkullbonezCore::Geometry;


namespace SkullbonezCore
{
namespace Physics
{
/* -- ResponseInformation ----------------------------------------------------------------------------------------------------------------------------------------

    Contains the short-lived terrain hit information passed from terrain
    detection to manifold creation.

    Layman version:
      - testingRay/testingPlane are the question: "if the object keeps moving,
        does this path cross this terrain plane?"
      - collidedRay/collidedPlane are the answer captured when a hit is found.
      - collisionTime is the fraction of the current sweep where the hit occurs.

    The shared solver does not read this struct directly. GameModel turns it
    into TerrainContactManifold rows first, then GameModelCollection solves them.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
struct ResponseInformation
{
    Geometry::Ray testingRay;      // Ray the model is currently being tested against
    Geometry::Plane testingPlane;  // Plane currently being tested for collision against
    Geometry::Ray collidedRay;     // Ray the model has had a collision with
    Geometry::Plane collidedPlane; // Plane in which the model has collided with
    float collisionTime;           // Time in which collidedRay intersects with collidedPlane
};
} // namespace Physics
} // namespace SkullbonezCore
