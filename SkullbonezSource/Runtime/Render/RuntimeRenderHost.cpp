/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderHost.cpp
Purpose:
  Implements render-host services that need concrete model or render helper types.

Mental model:
  RuntimeRenderHost is still a bridge, but bridge methods should draw from
  named runtime owners instead of callback-bouncing into Run.

Glossary:
  Render host: Borrowed service view used by render passes while Run remains
  the broader composition root.
  Pass: Ordered unit of frame rendering owned by RuntimeRenderer.
  Replay ghost: Transparent predicted-body draw used to preview replay future
  path samples.

Invariants:
  - Host methods borrow runtime state and must not take ownership of models,
    UI, replay, editor, or renderer resources.
  - Replay ghost model indices are validated against the current model vector
    before any draw request is submitted.

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
#include "../Replay/ReplayOverlayRenderer.h"
#include "../RunState.h"

#include <cstddef>
#include <variant>
#include <vector>

using namespace SkullbonezCore::Basics;

ReplayOverlay::ReplayOverlayRenderContext RuntimeRenderHost::BuildReplayOverlayRenderContext() const
{
    return { m_replayRuntime,
             m_cGameModelCollection.Models(),
             m_editor.editorModeEnabled,
             m_UI.IsVisible(),
             m_UI.IsMinimized(),
             WindowScreenWidth(),
             WindowScreenHeight(),
             m_timers.simulationTimer.GetTotalTime() };
}

void RuntimeRenderHost::RenderReplayScrubberOverlay() const
{
    ReplayOverlay::RenderReplayScrubberOverlay( BuildReplayOverlayRenderContext() );
}

void RuntimeRenderHost::RenderReplayCauseTreeOverlay() const
{
    ReplayOverlay::RenderReplayCauseTreeOverlay( BuildReplayOverlayRenderContext() );
}

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
