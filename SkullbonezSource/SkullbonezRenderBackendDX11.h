#pragma once


// --- Includes ---
#include "SkullbonezIRenderBackend.h"
#include <d3d11.h>
#include <dxgi.h>
#include <vector>


namespace SkullbonezCore
{
namespace Rendering
{

class ShaderDX11;


// Texture entry for the DX SRV registry
struct TextureEntryDX
{
    ID3D11ShaderResourceView* srv;
    ID3D11Texture2D* tex;
    ID3D11SamplerState* sampler;
    bool owned; // false for FBO-registered SRVs (not owned by this entry)
};


// Dynamic vertex buffer for per-frame geometry (text, HUD)
struct DynamicVBDX
{
    ID3D11Buffer* vb;
    ID3D11InputLayout* inputLayout;
    int floatsPerVertex;
    int maxVertices;
    int stride;
    const void* lastVSBytecode;
    int numAttribs;
    int attribComponents[8];
};


// Instanced MeshGL (shadow decals)
struct InstancedMeshDX
{
    ID3D11Buffer* staticVB;
    ID3D11Buffer* instanceVB;
    ID3D11InputLayout* inputLayout;
    int staticFloatsPerVert;
    int staticStride;
    int instanceFloats;
    int instanceStride;
    const void* lastVSBytecode;
    int instanceStartAttrib;
    int numInstanceAttribs;
    int instanceAttribSizes[8];
};


/* -- RenderBackendDX11 -------------------------------------------------------------------------------------------------------------------------------------------

    DirectX 11 implementation of the render backend interface.
    Manages D3D11 device, immediate context, swap chain, and all rendering state.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class RenderBackendDX11 : public IRenderBackend
{

  private:
    static RenderBackendDX11* s_instance;

    IDXGISwapChain* m_swapChain;
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    ID3D11RenderTargetView* m_backBufferRTV;
    ID3D11Texture2D* m_depthStencilTex;
    ID3D11DepthStencilView* m_depthStencilView;
    int m_width;
    int m_height;

    // State tracking
    bool m_depthTestEnabled;
    bool m_blendEnabled;
    float m_clearColor[4];
    float m_clearDepth;

    // State objects
    ID3D11DepthStencilState* m_dsDepthOn;
    ID3D11DepthStencilState* m_dsDepthOff;
    ID3D11BlendState* m_blendOn;
    ID3D11BlendState* m_blendOff;
    ID3D11RasterizerState* m_rsCullOn;
    ID3D11RasterizerState* m_rsCullOff;
    ID3D11RasterizerState* m_rsCullOnPolyOffset;
    ID3D11RasterizerState* m_rsCullOffPolyOffset;
    ID3D11SamplerState* m_samplerLinear;
    ID3D11SamplerState* m_samplerNearest;

    bool m_cullEnabled;
    bool m_polyOffsetEnabled;

    // Texture registry
    std::vector<TextureEntryDX> m_textures;

    // Dynamic VBs and instanced meshes
    std::vector<DynamicVBDX> m_dynamicVBs;
    std::vector<InstancedMeshDX> m_instancedMeshes;

    // Active ShaderGL
    ShaderDX11* m_activeShader;

    void CreateStateObjects();
    void ApplyRasterizerState();

  public:
    RenderBackendDX11();
    ~RenderBackendDX11() override
    {
        Shutdown();
    }

    static RenderBackendDX11* Get()
    {
        return s_instance;
    }

    bool Init( HWND hwnd, HDC hdc, int width, int height ) override;
    void Shutdown() override;
    void Present() override;
    void Finish() override;
    void FlushGPU() override;
    void Resize( int width, int height ) override;

    void SetViewport( int x, int y, int w, int h ) override;
    void Clear( bool color, bool depth ) override;
    void SetClearColor( float r, float g, float b, float a ) override;
    void SetClearDepth( float depth ) override;

    void SetDepthTest( bool enable ) override;
    void SetBlend( bool enable ) override;
    void SetBlendFunc( BlendFactor src, BlendFactor dst ) override;
    void SetCullFace( bool enable ) override;
    void SetPolygonOffset( bool enable, float factor = 0.0f, float units = 0.0f ) override;
    void SetClipPlane( int index, bool enable ) override;

    std::unique_ptr<IShader> CreateShader( const char* vertPath, const char* fragPath ) override;
    std::unique_ptr<IMesh> CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords ) override;
    std::unique_ptr<IFramebuffer> CreateFramebuffer( int width, int height ) override;

    uint32_t CreateTexture2D( const uint8_t* data, int w, int h, int channels, bool generateMips, bool linearFilter ) override;
    void BindTexture( uint32_t handle, int slot ) override;
    void DeleteTexture( uint32_t handle ) override;

    std::vector<uint8_t> CaptureBackbuffer( int& outWidth, int& outHeight ) override;

    int GetWidth() const override;
    int GetHeight() const override;

    bool IsDepthTestEnabled() const override;
    bool IsBlendEnabled() const override;
    bool UsesZeroToOneDepth() const override;
    const char* GetRendererName() const override
    {
        return "DirectX 11";
    }

    uint32_t CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices ) override;
    void UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount ) override;
    void DestroyDynamicVB( uint32_t handle ) override;

    uint32_t CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int maxInstances, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs ) override;
    void UploadInstanceData( uint32_t handle, const float* data, int floatCount ) override;
    void DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount ) override;
    void DestroyInstancedMesh( uint32_t handle ) override;

    // DX-specific helpers
    void SetActiveShader( ShaderDX11* ShaderGL )
    {
        m_activeShader = ShaderGL;
    }
    ShaderDX11* GetActiveShader() const
    {
        return m_activeShader;
    }
    ID3D11Device* GetDevice() const
    {
        return m_device;
    }
    ID3D11DeviceContext* GetContext() const
    {
        return m_context;
    }

    uint32_t RegisterSRV( ID3D11ShaderResourceView* srv );
    void UnregisterSRV( uint32_t handle );
};
} // namespace Rendering
} // namespace SkullbonezCore