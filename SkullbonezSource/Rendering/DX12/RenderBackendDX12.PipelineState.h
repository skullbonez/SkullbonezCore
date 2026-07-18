/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.PipelineState.h
Purpose:
  Defines the allocation-free desired-state record owned by Dx12PipelineOwner.

Summary:
  Pipeline COM objects have explicit release logic, while desired draw state is
  a plain reusable value. Resetting this record restores the exact state a new
  backend publishes before its first ordinary raster draw.

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

#include "../IRenderCommandContext.h"

#include <d3d12.h>
#include <cstddef>

namespace SkullbonezCore
{
namespace Rendering
{
class ShaderDX12;

struct Dx12PipelineDesiredState
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
        m_depthTestEnabled = true;
        m_depthWriteEnabled = true;
        m_blendEnabled = false;
        m_blendSrc = BlendFactor::One;
        m_blendDst = BlendFactor::Zero;
        m_cullEnabled = true;
        m_polyOffsetEnabled = false;
        m_polyOffsetFactor = 0.0f;
        m_polyOffsetUnits = 0.0f;
        m_renderingToFBO = false;
        m_lastPSOHash = 0;
        m_psoDirty = true;
        m_targetsDirty = true;
    }

    const ShaderDX12* m_activeShader = nullptr; // Borrowed identity; the owner never releases a shader.
    D3D12_VIEWPORT m_viewport = {};
    D3D12_RECT m_scissorRect = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentRTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentDSV = {};
    DXGI_FORMAT m_currentRTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool m_depthTestEnabled = true;
    bool m_depthWriteEnabled = true;
    bool m_blendEnabled = false;
    BlendFactor m_blendSrc = BlendFactor::One;
    BlendFactor m_blendDst = BlendFactor::Zero;
    bool m_cullEnabled = true;
    bool m_polyOffsetEnabled = false;
    float m_polyOffsetFactor = 0.0f;
    float m_polyOffsetUnits = 0.0f;
    bool m_renderingToFBO = false;
    size_t m_lastPSOHash = 0;
    bool m_psoDirty = true;
    bool m_targetsDirty = true;
};
} // namespace Rendering
} // namespace SkullbonezCore
