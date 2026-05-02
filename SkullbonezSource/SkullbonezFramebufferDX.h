#pragma once


// --- Includes ---
#include "SkullbonezIFramebuffer.h"
#include <d3d11.h>


namespace SkullbonezCore
{
namespace Rendering
{

class RenderBackendDX;

/* -- FramebufferDX ---------------------------------------------------------------------------------------------------------------------------------------------

    DirectX 11 implementation of the IFramebuffer interface.
    Creates an offscreen render target with a color texture (SRV) and depth stencil view.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class FramebufferDX : public IFramebuffer
{

  private:
    RenderBackendDX* m_backend;
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    ID3D11Texture2D* m_colorTex;
    ID3D11RenderTargetView* m_rtv;
    ID3D11ShaderResourceView* m_srv;
    ID3D11Texture2D* m_depthTex;
    ID3D11DepthStencilView* m_dsv;
    uint32_t m_textureHandle;
    int m_width;
    int m_height;

    // Saved state for Bind/Unbind
    mutable ID3D11RenderTargetView* m_savedRTV;
    mutable ID3D11DepthStencilView* m_savedDSV;

  public:
    FramebufferDX( RenderBackendDX* backend, ID3D11Device* device, ID3D11DeviceContext* context );
    ~FramebufferDX() override;

    bool Create( int width, int height );

    void Bind() const override;
    void Unbind() const override;
    uint32_t GetColorTextureHandle() const override;
    int GetWidth() const override
    {
        return m_width;
    }
    int GetHeight() const override
    {
        return m_height;
    }
    void ResetResources() override;
};
} // namespace Rendering
} // namespace SkullbonezCore