/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
Purpose:
  Declares the production DX12 renderer plus concrete texture, pipeline,
  geometry, and raytracing owners.

Summary:
  RenderBackendDX12 coordinates device/frame work. Dx12TextureOwner retains
  texture residency and binding state, Dx12PipelineOwner retains the ordinary
  raster recipe, Dx12GeometryOwner retains bounded geometry resources, and
  Dx12RaytracingOwner retains the optional reflection path. Dx12DescriptorHeaps
  owns every descriptor table and row allocator, Dx12BackbufferCapture owns
  screenshot readback/quarantine, Dx12GraphTransientPool owns physical graph
  targets and their balanced binding transaction, Dx12Diagnostics owns timing
  and draw evidence, Dx12ShaderDevelopment owns cold reload registration and
  adoption, and the private frame epoch and retirement owners live in
  Dx12FrameOwner.h.

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
  Platform profiler GPU stack: Fixed marker-name and nesting rows suspended
  before command-list submission and restored on the replacement list.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - Recording failure prevents further command emission and allocator/upload
    reuse; only successful device initialization clears it.

Related:
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp
  - SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.h
  - SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h
  - SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.h
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../../Core/PlatformWin32.h"


#include "../IRenderCaptureBackend.h"
#include "../../Runtime/Scene/SceneCapacity.h"
#include "../IRenderCommandContext.h"
#include "../IRenderDeviceLifecycle.h"
#include "../IRenderDiagnostics.h"
#include "../IRenderResourceFactory.h"
#include "../IRenderRayTracing.h"
#include "../IRenderShaderDevelopment.h"
#include "../RenderRasterBindingContract.h"
#include "RenderBackendDX12.CommandRecordingState.h"
#include "RenderBackendDX12.PipelineState.h"
#include "Dx12CachedPsoStore.h"
#include "RenderGraphTransientDX12.h"
#include "RenderDeviceDX12.h"
#include "Dx12BackbufferCapture.h"
#include "Dx12GraphTransientPool.h"
#include "Dx12TextureRegistry.h"
#include "MeshDX12.h"
#include "Dx12DescriptorHeaps.h"
#include "Dx12Diagnostics.h"
#include "Dx12ShaderDevelopment.h"
#include "Dx12FrameOwner.h"
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
class Dx12PipelineOwner;
class Dx12TextureOwner;
class RenderBackendDX12;


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

// Capability: texture creation and mip generation may record resource work,
// allocate descriptor rows, reserve upload bytes, and retire textures. The
// capability contains only the concrete device/frame owners and cannot recover
// the aggregate backend or any sibling renderer business state.
class Dx12TextureCommands
{
  public:
    Dx12TextureCommands( Dx12RenderDevice& device, Dx12FrameOwner& frame ) : m_device( device ), m_frame( frame )
    {
    }
    ID3D12Device* Device() const
    {
        return m_device.Device();
    }
    ID3D12GraphicsCommandList* CommandList() const
    {
        return m_frame.CommandList();
    }
    SkullbonezCore::Core::SbResult EnsureOpen()
    {
        return m_frame.EnsureOpen();
    }
    UINT AllocateStaticSrv()
    {
        return m_frame.Descriptors().AllocateStatic();
    }
    UINT StaticDescriptorCapacity() const
    {
        return m_frame.Descriptors().GetStats().staticCapacity;
    }
    UINT AllocateTransientSrv()
    {
        return m_frame.Descriptors().AllocateTransient();
    }
    UINT AllocateTransientSrvRange( UINT count )
    {
        return m_frame.Descriptors().AllocateTransientRange( count );
    }
    D3D12_CPU_DESCRIPTOR_HANDLE StagingCpuHandle( UINT index ) const
    {
        return m_frame.Descriptors().StagingCpuHandle( index );
    }
    D3D12_CPU_DESCRIPTOR_HANDLE ShaderVisibleCpuHandle( UINT index ) const
    {
        return m_frame.Descriptors().ShaderVisibleCpuHandle( index );
    }
    void PublishStaticDescriptor( UINT index ) const
    {
        m_frame.Descriptors().PublishStaticDescriptor( m_device.Device(), index );
    }
    D3D12_GPU_DESCRIPTOR_HANDLE ShaderVisibleGpuHandle( UINT index ) const
    {
        return m_frame.Descriptors().ShaderVisibleGpuHandle( index );
    }
    D3D12_GPU_VIRTUAL_ADDRESS ReserveUpload( UINT64 size, UINT64 alignment )
    {
        return m_frame.UploadReservations().ReserveUpload( size, alignment, RenderUploadCategory::TextureRows );
    }
    uint8_t* UploadPointer( D3D12_GPU_VIRTUAL_ADDRESS address ) const
    {
        return m_frame.UploadReservations().UploadPointer( address );
    }
    UINT64 UploadOffset( D3D12_GPU_VIRTUAL_ADDRESS address ) const
    {
        return m_frame.Uploads().OffsetFromAddress( m_frame.AllocatorIndex(), address );
    }
    ID3D12Resource* UploadResource() const
    {
        return m_frame.Uploads().Resource( m_frame.AllocatorIndex() );
    }
    void Retire( ID3D12Resource* resource )
    {
        m_frame.ResourceRelease().Retire( resource );
    }
    void Retire( ID3D12Resource* resource, UINT descriptorIndex )
    {
        m_frame.ResourceRelease().Retire( resource, descriptorIndex );
    }
    void RetireStaticDescriptor( UINT descriptorIndex )
    {
        m_frame.ResourceRelease().RetireStaticDescriptor( descriptorIndex );
    }
    bool Transition( const char* passName,
                     const char* resourceName,
                     ID3D12Resource* resource,
                     RenderGraphResourceAccess before,
                     RenderGraphResourceAccess after,
                     UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES );
    bool UavBarrier( const char* passName, const char* resourceName, ID3D12Resource* resource );

  private:
    Dx12RenderDevice& m_device;
    Dx12FrameOwner& m_frame;
};

// Concept: texture lifetime is independent from frame/device orchestration.
// This owner retains the 1-based handle table, binding rows, and mip pipeline;
// callers lend command-recording dependencies only for the duration of an
// operation, so shutdown cannot leave a stored pointer back into the backend.
class Dx12TextureOwner
{
  public:
    SkullbonezCore::Core::SbResult Initialize( Dx12TextureCommands& commands );
    SkullbonezCore::Core::SbResult PrepareGenerateMipsShaderReload( ID3D12Device* device,
                                                                    ID3D12PipelineState*& candidate );
    void AdoptGenerateMipsShaderReload( ID3D12PipelineState* candidate );
    void Shutdown();
    uint32_t CreateTexture2D( Dx12TextureCommands& commands,
                              const uint8_t* data,
                              int width,
                              int height,
                              int channels,
                              bool generateMips,
                              bool linearFilter,
                              bool& graphicsStateInvalidated );
    void BindTexture( uint32_t handle, int slot );
    void DeleteTexture( Dx12TextureCommands& commands, uint32_t handle );
    UINT RegisterSRV( UINT srvIndex );
    UINT UnregisterSRV( uint32_t handle );
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
    TextureEntryDX12* ResolveEntry( uint32_t handle );
    const TextureEntryDX12* ResolveEntry( uint32_t handle ) const;
    uint32_t ReuseOrAppend( const TextureEntryDX12& entry );
    void ReportStaleHandle( uint32_t handle ) const;
    bool GenerateMips( Dx12TextureCommands& commands,
                       ID3D12Resource* texture,
                       DXGI_FORMAT format,
                       UINT width,
                       UINT height,
                       UINT mipCount,
                       bool& graphicsStateInvalidated );

    Dx12TextureRegistry m_registry;
    UINT m_boundTexSlot[TEXTURE_SLOT_COUNT] = { UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX };
    UINT m_nullTextureSRVIndex = UINT_MAX;
    ID3D12PipelineState* m_genMipsPSO = nullptr;
    ID3D12RootSignature* m_genMipsRS = nullptr;
    UINT m_genMipsNullUAV = UINT_MAX;
    bool m_texBindingsDirty = true;
    mutable bool m_staleHandleReported = false;
};

// Concept: a pipeline is the complete draw recipe, not a collection of backend
// flags. This owner retains root-signature, fixed-state intent, target state,
// PSO cache, and the dirty-state fast path as one coherent lifetime.
class Dx12PipelineOwner
{
  public:
    SkullbonezCore::Core::SbResult Initialize( ID3D12Device* device );
    void Shutdown();
    bool PrepareDraw( ID3D12Device* device,
                      ID3D12GraphicsCommandList* commandList,
                      Dx12CommandRecordingState& recording,
                      Dx12TextureOwner& textures,
                      VertexFormat12 format,
                      bool instanced,
                      const InstancedMeshDX12* instancedMesh,
                      const DynamicVBDX12* dynamicVertexBuffer );
    void SetActiveShader( ShaderDX12* shader );
    void ReleaseShaderPipelinesForReload();
    void RestoreShaderPipelinesAfterReload();
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
    static constexpr size_t ROOT_SIGNATURE_SERIALIZED_CAPACITY = 4096;
    Dx12CachedPsoStore m_persistentPsoCache;
    std::array<CachedPSODX12, CACHE_CAPACITY> m_psoCache = {};
    size_t m_psoCacheCount = 0;
    // Canonical UnifiedRaster bytes reopen the P4 cache after a changed manifest
    // without retaining the temporary root-signature serialization blob.
    std::array<std::uint8_t, ROOT_SIGNATURE_SERIALIZED_CAPACITY> m_rootSignatureSerialized = {};
    size_t m_rootSignatureSerializedSize = 0;
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

// Concept: dynamic and instanced geometry share one bounded backend lifetime.
//
// This owner retains handle registries plus warmed overlay shaders/PSOs. Frame
// coordination lends device, command, upload, pipeline, and diagnostics values
// to each operation; the owner stores no backend host pointer or callback seam.
class Dx12GeometryOwner
{
  public:
    uint32_t CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices );
    void UploadAndDrawDynamicVB( uint32_t handle,
                                 const float* data,
                                 int vertexCount,
                                 D3D12_GPU_VIRTUAL_ADDRESS address,
                                 uint8_t* uploadPointer,
                                 ID3D12GraphicsCommandList* commandList,
                                 Dx12DrawGate& drawGate,
                                 Dx12Diagnostics& diagnostics );
    void DestroyDynamicVB( uint32_t handle );
    void AdoptGridLineShader( std::unique_ptr<IShader> shader );
    bool EnsureGridLinePipeline( ID3D12Device* device, Dx12PipelineOwner& pipeline, DXGI_FORMAT rtvFormat );
    void AdoptTransientTriangleShader( TransientTriangleStyle style, std::unique_ptr<IShader> shader );
    static const char* TransientShaderBaseName( TransientTriangleStyle style );
    bool HasTransientTriangleShader( TransientTriangleStyle style ) const;
    void InvalidateGridLinePipelinesForShaderReload();
    UINT GridLineConstantBytes() const;
    UINT TransientConstantBytes( TransientTriangleStyle style ) const;
    void DrawLinesColored( const float* data,
                           int vertexCount,
                           const float* viewProjMatrix16,
                           D3D12_GPU_VIRTUAL_ADDRESS vertexAddress,
                           uint8_t* uploadPointer,
                           ID3D12GraphicsCommandList* commandList,
                           Dx12PipelineOwner& pipeline,
                           Dx12DrawGate& drawGate,
                           Dx12Diagnostics& diagnostics );
    void DrawTransientColoredTriangles( const float* data,
                                        int vertexCount,
                                        const float* viewProjMatrix16,
                                        TransientTriangleStyle style,
                                        int viewportWidth,
                                        int viewportHeight,
                                        D3D12_GPU_VIRTUAL_ADDRESS vertexAddress,
                                        uint8_t* uploadPointer,
                                        ID3D12GraphicsCommandList* commandList,
                                        Dx12DrawGate& drawGate,
                                        Dx12Diagnostics& diagnostics );
    uint32_t CreateInstancedMesh( const float* staticData,
                                  int staticVertCount,
                                  int staticFloatsPerVert,
                                  int instanceFloats,
                                  int instanceStartAttrib,
                                  const int* instanceAttribSizes,
                                  int numInstanceAttribs,
                                  const int* staticAttribSizes,
                                  int numStaticAttribs,
                                  ID3D12Device* device,
                                  ID3D12GraphicsCommandList* commandList,
                                  ID3D12Resource* uploadResource,
                                  D3D12_GPU_VIRTUAL_ADDRESS uploadAddress,
                                  uint8_t* uploadPointer );
    void UploadInstanceData( uint32_t handle,
                             const float* data,
                             int floatCount,
                             D3D12_GPU_VIRTUAL_ADDRESS address,
                             uint8_t* uploadPointer );
    void DrawInstancedMesh( uint32_t handle,
                            int staticVertCount,
                            int instanceCount,
                            ID3D12GraphicsCommandList* commandList,
                            Dx12DrawGate& drawGate,
                            Dx12Diagnostics& diagnostics );
    void DestroyInstancedMesh( uint32_t handle );
    uint64_t StaticVertexBufferAddress( uint32_t handle ) const;
    int StaticVertexStride( uint32_t handle ) const;
    size_t DynamicCount() const;
    size_t DynamicCapacity() const;
    UINT64 DynamicUploadBytes( uint32_t handle, int vertexCount ) const;
    size_t InstancedCount() const;
    size_t InstancedCapacity() const;
    void Shutdown();

  private:
    static constexpr size_t MAX_GRID_LINE_PSOS = 4;
    static constexpr size_t TRANSIENT_TRIANGLE_STYLE_COUNT = 4;
    std::vector<DynamicVBDX12> m_dynamicVBs;
    std::vector<InstancedMeshDX12> m_instancedMeshes;
    std::unique_ptr<IShader> m_gridLineShader;
    std::array<GridLinePSODX12, MAX_GRID_LINE_PSOS> m_gridLinePSOs = {};
    size_t m_gridLinePSOCount = 0;
    std::array<std::unique_ptr<IShader>, TRANSIENT_TRIANGLE_STYLE_COUNT> m_transientTriangleShaders;
};

struct Dx12RaytracingSetupOutcome
{
    SkullbonezCore::Core::SbResult result = SkullbonezCore::Core::SbResult::Success();
    bool recordedBuildWork = false;
};

struct Dx12RaytracingDispatchOutcome
{
    SkullbonezCore::Core::SbResult result = SkullbonezCore::Core::SbResult::Success();
    bool rasterStateInvalidated = false;
};

// Concept: raytracing is one resource lifecycle, not backend frame state.
//
// This owner retains the optional Device5/command-list4 capability, reflection
// pipeline, acceleration structures, descriptors, constants, instance table,
// and bounded fallback reason. Operations borrow only the concrete device,
// command-list, descriptor, and texture facilities needed for that call; the
// owner never stores or reaches back through RenderBackendDX12.
class Dx12RaytracingOwner
{
  public:
    void ProbeCapability( ID3D12Device* device );
    bool Supported() const;
    bool Initialized() const;
    const SkullbonezCore::Core::SbResult& FeatureResult() const;

    Dx12RaytracingSetupOutcome BeginSetup( ID3D12Device* device,
                                           ID3D12GraphicsCommandList* commandList,
                                           Dx12DescriptorHeaps& descriptors,
                                           int renderWidth,
                                           int renderHeight,
                                           uint64_t terrainVBVA,
                                           int terrainVertCount,
                                           int terrainStride,
                                           uint64_t sphereVBVA,
                                           int sphereVertCount,
                                           int sphereStride );
    SkullbonezCore::Core::SbResult CompleteSetup( ID3D12Device* device, int maxInstances );
    void AbortSetup( const SkullbonezCore::Core::SbResult& failure );
    SkullbonezCore::Core::SbResult BuildScene( const float* instanceTransforms, int instanceCount );
    Dx12RaytracingDispatchOutcome DispatchReflections( ID3D12Device* device,
                                                       Dx12DescriptorHeaps& descriptors,
                                                       const Dx12TextureOwner& textures,
                                                       const float* invViewProj,
                                                       const float* cameraPos,
                                                       float waterY,
                                                       float time,
                                                       const float* lightPos,
                                                       const float* skyColorTop,
                                                       const float* skyColorBottom,
                                                       const uint32_t textureHandles[8] );
    UINT ReflectionSrvIndex() const;
    void Shutdown();

  private:
    SkullbonezCore::Core::SbResult CreateRootSignature( ID3D12Device* device );
    SkullbonezCore::Core::SbResult CreatePipeline();
    SkullbonezCore::Core::SbResult
    CreateReflectionTexture( ID3D12Device* device, Dx12DescriptorHeaps& descriptors, int width, int height );
    bool m_supported = false;
    SkullbonezCore::Core::SbResult m_featureResult = SkullbonezCore::Core::SbResult::Success();
    ID3D12Device5* m_device5 = nullptr;
    ID3D12GraphicsCommandList4* m_commandList4 = nullptr;
    ID3D12StateObject* m_pipeline = nullptr;
    ID3D12StateObjectProperties* m_pipelineProperties = nullptr;
    ID3D12RootSignature* m_rootSignature = nullptr;
    ID3D12Resource* m_reflectionTexture = nullptr;
    UINT m_reflectionUavIndex = 0;
    UINT m_reflectionSrvIndex = 0;
    int m_reflectionWidth = 0;
    int m_reflectionHeight = 0;
    bool m_reflectionInSrvState = false;
    ID3D12Resource* m_constantBuffer = nullptr;
    uint8_t* m_constantBufferMapped = nullptr;
    int m_maxInstances = 0;
    std::array<D3D12_RAYTRACING_INSTANCE_DESC, SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS + 1> m_instances = {};
    BLAS m_terrainBlas;
    BLAS m_sphereBlas;
    TLAS m_tlas;
    SBT m_sbt;
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
// Inheritance retention: rendering owns seven role facets so runtime callers
// receive only lifecycle, resource, command, diagnostics, capture, raytracing,
// or shader-development authority. Command calls are per-frame/per-draw; other
// facets are cold or diagnostic. Flattening them would republish the complete
// backend and violate capability narrowing. Retention is covered by the dated
// interface measurement plus DX12/perf gates.
class RenderBackendDX12 : public IRenderDeviceLifecycle,
                          public IRenderResourceFactory,
                          public IRenderCommandContext,
                          public IRenderDiagnostics,
                          public IRenderCaptureBackend,
                          public IRenderRayTracing,
                          public IRenderShaderDevelopment
{

  private:
    // Frame management:
    //
    // Two frames can be in flight. Each frame owns its own command allocator,
    // upload arena, transient descriptors, and fence value so the CPU never
    // overwrites memory or descriptor rows still being read by the GPU.
    static constexpr int FRAME_COUNT = Dx12FrameOwner::FRAME_COUNT;
    // Replay/debug geometry is owner-bounded before it reaches this arena. A
    // steady-phase overflow drops that draw; cold lifecycle/capture work may drain.
    // Capacity: 32 MiB per frame means two arenas reserve 64 MiB total. Raising
    // FRAME_COUNT to three would reserve 96 MiB and buy more CPU/GPU overlap
    // without changing the no-growth overflow policy: steady runtime would
    // still drop the bounded draw instead of growing.
    static const UINT64 UPLOAD_BUFFER_SIZE = 32 * 1024 * 1024;

    // Ordinary raster binding ABI lives in RenderRasterBindingContract.h so
    // runtime passes and the DX12 backend consume one shader/root-signature map.

    // CPU-side registries. These are not GPU resources by themselves; they are
    // lookup tables the backend uses to find cached GPU objects and descriptor
    // rows while translating engine draw calls into command-list operations.
    // Runtime allocation policy: PSO discovery is bounded. A cache miss may
    // compile a GPU object during warm-up, but inserting it never grows a heap
    // container and cap exhaustion fails with the missing pipeline shape.
    Dx12TextureOwner m_textureOwner;
    Dx12PipelineOwner m_pipelineOwner;
    Dx12GeometryOwner m_geometryOwner;
    // Cold-only registry and transactional shader-generation adoption. It
    // borrows concrete shader-domain owners and has no per-frame authority.
    Dx12ShaderDevelopment m_shaderDevelopment;

    // Currently bound render state. DX12 does not remember high-level engine
    // intent for us, so the backend tracks the desired state and emits concrete
    // command-list binds only when the state becomes dirty.
    Dx12RaytracingOwner m_raytracingOwner;
    uint32_t m_reflectionTextureHandle = 0; // Cold-published handle for the DXR reflection SRV.
    Dx12Diagnostics m_diagnostics;

    // The render device owns the core D3D12 lifetime: factory, device, queue,
    // swap chain, command allocators, command list, and frame fence. Every use
    // resolves through that owner so partial initialization, shutdown, or a
    // future device recreation cannot leave backend-side aliases dangling.
    Dx12RenderDevice m_renderDevice;
    // Lifetime: heaps and row allocators form one device epoch and outlive the
    // frame owner that borrows them for fence-proven reuse.
    Dx12DescriptorHeaps m_descriptorHeaps;
    Dx12FrameOwner m_frameOwner;
    // Lifetime: uncertain screenshot resources remain inside this owner until
    // shutdown proves both command-queue and present-queue completion.
    Dx12BackbufferCapture m_backbufferCapture;
    // Lifetime: graph pool borrows the concrete device, descriptor, frame,
    // texture, and pipeline owners declared above. It holds no raw heap pointer
    // and releases its native slots before those owners shut down.
    Dx12GraphTransientPool m_graphTransientPool;
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

    uint64_t m_recreationGeneration = 0;    // Advances only after complete resize publication.

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
    int m_width = 0;
    int m_height = 0;
    bool m_isVsyncEnabled = true;
    bool m_allowTearing = false;

    float m_clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float m_clearDepth = 1.0f;
    // --- Internal helpers ---
    SkullbonezCore::Core::SbResult WaitForGpu();
    SkullbonezCore::Core::SbResult EnsureCommandListOpen();
    SkullbonezCore::Core::SbResult SubmitClosedCommandList();
    void AssignDeferredResourceReleaseFence( UINT64 fenceValue );
    void ReleaseCompletedDeferredResources( bool releaseUnfenced );
    SkullbonezCore::Core::SbResult CreateDepthStencil( int w, int h );
    SkullbonezCore::Core::SbResult CreateDepthStencilResource( int w, int h, ID3D12Resource*& outResource );
    // Keeps cached texture-slot state from pointing at an SRV descriptor row
    // whose owning resource is being deleted or unregistered.
    void ClearBoundTextureSlotsForSrv( UINT srvIndex );
    void ReportDeviceLost( const char* context, HRESULT result ) const;
    void CheckDXRSupport();
    void AssertPlatformProfilerGpuStackClosed( const char* reason ) const;
    int SuspendPlatformProfilerGpuStackForSubmit( const char* reason );
    void RestorePlatformProfilerGpuStackAfterSubmit( int suspendedDepth );


  public:
    RenderBackendDX12();
    ~RenderBackendDX12() override
    {
        Shutdown();
    }

    SkullbonezCore::Core::SbResult Init( HWND hwnd, HDC hdc, int width, int height ) override;
    void Shutdown() override;
    SkullbonezCore::Core::SbResult Present() override;
    void SetVsyncEnabled( bool enabled ) override;
    bool IsVsyncEnabled() const override;
    SkullbonezCore::Core::SbResult Finish() override;
    SkullbonezCore::Core::SbResult FlushGPU() override;
    SkullbonezCore::Core::SbResult DrainForResourceRelease() override;
    SkullbonezCore::Core::SbResult Resize( int width, int height ) override;
    bool ShaderHotReloadEnabled() const override;
    SkullbonezCore::Core::SbResult ReloadShadersFromSource() override;

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

    SkullbonezCore::Core::SbResult
    CaptureBackbuffer( std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight ) override;
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
        capabilities.supportsGpuTimers = m_diagnostics.SupportsGpuTimers();
        capabilities.supportsDxrReflection = m_raytracingOwner.Supported();
        capabilities.supportsDebugLines = true;
        return capabilities;
    }

    void ResetFrameDrawCalls() override
    {
        m_diagnostics.ResetFrameDrawCalls();
    }
    void RecordDrawCall( const DrawCallRecord& record ) override
    {
        m_diagnostics.RecordDrawCall( record );
    }
    void RecordDrawCall()
    {
        RecordDrawCall( DrawCallRecord() );
    }
    int GetFrameDrawCallCount() const override
    {
        return m_diagnostics.FrameDrawCallCount();
    }
    void RecordVisibility( RenderVisibilityView view, int candidates, int submitted, int culled, int draws ) override
    {
        m_diagnostics.RecordVisibility( view, candidates, submitted, culled, draws );
    }
    RenderVisibilityStats GetFrameVisibilityStats() const override
    {
        return m_diagnostics.FrameVisibilityStats();
    }
    DrawCallTraceSnapshot GetFrameDrawCallTrace() const override
    {
        return m_diagnostics.FrameDrawCallTrace();
    }
    void PushDrawCallTraceScope( const char* fullPathOrLeaf, uint32_t hash ) override
    {
        m_diagnostics.PushDrawCallTraceScope( fullPathOrLeaf, hash );
    }
    void PopDrawCallTraceScope( uint32_t hash ) override
    {
        m_diagnostics.PopDrawCallTraceScope( hash );
    }

    SkullbonezCore::Core::SbResult InitDXR( uint64_t terrainVBVA,
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
    bool ExecuteGraphUavBarrier( const char* passName, const char* resourceName, ID3D12Resource* resource );

    // Reserve CPU-written upload memory for the current command stream.
    //
    // This is the safe public upload path. It probes the current frame upload
    // arena with the exact same size/alignment used for the final allocation.
    // Steady phases drop the requesting caller when the arena is full. Cold
    // phases may submit/wait/reset and retry. Callers should not bypass the
    // frame owner's reservation capability.
    D3D12_GPU_VIRTUAL_ADDRESS ReserveUpload( UINT64 size, UINT64 alignment, RenderUploadCategory category );
    uint8_t* GetUploadPtr( D3D12_GPU_VIRTUAL_ADDRESS addr );
    ID3D12Resource* GetUploadBuffer() const
    {
        return m_frameOwner.Uploads().Resource( m_frameOwner.AllocatorIndex() );
    }
    D3D12_CPU_DESCRIPTOR_HANDLE AllocateRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE AllocateDSV();
    UINT AllocateStaticSRV();
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRVStagingCpuHandle( UINT index );
};
} // namespace Rendering
} // namespace SkullbonezCore
