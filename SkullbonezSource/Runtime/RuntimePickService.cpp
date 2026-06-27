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

Related:
  - SkullbonezSource/Runtime/RuntimePickService.h
*/
#include "RuntimePickService.h"

#include <algorithm>
#include <cmath>

#include "../GameObjects/GameModel.h"
#include "../Physics/CollisionShape.h"

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
float PickModelRadius( const GameObjects::GameModel& model )
{
    return (std::max)( Math::CollisionDetection::GetShapeBoundingRadius( model.GetCollisionShape() ), 1.0f );
}


bool IntersectRaySphere( const Math::Vector::Vector3& rayOrigin,
                         const Math::Vector::Vector3& rayDirection,
                         const Math::Vector::Vector3& center,
                         float radius,
                         float& outT )
{
    const Math::Vector::Vector3 m = rayOrigin - center;
    const float b = m * rayDirection;
    const float c = ( m * m ) - radius * radius;
    if ( c > 0.0f && b > 0.0f )
    {
        return false;
    }

    const float discriminant = b * b - c;
    if ( discriminant < 0.0f )
    {
        return false;
    }

    outT = -b - sqrtf( discriminant );
    if ( outT < 0.0f )
    {
        outT = 0.0f;
    }
    return true;
}
} // namespace

bool RuntimePickService::TryPickModel( const RuntimePickRequest& request, RuntimePickResult& outResult )
{
    outResult = RuntimePickResult{};
    if ( request.models == nullptr )
    {
        return false;
    }

    switch ( request.purpose )
    {
    case RuntimePickPurpose::EditorSelection:
    case RuntimePickPurpose::AttachCameraTarget:
    case RuntimePickPurpose::ReplayPathTarget:
        break;
    case RuntimePickPurpose::ManipulatorPickup:
        // Why: the manipulator needs a physical grab target, so fixed bodies
        // are filtered before the broader closest-hit selection policy below.
        for ( int i = 0; i < static_cast<int>( request.models->size() ); ++i )
        {
            const GameObjects::GameModel& model = ( *request.models )[static_cast<std::size_t>( i )];
            if ( model.IsFixed() )
            {
                continue;
            }

            float rayT = 0.0f;
            const float radius = PickModelRadius( model ) + request.modelRadiusPadding;
            if ( IntersectRaySphere( request.rayOrigin, request.rayDirection, model.GetPosition(), radius, rayT ) &&
                 rayT < outResult.rayT )
            {
                outResult.rayT = rayT;
                outResult.modelIndex = i;
            }
        }
        return outResult.modelIndex >= 0;
    }

    const std::vector<GameObjects::GameModel>& models = *request.models;
    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        const GameObjects::GameModel& model = models[static_cast<std::size_t>( i )];
        const float radius = PickModelRadius( model ) + request.modelRadiusPadding;
        const Math::Vector::Vector3 toCenter = model.GetPosition() - request.rayOrigin;
        const float rayT = toCenter * request.rayDirection;
        if ( rayT < 0.0f || rayT >= outResult.rayT )
        {
            continue;
        }

        const Math::Vector::Vector3 closest = request.rayOrigin + request.rayDirection * rayT;
        if ( Math::Vector::VectorMagSquared( model.GetPosition() - closest ) <= radius * radius )
        {
            outResult.rayT = rayT;
            outResult.modelIndex = i;
        }
    }

    return outResult.modelIndex >= 0;
}
} // namespace Basics
} // namespace SkullbonezCore
