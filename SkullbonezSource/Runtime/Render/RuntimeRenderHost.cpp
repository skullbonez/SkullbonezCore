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

#include "../../Core/Profiler.h"
#include "../../Rendering/IRenderBackend.h"
#include "../RunInternal.h"

#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../RunState.h"
#include "../Scene/SceneRuntimeLoad.h"
#include "../Window.h"

#include <stdexcept>

using namespace SkullbonezCore::Basics;

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

bool RuntimeRenderHost::BuildReplayFocusModelMask( const RenderFrameContext& frame ) const
{
    if ( !frame.bodyStore )
    {
        return false;
    }
    return m_replayRuntime.BuildFocusModelMask( *frame.bodyStore, frame.modelCount );
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
