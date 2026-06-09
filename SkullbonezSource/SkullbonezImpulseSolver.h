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

  public:
    static bool RespondCollisionTerrain( GameModel& gameModel, float changeInTime ); // Unified sphere+box terrain response; returns true when contact can sleep
};
} // namespace Physics
} // namespace SkullbonezCore
