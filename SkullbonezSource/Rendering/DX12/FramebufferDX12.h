/*
File: SkullbonezSource/Rendering/DX12/FramebufferDX12.h
Purpose:
  Declares off-screen framebuffer resources and descriptor views for the DX12 renderer.

Mental model:
  DX12 separates resource memory, descriptor rows, command recording, and GPU
  execution. Ownership, state transitions, descriptor lifetime, and fence
  ordering are the important ideas.

Glossary:
  RTV (Render Target View): Descriptor row used when the GPU writes color
  pixels into a texture or back buffer.
  DSV (Depth Stencil View): Descriptor row used when the GPU reads or writes
  depth/stencil data for depth testing.
  SRV (Shader Resource View): Descriptor row used when shaders read textures
  or buffers.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../IFramebuffer.h"
#include <d3d12.h>
#include <cstdint>


namespace SkullbonezCore
{
namespace Rendering
{

class RenderBackendDX12;


/* -- FramebufferDX12
-------------------------------------------------------------------------------------------------------------------------------------------

    DirectX 12 off-screen framebuffer.

    The engine-facing idea is "draw into a texture, then sample it later." DX12
    expresses that through separate resources and descriptors: an RTV for color
    writes, a DSV for depth writes, and SRVs for later shader reads.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class FramebufferDX12 : public IFramebuffer
{

  private:
    ID3D12Resource* m_colorTexture;
    ID3D12Resource* m_depthTexture;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsvHandle;
    // Static descriptor rows that expose the color/depth resources to shaders
    // after the framebuffer is unbound.
    UINT m_srvIndex;
    UINT m_depthSrvIndex;
    uint32_t m_texHandle;      // Color handle returned by backend's texture registry
    uint32_t m_depthTexHandle; // Depth handle returned by backend's texture registry
    FramebufferColorFormat m_colorFormat;
    int m_width;
    int m_height;
    mutable D3D12_RESOURCE_STATES m_depthState;

    // Saved main targets for restore on Unbind
    mutable D3D12_CPU_DESCRIPTOR_HANDLE m_savedRTV;
    mutable D3D12_CPU_DESCRIPTOR_HANDLE m_savedDSV;

  public:
    explicit FramebufferDX12( FramebufferColorFormat colorFormat = FramebufferColorFormat::RGBA8 );
    ~FramebufferDX12() override;

    void Create( int width, int height );

    void Bind() const override;
    void Unbind() const override;
    uint32_t GetColorTextureHandle() const override
    {
        return m_texHandle;
    }
    uint32_t GetDepthTextureHandle() const override
    {
        return m_depthTexHandle;
    }
    FramebufferColorFormat GetColorFormat() const override
    {
        return m_colorFormat;
    }
    int GetWidth() const override
    {
        return m_width;
    }
    int GetHeight() const override
    {
        return m_height;
    }
    void ResetResources() override;

    ID3D12Resource* GetColorResource() const
    {
        return m_colorTexture;
    }
    UINT GetSRVIndex() const
    {
        return m_srvIndex;
    }
};
} // namespace Rendering
} // namespace SkullbonezCore
