/*
File: SkullbonezSource/SkullbonezFramebufferDX11.h
Purpose:
  Declares off-screen framebuffer resources for the DX11 parity renderer.

Mental model:
  DX11 is a legacy parity renderer. It follows the renderer interface while
  staying close enough to DX12 and OpenGL output for visual comparison.

Glossary:
  SRV (Shader Resource View): Descriptor row used when shaders read textures
  or buffers.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Parity renderer output should stay visually aligned with the DX12
  production path while these backends remain.

Related:
  - SkullbonezSource/SkullbonezFramebufferDX11.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "SkullbonezIFramebuffer.h"
#include <d3d11.h>


namespace SkullbonezCore
{
namespace Rendering
{

class RenderBackendDX11;

/* -- FramebufferDX11 ---------------------------------------------------------------------------------------------------------------------------------------------

    DirectX 11 implementation of the IFramebuffer interface.
    Creates an offscreen render target with a color texture (SRV) and depth stencil view.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class FramebufferDX11 : public IFramebuffer
{

  private:
    RenderBackendDX11* m_backend;
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    ID3D11Texture2D* m_colorTex;
    ID3D11RenderTargetView* m_rtv;
    ID3D11ShaderResourceView* m_srv;
    ID3D11Texture2D* m_depthTex;
    ID3D11DepthStencilView* m_dsv;
    ID3D11ShaderResourceView* m_depthSRV;
    uint32_t m_textureHandle;
    uint32_t m_depthTextureHandle;
    FramebufferColorFormat m_colorFormat;
    int m_width;
    int m_height;

    // Saved state for Bind/Unbind
    mutable ID3D11RenderTargetView* m_savedRTV;
    mutable ID3D11DepthStencilView* m_savedDSV;

  public:
    FramebufferDX11( RenderBackendDX11* backend, ID3D11Device* device, ID3D11DeviceContext* context, FramebufferColorFormat colorFormat = FramebufferColorFormat::RGBA8 );
    ~FramebufferDX11() override;

    bool Create( int width, int height );

    void Bind() const override;
    void Unbind() const override;
    uint32_t GetColorTextureHandle() const override;
    uint32_t GetDepthTextureHandle() const override;
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
};
} // namespace Rendering
} // namespace SkullbonezCore
