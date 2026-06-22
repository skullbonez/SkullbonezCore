/*
File: SkullbonezSource/RenderBackendDX12.h
Purpose:
  Declares the production DX12 renderer and its frame, resource, and pipeline state.

Mental model:
  DX12 separates resource memory, descriptor rows, command recording, and GPU
  execution. Ownership, state transitions, descriptor lifetime, and fence
  ordering are the important ideas.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  TLAS (Top-Level Acceleration Structure): Raytracing spatial index for scene
  instances that point at BLAS geometry.
  SBT (Shader Binding Table): DXR table that maps ray records to
  ray-generation, miss, and hit shaders.
  RTV (Render Target View): Descriptor row used when the GPU writes color
  pixels into a texture or back buffer.
  DSV (Depth Stencil View): Descriptor row used when the GPU reads or writes
  depth/stencil data for depth testing.
  SRV (Shader Resource View): Descriptor row used when shaders read textures
  or buffers.
  UAV (Unordered Access View): Descriptor row used when compute or raytracing
  shaders write textures or buffers.
  CBV (Constant Buffer View): Descriptor row used when shaders read a packed
  block of constants.
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/RenderBackendDX12.cpp
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "IRenderBackend.h"
#include "RenderDeviceDX12.h"
#include "MeshDX12.h"
#include "BLASDX12.h"
#include "TLASDX12.h"
#include "SBTDX12.h"
#include "RenderGraph.h"
#include "Common.h"
#include <d3d12.h>
#include <dxgi1_5.h>
#include <array>
#include <unordered_map>
#include <vector>


namespace SkullbonezCore
{
namespace Rendering
{

class ShaderDX12;


// Texture entry for the DX12 SRV registry.
//
// "SRV" means Shader Resource View. It is the descriptor flavor a shader uses
// when it wants to read a texture. The ID3D12Resource below is the actual image
// memory. The srvIndex is only a row number in the descriptor heap table that
// tells the shader how to read that image.
struct TextureEntryDX12
{
    ID3D12Resource* resource;
    UINT srvIndex;                                                 // Index in the persistent SRV region
    bool owned;                                                    // False for FBO-registered SRVs
};


// Dynamic vertex buffer (text, HUD)
struct DynamicVBDX12
{
    int floatsPerVertex;
    int maxVertices;
    int stride;
    int numAttribs;
    int attribComponents[12];
};


// Instanced mesh
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


// PSO cache key.
//
// A graphics PSO is expensive to create and must match the exact shader pair,
// root signature, vertex layout, blend/depth/cull flags, polygon offset, and
// render-target format. This key is the "recipe fingerprint" used to reuse
// compatible PSOs instead of compiling a new one for every draw.
struct PSOKey12
{
    // Borrowed identity only. The root signature owns the shader binding
    // contract; two PSOs with identical shaders but different root signatures
    // are not compatible cache entries.
    const void* rootSignature;
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
    INT polyOffsetDepthBias;
    float polyOffsetSlopeScaledDepthBias;
    DXGI_FORMAT rtvFormat;
};

inline constexpr int DX12_TIMER_HEAP_MARKERS = 128;
inline constexpr int DX12_TIMER_HEAP_SIZE = DX12_TIMER_HEAP_MARKERS * 2;

struct GpuTimerStateDX12
{
    ID3D12QueryHeap* queryHeap = nullptr;
    Dx12ReadbackBuffer readback;
    float resultMs[DX12_TIMER_HEAP_MARKERS] = {};
    bool resultValid[DX12_TIMER_HEAP_MARKERS] = {};
    uint64_t freq = 1;
    bool readPending = false;
    UINT64 readFenceValue = 0;                                     // fence value that guarantees the latest ResolveQueryData has completed
    bool slotWritten[DX12_TIMER_HEAP_SIZE] = {};                   // true for each timestamp slot that had EndQuery recorded this frame
};

// One live DX12 transition barrier emitted through the graph-owned helper while
// the backend records commands.
//
// This is diagnostic data for the render-graph migration. It is deliberately a
// small CPU-side record: resource pointer identity, resource name,
// subresource/all-subresources, graph access, before/after DX12 states, and a
// short source label. It does not affect command recording. The goal is to make
// the actual graph-owned barrier path auditable without scattering DX12 policy
// back into pass code.
struct LiveBarrierRecordDX12
{
    const void* resource = nullptr;
    char resourceName[64] = {};
    RenderGraphResourceAccess beforeAccess = RenderGraphResourceAccess::Unknown;
    RenderGraphResourceAccess afterAccess = RenderGraphResourceAccess::Unknown;
    D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_COMMON;
    UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    char source[64] = {};
};

struct LiveUavBarrierRecordDX12
{
    const void* resource = nullptr;
    char resourceName[64] = {};
    char source[64] = {};
};


// Concept: RenderBackendDX12 is the engine-facing facade over explicit DX12 state.
//
// The public IRenderBackend API uses engine verbs: set a shader, set textures,
// draw meshes, present the frame. Internally, DX12 requires the backend to make
// every hidden GPU concept explicit: descriptor table rows, command allocators,
// resource states, fences, upload memory, and compiled pipeline state. This
// class is the bridge between the simple engine contract and that explicit DX12
// machinery.
class RenderBackendDX12 : public IRenderBackend
{

  private:
    static RenderBackendDX12* s_instance;

    // Frame management:
    //
    // Two frames can be in flight. Each frame owns its own command allocator,
    // upload arena, transient descriptors, and fence value so the CPU never
    // overwrites memory or descriptor rows still being read by the GPU.
    static const int FRAME_COUNT = 2;
    static const UINT MAX_RTV_DESCRIPTORS = 32;
    static const UINT MAX_DSV_DESCRIPTORS = 16;
    static const UINT MAX_STATIC_SRVS = 128;
    static const UINT MAX_TRANSIENT_SRVS = 2048;                   // per frame allocator
    static const UINT64 UPLOAD_BUFFER_SIZE = 8 * 1024 * 1024;
    static const int TIMER_HEAP_MARKERS = DX12_TIMER_HEAP_MARKERS; // must be >= Profiler::MAX_MARKERS
    static const int TIMER_HEAP_SIZE = DX12_TIMER_HEAP_SIZE;       // begin + end per marker

    // Ordinary raster binding ABI:
    //
    // This is the public shader/resource layout for the current graphics root
    // signature. Keep BindTexture(handle, slot) mapped directly to SRV register
    // t<slot> until a concrete material/pass contract requires a new root
    // signature. Material-table work updates this block, HLSL
    // registers, and shader contract docs together.
    static constexpr UINT ROOT_PARAMETER_FRAME_CONSTANTS = 0;      // CBV b0
    static constexpr UINT ROOT_PARAMETER_FIRST_TEXTURE = 1;        // t0 descriptor table
    static constexpr UINT SHADER_REGISTER_FRAME_CONSTANTS = 0;
    static constexpr UINT SHADER_REGISTER_FIRST_TEXTURE = 0;
    static constexpr UINT SAMPLER_REGISTER_LINEAR_WRAP = 0;        // s0
    static constexpr UINT SAMPLER_REGISTER_LINEAR_CLAMP = 1;       // s1
    static constexpr UINT SAMPLER_REGISTER_SHADOW_POINT_CLAMP = 3; // s3
    static constexpr int TEXTURE_SLOT_COUNT = 5;                   // SRV slots t0..t4
    static constexpr UINT ORDINARY_RASTER_ROOT_PARAMETER_COUNT = ROOT_PARAMETER_FIRST_TEXTURE + TEXTURE_SLOT_COUNT;
    static_assert( TEXTURE_SLOT_COUNT == 5,
                   "Ordinary raster ABI exposes SRV slots t0..t4, including t4 for the object material table." );

    // CPU-side registries. These are not GPU resources by themselves; they are
    // lookup tables the backend uses to find cached GPU objects and descriptor
    // rows while translating engine draw calls into command-list operations.
    std::unordered_map<size_t, ID3D12PipelineState*> m_psoCache;
    std::vector<TextureEntryDX12> m_textures;                      // Texture registry (1-based, index 0 unused)
    std::vector<DynamicVBDX12> m_dynamicVBs;
    std::vector<InstancedMeshDX12> m_instancedMeshes;

    // Currently bound render state. DX12 does not remember high-level engine
    // intent for us, so the backend tracks the desired state and emits concrete
    // command-list binds only when the state becomes dirty.
    D3D12_VIEWPORT m_viewport = {};
    D3D12_RECT m_scissorRect = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentRTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentDSV = {};
    DXGI_FORMAT m_currentRTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    BLAS m_terrainBLAS;
    BLAS m_sphereBLAS;
    TLAS m_tlas;
    SBT m_sbt;
    GpuTimerStateDX12 m_gpuTimers;

    // The render device owns the core D3D12 lifetime: factory, device, queue,
    // swap chain, command allocators, command list, and frame fence. The raw
    // pointers below are borrowed aliases kept only so the existing backend
    // methods can be migrated in small slices without changing every call site
    // at once.
    Dx12RenderDevice m_renderDevice;

    // Borrowed core device aliases. Do not Release() these in the backend.
    IDXGIFactory4* m_factory = nullptr;
    IDXGISwapChain3* m_swapChain = nullptr;
    ID3D12Device* m_device = nullptr;
    ID3D12CommandQueue* m_commandQueue = nullptr;
    ID3D12GraphicsCommandList* m_commandList = nullptr;
    ID3D12CommandAllocator* m_commandAllocators[FRAME_COUNT] = {};
    bool m_commandListOpen = false;
    int m_platformProfilerGpuDepth = 0;

    ID3D12Resource* m_renderTargets[FRAME_COUNT] = {};
    UINT m_frameIndex = 0;
    UINT m_allocatorIndex = 0;                                     // Which allocator is active (alternates 0/1)

    UINT64 m_frameFenceValues[FRAME_COUNT] = {};                   // Fence value signaled by each frame's submission

    // Descriptor heaps are descriptor tables, not texture arrays.
    //
    // Each heap stores one kind of "view" record:
    //
    // - RTV: Render Target View. The GPU can write color pixels through it.
    // - DSV: Depth Stencil View. The GPU can read/write depth and stencil.
    // - SRV: Shader Resource View. Shaders can read textures/buffers through it.
    // - UAV: Unordered Access View. Compute/raytracing shaders can write through it.
    //
    // RTV and DSV heaps are CPU-only descriptor tables. They do not need the
    // per-frame shader-visible lifetime rules that SRVs need, but they still
    // need named row allocation so the renderer can report usage and fail with
    // useful heap/capacity diagnostics instead of silently walking past the end
    // of a descriptor table.
    ID3D12DescriptorHeap* m_rtvHeap = nullptr;
    ID3D12DescriptorHeap* m_dsvHeap = nullptr;
    ID3D12DescriptorHeap* m_srvHeap = nullptr;                     // GPU-visible table shaders can read during draws/dispatches.
    ID3D12DescriptorHeap* m_srvStagingHeap = nullptr;              // CPU-only table holding persistent descriptor templates.
    UINT m_rtvDescSize = 0;
    UINT m_dsvDescSize = 0;
    UINT m_srvDescSize = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE m_backBufferRTVs[FRAME_COUNT] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_mainDSV = {};

    // RTV/DSV descriptor allocators reserve CPU-only table rows. They do not
    // create the render target or depth texture; they reserve the row where
    // CreateRenderTargetView/CreateDepthStencilView writes the binding record.
    Dx12CpuDescriptorAllocator m_rtvDescriptors;
    Dx12CpuDescriptorAllocator m_dsvDescriptors;

    // First DX12 shader-visible descriptor extraction point:
    //
    // The old backend used loose integer counters for descriptor heap slots.
    // That worked, but it made the lifetime rule implicit.
    //
    // A descriptor allocator is not a texture allocator. Textures live in GPU
    // resources. A descriptor allocator hands out numbered rows in a descriptor
    // heap, which is the table shaders use to find textures and UAVs. In DX12,
    // the engine must manage those rows itself.
    //
    // If the CPU overwrites a descriptor row while the GPU is still following a
    // handle to that row, the shader can sample the wrong texture or trip the
    // validation layer. This allocator owns the static and per-frame transient
    // ranges so that rule is visible at the architecture boundary.
    Dx12DescriptorAllocator m_srvDescriptors;

    ID3D12Resource* m_depthStencil = nullptr;

    // Upload memory is the CPU-written staging area for constants, dynamic
    // vertices, instance data, and texture rows. It is the bridge between CPU
    // code that prepares frame data and GPU commands that read that data later.
    //
    // Each frame allocator gets its own arena. That matters because the CPU can
    // begin preparing a later frame before the GPU has finished an earlier one.
    // Resetting an arena too early would let the CPU overwrite bytes the GPU has
    // not read yet. Dx12FrameUploadSystem owns the upload resources, their
    // persistent CPU Map() pointers, and the arena reset policy tied to the
    // frame fence.
    Dx12FrameUploadSystem m_uploadSystem;

    ID3D12RootSignature* m_rootSignature = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_isVsyncEnabled = true;
    bool m_allowTearing = false;
    int m_frameDrawCallCount = 0;
    DrawCallTrace m_drawCallTrace;

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
    std::vector<LiveBarrierRecordDX12> m_liveBarrierRecords;
    std::vector<LiveUavBarrierRecordDX12> m_liveUavBarrierRecords;

    ShaderDX12* m_activeShader = nullptr;
    // Currently bound persistent SRV descriptor indices for shader texture
    // slots t0..t4. These are not GPU handles. Before a draw, the backend copies
    // each persistent descriptor into a transient shader-visible row and binds
    // that transient GPU handle through the root signature.
    UINT m_boundTexSlot[TEXTURE_SLOT_COUNT] = { UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX };
    UINT m_nullTextureSRVIndex = UINT_MAX;                         // Static null Texture2D SRV copied into cleared texture slots.

    // Grid line overlay (lazy-init in DrawLinesColored)
    std::unique_ptr<IShader> m_gridLineShader;
    ID3D12PipelineState* m_gridLinePSO = nullptr;
    int m_gridLineVBCapacity = 0;

    bool m_renderingToFBO = false;
    bool m_backBufferIsRT = false;                                 // True if back buffer is in RENDER_TARGET state

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
    // Descriptor rows for the DXR reflection texture. The same resource is a
    // UAV while rays write pixels and an SRV when the water shader samples the
    // finished reflection.
    UINT m_reflectionUAVIndex = 0;
    UINT m_reflectionSRVIndex = 0;
    int m_reflectionWidth = 0;
    int m_reflectionHeight = 0;
    // Tracks the current resource state of m_reflectionUAV so DispatchRays and
    // the water pass can transition between write/read usage explicitly.
    bool m_reflectionInSRVState = false;
    ID3D12Resource* m_rtConstantBuffer = nullptr;
    uint8_t* m_rtConstantBufferMapped = nullptr;
    int m_dxrMaxInstances = 0;
    std::array<D3D12_RAYTRACING_INSTANCE_DESC, MAX_GAME_MODELS + 1> m_tlasInstances = {};

    ID3D12PipelineState* m_genMipsPSO = nullptr;                   // Compute PSO for generate_mips.hlsl
    ID3D12RootSignature* m_genMipsRS = nullptr;                    // Root signature: 4 root constants + SRV + 4 UAVs
    UINT m_genMipsNullUAV = 0;                                     // Static SRV slot holding a null UAV (padding)

    // --- Internal helpers ---
    void WaitForGpu();
    void EnsureCommandListOpen();
    void TryConsumeGpuTimerReadback( bool waitForFence );
    void CreateRootSignature();
    void CreateDepthStencil( int w, int h );
    UINT AllocateTransientSRV();
    UINT AllocateTransientSRVRange( UINT count );
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGpuHandle( UINT index );
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle( UINT index );
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle( UINT index );
    void RecordLiveBarrier( const char* source,
                            const char* resourceName,
                            ID3D12Resource* resource,
                            RenderGraphResourceAccess beforeAccess,
                            RenderGraphResourceAccess afterAccess,
                            D3D12_RESOURCE_STATES before,
                            D3D12_RESOURCE_STATES after,
                            UINT subresource );
    void RecordLiveUavBarrier( const char* source, const char* resourceName, ID3D12Resource* resource );
    // Keeps cached texture-slot state from pointing at an SRV descriptor row
    // whose owning resource is being deleted or unregistered.
    void ClearBoundTextureSlotsForSrv( UINT srvIndex );
    void FlushUploadBuffer();
    void FlushUploadBufferIfNeeded( UINT64 size, UINT64 alignment );
    D3D12_GPU_VIRTUAL_ADDRESS SubAllocateUpload( UINT64 size, UINT64 alignment );
    void ReportArchitectureStats( const char* reason ) const;
    void DumpFrameGraphSkeleton() const;
    void ReportDeviceLost( const char* context, HRESULT result ) const;
    size_t HashPSOKey( const PSOKey12& key );
    ID3D12PipelineState*
    CreatePSO( VertexFormat12 format, bool instanced, const InstancedMeshDX12* im, const DynamicVBDX12* dvb );
    void CheckDXRSupport();
    void CreateRTRootSignature();
    void CreateRTPipeline();
    void CreateReflectionUAV( int width, int height );
    void InitGenMipsPipeline();
    void GenerateMipsGPU( ID3D12Resource* tex, DXGI_FORMAT fmt, UINT w, UINT h, UINT numMips );
    void AssertPlatformProfilerGpuStackClosed( const char* reason ) const;

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
    std::unique_ptr<IMesh>
    CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords ) override;
    std::unique_ptr<IFramebuffer>
    CreateFramebuffer( int width,
                       int height,
                       FramebufferColorFormat colorFormat = FramebufferColorFormat::RGBA8 ) override;

    uint32_t
    CreateTexture2D( const uint8_t* data, int w, int h, int channels, bool generateMips, bool linearFilter ) override;
    void BindTexture( uint32_t handle, int slot ) override;
    void DeleteTexture( uint32_t handle ) override;

    std::vector<uint8_t> CaptureBackbuffer( int& outWidth, int& outHeight ) override;

    int GetWidth() const override;
    int GetHeight() const override;

    bool IsDepthTestEnabled() const override;
    bool IsDepthWriteEnabled() const override;
    bool IsBlendEnabled() const override;
    bool IsCullFaceEnabled() const override;
    void GetBlendFunc( BlendFactor& outSrc, BlendFactor& outDst ) const override;
    const char* GetRendererName() const override
    {
        return "DirectX 12";
    }
    RenderCapabilities GetCapabilities() const override
    {
        RenderCapabilities capabilities;
        capabilities.supportsGpuTimers = m_gpuTimers.queryHeap != nullptr;
        capabilities.supportsDxrReflection = m_dxrSupported;
        capabilities.supportsDebugLines = true;
        return capabilities;
    }

    void ResetFrameDrawCalls() override
    {
        m_frameDrawCallCount = 0;
        m_drawCallTrace.BeginFrame();
    }
    void RecordDrawCall( const DrawCallRecord& record ) override
    {
        ++m_frameDrawCallCount;
        m_drawCallTrace.RecordDrawCall( record );
    }
    void RecordDrawCall()
    {
        RecordDrawCall( DrawCallRecord() );
    }
    int GetFrameDrawCallCount() const override
    {
        return m_frameDrawCallCount;
    }
    DrawCallTraceSnapshot GetFrameDrawCallTrace() const override
    {
        return m_drawCallTrace.Snapshot();
    }
    void PushDrawCallTraceScope( const char* fullPathOrLeaf, uint32_t hash ) override
    {
        m_drawCallTrace.PushScope( fullPathOrLeaf, hash );
    }
    void PopDrawCallTraceScope( uint32_t hash ) override
    {
        m_drawCallTrace.PopScope( hash );
    }

    void InitDXR( uint64_t terrainVBVA,
                  int terrainVertCount,
                  int terrainStride,
                  uint64_t sphereVBVA,
                  int sphereVertCount,
                  int sphereStride,
                  int maxInstances ) override;
    void DispatchReflectionRays( const float* invViewProj,
                                 const float* cameraPos,
                                 float waterY,
                                 float time,
                                 const float* lightPos,
                                 int width,
                                 int height,
                                 uint32_t sphereTexHandle,
                                 uint32_t terrainTexHandle,
                                 uint32_t skyUpHandle,
                                 uint32_t skyDownHandle,
                                 uint32_t skyRightHandle,
                                 uint32_t skyLeftHandle,
                                 uint32_t skyFrontHandle,
                                 uint32_t skyBackHandle ) override;
    void
    BuildTLAS( const float* instanceTransforms, int instanceCount, uint64_t terrainBLAS, uint64_t sphereBLAS ) override;
    uint32_t GetReflectionUAVTexture() const override;
    void ShutdownDXR() override;
    uint64_t GetInstancedMeshStaticVBVA( uint32_t handle ) const override;
    int GetInstancedMeshStaticStride( uint32_t handle ) const override;

    void GpuTimerBegin( int markerIdx ) override;
    void GpuTimerEnd( int markerIdx ) override;
    void GpuTimerInvalidate() override;
    bool GpuTimerRead( int markerIdx, float& outMs ) override;
    void PlatformProfilerGpuBegin( const char* name, uint32_t hash ) override;
    void PlatformProfilerGpuEnd() override;
    void PlatformProfilerGpuMarker( const char* name, uint32_t hash ) override;

    uint32_t CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices ) override;
    void UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount ) override;
    void DestroyDynamicVB( uint32_t handle ) override;

    void DrawLinesColored( const float* data, int vertCount, const float* viewProjMatrix16 ) override;

    uint32_t CreateInstancedMesh( const float* staticData,
                                  int staticVertCount,
                                  int staticFloatsPerVert,
                                  int maxInstances,
                                  int instanceFloats,
                                  int instanceStartAttrib,
                                  const int* instanceAttribSizes,
                                  int numInstanceAttribs,
                                  const int* staticAttribSizes = nullptr,
                                  int numStaticAttribs = 0 ) override;
    void UploadInstanceData( uint32_t handle, const float* data, int floatCount ) override;
    void DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount ) override;
    void DestroyInstancedMesh( uint32_t handle ) override;

    // DX12-specific helpers for mesh, shader, and framebuffer classes.
    void SetActiveShader( ShaderDX12* shader );
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

    void PrepareDraw( VertexFormat12 format,
                      bool instanced = false,
                      const InstancedMeshDX12* im = nullptr,
                      const DynamicVBDX12* dvb = nullptr );
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
    void SetRenderingToFBO( bool rendering,
                            UINT fboSrvIndex = UINT_MAX,
                            UINT fboDepthSrvIndex = UINT_MAX,
                            DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM );
    void ExecuteGraphTransition( const char* passName,
                                 const char* resourceName,
                                 ID3D12Resource* resource,
                                 RenderGraphResourceAccess before,
                                 RenderGraphResourceAccess after,
                                 UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES );
    void ExecuteGraphUavBarrier( const char* passName, const char* resourceName, ID3D12Resource* resource );

    // Reserve CPU-written upload memory for the current command stream.
    //
    // This is the safe public upload path. It probes the current frame upload
    // arena with the exact same size/alignment used for the final allocation.
    // If the arena is full, it submits the current command list, waits for the
    // GPU, resets the frame upload arena, and then allocates. Callers should not
    // call SubAllocateUpload() directly because that bypasses the safety probe.
    D3D12_GPU_VIRTUAL_ADDRESS ReserveUpload( UINT64 size, UINT64 alignment );
    uint8_t* GetUploadPtr( D3D12_GPU_VIRTUAL_ADDRESS addr );
    ID3D12Resource* GetUploadBuffer() const
    {
        return m_uploadSystem.Resource( m_allocatorIndex );
    }
    D3D12_CPU_DESCRIPTOR_HANDLE AllocateRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE AllocateDSV();
    UINT AllocateStaticSRV();
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRVStagingCpuHandle( UINT index );
};
} // namespace Rendering
} // namespace SkullbonezCore
