// --- Includes ---
#include "SkullbonezRenderState.h"
#include "SkullbonezWindowDX.h"

// --- Usings ---
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Basics;

static Microsoft::WRL::ComPtr<ID3D11DepthStencilState> g_depthStencilState;
static Microsoft::WRL::ComPtr<ID3D11BlendState> g_blendState;
static float g_clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static float g_clearDepth = 1.0f;

void RenderState::SetupInitial()
{
    ID3D11Device* device = SkullbonezWindow::Instance()->GetDevice();
    ID3D11DeviceContext* context = SkullbonezWindow::Instance()->GetDeviceContext();

    // Create depth stencil state
    D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    depthStencilDesc.StencilEnable = FALSE;
    device->CreateDepthStencilState( &depthStencilDesc, &g_depthStencilState );
    context->OMSetDepthStencilState( g_depthStencilState.Get(), 0 );

    // Create blend state
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState( &blendDesc, &g_blendState );

    SetClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
    SetClearDepth( 1.0f );
}

void RenderState::EnableDepthTest()
{
    if ( g_depthStencilState )
    {
        SkullbonezWindow::Instance()->GetDeviceContext()->OMSetDepthStencilState( g_depthStencilState.Get(), 0 );
    }
}

void RenderState::DisableDepthTest()
{
    SkullbonezWindow::Instance()->GetDeviceContext()->OMSetDepthStencilState( nullptr, 0 );
}

void RenderState::EnableBlending()
{
    if ( g_blendState )
    {
        float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        SkullbonezWindow::Instance()->GetDeviceContext()->OMSetBlendState( g_blendState.Get(), blendFactor, 0xFFFFFFFF );
    }
}

void RenderState::DisableBlending()
{
    SkullbonezWindow::Instance()->GetDeviceContext()->OMSetBlendState( nullptr, nullptr, 0xFFFFFFFF );
}

void RenderState::SetClearColor( float r, float g, float b, float a )
{
    g_clearColor[0] = r;
    g_clearColor[1] = g;
    g_clearColor[2] = b;
    g_clearColor[3] = a;
}

void RenderState::SetClearDepth( float depth )
{
    g_clearDepth = depth;
}

void RenderState::ClearFramebuffer()
{
    ID3D11DeviceContext* context = SkullbonezWindow::Instance()->GetDeviceContext();
    context->ClearRenderTargetView( SkullbonezWindow::Instance()->m_renderTargetView.Get(), g_clearColor );
    context->ClearDepthStencilView( SkullbonezWindow::Instance()->m_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, g_clearDepth, 0 );
}
