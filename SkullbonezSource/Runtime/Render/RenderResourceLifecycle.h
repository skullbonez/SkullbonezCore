/*
File: SkullbonezSource/Runtime/Render/RenderResourceLifecycle.h
Purpose:
  Owns renderer resources whose validity follows the active DX12 backend epoch.

Summary:
  RenderResourceLifecycle binds concrete backend owners and the established
  world owner view, creates process and scene resources, and projects safe
  preview values. Frame ordering remains in RuntimeRenderer; backend-epoch
  state does not.

Glossary:
  Backend epoch: The interval during which one set of concrete DX12 owners is
    valid and may create, use, or release GPU resources.
  Warmup: Cold scene-activation work that creates optional ray-tracing geometry.
  Preview snapshot: Value-only render-target metadata safe for UI consumption.

Invariants:
  - Every stored borrowed reference outlives this lifecycle owner.
  - Backend-owned resources are released before the concrete backend owners die.
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

#include <functional>
#include <memory>
#include <optional>

namespace SkullbonezCore
{
namespace Geometry
{
class SkyBox;
}
namespace Rendering
{
class Dx12Diagnostics;
class Dx12FrameOwner;
class Dx12GeometryOwner;
class Dx12GraphTransientPool;
class Dx12RaytracingOwner;
class Dx12RenderDevice;
class Dx12ResourceBuilder;
class Dx12TextureOwner;
class RenderBackendDX12;
} // namespace Rendering
namespace Runtime
{
class RenderResourceLifecycle
{
  public:
    RenderResourceLifecycle( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                             Rendering::RenderBackendDX12& backend, const RenderWorldView& world, int sceneIndex,
                             int sceneLoadCount );
    ~RenderResourceLifecycle();

    SkullbonezCore::Core::SbResult InitialiseProcessResources( bool dumpTextureAssets );
    SkullbonezCore::Core::SbResult EnsureUiTextResources( int screenW, int screenH );
    SkullbonezCore::Core::SbResult InitialiseSceneRayTracing( Geometry::Terrain* terrain, int modelCapacity );
    RuntimeRenderTargetPreviewSnapshot BuildRenderTargetPreviewSnapshot( bool shadowsAvailable,
                                                                         bool cinematicTargetsAvailable,
                                                                         bool volumetricAvailable ) const;
    bool ShouldRenderUiText( const UiTextVisibility& visibility ) const;
    void SetUiTextDxrReflectionPreviewTexture( uint32_t textureHandle );

  private:
    friend class RuntimeRenderer;

    Rendering::Dx12FrameOwner& RenderFrame() const
    {
        return m_renderFrame;
    }
    Rendering::Dx12RenderDevice& RenderDevice() const
    {
        return m_renderDevice;
    }
    Rendering::Dx12GraphTransientPool& RenderGraph() const
    {
        return m_renderGraph;
    }
    Rendering::Dx12ResourceBuilder& RenderResources() const
    {
        return m_renderResources;
    }
    Rendering::Dx12TextureOwner& RenderTextures() const
    {
        return m_renderTextures;
    }
    Rendering::Dx12GeometryOwner& RenderGeometry() const
    {
        return m_renderGeometry;
    }
    Rendering::Dx12Diagnostics& RenderDiagnostics() const
    {
        return m_renderDiagnostics;
    }
    Rendering::Dx12RaytracingOwner& Raytracing() const
    {
        return m_raytracing;
    }
    bool RaytracingAvailable() const
    {
        return m_raytracingAvailable;
    }

    // Lifetime: teardown remains ordered by RuntimeRenderer because pass
    // consumers must release between these owned phases. Each command mutates
    // only state genuinely owned here; it never reaches back into Run.
    void ReleaseHelperResources();
    void ReleaseUiTextResources();
    void InvalidateProfilerResources();
    void ReleaseTextureResources();
    void ReleaseSkyResources();

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

    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    Rendering::Dx12RenderDevice& m_renderDevice;
    Rendering::Dx12FrameOwner& m_renderFrame;
    Rendering::Dx12GraphTransientPool& m_renderGraph;
    Rendering::Dx12ResourceBuilder& m_renderResources;
    Rendering::Dx12TextureOwner& m_renderTextures;
    Rendering::Dx12GeometryOwner& m_renderGeometry;
    Rendering::Dx12Diagnostics& m_renderDiagnostics;
    Rendering::Dx12RaytracingOwner& m_raytracing;
    bool m_raytracingAvailable = false;
    RenderResourceLifecycleLog m_lifecycleLog;
    Assets::AssetSystem& m_assets;
    Textures::TextureCollection m_textures;
    std::unique_ptr<Geometry::SkyBox> m_skyBox;
    RuntimeRenderPassResources m_passResources;
    SkullbonezCore::Core::EngineConfig& m_config;
    std::optional<Rendering::PrimitiveBatchRenderer> m_primitiveBatches;
    Rendering::RenderGpuTimingOwner m_gpuTiming;
    UiTextPass m_uiTextPass;
};
} // namespace Runtime
} // namespace SkullbonezCore
