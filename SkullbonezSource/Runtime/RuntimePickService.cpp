/*
File: SkullbonezSource/Runtime/RuntimePickService.cpp
Purpose:
  Implements centralized runtime picking policies.

Mental model:
  Pick callers supply a world-space ray and a purpose. The service owns common
  closest-hit behavior so future selection, replay, and tool changes have one
  policy surface instead of several ad hoc loops.

Glossary:
  Pick ray: World-space ray projected from the current screen-space pointer.
  Pick purpose: Tool-specific policy for interpreting candidate hits.
  RayT: Distance along the pick ray to the candidate hit.

Invariants:
  - The service never stores model references; results are frame-local indices
    that callers must revalidate before use.
  - Manipulator pickup ignores fixed bodies, while selection-style purposes may
    return any closest model.
  - Model radius padding is legacy request data; exact shape picking does not
    expand collision geometry.

Related:
  - SkullbonezSource/Runtime/RuntimePickService.h
*/
#include "RuntimePickService.h"

#include "../GameObjects/GameModel.h"
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
    if ( request.models == nullptr )
    {
        return false;
    }

    const std::vector<GameObjects::GameModel>& models = *request.models;
    const bool skipFixedBodies = request.purpose == RuntimePickPurpose::ManipulatorPickup;
    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        const GameObjects::GameModel& model = models[static_cast<std::size_t>( i )];
        if ( skipFixedBodies && model.IsFixed() )
        {
            continue;
        }

        RuntimePickShapeTransform transform;
        transform.position = model.GetPosition();
        transform.orientation = model.GetOrientation();

        // Invariant: picking now answers against authored collision geometry,
        // not the conservative broadphase radius. Padding would reintroduce the
        // tree-trunk failure where large foliage envelopes hide narrow trunks.
        float rayT = 0.0f;
        if ( TryIntersectRuntimePickShape( model.GetCollisionShape(),
                                           transform,
                                           request.rayOrigin,
                                           request.rayDirection,
                                           rayT ) &&
             rayT + PICK_TIE_EPSILON < outResult.rayT )
        {
            outResult.rayT = rayT;
            outResult.modelIndex = i;
        }
    }

    return outResult.modelIndex >= 0;
}
} // namespace Basics
} // namespace SkullbonezCore
