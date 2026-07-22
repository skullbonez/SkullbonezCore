/*
File: SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.h
Purpose:
  Declares the cold DX12 shader rebake and transactional adoption owner.

Summary:
  Live raster shaders register with one fixed-capacity owner. A developer reload
  bakes one complete offline-DXC generation, stages every raster and compute
  replacement, drains the concrete frame epoch, and publishes the generation
  transactionally. Per-frame draw and pipeline-selection policy remain outside
  this owner.

Glossary:
  Live shader registry: Bounded table of ShaderDX12 objects eligible for manual
    replacement during the current backend lifetime.
  Candidate generation: Verified replacement bytecode and native pipeline
    objects that are not visible to rendering until the no-fail commit begins.
  Cold path: Explicit developer action outside steady frame allocation and
    submission accounting.

Invariants:
  - Registration never grows dynamically; capacity exhaustion is Lane F.
  - A bake or candidate-validation failure leaves every live shader and PSO unchanged.
  - ReloadShadersFromSource proves GPU completion before releasing old PSOs.
  - This owner stores concrete shader-domain owners, never the aggregate backend
    or per-frame command authority.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.cpp
  - SkullbonezSource/Rendering/DX12/ShaderDX12.cpp
  - SkullbonezSource/Runtime/InputFrameExecution.cpp
*/
#pragma once

#include "../../Core/SbResult.h"

#include <array>
#include <cstddef>

struct ID3D12Device;

namespace SkullbonezCore::Rendering
{
class Dx12GeometryOwner;
class Dx12Diagnostics;
class Dx12FrameOwner;
class Dx12PipelineOwner;
class Dx12RenderDevice;
class Dx12TextureOwner;
class ShaderDX12;

class Dx12ShaderDevelopment
{
  public:
    Dx12ShaderDevelopment( Dx12PipelineOwner& pipeline,
                           Dx12TextureOwner& textures,
                           Dx12GeometryOwner& geometry,
                           Dx12RenderDevice& device,
                           Dx12FrameOwner& frame,
                           Dx12Diagnostics& diagnostics );

    bool Enabled() const;
    SkullbonezCore::Core::SbResult ReloadShadersFromSource();
    void RegisterShader( ShaderDX12* shader );
    void UnregisterShader( ShaderDX12* shader );

    // Runs the pinned offline bake only. The composition root drains submitted
    // GPU work after this succeeds and before calling ReloadBakedGeneration.
    SkullbonezCore::Core::SbResult BakeSourceGeneration() const;
    SkullbonezCore::Core::SbResult ReloadBakedGeneration( ID3D12Device* device );
    void ResetAfterShutdown();

  private:
    static constexpr size_t LIVE_SHADER_CAPACITY = 64;

    Dx12PipelineOwner& m_pipeline;
    Dx12TextureOwner& m_textures;
    Dx12GeometryOwner& m_geometry;
    Dx12RenderDevice& m_device;
    Dx12FrameOwner& m_frame;
    Dx12Diagnostics& m_diagnostics;
    // Lifetime: rows borrow ShaderDX12 objects. Geometry-owned rows unregister
    // before reset; the final reset also makes late external shader teardown inert.
    std::array<ShaderDX12*, LIVE_SHADER_CAPACITY> m_liveShaders = {};
    size_t m_liveShaderCount = 0;
};
} // namespace SkullbonezCore::Rendering
