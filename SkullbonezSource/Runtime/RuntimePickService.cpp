/*
File: SkullbonezSource/Runtime/RuntimePickService.cpp
Purpose:
  Implements centralized runtime picking policies.

Mental model:
  Pick callers supply a world-space ray and a purpose. The service owns common
  closest-hit behavior so future selection, replay, and tool changes have one
  policy surface over physics store records instead of several ad hoc model
  loops.

Glossary:
  Pick ray: World-space ray projected from the current screen-space pointer.
  Pick purpose: Tool-specific policy for interpreting candidate hits.
  Physics body handle: Generational id for the body-store row selected by the
    pick ray.
  RayT: Distance along the pick ray to the candidate hit.

Invariants:
  - The service never stores physics-store references; results are frame-local
    handles/indices that callers must revalidate before use.
  - Manipulator pickup ignores fixed bodies, while selection-style purposes may
    return any closest model.
  - Exact shape picking does not expand collision geometry; broadphase padding
    belongs outside this policy surface.

Related:
  - SkullbonezSource/Runtime/RuntimePickService.h
*/
#include "RuntimePickService.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsBodyStore.h"
#include "RuntimePickGeometry.h"

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
constexpr float PICK_TIE_EPSILON = 1.0e-4f;
} // namespace

bool RuntimePickService::TryPickModel( const RuntimePickRequest& request, RuntimePickResult& outResult )
{
    outResult = RuntimePickResult{};
    if ( request.bodyStore == nullptr || request.colliderStore == nullptr )
    {
        return false;
    }

    const auto& bodies = request.bodyStore->Records();
    const auto& colliders = request.colliderStore->Records();
    const int candidateCount = static_cast<int>( (std::min)( bodies.size(), colliders.size() ) );
    const bool skipFixedBodies = request.purpose == RuntimePickPurpose::ManipulatorPickup;
    for ( int i = 0; i < candidateCount; ++i )
    {
        const Physics::PhysicsBodyRecord& body = bodies[static_cast<std::size_t>( i )];
        const Physics::ColliderRecord& collider = colliders[static_cast<std::size_t>( i )];
        if ( collider.body != body.handle )
        {
            continue;
        }
        if ( skipFixedBodies && body.isFixed )
        {
            continue;
        }

        RuntimePickShapeTransform transform;
        transform.position = body.position;
        transform.orientation = body.orientation;

        // Invariant: picking now answers against authored collision geometry,
        // not the conservative broadphase radius. Padding would reintroduce the
        // tree-trunk failure where large foliage envelopes hide narrow trunks.
        float rayT = 0.0f;
        if ( TryIntersectRuntimePickShape( collider.shape, transform, request.rayOrigin, request.rayDirection, rayT ) &&
             rayT + PICK_TIE_EPSILON < outResult.rayT )
        {
            outResult.body = body.handle;
            outResult.collider = collider.handle;
            outResult.rayT = rayT;
            outResult.modelRow.value = i;
        }
    }

    return outResult.modelRow.IsValid() && outResult.body.IsValid();
}
} // namespace Basics
} // namespace SkullbonezCore
