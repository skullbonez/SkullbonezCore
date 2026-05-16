#pragma once


// --- Includes ---
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezMeshDX12.h"
#include "SkullbonezBLASDX12.h"
#include "SkullbonezTLASDX12.h"
#include "SkullbonezSBTDX12.h"
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


// Instanced mesh (shadow decals)
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
    int numStaticAttribs;
    int staticAttribSizes[8];
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
    bool depthWriteEnabled;
    bool cullEnabled;
    bool polyOffsetEnabled;
};

inline constexpr int DX12_TIMER_HEAP_MARKERS = 64;
inline constexpr int DX12_TIMER_HEAP_SIZE = DX12_TIMER_HEAP_MARKERS * 2;

struct GpuTimerStateDX12
{
    ID3D12QueryHeap* queryHeap = nullptr;
    ID3D12Resource* readbackBuf = nullptr;
    float resultMs[DX12_TIMER_HEAP_MARKERS] = {};
    bool resultValid[DX12_TIMER_HEAP_MARKERS] = {};
    uint64_t freq = 1;
    bool readPending = false;
    UINT64 readFenceValue = 0;                   // fence value that guarantees the latest ResolveQueryData has completed
    bool slotWritten[DX12_TIMER_HEAP_SIZE] = {}; // true for each timestamp slot that had EndQuery recorded this frame
};


/* -- RenderBackendDX12 -----------------------------------------------------------------------------------------------------------------------------------------

    DirectX 12 implementation of the render backend interface.
    Manages D3D12 device, command queue, swap chain, descriptor heaps, and all rendering state.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class RenderBackendDX12 : public IRenderBackend
{

  private:
    static RenderBackendDX12* s_instance;

    // Frame management
    static const int FRAME_COUNT = 2;
    static const UINT MAX_STATIC_SRVS = 128;
    static const UINT MAX_TRANSIENT_SRVS = 2048; // per frame allocator
    static const UINT64 UPLOAD_BUFFER_SIZE = 8 * 1024 * 1024;
    static const int TIMER_HEAP_MARKERS = DX12_TIMER_HEAP_MARKERS; // must be >= Profiler::MAX_MARKERS
    static const int TIMER_HEAP_SIZE = DX12_TIMER_HEAP_SIZE;       // begin + end per marker

    // Containers
    std::unordered_map<size_t, ID3D12PipelineState*> m_psoCache;
    std::vector<TextureEntryDX12> m_textures; // Texture registry (1-based, index 0 unused)
    std::vector<DynamicVBDX12> m_dynamicVBs;
    std::vector<InstancedMeshDX12> m_instancedMeshes;

    // Struct state
    D3D12_VIEWPORT m_viewport = {};
    D3D12_RECT m_scissorRect = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentRTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentDSV = {};
    BLAS m_terrainBLAS;
    BLAS m_sphereBLAS;
    TLAS m_tlas;
    SBT m_sbt;
    GpuTimerStateDX12 m_gpuTimers;

    // Primitives / resource handles
    IDXGIFactory4* m_factory = nullptr;
    IDXGISwapChain3* m_swapChain = nullptr;
    ID3D12Device* m_device = nullptr;
    ID3D12CommandQueue* m_commandQueue = nullptr;
    ID3D12GraphicsCommandList* m_commandList = nullptr;
    ID3D12CommandAllocator* m_commandAllocators[FRAME_COUNT] = {};
    bool m_commandListOpen = false;

    ID3D12Resource* m_renderTargets[FRAME_COUNT] = {};
    UINT m_frameIndex = 0;
    UINT m_allocatorIndex = 0; // Which allocator is active (alternates 0/1)

    ID3D12Fence* m_fence = nullptr;
    UINT64 m_fenceValue = 0;
    UINT64 m_frameFenceValues[FRAME_COUNT] = {}; // Fence value signaled by each frame's submission
    HANDLE m_fenceEvent = nullptr;

    ID3D12DescriptorHeap* m_rtvHeap = nullptr;
    ID3D12DescriptorHeap* m_dsvHeap = nullptr;
    ID3D12DescriptorHeap* m_srvHeap = nullptr;        // GPU-visible (shader-visible) for binding
    ID3D12DescriptorHeap* m_srvStagingHeap = nullptr; // CPU-only for persistent SRV storage
    UINT m_rtvDescSize = 0;
    UINT m_dsvDescSize = 0;
    UINT m_srvDescSize = 0;
    UINT m_nextRTV = FRAME_COUNT; // Next available RTV slot (0-1 are swap chain)
    UINT m_nextDSV = 1;           // Next available DSV slot (0 is main depth)

    UINT m_nextStaticSRV = 0;
    UINT m_nextTransientSRV = 0;

    ID3D12Resource* m_depthStencil = nullptr;

    // One upload buffer per frame allocator. Partitioned so that frame N+1's CPU recording never
    // overwrites data in the buffer that frame N's GPU is still reading. Mirrors the per-allocator
    // partitioning applied to the transient SRV heap.
    ID3D12Resource* m_uploadBuffers[FRAME_COUNT] = {};
    uint8_t* m_uploadBufferMapped[FRAME_COUNT] = {};
    UINT64 m_uploadOffset = 0;

    ID3D12RootSignature* m_rootSignature = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_isVsyncEnabled = true;

    bool m_depthTestEnabled = true;
    bool m_depthWriteEnabled = true;
    bool m_blendEnabled = false;
    BlendFactor m_blendSrc = BlendFactor::One;
    BlendFactor m_blendDst = BlendFactor::Zero;
    bool m_cullEnabled = true;
    bool m_polyOffsetEnabled = false;
    float m_polyOffsetFactor = 0.0f;
    float m_polyOffsetUnits = 0.0f;
    float m_clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float m_clearDepth = 1.0f;
    bool m_psoDirty = true;

    ShaderDX12* m_activeShader = nullptr;
    UINT m_boundTexSlot[2] = { UINT_MAX, UINT_MAX }; // Currently bound SRV indices for t0/t1

    bool m_renderingToFBO = false;
    bool m_backBufferIsRT = false; // True if back buffer is in RENDER_TARGET state

    size_t m_lastPSOHash = 0;
    bool m_texBindingsDirty = true;
    bool m_targetsDirty = true;

    bool m_dxrSupported = false;
    ID3D12Device5* m_device5 = nullptr;
    ID3D12GraphicsCommandList4* m_cmdList4 = nullptr;
    ID3D12StateObject* m_rtPSO = nullptr;
    ID3D12StateObjectProperties* m_rtPSOProps = nullptr;
    ID3D12RootSignature* m_rtRootSignature = nullptr;
    ID3D12Resource* m_reflectionUAV = nullptr;
    UINT m_reflectionUAVIndex = 0; // UAV descriptor index in SRV heap
    UINT m_reflectionSRVIndex = 0; // SRV for water shader sampling
    int m_reflectionWidth = 0;
    int m_reflectionHeight = 0;
    bool m_reflectionInSRVState = false; // True after dispatch (SRV), false initially (UAV)
    ID3D12Resource* m_rtConstantBuffer = nullptr;
    uint8_t* m_rtConstantBufferMapped = nullptr;

    ID3D12PipelineState* m_genMipsPSO = nullptr; // Compute PSO for generate_mips.hlsl
    ID3D12RootSignature* m_genMipsRS = nullptr;  // Root signature: 4 root constants + SRV + 4 UAVs
    UINT m_genMipsNullUAV = 0;                   // Static SRV slot holding a null UAV (padding)

    // --- Internal helpers ---
    void WaitForGpu();
    void EnsureCommandListOpen();
    void TryConsumeGpuTimerReadback( bool waitForFence );
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
    void CheckDXRSupport();
    void CreateRTRootSignature();
    void CreateRTPipeline();
    void CreateReflectionUAV( int width, int height );
    void InitGenMipsPipeline();
    void GenerateMipsGPU( ID3D12Resource* tex, DXGI_FORMAT fmt, UINT w, UINT h, UINT numMips );

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
    const char* GetRendererName() const override
    {
        return "DirectX 12";
    }

    bool IsDXRSupported() const override
    {
        return m_dxrSupported;
    }
    void InitDXR( uint64_t terrainVBVA, int terrainVertCount, int terrainStride, uint64_t sphereVBVA, int sphereVertCount, int sphereStride, int maxInstances ) override;
    void DispatchReflectionRays( const float* invViewProj, const float* cameraPos, float waterY, float time, const float* lightPos, int width, int height, uint32_t sphereTexHandle, uint32_t terrainTexHandle, uint32_t skyUpHandle, uint32_t skyDownHandle, uint32_t skyRightHandle, uint32_t skyLeftHandle, uint32_t skyFrontHandle, uint32_t skyBackHandle ) override;
    void BuildTLAS( const float* instanceTransforms, int instanceCount, uint64_t terrainBLAS, uint64_t sphereBLAS ) override;
    uint32_t GetReflectionUAVTexture() const override;
    void ShutdownDXR() override;
    uint64_t GetInstancedMeshStaticVBVA( uint32_t handle ) const override;
    int GetInstancedMeshStaticStride( uint32_t handle ) const override;

    bool SupportsGpuTimers() const override
    {
        return m_gpuTimers.queryHeap != nullptr;
    }
    void GpuTimerBegin( int markerIdx ) override;
    void GpuTimerEnd( int markerIdx ) override;
    void GpuTimerInvalidate() override;
    bool GpuTimerRead( int markerIdx, float& outMs ) override;

    uint32_t CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices ) override;
    void UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount ) override;
    void DestroyDynamicVB( uint32_t handle ) override;

    uint32_t CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int maxInstances, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs, const int* staticAttribSizes = nullptr, int numStaticAttribs = 0 ) override;
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
        return m_uploadBuffers[m_allocatorIndex];
    }
    D3D12_CPU_DESCRIPTOR_HANDLE AllocateRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE AllocateDSV();
    UINT AllocateStaticSRV();
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRVStagingCpuHandle( UINT index );
};
} // namespace Rendering
} // namespace SkullbonezCore
