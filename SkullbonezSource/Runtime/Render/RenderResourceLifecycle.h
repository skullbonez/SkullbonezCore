/*
File: SkullbonezSource/Runtime/Render/RenderResourceLifecycle.h
Purpose:
  Owns renderer resources whose validity follows the active DX12 backend epoch.

Summary:
  RenderResourceLifecycle binds the established backend and world owner views,
  creates process and scene resources, and projects safe preview values. Frame
  ordering remains in RuntimeRenderer; backend-epoch state does not.

Glossary:
  Backend epoch: The interval during which one set of concrete DX12 owners is
    valid and may create, use, or release GPU resources.
  Warmup: Cold scene-activation work that creates optional ray-tracing geometry.
  Preview snapshot: Value-only render-target metadata safe for UI consumption.

Invariants:
  - Every stored owner borrow outlives this lifecycle owner.
  - Backend-owned resources are released before the enclosing backend view dies.
  - Preview projection never exposes framebuffer or pass-resource ownership.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderResources.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
*/
#pragma once

#include "RuntimeRenderHost.h"
#include "RuntimeRenderPasses.h"
#include "RuntimeRenderResources.h"
#include "../../Assets/TextureCollection.h"
#include "../../Rendering/PrimitiveBatchRenderer.h"
#include "../../Rendering/RenderGpuTimingOwner.h"

#include <memory>
#include <optional>

namespace SkullbonezCore
{
namespace Geometry
{
class SkyBox;
}
namespace Runtime
{
class RenderResourceLifecycle
{
  public:
    RenderResourceLifecycle( RuntimeRenderBackendView backend, const RenderWorldView& world,
                             const SceneSessionState& scene );
    ~RenderResourceLifecycle();

    SkullbonezCore::Core::SbResult InitialiseProcessResources( bool dumpTextureAssets );
    SkullbonezCore::Core::SbResult EnsureUiTextResources( int screenW, int screenH );
    SkullbonezCore::Core::SbResult InitialiseSceneRayTracing( int modelCapacity );
    RuntimeRenderTargetPreviewSnapshot BuildRenderTargetPreviewSnapshot( bool shadowsAvailable,
                                                                         bool cinematicTargetsAvailable,
                                                                         bool volumetricAvailable ) const;
    bool ShouldRenderUiText( const OverlayDebugState& debug, const SceneSessionState& scene, bool crossScenePauseLocked,
                             const CameraControlState& camera, const UI::InGameUI& ui, bool replayScrubberVisible,
                             bool replayPathVisualizerHasTarget ) const;
    void SetUiTextRayTracingCapability( Rendering::Dx12RaytracingOwner* rayTracing );

  private:
    friend class RuntimeRenderer;

    // Concept: only capabilities that participate in renderer resource setup,
    // frame recording, or release survive construction. Capture, shader
    // development, and development-UI owners remain at the composition root.
    struct BackendEpochOwners
    {
        Rendering::Dx12FrameOwner* renderFrame = nullptr;
        Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
        Rendering::Dx12ResourceBuilder* renderResources = nullptr;
        Rendering::Dx12TextureOwner* renderTextures = nullptr;
        Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
        Rendering::Dx12Diagnostics* renderDiagnostics = nullptr;
        Rendering::Dx12RaytracingOwner* raytracing = nullptr;
    };

    // Lifetime: teardown remains ordered by RuntimeRenderer because pass
    // consumers must release between these owned phases. Each command mutates
    // only state genuinely owned here; it never reaches back into Run.
    void ReleaseHelperResources();
    void ReleaseUiTextResources();
    void InvalidateProfilerResources();
    void ReleaseTextureResources();
    void ReleaseSkyResources();

    const BackendEpochOwners& Backend() const
    {
        return m_backend;
    }
    RenderResourceLifecycleLog& Log()
    {
        return m_lifecycleLog;
    }
    Assets::AssetSystem& Assets()
    {
        return m_assets;
    }
    Textures::TextureCollection& Textures()
    {
        return m_textures;
    }
    SceneTerrain& Terrain()
    {
        return m_terrain;
    }
    Geometry::SkyBox* SkyBox() const
    {
        return m_skyBox.get();
    }
    std::unique_ptr<Geometry::SkyBox>& SkyBoxOwner()
    {
        return m_skyBox;
    }
    RuntimeRenderPassResources& PassResources()
    {
        return m_passResources;
    }
    const RuntimeRenderPassResources& PassResources() const
    {
        return m_passResources;
    }
    SkullbonezCore::Core::EngineConfig& Config()
    {
        return m_config;
    }
    Rendering::PrimitiveBatchRenderer& PrimitiveBatches();
    const Rendering::PrimitiveBatchRenderer& PrimitiveBatches() const;
    Rendering::RenderGpuTimingOwner& GpuTiming()
    {
        return m_gpuTiming;
    }
    UiTextPass& UiText()
    {
        return m_uiTextPass;
    }
    const UiTextPass& UiText() const
    {
        return m_uiTextPass;
    }

    // Concept: this is a cohesive backend-epoch owner, not a generic service
    // bag. Every member either creates, names, previews, or releases resources
    // whose handles become invalid together when the backend is rebuilt.
    BackendEpochOwners m_backend;
    RenderResourceLifecycleLog m_lifecycleLog;
    Assets::AssetSystem& m_assets;
    Textures::TextureCollection m_textures;
    SceneTerrain& m_terrain;
    std::unique_ptr<Geometry::SkyBox> m_skyBox;
    RuntimeRenderPassResources m_passResources;
    SkullbonezCore::Core::EngineConfig& m_config;
    std::optional<Rendering::PrimitiveBatchRenderer> m_primitiveBatches;
    Rendering::RenderGpuTimingOwner m_gpuTiming;
    UiTextPass m_uiTextPass;
};
} // namespace Runtime
} // namespace SkullbonezCore
