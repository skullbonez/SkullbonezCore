#pragma once

#include <cstdint>
#include "SkullbonezGameModel.h"
#include "SkullbonezVector3.h"

namespace SkullbonezCore
{
namespace Physics
{
// CATTO REF:
//   Catto 2005, Section 4 "Contact Model" represents each contact row with a
//   world-space contact point, a normal, penetration/separation, and the rA/rB
//   arms from each body center to the contact point. The iterative solver in
//   GameModelCollection consumes exactly that row shape.
// ENGINE-SPECIFIC / NOVEL:
//   Catto does not prescribe this engine's 3D narrowphase. This file builds the
//   Skullbonez shape-pair manifolds that feed Catto-style rows: sphere/sphere,
//   sphere/OBB, and OBB/OBB. Feature IDs are deterministic local encodings so
//   warm-started impulses can be matched across frames.
struct ObjectContactPoint
{
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR; // World-space representative contact point.
    Math::Vector::Vector3 rA = Math::Vector::ZERO_VECTOR;    // Contact arm from body A center; Catto Equations 9-11.
    Math::Vector::Vector3 rB = Math::Vector::ZERO_VECTOR;    // Contact arm from body B center; Catto Equations 9-11.
    float penetration = 0.0f;                                // Positive overlap depth used by Baumgarte bias.
    uint32_t featureId = 0;                                  // Stable local feature key for temporal coherence.
};

struct ObjectContactManifold
{
    int bodyA = -1;
    int bodyB = -1;
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR; // Points from body A toward body B.
    ObjectContactPoint points[4];                             // Up to four face contacts, matching box face clipping.
    uint8_t pointCount = 0;
};

bool BuildObjectContactManifold( const GameObjects::GameModel& a,
                                 const GameObjects::GameModel& b,
                                 int bodyA,
                                 int bodyB,
                                 float contactSkin,
                                 ObjectContactManifold& out );
} // namespace Physics
} // namespace SkullbonezCore
