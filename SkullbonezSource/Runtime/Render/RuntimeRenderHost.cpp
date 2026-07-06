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
#include "RuntimeRenderInputs.h"
#include "RuntimeRenderPasses.h"

#include "../../Assets/TextureCollection.h"
#include "../../Core/Profiler.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Rendering/Helper.h"
#include "../../Rendering/IRenderBackend.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../RunInternal.h"

#include <cassert>
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Replay/ReplayOverlayRenderer.h"
#include "../RunState.h"
#include "../Scene/SceneRuntimeLoad.h"
#include "../Window.h"

#include <cstddef>
#include <stdexcept>
#include <variant>
#include <vector>

using namespace SkullbonezCore::Basics;

CinematicRenderConfig& RuntimeRenderHost::ActiveCinematicConfig() const
{
    return RunInternal::RuntimeActiveCinematicConfig( m_sceneController.State(), m_config );
}

bool RuntimeRenderHost::IsCinematicRenderingEnabled() const
{
    return RunInternal::RuntimeCinematicRenderingEnabled( m_sceneController.State(),
                                                          m_config,
                                                          m_launchOptions,
                                                          m_debug,
                                                          ActiveRenderBackend() != nullptr );
}

bool RuntimeRenderHost::IsLauncherCameraMode() const
{
    return m_camera.mode == RunCameraMode::Launcher;
}

int RuntimeRenderHost::WindowScreenWidth() const
{
    return RunInternal::RuntimeWindowScreenWidth( m_systems, m_config );
}

int RuntimeRenderHost::WindowScreenHeight() const
{
    return RunInternal::RuntimeWindowScreenHeight( m_systems, m_config );
}


SkullbonezCore::Rendering::IRenderBackend* RuntimeRenderHost::ActiveRenderBackend() const
{
    return m_renderBackend.renderBackend;
}


SkullbonezCore::Rendering::IRenderRayTracing* RuntimeRenderHost::ActiveRayTracingBackend() const
{
    return m_renderBackend.rayTracingBackend;
}


const char* RuntimeRenderHost::RendererNameOrDefault( const char* fallbackName ) const
{
    const SkullbonezCore::Rendering::IRenderBackend* renderBackend = ActiveRenderBackend();
    return renderBackend ? renderBackend->GetRendererName() : fallbackName;
}


bool RuntimeRenderHost::SupportsDxrReflection() const
{
    const SkullbonezCore::Rendering::IRenderBackend* renderBackend = ActiveRenderBackend();
    return renderBackend && renderBackend->GetCapabilities().supportsDxrReflection;
}


void RuntimeRenderHost::SetVsyncEnabled( bool enabled ) const
{
    SkullbonezCore::Rendering::IRenderBackend* renderBackend = ActiveRenderBackend();
    if ( renderBackend )
    {
        renderBackend->SetVsyncEnabled( enabled );
    }
}


const RunSceneState& RuntimeRenderHost::SceneState() const
{
    return m_sceneController.State();
}

ReplayOverlay::ReplayOverlayRenderContext
RuntimeRenderHost::BuildReplayOverlayRenderContext( const UI::UIRenderContext& uiRender,
                                                    const RuntimeRenderModelFrameView& models ) const
{
    assert( uiRender.IsReady() );
    return { *uiRender.commands,
             m_replayRuntime,
             models.presentationRecords,
             models.bodyStore,
             m_editor.editorModeEnabled,
             m_UI.IsVisible(),
             m_UI.IsMinimized(),
             SceneState().isScenePhysics,
             WindowScreenWidth(),
             WindowScreenHeight(),
             m_timers.simulationTimer.GetTotalTime() };
}

bool RuntimeRenderHost::BuildReplayFocusModelMask( const RenderFrameContext& frame ) const
{
    if ( !frame.bodyStore )
    {
        return false;
    }
    return m_replayRuntime.BuildFocusModelMask( *frame.bodyStore, frame.modelCount );
}

void RuntimeRenderHost::RenderReplayScrubberOverlay( const UI::UIRenderContext& uiRender,
                                                     const RuntimeRenderModelFrameView& models ) const
{
    ReplayOverlay::RenderReplayScrubberOverlay( BuildReplayOverlayRenderContext( uiRender, models ) );
}

void RuntimeRenderHost::RenderReplayCauseTreeOverlay( const UI::UIRenderContext& uiRender,
                                                      const RuntimeRenderModelFrameView& models ) const
{
    ReplayOverlay::RenderReplayCauseTreeOverlay( BuildReplayOverlayRenderContext( uiRender, models ) );
}

int RuntimeRenderHost::CurrentSceneBrowserIndex() const
{
    return SkullbonezCore::Basics::CurrentSceneBrowserIndex( m_sceneController, m_sceneBrowser );
}

bool RuntimeRenderHost::ToolHasSelectionOverlayWork( int modelCount ) const
{
    return m_runtimeTools.HasSelectionOverlayWork( modelCount, m_camera.mode );
}

bool RuntimeRenderHost::ToolHasMousePickupOverlayWork( int modelCount ) const
{
    return m_runtimeTools.HasMousePickupOverlayWork( modelCount );
}

void RuntimeRenderHost::RenderReplayPredictionGhosts( const RenderFrameContext& frame,
                                                      const CinematicRenderConfig* cinematic,
                                                      const Rendering::ShadowFrameData* shadow ) const
{
    PROFILE_SCOPED( "Frame/Render/ReplayPredictionGhosts" );
    if ( !frame.presentationRecords || !frame.bodyStore ||
         !m_replayRuntime.BuildPredictionGhostDrawRequests( *frame.presentationRecords, *frame.bodyStore ) )
    {
        return;
    }

    // Why: ghost drawing is a render projection path. Shape and material come
    // from the prepared store snapshots so replay visualization does not need
    // the GameModel collider mirror to stay fresh after physics steps.
    if ( !frame.colliders || !frame.renderInstances )
    {
        return;
    }
    const auto& colliders = frame.colliders->Records();
    const std::vector<Rendering::RenderInstanceRecord>& renderInstances = frame.renderInstances->Records();

    assert( frame.textures && "RenderFrameContext requires a texture collection" );
    frame.textures->SelectTexture( TEXTURE_BOUNDING_SPHERE );
    assert( frame.renderResources && frame.renderCommands && frame.assets );
    const RenderHelperContext helperContext{ *frame.renderResources, *frame.renderCommands, *frame.assets, m_config };
    RenderHelper::DrawBoxBatchBegin( helperContext,
                                     frame.baseView,
                                     frame.projection,
                                     frame.lightPosition,
                                     true,
                                     cinematic,
                                     shadow,
                                     1.0f );

    for ( const ReplayPredictionGhostDrawRequest& request : m_replayRuntime.PredictionGhostDrawRequests() )
    {
        if ( request.modelIndex < 0 || request.modelIndex >= static_cast<int>( colliders.size() ) ||
             request.modelIndex >= static_cast<int>( renderInstances.size() ) )
        {
            continue;
        }

        const std::size_t modelIndex = static_cast<std::size_t>( request.modelIndex );
        const Physics::ColliderRecord& collider = colliders[modelIndex];
        const Math::CollisionDetection::BoundingBox* box =
            std::get_if<Math::CollisionDetection::BoundingBox>( &collider.shape );
        if ( !box )
        {
            continue;
        }

        Rendering::RenderMaterial material = renderInstances[modelIndex].material;
        material.baseColor[3] = request.alpha;
        const Math::Transformation::Matrix4 modelMatrix =
            box->GetModelMatrix( request.position,
                                 Math::Transformation::Matrix4::FromQuaternion( request.orientation ) );
        RenderHelper::DrawBoxBatchModel( modelMatrix, material );
    }

    RenderHelper::DrawBoxBatchEnd( helperContext );
}
