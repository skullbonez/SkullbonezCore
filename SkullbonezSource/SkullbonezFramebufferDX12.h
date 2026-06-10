#pragma once


// --- Includes ---
#include "SkullbonezIFramebuffer.h"
#include <d3d12.h>
#include <cstdint>


namespace SkullbonezCore
{
namespace Rendering
{

class RenderBackendDX12;


/* -- FramebufferDX12 -------------------------------------------------------------------------------------------------------------------------------------------

    DirectX 12 offscreen FramebufferGL. Color texture (RENDER_TARGET + SRV) and depth buffer.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class FramebufferDX12 : public IFramebuffer
{

  private:
    ID3D12Resource* m_colorTexture;
    ID3D12Resource* m_depthTexture;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsvHandle;
    UINT m_srvIndex;           // Color SRV index in the static SRV region
    UINT m_depthSrvIndex;      // Depth SRV index in the static SRV region
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
