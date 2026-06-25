/*
File: SkullbonezSource/Runtime/RuntimePickService.cpp
Purpose:
  Implements centralized runtime picking policies.

Mental model:
  Pick callers supply a world-space ray and a purpose. The service owns common
  closest-hit behavior so future selection, replay, and tool changes have one
  policy surface instead of several ad hoc loops.

Related:
  - SkullbonezSource/Runtime/RuntimePickService.h
*/
#include "RuntimePickService.h"

#include <algorithm>

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
        break;
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
