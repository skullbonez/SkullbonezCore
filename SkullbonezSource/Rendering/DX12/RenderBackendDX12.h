/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
Purpose:
  Declares the production DX12 renderer plus concrete texture and pipeline owners.

Mental model:
  RenderBackendDX12 coordinates device/frame work. Dx12TextureOwner retains
  texture residency and binding state, while Dx12PipelineOwner retains the
  ordinary raster recipe and draw-preparation cache.

Glossary:
  Recording epoch: Logical open/closed state of the reusable command list,
  advanced only by successful Close or Reset operations.
  Sticky failure: First active command-path error retained until device
  initialization resets the epoch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  TLAS (Top-Level Acceleration Structure): Raytracing spatial index for scene
  instances that point at BLAS geometry.
  SBT (Shader Binding Table): Raytracing table that maps ray records to
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
  Descriptor heap: DX12 table of descriptor rows; shader-visible heaps can be
  indexed by GPU commands.
  Fence: GPU/CPU synchronization counter used to prove submitted command work
  has completed before memory is reused.
  Root signature: DX12 binding contract that declares which descriptor tables
  and constants shaders may access.
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - Recording failure prevents further command emission and allocator/upload
    reuse; only successful device initialization clears it.

Related:
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../IRenderCaptureBackend.h"
#include "../../GameObjects/SceneCapacity.h"
#include "../IRenderCommandContext.h"
#include "../IRenderDeviceLifecycle.h"
#include "../IRenderDiagnostics.h"
#include "../IRenderResourceFactory.h"
#include "../IRenderRayTracing.h"
#include "../RenderRasterBindingContract.h"
#include "RenderBackendDX12.CommandRecordingState.h"
#include "RenderBackendDX12.PipelineState.h"
#include "RenderGraphTransientDX12.h"
#include "RenderDeviceDX12.h"
#include "MeshDX12.h"
#include "BLASDX12.h"
#include "TLASDX12.h"
#include "SBTDX12.h"
#include "../RenderGraph.h"
#include "../../Core/Common.h"
#include <d3d12.h>
#include <dxgi1_5.h>
#include <array>
#include <cstddef>
#include <vector>


namespace SkullbonezCore
{
namespace Rendering
{

class ShaderDX12;
class RenderBackendDX12;


// Texture entry for the DX12 SRV registry.
//
// "SRV" means Shader Resource View. It is the descriptor flavor a shader uses
// when it wants to read a texture. The ID3D12Resource below is the actual image
// memory. The srvIndex is only a row number in the descriptor heap table that
// tells the shader how to read that image.
struct TextureEntryDX12
{
    ID3D12Resource* resource;
    UINT srvIndex;                                                     // Index in the persistent SRV region
    bool owned;                                                        // False for FBO-registered SRVs
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
    // contract; bytecode hashes keep recompiled identical shaders from
    // exploding the cache during scene reload stress.
    const void* rootSignature;
    size_t shaderVSHash;
    size_t shaderPSHash;
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

struct CachedPSODX12
{
    size_t hash = 0;
    ID3D12PipelineState* pso = nullptr;
};

struct GridLinePSODX12
{
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    ID3D12PipelineState* pso = nullptr;
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
    UINT64 readFenceValue = 0;                                         // fence value that guarantees the latest ResolveQueryData has completed
    bool slotWritten[DX12_TIMER_HEAP_SIZE] = {};                       // true for each timestamp slot that had EndQuery recorded this frame
};

struct DeferredResourceReleaseDX12
{
    ID3D12Resource* resource = nullptr;
    UINT64 fenceValue = 0;
    bool fenceAssigned = false;
};

// Concept: texture lifetime is independent from frame/device orchestration.
// This owner retains the 1-based handle table, binding rows, and mip pipeline;
// callers lend command-recording dependencies only for the duration of an
// operation, so shutdown cannot leave a stored pointer back into the backend.
class Dx12TextureOwner
{
  public:
    Basics::SbResult Initialize( RenderBackendDX12& backend );
    void Shutdown();
    uint32_t CreateTexture2D( RenderBackendDX12& backend,
                              const uint8_t* data,
                              int width,
                              int height,
                              int channels,
                              bool generateMips,
                              bool linearFilter,
                              bool& graphicsStateInvalidated );
    void BindTexture( uint32_t handle, int slot );
    void DeleteTexture( RenderBackendDX12& backend, uint32_t handle );
    UINT RegisterSRV( UINT srvIndex );
    void UnregisterSRV( uint32_t handle );
    void ClearBoundSlotsForSrv( UINT srvIndex );
    UINT ResolveBoundSrv( int slot ) const;
    void SetNullSrvIndex( UINT index );
    void MarkBindingsClean();
    void InvalidateBindings();
    bool BindingsDirty() const;
    size_t RegistryCount() const;
    size_t RegistryCapacity() const;
    UINT ResolveSrv( uint32_t handle ) const;
    uint32_t FindHandleForSrv( UINT srvIndex ) const;

  private:
    bool GenerateMips( RenderBackendDX12& backend,
                       ID3D12Resource* texture,
                       DXGI_FORMAT format,
                       UINT width,
                       UINT height,
                       UINT mipCount,
                       bool& graphicsStateInvalidated );

    std::vector<TextureEntryDX12> m_textures;
    UINT m_boundTexSlot[TEXTURE_SLOT_COUNT] = { UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX };
    UINT m_nullTextureSRVIndex = UINT_MAX;
    ID3D12PipelineState* m_genMipsPSO = nullptr;
    ID3D12RootSignature* m_genMipsRS = nullptr;
    UINT m_genMipsNullUAV = UINT_MAX;
    bool m_texBindingsDirty = true;
};

// Concept: a pipeline is the complete draw recipe, not a collection of backend
// flags. This owner retains root-signature, fixed-state intent, target state,
// PSO cache, and the dirty-state fast path as one coherent lifetime.
class Dx12PipelineOwner
{
  public:
    Basics::SbResult Initialize( ID3D12Device* device );
    void Shutdown();
    bool PrepareDraw( ID3D12Device* device,
                      ID3D12GraphicsCommandList* commandList,
                      Dx12CommandRecordingState& recording,
                      Dx12TextureOwner& textures,
                      Dx12DescriptorAllocator& descriptors,
                      VertexFormat12 format,
                      bool instanced,
                      const InstancedMeshDX12* instancedMesh,
                      const DynamicVBDX12* dynamicVertexBuffer );
    void SetActiveShader( ShaderDX12* shader );
    ShaderDX12* ActiveShader() const;
    void SetCurrentTargets( D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv );
    void SetRenderingToFBO( bool rendering, DXGI_FORMAT rtvFormat );
    void SetViewport( const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor );
    void SetDepthTest( bool enabled );
    void SetDepthWrite( bool enabled );
    void SetBlend( bool enabled );
    void SetBlendFunc( BlendFactor src, BlendFactor dst );
    void SetCullFace( bool enabled );
    void SetPolygonOffset( bool enabled, float factor, float units );
    bool DepthTestEnabled() const;
    bool DepthWriteEnabled() const;
    bool BlendEnabled() const;
    bool CullEnabled() const;
    void GetBlendFunc( BlendFactor& src, BlendFactor& dst ) const;
    void InvalidateCommandState();
    void InvalidateTargets();
    DXGI_FORMAT RenderTargetFormat() const;
    ID3D12RootSignature* RootSignature() const;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRTV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentDSV() const;
    bool RenderingToFramebuffer() const;
    void RestoreRenderTargetFormat( DXGI_FORMAT format );
    void SetCurrentColorTarget( D3D12_CPU_DESCRIPTOR_HANDLE rtv );
    void BindCurrentOutputs( ID3D12GraphicsCommandList* commandList ) const;
    void ClearCurrentColor( ID3D12GraphicsCommandList* commandList, const float color[4] ) const;
    void ClearCurrentDepth( ID3D12GraphicsCommandList* commandList, float depth ) const;
    size_t CacheCount() const;

  private:
    static size_t HashPSOKey( const PSOKey12& key );
    static void BuildInputLayout( VertexFormat12 format, D3D12_INPUT_ELEMENT_DESC* output, UINT& count );
    static void
    BuildInstancedInputLayout( const InstancedMeshDX12& mesh, D3D12_INPUT_ELEMENT_DESC* output, UINT& count );
    static void BuildDynamicVBInputLayout( const DynamicVBDX12& buffer, D3D12_INPUT_ELEMENT_DESC* output, UINT& count );
    ID3D12PipelineState* CreatePSO( ID3D12Device* device,
                                    VertexFormat12 format,
                                    bool instanced,
                                    const InstancedMeshDX12* instancedMesh,
                                    const DynamicVBDX12* dynamicVertexBuffer );
    void ResetDesiredState();

    static constexpr size_t CACHE_CAPACITY = 96;
    std::array<CachedPSODX12, CACHE_CAPACITY> m_psoCache = {};
    size_t m_psoCacheCount = 0;
    ID3D12RootSignature* m_rootSignature = nullptr;
    ShaderDX12* m_activeShader = nullptr;
    D3D12_VIEWPORT m_viewport = {};
    D3D12_RECT m_scissorRect = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentRTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentDSV = {};
    DXGI_FORMAT m_currentRTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool m_depthTestEnabled = true;
    bool m_depthWriteEnabled = true;
    bool m_blendEnabled = false;
    BlendFactor m_blendSrc = BlendFactor::One;
    BlendFactor m_blendDst = BlendFactor::Zero;
    bool m_cullEnabled = true;
    bool m_polyOffsetEnabled = false;
    float m_polyOffsetFactor = 0.0f;
    float m_polyOffsetUnits = 0.0f;
    bool m_renderingToFBO = false;
    size_t m_lastPSOHash = 0;
    bool m_psoDirty = true;
    bool m_targetsDirty = true;
};

// Lifetime: graph transient slots own their DX12 resource until the backend
// releases the graph pool. Descriptor rows come from the backend descriptor
// allocators and are reused with the slot; they must not be mixed into
// material/object texture ownership.
// Concept: RenderBackendDX12 composes the concrete DX12 owners behind the
// engine-facing capability interfaces.
//
// The public interfaces use engine verbs: set a shader, set textures, draw
// meshes, present the frame. Internally, DX12 requires the backend to make every
// hidden GPU concept explicit: descriptor table rows, command allocators,
// resource states, fences, upload memory, and compiled pipeline state. Texture
// and pipeline lifetime belong to the named owners above; this class sequences
// their work with the device/frame command stream.
class RenderBackendDX12 : public IRenderDeviceLifecycle,
                          public IRenderResourceFactory,
                          public IRenderCommandContext,
                          public IRenderDiagnostics,
                          public IRenderCaptureBackend,
                          public IRenderRayTracing
{
    friend class Dx12TextureOwner;

  private:
    // Frame management:
    //
    // Two frames can be in flight. Each frame owns its own command allocator,
    // upload arena, transient descriptors, and fence value so the CPU never
    // overwrites memory or descriptor rows still being read by the GPU.
    static const int FRAME_COUNT = 2;
    static const UINT MAX_RTV_DESCRIPTORS = 32;
    static const UINT MAX_DSV_DESCRIPTORS = 16;
    static const UINT MAX_STATIC_SRVS = 128;
    static const UINT MAX_TRANSIENT_SRVS = 2048;                       // per frame allocator
    // Hazard: replay prediction ribbons upload transient line geometry through
    // this frame arena. Exhaustion is fatal, so keep this cap aligned with the
    // largest expected debug/prediction overlay until the overlay is bounded.
    static const UINT64 UPLOAD_BUFFER_SIZE = 32 * 1024 * 1024;
    static const int TIMER_HEAP_MARKERS = DX12_TIMER_HEAP_MARKERS;     // must be >= Profiler::MAX_MARKERS
    static const int TIMER_HEAP_SIZE = DX12_TIMER_HEAP_SIZE;           // begin + end per marker

    // Ordinary raster binding ABI lives in RenderRasterBindingContract.h so
    // runtime passes and the DX12 backend consume one shader/root-signature map.
    static constexpr size_t MAX_GRID_LINE_PSOS = 4;
    static constexpr size_t TRANSIENT_TRIANGLE_STYLE_COUNT = 4;

    // CPU-side registries. These are not GPU resources by themselves; they are
    // lookup tables the backend uses to find cached GPU objects and descriptor
    // rows while translating engine draw calls into command-list operations.
    // Runtime allocation policy: PSO discovery is bounded. A cache miss may
    // compile a GPU object during warm-up, but inserting it never grows a heap
    // container and cap exhaustion fails with the missing pipeline shape.
    Dx12TextureOwner m_textureOwner;
    Dx12PipelineOwner m_pipelineOwner;
    std::vector<DynamicVBDX12> m_dynamicVBs;
    std::vector<InstancedMeshDX12> m_instancedMeshes;

    // Currently bound render state. DX12 does not remember high-level engine
    // intent for us, so the backend tracks the desired state and emits concrete
    // command-list binds only when the state becomes dirty.
    BLAS m_terrainBLAS;
    BLAS m_sphereBLAS;
    TLAS m_tlas;
    SBT m_sbt;
    GpuTimerStateDX12 m_gpuTimers;

    // The render device owns the core D3D12 lifetime: factory, device, queue,
    // swap chain, command allocators, command list, and frame fence. Access the
    // device, swap chain, and command list through these helpers so resize or
    // device-owner work cannot leave backend-side aliases dangling.
    Dx12RenderDevice m_renderDevice;
    ID3D12Device* Device() const
    {
        return m_renderDevice.Device();
    }
    IDXGISwapChain3* SwapChain() const
    {
        return m_renderDevice.SwapChain();
    }
    ID3D12GraphicsCommandList* CommandList() const
    {
        return m_renderDevice.CommandList();
    }

    // Borrowed core queue/allocator aliases. Do not Release() these in the backend.
    IDXGIFactory4* m_factory = nullptr;
    ID3D12CommandQueue* m_commandQueue = nullptr;
    ID3D12CommandAllocator* m_commandAllocators[FRAME_COUNT] = {};
    static constexpr int PLATFORM_PROFILER_GPU_SCOPE_STACK_MAX = 64;
    static constexpr std::size_t PLATFORM_PROFILER_GPU_MARKER_NAME_CHARS = 256;
    struct PlatformProfilerGpuScopeDX12
    {
        char name[PLATFORM_PROFILER_GPU_MARKER_NAME_CHARS] = {};
        uint32_t hash = 0;
    };

    // Invariant: only successful Close/Reset operations change this epoch. A
    // sticky failure makes every later recording entry point a no-op until a
    // new device initialization establishes a fresh command-list lifetime.
    Dx12CommandRecordingState m_commandRecording;
    // Submission completion is separate from recording state: a closed list
    // may already be executing without a covering fence. This value blocks
    // allocator/resource reuse until a real fence proves completion.
    Dx12SubmittedWorkState m_submittedWork;
    Dx12DeviceHealthState m_deviceHealth;
    Dx12FaultInjectionState m_faultInjection;
    uint64_t m_recreationGeneration = 0;                               // Advances only after complete resize publication.
    int m_platformProfilerGpuDepth = 0;                                // Nesting depth guard for platform GPU marker begin/end balance.
    std::array<PlatformProfilerGpuScopeDX12, PLATFORM_PROFILER_GPU_SCOPE_STACK_MAX> m_platformProfilerGpuStack = {};

    ID3D12Resource* m_renderTargets[FRAME_COUNT] = {};
    UINT m_frameIndex = 0;
    UINT m_allocatorIndex = 0;                                         // Which allocator is active (alternates 0/1)

    UINT64 m_frameFenceValues[FRAME_COUNT] = {};                       // Fence value signaled by each frame's submission

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
    ID3D12DescriptorHeap* m_srvHeap = nullptr;                         // GPU-visible table shaders can read during draws/dispatches.
    ID3D12DescriptorHeap* m_srvStagingHeap = nullptr;                  // CPU-only table holding persistent descriptor templates.
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

    // Lifetime: an uncertain screenshot submission cannot release its readback
    // buffer. Keep the bounded COM references until terminal shutdown proves a
    // full queue drain; a failed terminal drain stops before Release.
    std::array<ID3D12Resource*, FRAME_COUNT> m_uncertainReadbackResources = {};
    size_t m_uncertainReadbackResourceCount = 0;

    int m_width = 0;
    int m_height = 0;
    bool m_isVsyncEnabled = true;
    bool m_allowTearing = false;
    int m_frameDrawCallCount = 0;
    DrawCallTrace m_drawCallTrace;

    float m_clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float m_clearDepth = 1.0f;
    // Lifetime: resource owners transfer COM references here when a framebuffer
    // or texture is invalidated before the GPU has necessarily consumed the
    // command stream that mentioned it.
    std::vector<DeferredResourceReleaseDX12> m_deferredResourceReleases;
    // Graph-created transient targets use the backend descriptor allocators, but
    // they are tracked in their own pool so material/object texture tables do
    // not become the owner of frame-target lifetime. A pool slot may be reused
    // only when the graph compiler has already proven descriptor and lifetime
    // compatibility for that slot.
    std::vector<GraphTransientResourceDX12> m_graphTransientResources;
    // Maps logical graph handles from the latest compile to physical pool slots
    // so callbacks can resolve the resource named by their pass declaration.
    std::vector<GraphTransientBindingDX12> m_graphTransientBindings;
    GraphTransientMaterializationStatsDX12 m_graphTransientStats;
    bool m_graphRenderTargetActive = false;
    RenderGraphResourceHandle m_activeGraphRenderTarget;
    D3D12_CPU_DESCRIPTOR_HANDLE m_savedGraphRTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_savedGraphDSV = {};
    DXGI_FORMAT m_savedGraphRTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;


    // Runtime allocation policy: debug-line shader and PSOs are warmed during
    // backend setup for every engine RTV format, so overlay draws do not compile
    // shaders or grow GPU object caches inside the render phase.
    std::unique_ptr<IShader> m_gridLineShader;
    std::array<GridLinePSODX12, MAX_GRID_LINE_PSOS> m_gridLinePSOs = {};
    size_t m_gridLinePSOCount = 0;
    int m_gridLineVBCapacity = 0;
    // Runtime allocation policy: transient triangle shaders are warmed at
    // backend init for each generic style so overlay draws do not compile HLSL
    // while building a frame.
    std::array<std::unique_ptr<IShader>, TRANSIENT_TRIANGLE_STYLE_COUNT> m_transientTriangleShaders;

    // Invariant: this is the graph-visible state for the current swap-chain
    // image in m_frameIndex. It resets to Present whenever DXGI gives us a new
    // current backbuffer through resize or Present.
    RenderGraphResourceAccess m_backBufferAccess = RenderGraphResourceAccess::Present;


    bool m_dxrSupported = false;
    Basics::SbResult m_dxrFeatureResult = Basics::SbResult::Success(); // Retains one bounded optional-fallback reason.
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


    // --- Internal helpers ---
    Basics::SbResult WaitForGpu();
    Basics::SbResult EnsureCommandListOpen();
    Basics::SbResult SubmitClosedCommandList();
    void ConfigureFaultInjection();
    void WriteFaultInjectionProbeReport() const;
    void AssignDeferredResourceReleaseFence( UINT64 fenceValue );
    void ReleaseCompletedDeferredResources( bool releaseUnfenced );
    void TryConsumeGpuTimerReadback( bool waitForFence );
    Basics::SbResult CreateDepthStencil( int w, int h );
    Basics::SbResult CreateDepthStencilResource( int w, int h, ID3D12Resource*& outResource );
    void PublishDepthStencilView( ID3D12Resource* resource );
    UINT AllocateTransientSRV();
    UINT AllocateTransientSRVRange( UINT count );
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGpuHandle( UINT index );
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle( UINT index );
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle( UINT index );
    bool TransitionBackbuffer( const char* passName, RenderGraphResourceAccess after );
    // Keeps cached texture-slot state from pointing at an SRV descriptor row
    // whose owning resource is being deleted or unregistered.
    void ClearBoundTextureSlotsForSrv( UINT srvIndex );
    Basics::SbResult FlushUploadBuffer();
    Basics::SbResult FlushUploadBufferIfNeeded( UINT64 size, UINT64 alignment );
    D3D12_GPU_VIRTUAL_ADDRESS SubAllocateUpload( UINT64 size, UINT64 alignment );
    void ReportArchitectureStats( const char* reason ) const;
    GraphTransientResourceDX12* FindGraphTransientSlot( RenderGraphResourceHandle resource );
    const GraphTransientResourceDX12* FindGraphTransientSlot( RenderGraphResourceHandle resource ) const;
    void ReleaseGraphTransientResources( const char* reason );
    void ReportDeviceLost( const char* context, HRESULT result ) const;
    ID3D12PipelineState* EnsureGridLinePipeline( DXGI_FORMAT rtvFormat );
    void CheckDXRSupport();
    Basics::SbResult CreateRTRootSignature();
    Basics::SbResult CreateRTPipeline();
    Basics::SbResult CreateReflectionUAV( int width, int height );
    void AssertPlatformProfilerGpuStackClosed( const char* reason ) const;
    int SuspendPlatformProfilerGpuStackForSubmit( const char* reason );
    void RestorePlatformProfilerGpuStackAfterSubmit( int suspendedDepth );
    IShader* EnsureTransientTriangleShader( TransientTriangleStyle style );


  public:
    RenderBackendDX12();
    ~RenderBackendDX12() override
    {
        Shutdown();
    }

    Basics::SbResult Init( HWND hwnd, HDC hdc, int width, int height ) override;
    void Shutdown() override;
    Basics::SbResult Present() override;
    void SetVsyncEnabled( bool enabled ) override;
    bool IsVsyncEnabled() const override;
    Basics::SbResult Finish() override;
    Basics::SbResult FlushGPU() override;
    Basics::SbResult DrainForResourceRelease() override;
    Basics::SbResult Resize( int width, int height ) override;

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
    RenderGraphTransientMaterializationStats
    MaterializeGraphTransientResources( const RenderGraph& graph, const RenderGraphCompileResult& compiled ) override;
    RenderGraphTextureBinding ResolveGraphTextureBinding( RenderGraphResourceHandle resource ) const override;
    void BeginGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName ) override;
    void EndGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName ) override;

    Basics::SbResult CaptureBackbuffer( std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight ) override;
    bool SupportsBackbufferCapture() const override
    {
        return true;
    }

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
    RenderMemoryStats GetRenderMemoryStats() const override;
    RenderCapabilities GetCapabilities() const override
    {
        RenderCapabilities capabilities;
        capabilities.supportsBackbufferCapture = SupportsBackbufferCapture();
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

    Basics::SbResult InitDXR( uint64_t terrainVBVA,
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
                                 const float* skyColorTop,
                                 const float* skyColorBottom,
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
    void DrawTransientColoredTriangles( const float* data,
                                        int vertexCount,
                                        const float* viewProjMatrix16,
                                        TransientTriangleStyle style ) override;

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
        return m_pipelineOwner.ActiveShader();
    }
    ID3D12Device* GetDevice() const
    {
        return Device();
    }
    ID3D12GraphicsCommandList* GetCommandList() const
    {
        return CommandList();
    }

    bool PrepareDraw( VertexFormat12 format,
                      bool instanced = false,
                      const InstancedMeshDX12* im = nullptr,
                      const DynamicVBDX12* dvb = nullptr );
    UINT RegisterSRV( UINT srvIndex );
    void UnregisterSRV( uint32_t handle );
    // Transfers one COM reference to the backend so it can be released only
    // after the frame fence proves submitted command lists no longer reference it.
    void RetireResource( ID3D12Resource* resource );

    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const
    {
        return m_pipelineOwner.CurrentRTV();
    }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentDSV() const
    {
        return m_pipelineOwner.CurrentDSV();
    }
    void SetCurrentTargets( D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv );
    void SetRenderingToFBO( bool rendering,
                            UINT fboSrvIndex = UINT_MAX,
                            UINT fboDepthSrvIndex = UINT_MAX,
                            DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM );
    bool ExecuteGraphTransition( const char* passName,
                                 const char* resourceName,
                                 ID3D12Resource* resource,
                                 RenderGraphResourceAccess before,
                                 RenderGraphResourceAccess after,
                                 UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES );
    bool ExecuteGraphUavBarrier( const char* passName, const char* resourceName, ID3D12Resource* resource );

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
