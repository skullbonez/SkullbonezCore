/*
File: SkullbonezSource/Rendering/DX12/FramebufferDX12.h
Purpose:
  Declares off-screen framebuffer resources and descriptor views for the DX12 renderer.

Summary:
  FramebufferDX12.h declares off-screen framebuffer resources and their views.
  It borrows one Dx12DescriptorHeaps owner for RTV, DSV, and SRV rows so the
  framebuffer cannot retain allocator aliases with independent lifetimes.

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
  - The descriptor owner, texture, pipeline, recording, retirement, and device
    references outlive every framebuffer created for that device epoch.
  - Bind/Unbind never emits a resource barrier; the render graph owns state.

Related:
  - SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../IFramebuffer.h"
#include <d3d12.h>
#include <cstdint>
#include <vector>


namespace SkullbonezCore
{
namespace Rendering
{

class Dx12RenderDevice;
class Dx12PipelineOwner;
class Dx12TextureOwner;
class Dx12DescriptorHeaps;
class Dx12CommandRecordingState;
class Dx12DeferredReleaseOwner;
class Dx12DrawGate;
class Dx12ResourceRelease;


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
    // Lifetime: these named owners are stable members of the active renderer.
    // No field points back to the aggregate backend.
    Dx12RenderDevice& m_device;
    Dx12PipelineOwner& m_pipeline;
    Dx12TextureOwner& m_textures;
    Dx12DescriptorHeaps& m_descriptors;
    Dx12DrawGate& m_drawGate;
    Dx12ResourceRelease& m_resourceRelease;
    ID3D12Resource* m_colorTexture;
    ID3D12Resource* m_depthTexture;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsvHandle;
    UINT m_rtvIndex;
    UINT m_dsvIndex;
    // Static descriptor rows that expose the color/depth resources to shaders
    // after the framebuffer is unbound.
    UINT m_srvIndex;
    UINT m_depthSrvIndex;
    uint32_t m_texHandle;      // Color handle returned by backend's texture registry
    uint32_t m_depthTexHandle; // Depth handle returned by backend's texture registry
    FramebufferColorFormat m_colorFormat;
    int m_width;
    int m_height;
    // Saved main targets for restore on Unbind
    mutable D3D12_CPU_DESCRIPTOR_HANDLE m_savedRTV;
    mutable D3D12_CPU_DESCRIPTOR_HANDLE m_savedDSV;

  public:
    FramebufferDX12( Dx12RenderDevice& device,
                     Dx12PipelineOwner& pipeline,
                     Dx12TextureOwner& textures,
                     Dx12DescriptorHeaps& descriptors,
                     Dx12DrawGate& drawGate,
                     Dx12ResourceRelease& resourceRelease,
                     FramebufferColorFormat colorFormat = FramebufferColorFormat::RGBA8 );
    ~FramebufferDX12() override;

    bool Create( int width, int height );

    // The render graph owns resource states. These methods bind/restore output
    // descriptors only and never emit barriers.
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
