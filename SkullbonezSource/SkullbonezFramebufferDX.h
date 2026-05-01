#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace SkullbonezCore
{
namespace Rendering
{
class Framebuffer
{

  private:
    ComPtr<ID3D11Texture2D> m_colorTexture;
    ComPtr<ID3D11RenderTargetView> m_renderTargetView;
    ComPtr<ID3D11Texture2D> m_depthStencilTexture;
    ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    ComPtr<ID3D11ShaderResourceView> m_colorSRV;
    int m_width;
    int m_height;

    void Build();

  public:
    Framebuffer( int width, int height );
    ~Framebuffer();

    void Bind() const;
    void Unbind() const;
    ID3D11ShaderResourceView* GetColorTexture() const;
    int GetWidth() const;
    int GetHeight() const;
    void ResetGLResources();
};
} // namespace Rendering
} // namespace SkullbonezCore
