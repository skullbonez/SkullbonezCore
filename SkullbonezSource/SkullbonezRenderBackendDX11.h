#pragma once


// --- Includes ---
#include "SkullbonezIRenderBackend.h"
#include <d3d11.h>
#include <dxgi1_5.h>
#include <vector>
#include <unordered_map>


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


// Instanced mesh (shadow decals)
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
    int numStaticAttribs;
    int staticAttribSizes[8];
};

struct DrawStateTrackingDX11
{
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    bool blendEnabled = false;
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float clearDepth = 1.0f;
    bool cullEnabled = true;
    bool polyOffsetEnabled = false;
};

struct RenderTargetCacheDX11
{
    ID3D11RenderTargetView* currentRTV = nullptr;
    ID3D11DepthStencilView* currentDSV = nullptr;
};

struct CaptureReadbackStateDX11
{
    ID3D11Texture2D* stagingTex = nullptr;
    int stagingWidth = 0;
    int stagingHeight = 0;
};

struct BlendKeyDX11
{
    BlendFactor src;
    BlendFactor dst;
    bool operator==( const BlendKeyDX11& o ) const
    {
        return src == o.src && dst == o.dst;
    }
};

struct BlendKeyHashDX11
{
    size_t operator()( const BlendKeyDX11& k ) const
    {
        return std::hash<int>()( (int)k.src * 16 + (int)k.dst );
    }
};


// GPU timer constants — must match Profiler::MAX_MARKERS
inline constexpr int DX11_TIMER_MARKERS = 64;
inline constexpr int DX11_TIMER_FRAMES = 2; // one-frame-lag double buffer

// Double-buffered D3D11 GPU timestamp query state.
// DX11 timestamp queries use ID3D11Query rather than a query heap.
// Each frame: one TIMESTAMP_DISJOINT wraps all per-marker TIMESTAMP begin/end pairs.
// Results are read non-blocking one frame later via GetData(DONOTFLUSH).
//
// Timestamp query pattern:
//   GpuTimerBegin(i) -> context->End(ts[write][i][0])   (DX11 "begin" = first End)
//   GpuTimerEnd(i)   -> context->End(ts[write][i][1])
//   Present()        -> context->End(disjoint[write]) -> try read disjoint[read] -> advance write
struct GpuTimerStateDX11
{
    // Disjoint queries — one per double-buffer slot
    // D3D11_QUERY_TIMESTAMP_DISJOINT provides clock frequency + disjoint flag per frame.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query
    ID3D11Query* disjoint[DX11_TIMER_FRAMES] = {};

    // Per-marker timestamp pairs — [frame][markerIdx][0=begin, 1=end]
    // Each marker gets two D3D11_QUERY_TIMESTAMP queries per double-buffer slot.
    // DX11 timestamps require End() for both begin and end (no Begin() for TIMESTAMP).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query
    ID3D11Query* ts[DX11_TIMER_FRAMES][DX11_TIMER_MARKERS][2] = {};

    // Results read back from the previous frame's queries
    float resultMs[DX11_TIMER_MARKERS] = {};
    bool resultValid[DX11_TIMER_MARKERS] = {};

    bool initialized = false;
    int writeIdx = 0;                        // current write frame slot (0 or 1)
    bool disjointBegunThisFrame = false;     // true once Begin(disjoint[writeIdx]) has been issued
    bool frameReady[DX11_TIMER_FRAMES] = {}; // true when a slot has been written and is pending readback
};


/* -- RenderBackendDX11 -------------------------------------------------------------------------------------------------------------------------------------------

    DirectX 11 implementation of the render backend interface.
    Manages D3D11 device, immediate context, swap chain, and all rendering state.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class RenderBackendDX11 : public IRenderBackend
{

  private:
    static RenderBackendDX11* s_instance;

    // Containers
    std::unordered_map<BlendKeyDX11, ID3D11BlendState*, BlendKeyHashDX11> m_blendCache; // Blend state cache keyed by (src, dst) factor pair
    std::vector<TextureEntryDX> m_textures;                                             // Texture registry
    std::vector<DynamicVBDX> m_dynamicVBs;                                              // Dynamic VBs
    std::vector<InstancedMeshDX> m_instancedMeshes;                                     // Instanced meshes

    // Struct state
    DrawStateTrackingDX11 m_drawState;
    RenderTargetCacheDX11 m_targetCache; // Cached render target (avoids OMGetRenderTargets per Clear)
    CaptureReadbackStateDX11 m_captureState;

    // Primitives / resource handles
    IDXGISwapChain* m_swapChain = nullptr;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    ID3D11RenderTargetView* m_backBufferRTV = nullptr;
    ID3D11Texture2D* m_depthStencilTex = nullptr;
    ID3D11DepthStencilView* m_depthStencilView = nullptr;

    // State objects
    ID3D11DepthStencilState* m_dsDepthOn = nullptr;
    ID3D11DepthStencilState* m_dsDepthOff = nullptr;
    ID3D11DepthStencilState* m_dsDepthOnWriteOff = nullptr;
    ID3D11BlendState* m_blendOff = nullptr;
    ID3D11RasterizerState* m_rsCullOn = nullptr;
    ID3D11RasterizerState* m_rsCullOff = nullptr;
    ID3D11RasterizerState* m_rsCullOnPolyOffset = nullptr;
    ID3D11RasterizerState* m_rsCullOffPolyOffset = nullptr;
    ID3D11SamplerState* m_samplerLinear = nullptr;
    ID3D11SamplerState* m_samplerNearest = nullptr;

    ID3D11BlendState* m_activeBlendState = nullptr;
    BlendFactor m_currentBlendSrc = BlendFactor::One;
    BlendFactor m_currentBlendDst = BlendFactor::Zero;
    ShaderDX11* m_activeShader = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_isVsyncEnabled = true;
    bool m_allowTearing = false;
    int m_frameDrawCallCount = 0;

    // Grid line overlay (lazy-init in DrawLinesColored)
    ID3D11Buffer* m_gridLineVB = nullptr;
    ID3D11InputLayout* m_gridLineIL = nullptr;
    std::unique_ptr<IShader> m_gridLineShader;
    int m_gridLineVBCapacity = 0;

    void CreateStateObjects();
    void ApplyRasterizerState();
    void ApplyDepthState();
    GpuTimerStateDX11 m_gpuTimers;
    void InitGpuTimers();
    void ShutdownGpuTimers();

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
    void SetVsyncEnabled( bool enabled ) override;
    bool IsVsyncEnabled() const override;
    void Finish() override;
    void FlushGPU() override;
    void Resize( int width, int height ) override;

    void SetViewport( int x, int y, int w, int h ) override;
    void Clear( bool color, bool depth ) override;
    void SetClearColor( float r, float g, float b, float a ) override;
    void SetClearDepth( float depth ) override;

    void SetDepthTest( bool enable ) override;
    void SetDepthWrite( bool enable ) override;
    void SetBlend( bool enable ) override;
    void SetBlendFunc( BlendFactor src, BlendFactor dst ) override;
    void SetCullFace( bool enable ) override;
    void SetPolygonOffset( bool enable, float factor = 0.0f, float units = 0.0f ) override;
    void SetClipPlane( int index, bool enable ) override;

    std::unique_ptr<IShader> CreateShader( const char* baseName ) override;
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
    void ResetFrameDrawCallCount() override
    {
        m_frameDrawCallCount = 0;
    }
    void NoteDrawCall() override
    {
        ++m_frameDrawCallCount;
    }
    int GetFrameDrawCallCount() const override
    {
        return m_frameDrawCallCount;
    }
    const char* GetRendererName() const override
    {
        return "DirectX 11";
    }

    bool IsDXRSupported() const override
    {
        return false;
    }
    void InitDXR( uint64_t, int, int, uint64_t, int, int, int ) override
    {
    }
    void DispatchReflectionRays( const float*, const float*, float, float, const float*, int, int, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t ) override
    {
    }
    void BuildTLAS( const float*, int, uint64_t, uint64_t ) override
    {
    }
    uint32_t GetReflectionUAVTexture() const override
    {
        return 0;
    }
    void ShutdownDXR() override
    {
    }
    uint64_t GetInstancedMeshStaticVBVA( uint32_t ) const override
    {
        return 0;
    }
    int GetInstancedMeshStaticStride( uint32_t ) const override
    {
        return 0;
    }

    uint32_t CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices ) override;
    void UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount ) override;
    void DestroyDynamicVB( uint32_t handle ) override;

    void DrawLinesColored( const float* data, int vertCount, const float* viewProjMatrix16 ) override;

    uint32_t CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int maxInstances, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs, const int* staticAttribSizes = nullptr, int numStaticAttribs = 0 ) override;
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

    // RT cache update (called by FBO bind/unbind)
    void SetRenderTargetCache( ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv )
    {
        m_targetCache.currentRTV = rtv;
        m_targetCache.currentDSV = dsv;
    }
    ID3D11RenderTargetView* GetBackBufferRTV() const
    {
        return m_backBufferRTV;
    }
    ID3D11DepthStencilView* GetDepthStencilView() const
    {
        return m_depthStencilView;
    }

    uint32_t RegisterSRV( ID3D11ShaderResourceView* srv );
    void UnregisterSRV( uint32_t handle );

    bool SupportsGpuTimers() const override;
    void GpuTimerBegin( int markerIdx ) override;
    void GpuTimerEnd( int markerIdx ) override;
    void GpuTimerInvalidate() override;
    bool GpuTimerRead( int markerIdx, float& outMs ) override;
};
} // namespace Rendering
} // namespace SkullbonezCore
