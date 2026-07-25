/*
File: SkullbonezSource/Physics/TerrainContactManifold.h
Purpose:
  Builds object/terrain contact sweep results and manifolds for the persistent solver.

Summary:
  Terrain contact is a value report. The sweep helper finds when a body reaches
  the heightfield, and the manifold helper turns that hit into solver rows from
  body/collider records.

Glossary:
  Terrain sweep: Continuous collision query against the terrain plane under a body.
  Manifold: Set of contact points and normals describing one body touching terrain.
  Feature ID: Deterministic contact key used to match rows across frames for warm
    starting.
  Resting policy: Metadata that decides whether a terrain contact may seed sleep
    and cached support impulses.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines are
    the validation contract.
  - TerrainContactBodyView is a borrowed, per-call view. Callers must not cache
    terrain pointers or shape references beyond the current physics operation.

Related:
  - SkullbonezSource/Physics/TerrainContactManifold.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>

#include "CollisionShape.h"
#include "../Maths/GeometricStructures.h"
#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
}
namespace Geometry
{
class Terrain;
}

namespace Physics
{
struct TerrainContactBodyView
{
    // Terrain contact needs pose, velocity, shape policy, and a borrowed
    // heightfield. It deliberately does not expose the full legacy model.
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Geometry::Terrain* terrain = nullptr;
    float boundingRadius = 0.0f;
    float contactEpsilon = 0.0f;
    float terrainContactThreshold = 0.0f;
    float restitutionThreshold = 0.0f;
    bool isFixed = false;
};

struct TerrainContactPoint
{
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rA = Math::Vector::ZERO_VECTOR;
    float penetration = 0.0f;
    uint32_t featureId = 0;
};

struct TerrainContactManifold
{
    int bodyA = -1;
    int bodyB = -1;                // -1 marks terrain, which is static and not stored in the body array.
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent1 = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent2 = Math::Vector::ZERO_VECTOR;
    TerrainContactPoint points[8];
    uint8_t pointCount = 0;
    float timeOfImpact = 0.0f;
    bool sweptHit = false;
    bool supportsRestingPolicy = true;
    bool allowsTangentFriction = true;
    bool inhibitsSleep = false;
    uint32_t terrainCellId = 0;
    uint32_t materialId = 0;
};

struct TerrainContactSweepResult
{
    bool hit = false;              // Valid terrain hit occurred in the tested substep.
    float collisionTime = 0.0f;    // Seconds from the start of the tested substep.
    Geometry::Ray collidedRay;     // Sweep ray captured for diagnostics and future terrain row metadata.
    Geometry::Plane collidedPlane; // Terrain plane used to build the contact manifold.
};

TerrainContactSweepResult SweepTerrainContact(
    Core::Profiler* profiler,
    const TerrainContactBodyView& body,
    const Math::CollisionDetection::CollisionShape& shape,
    float changeInTime
);
bool BuildTerrainContactManifold(
    Core::Profiler* profiler,
    const TerrainContactBodyView& body,
    const Math::CollisionDetection::CollisionShape& shape,
    int bodyIndex,
    const TerrainContactSweepResult& sweep,
    float availableTime,
    TerrainContactManifold& out
);
inline TerrainContactSweepResult SweepTerrainContact(
    const TerrainContactBodyView& body,
    const Math::CollisionDetection::CollisionShape& shape,
    float changeInTime
)
{
    return SweepTerrainContact( nullptr, body, shape, changeInTime );
}
inline bool BuildTerrainContactManifold(
    const TerrainContactBodyView& body,
    const Math::CollisionDetection::CollisionShape& shape,
    int bodyIndex,
    const TerrainContactSweepResult& sweep,
    float availableTime,
    TerrainContactManifold& out
)
{
    return BuildTerrainContactManifold( nullptr, body, shape, bodyIndex, sweep, availableTime, out );
}
} // namespace Physics
} // namespace SkullbonezCore
