/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderHost.cpp
Purpose:
  Implements render-host services that need concrete model or render helper types.

Mental model:
  RuntimeRenderHost is still a bridge, but bridge methods should draw from
  named runtime owners instead of callback-bouncing into Run.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
  - Agentic/Plans/run-composition-root-shrink-plan.md
*/
#include "RuntimeRenderHost.h"
#include "RuntimeRenderPasses.h"

#include "../../Core/Profiler.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Rendering/Helper.h"

#include <cstddef>
#include <variant>
#include <vector>

using namespace SkullbonezCore::Basics;

void RuntimeRenderHost::RenderReplayPredictionGhosts( const RenderFrameContext& frame,
                                                      const CinematicRenderConfig* cinematic,
                                                      const Rendering::ShadowFrameData* shadow ) const
{
    PROFILE_SCOPED( "Frame/Render/ReplayPredictionGhosts" );
    const std::vector<GameObjects::GameModel>& models = m_cGameModelCollection.Models();
    if ( !m_replayRuntime.BuildPredictionGhostDrawRequests( models ) )
    {
        return;
    }

    SelectRenderTexture( TEXTURE_BOUNDING_SPHERE );
    RenderHelper::DrawBoxBatchBegin( frame.baseView,
                                     frame.projection,
                                     frame.lightPosition,
                                     true,
                                     cinematic,
                                     shadow,
                                     1.0f );

    for ( const ReplayPredictionGhostDrawRequest& request : m_replayRuntime.PredictionGhostDrawRequests() )
    {
        if ( request.modelIndex < 0 || request.modelIndex >= static_cast<int>( models.size() ) )
        {
            continue;
        }

        const GameObjects::GameModel& model = models[static_cast<std::size_t>( request.modelIndex )];
        const Math::CollisionDetection::BoundingBox* box =
            std::get_if<Math::CollisionDetection::BoundingBox>( &model.GetCollisionShape() );
        if ( !box )
        {
            continue;
        }

        Rendering::RenderMaterial material = model.GetRenderMaterial();
        material.baseColor[3] = request.alpha;
        const Math::Transformation::Matrix4 modelMatrix =
            box->GetModelMatrix( request.position,
                                 Math::Transformation::Matrix4::FromQuaternion( request.orientation ) );
        RenderHelper::DrawBoxBatchModel( modelMatrix, material );
    }

    RenderHelper::DrawBoxBatchEnd();
}
