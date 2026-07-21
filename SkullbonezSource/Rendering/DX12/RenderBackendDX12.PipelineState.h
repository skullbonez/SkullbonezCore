/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.PipelineState.h
Purpose:
  Defines the allocation-free command-binding reset record owned by
  Dx12PipelineOwner.

Summary:
  Pipeline COM objects have explicit release logic, while dynamic output and
  command-binding state is a plain reusable value. Raster state is absent: each
  draw supplies its complete `RasterStateDesc` directly.

Glossary:
  PSO (Pipeline State Object): Compiled shader and fixed-function draw recipe.
  RTV (Render Target View): Descriptor row naming the current color output.
  DSV (Depth Stencil View): Descriptor row naming the current depth output.

Invariants:
  - Reset leaves all binding categories dirty so the next draw fully publishes state.
  - This record owns no COM reference and remains safe for CPU-only architecture tests.

Related:
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
  - Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp
*/
#pragma once

#include "../RenderCommandTypes.h"

#include <d3d12.h>
#include <cstddef>

namespace SkullbonezCore
{
namespace Rendering
{
class ShaderDX12;

struct Dx12PipelineCommandState
{
    void Reset()
    {
        // Invariant: lifecycle reuse must behave like a fresh owner, including
        // dirty flags that force complete publication on the first draw.
        m_activeShader = nullptr;
        m_viewport = {};
        m_scissorRect = {};
        m_currentRTV = {};
        m_currentDSV = {};
        m_currentRTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        m_renderingToFBO = false;
        m_lastPSOHash = 0;
        m_pipelineBindingDirty = true;
        m_targetsDirty = true;
    }

    const ShaderDX12* m_activeShader = nullptr; // Borrowed identity; the owner never releases a shader.
    D3D12_VIEWPORT m_viewport = {};
    D3D12_RECT m_scissorRect = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentRTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentDSV = {};
    DXGI_FORMAT m_currentRTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool m_renderingToFBO = false;
    size_t m_lastPSOHash = 0;
    bool m_pipelineBindingDirty = true;
    bool m_targetsDirty = true;
};
} // namespace Rendering
} // namespace SkullbonezCore
