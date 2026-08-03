/*
File: SkullbonezSource/Physics/ObjectContactManifold.h
Purpose:
  Builds precise object/object contact manifolds for the persistent solver.

Summary:
  Exposes the pose, candidate-reduction, sweep, and final manifold values at the
  Physics narrowphase boundary. The implementation dispatches shape pairs and
  publishes deterministic feature identity for persistent solver reuse.

Glossary:
  Baumgarte bias: Positional correction term that turns penetration depth into
  a solver velocity target.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Candidate reduction accepts at most 32 synchronous rows and returns at most
    four indices, all into the supplied candidate array.

Related:
  - SkullbonezSource/Physics/ObjectContactManifold.cpp
  - Agentic/Reports/2026-08-02/narrowphase-manifold-sleep-coverage-nm1-geometry.md
  - Agentic/Reports/2026-08-02/narrowphase-manifold-sleep-coverage-nm2-identity.md
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstdint>
#include "CollisionShape.h"
#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
}
namespace Physics
{
struct ObjectContactBodyView
{

    // Narrowphase contact geometry needs pose plus shape. PhysicsBodyRecord
    // callers fill this view directly while ColliderRecord borrows the exact
    // shape from ColliderStore's per-kind payload storage.
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation;
};

inline constexpr int MAX_OBJECT_CONTACT_CANDIDATES = 32;

struct ObjectContactCandidate
{

    // Concept: clipping can yield more geometry than the four-row solver
    // budget. Candidate rows keep geometry and warm-start identity together
    // until the deterministic reducer chooses the rows that survive.
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    float penetration = 0.0f;
    uint32_t featureId = 0;
};

struct ObjectContactCandidateSelection
{

    // Lifetime: indices borrow the caller's synchronous candidate array; this
    // value retains neither the candidates nor any owner authority.
    int indices[4] = {};
    uint8_t count = 0;
};

// CATTO REF:
//   Catto 2005, Section 4 "Contact Model" represents each contact row with a
//   world-space contact point, a normal, penetration/separation, and the rA/rB
//   arms from each body center to the contact point. PersistentContactSolver
//   consumes exactly that row shape.
// ENGINE-SPECIFIC:
//   Catto does not prescribe this engine's 3D narrowphase. This file builds the
//   Skullbonez shape-pair manifolds that feed Catto-style rows: sphere/sphere,
//   sphere/OBB, and OBB/OBB. Feature IDs are deterministic local encodings so
//   warm-started impulses can be matched across frames.
// LAYMAN VERSION:
//   A manifold is a small contact report. It says "these two bodies are touching
//   here, push along this direction, and this is how deep the overlap is." The
//   solver later turns each point in the report into one rule it can enforce.
struct ObjectContactPoint
{
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;  // World-space representative contact point.
    Math::Vector::Vector3 rA = Math::Vector::ZERO_VECTOR;     // Contact arm from body A center; Catto Equations 9-11.
    Math::Vector::Vector3 rB = Math::Vector::ZERO_VECTOR;     // Contact arm from body B center; Catto Equations 9-11.
    float penetration = 0.0f;                                 // Positive overlap depth used by Baumgarte bias.
    uint32_t featureId = 0;                                   // Stable local feature key for temporal coherence.
};

struct ObjectContactManifold
{
    int bodyA = -1;                                           // Solver index for body A; -1 means the row is not populated.
    int bodyB = -1;                                           // Solver index for body B; object manifolds require two live bodies.
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR; // Points from body A toward body B.
    ObjectContactPoint points[4];                             // Up to four face contacts, matching box face clipping.
    uint8_t pointCount = 0;
};

struct ObjectContactSweepResult
{
    bool hit = false;                                         // Candidate object/object hit occurred in the tested substep.
    float collisionTime = 0.0f;                               // Seconds from the start of the tested substep.
};

// Reduces transient clipped geometry to the solver's four-row budget. Returned
// indices borrow the caller's candidate array only for the current synchronous
// manifold build; the selection retains no pointer or owner authority. Null,
// non-positive, or over-capacity input returns an empty selection.
ObjectContactCandidateSelection SelectObjectContactCandidateIndices( const ObjectContactCandidate* candidates,
                                                                     int candidateCount,
                                                                     const Math::Vector::Vector3& normal );

// Runs the object/object CCD front-end from body/collider inputs. The result is
// only a time candidate; exact contact rows still come from BuildObjectContactManifold.
ObjectContactSweepResult SweepObjectContact( const ObjectContactBodyView& a,
                                             const Math::CollisionDetection::CollisionShape& shapeA,
                                             const Math::Vector::Vector3& linearVelocityA, const ObjectContactBodyView& b,
                                             const Math::CollisionDetection::CollisionShape& shapeB,
                                             const Math::Vector::Vector3& linearVelocityB, float changeInTime );
ObjectContactSweepResult SweepObjectContact( const ObjectContactBodyView& a,
                                             const Math::CollisionDetection::CollisionShapeReference& shapeA,
                                             const Math::Vector::Vector3& linearVelocityA, const ObjectContactBodyView& b,
                                             const Math::CollisionDetection::CollisionShapeReference& shapeB,
                                             const Math::Vector::Vector3& linearVelocityB, float changeInTime );
bool BuildObjectContactManifold( Core::Profiler* profiler, const ObjectContactBodyView& a,
                                 const Math::CollisionDetection::CollisionShape& shapeA, const ObjectContactBodyView& b,
                                 const Math::CollisionDetection::CollisionShape& shapeB, int bodyA, int bodyB,
                                 float contactSkin, ObjectContactManifold& out );
bool BuildObjectContactManifold( Core::Profiler* profiler, const ObjectContactBodyView& a,
                                 const Math::CollisionDetection::CollisionShapeReference& shapeA,
                                 const ObjectContactBodyView& b,
                                 const Math::CollisionDetection::CollisionShapeReference& shapeB, int bodyA, int bodyB,
                                 float contactSkin, ObjectContactManifold& out );
inline bool BuildObjectContactManifold( const ObjectContactBodyView& a,
                                        const Math::CollisionDetection::CollisionShape& shapeA,
                                        const ObjectContactBodyView& b,
                                        const Math::CollisionDetection::CollisionShape& shapeB, int bodyA, int bodyB,
                                        float contactSkin, ObjectContactManifold& out )
{
    return BuildObjectContactManifold( nullptr, a, shapeA, b, shapeB, bodyA, bodyB, contactSkin, out );
}
inline bool BuildObjectContactManifold( const ObjectContactBodyView& a,
                                        const Math::CollisionDetection::CollisionShapeReference& shapeA,
                                        const ObjectContactBodyView& b,
                                        const Math::CollisionDetection::CollisionShapeReference& shapeB, int bodyA,
                                        int bodyB, float contactSkin, ObjectContactManifold& out )
{
    return BuildObjectContactManifold( nullptr, a, shapeA, b, shapeB, bodyA, bodyB, contactSkin, out );
}
} // namespace Physics
} // namespace SkullbonezCore
