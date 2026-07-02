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

#include "../../Assets/TextureCollection.h"
#include "../../Core/Profiler.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Rendering/Helper.h"
#include "../../Rendering/IRenderBackend.h"
#include "../RunInternal.h"
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
                                                          IsGfxReady() );
}

bool RuntimeRenderHost::IsLauncherCameraMode() const
{
    return m_camera.mode == RunCameraMode::Launcher;
}

uint32_t RuntimeRenderHost::TextureHandle( uint32_t textureHash ) const
{
    // Hazard: render passes ask for texture handles before drawing, but tools
    // and headless validations can temporarily run without a texture collection.
    // Fail loudly instead of returning a sentinel that would mask a bad binding.
    if ( !m_systems.textures )
    {
        throw std::runtime_error( "Texture collection is not initialised." );
    }
    return m_systems.textures->GetTextureHandle( textureHash );
}

void RuntimeRenderHost::SelectRenderTexture( uint32_t textureHash ) const
{
    if ( !m_systems.textures )
    {
        throw std::runtime_error( "Texture collection is not initialised." );
    }
    m_systems.textures->SelectTexture( textureHash );
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
    // Lifetime: the process renderer remains owned by the bootstrap backend.
    // The host exposes only a per-call borrow so input/scene helpers do not
    // retain renderer pointers across teardown.
    return SkullbonezCore::Rendering::IsGfxReady() ? &SkullbonezCore::Rendering::Gfx() : nullptr;
}


SkullbonezCore::Rendering::IRenderRayTracing* RuntimeRenderHost::ActiveRayTracingBackend() const
{
    return SkullbonezCore::Rendering::IsGfxRayTracingReady() ? &SkullbonezCore::Rendering::GfxRayTracing() : nullptr;
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

ReplayOverlay::ReplayOverlayRenderContext RuntimeRenderHost::BuildReplayOverlayRenderContext() const
{
    return { m_replayRuntime,
             m_cGameModelCollection.Models(),
             m_editor.editorModeEnabled,
             m_UI.IsVisible(),
             m_UI.IsMinimized(),
             SceneState().isScenePhysics,
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

int RuntimeRenderHost::CurrentSceneBrowserIndex() const
{
    return SkullbonezCore::Basics::CurrentSceneBrowserIndex( m_sceneController, m_sceneBrowser );
}

bool RuntimeRenderHost::ToolHasSelectionOverlayWork() const
{
    return m_runtimeTools.HasSelectionOverlayWork( m_cGameModelCollection.GetModelCount(), m_camera.mode );
}

bool RuntimeRenderHost::ToolHasMousePickupOverlayWork() const
{
    return m_runtimeTools.HasMousePickupOverlayWork( m_cGameModelCollection.GetModelCount() );
}

MainMemoryStats RuntimeRenderHost::RefreshMainMemoryStats( double nowSeconds ) const
{
    return m_diagnosticsRuntime.RefreshMainMemoryStats( m_replayRuntime, m_cGameModelCollection, nowSeconds, false );
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
