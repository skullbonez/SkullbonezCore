#pragma once


// --- Includes ---
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezMeshDX12.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <unordered_map>
#include <vector>


namespace SkullbonezCore
{
namespace Rendering
{

class ShaderDX12;


// Texture entry for the DX12 SRV registry
struct TextureEntryDX12
{
    ID3D12Resource* resource;
    UINT srvIndex; // Index in the persistent SRV region
    bool owned;    // False for FBO-registered SRVs
};


// Dynamic vertex buffer (text, HUD)
struct DynamicVBDX12
{
    int floatsPerVertex;
    int maxVertices;
    int stride;
    int numAttribs;
    int attribComponents[8];
};


// Instanced MeshGL (shadow decals)
struct InstancedMeshDX12
{
    ID3D12Resource* staticVB;
    D3D12_VERTEX_BUFFER_VIEW staticVBV;
    int staticFloatsPerVert;
    int staticStride;
    int instanceFloats;
    int instanceStride;
    int instanceStartAttrib;
    int numInstanceAttribs;
    int instanceAttribSizes[8];
    D3D12_GPU_VIRTUAL_ADDRESS instanceDataAddr;
    UINT instanceDataSize;
};


// PSO cache key
struct PSOKey12
{
    const void* shaderVS;
    const void* shaderPS;
    VertexFormat12 format;
    bool isInstanced;
    bool blendEnabled;
    BlendFactor blendSrc;
    BlendFactor blendDst;
    bool depthEnabled;
    bool cullEnabled;
    bool polyOffsetEnabled;
};


/* -- RenderBackendDX12 -----------------------------------------------------------------------------------------------------------------------------------------

    DirectX 12 implementation of the render backend interface.
    Manages D3D12 device, command queue, swap chain, descriptor heaps, and all rendering state.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class RenderBackendDX12 : public IRenderBackend
{

  private:
    static RenderBackendDX12* s_instance;

    // Core objects
    IDXGIFactory4* m_factory;
    IDXGISwapChain3* m_swapChain;
    ID3D12Device* m_device;
    ID3D12CommandQueue* m_commandQueue;
    ID3D12GraphicsCommandList* m_commandList;
    ID3D12CommandAllocator* m_commandAllocator;
    bool m_commandListOpen;
    bool m_gpuIdle;

    // Frame management
    static const int FRAME_COUNT = 2;
    ID3D12Resource* m_renderTargets[FRAME_COUNT];
    UINT m_frameIndex;

    // Fence
    ID3D12Fence* m_fence;
    UINT64 m_fenceValue;
    HANDLE m_fenceEvent;

    // Descriptor heaps
    ID3D12DescriptorHeap* m_rtvHeap;
    ID3D12DescriptorHeap* m_dsvHeap;
    ID3D12DescriptorHeap* m_srvHeap;        // GPU-visible (ShaderGL-visible) for binding
    ID3D12DescriptorHeap* m_srvStagingHeap; // CPU-only for persistent SRV storage
    UINT m_rtvDescSize;
    UINT m_dsvDescSize;
    UINT m_srvDescSize;
    UINT m_nextRTV; // Next available RTV slot (0-1 are swap chain)
    UINT m_nextDSV; // Next available DSV slot (0 is main depth)

    // SRV allocation
    static const UINT MAX_STATIC_SRVS = 128;
    static const UINT MAX_TRANSIENT_SRVS = 2048;
    UINT m_nextStaticSRV;
    UINT m_nextTransientSRV;

    // Depth stencil
    ID3D12Resource* m_depthStencil;

    // Upload buffer
    ID3D12Resource* m_uploadBuffer;
    uint8_t* m_uploadBufferMapped;
    static const UINT64 UPLOAD_BUFFER_SIZE = 8 * 1024 * 1024;
    UINT64 m_uploadOffset;

    // Root signature
    ID3D12RootSignature* m_rootSignature;

    // PSO cache
    std::unordered_map<size_t, ID3D12PipelineState*> m_psoCache;

    // Viewport
    D3D12_VIEWPORT m_viewport;
    D3D12_RECT m_scissorRect;
    int m_width;
    int m_height;

    // Tracked render state
    bool m_depthTestEnabled;
    bool m_blendEnabled;
    BlendFactor m_blendSrc;
    BlendFactor m_blendDst;
    bool m_cullEnabled;
    bool m_polyOffsetEnabled;
    float m_polyOffsetFactor;
    float m_polyOffsetUnits;
    float m_clearColor[4];
    float m_clearDepth;
    bool m_psoDirty;

    // Active ShaderGL
    ShaderDX12* m_activeShader;

    // Texture registry (1-based, index 0 unused)
    std::vector<TextureEntryDX12> m_textures;
    UINT m_boundTexSlot[2]; // Currently bound SRV indices for t0/t1

    // Dynamic VBs and instanced meshes
    std::vector<DynamicVBDX12> m_dynamicVBs;
    std::vector<InstancedMeshDX12> m_instancedMeshes;

    // Current render targets (for FBO save/restore)
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentRTV;
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentDSV;
    bool m_renderingToFBO;
    bool m_backBufferIsRT; // True if back buffer is in RENDER_TARGET state

    // Redundant-call elimination: skip PSO/descriptor/state rebinds when unchanged
    size_t m_lastPSOHash;
    bool m_texBindingsDirty;
    bool m_targetsDirty;

    // --- Internal helpers ---
    void WaitForGpu();
    void EnsureCommandListOpen();
    void CreateRootSignature();
    void CreateDepthStencil( int w, int h );
    UINT AllocateTransientSRV();
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGpuHandle( UINT index );
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle( UINT index );
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle( UINT index );
    void TransitionBarrier( ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after );
    void FlushUploadBuffer();
    void FlushUploadBufferIfNeeded( UINT64 size, UINT64 alignment );
    size_t HashPSOKey( const PSOKey12& key );
    ID3D12PipelineState* CreatePSO( VertexFormat12 format, bool instanced, const InstancedMeshDX12* im, const DynamicVBDX12* dvb );

    static void BuildInputLayout( VertexFormat12 format, D3D12_INPUT_ELEMENT_DESC* out, UINT& count );
    static void BuildInstancedInputLayout( const InstancedMeshDX12& im, D3D12_INPUT_ELEMENT_DESC* out, UINT& count );
    static void BuildDynamicVBInputLayout( const DynamicVBDX12& dvb, D3D12_INPUT_ELEMENT_DESC* out, UINT& count );

  public:
    RenderBackendDX12();
    ~RenderBackendDX12() override
    {
        Shutdown();
    }

    static RenderBackendDX12* Get()
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
        return "DirectX 12";
    }

    uint32_t CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices ) override;
    void UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount ) override;
    void DestroyDynamicVB( uint32_t handle ) override;

    uint32_t CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int maxInstances, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs ) override;
    void UploadInstanceData( uint32_t handle, const float* data, int floatCount ) override;
    void DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount ) override;
    void DestroyInstancedMesh( uint32_t handle ) override;

    // DX12-specific helpers for MeshGL/ShaderGL/FramebufferGL classes
    void SetActiveShader( ShaderDX12* ShaderGL );
    ShaderDX12* GetActiveShader() const
    {
        return m_activeShader;
    }
    ID3D12Device* GetDevice() const
    {
        return m_device;
    }
    ID3D12GraphicsCommandList* GetCommandList() const
    {
        return m_commandList;
    }

    void PrepareDraw( VertexFormat12 format, bool instanced = false, const InstancedMeshDX12* im = nullptr, const DynamicVBDX12* dvb = nullptr );
    UINT RegisterSRV( UINT srvIndex );
    void UnregisterSRV( uint32_t handle );

    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const
    {
        return m_currentRTV;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentDSV() const
    {
        return m_currentDSV;
    }
    void SetCurrentTargets( D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv );
    void SetRenderingToFBO( bool rendering, UINT fboSrvIndex = UINT_MAX );

    D3D12_GPU_VIRTUAL_ADDRESS SubAllocateUpload( UINT64 size, UINT64 alignment );
    uint8_t* GetUploadPtr( D3D12_GPU_VIRTUAL_ADDRESS addr );
    ID3D12Resource* GetUploadBuffer() const
    {
        return m_uploadBuffer;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE AllocateRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE AllocateDSV();
    UINT AllocateStaticSRV();
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRVStagingCpuHandle( UINT index );
};
} // namespace Rendering
} // namespace SkullbonezCore
