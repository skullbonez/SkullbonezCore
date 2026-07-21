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
  Dx12FrameOwner.h. Dx12PipelineOwner consumes each draw's declared raster
  value directly while retaining only command bindings and output state.

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


#include "../../Core/SceneCapacity.h"
#include "../IRenderCommandContext.h"
#include "../IRenderDeviceLifecycle.h"
#include "../RenderDiagnosticsTypes.h"
#include "../RenderResourceTypes.h"
#include "../RenderRaytracingTypes.h"
#include "../RenderRasterBindingContract.h"
#include "RenderBackendDX12.CommandRecordingState.h"
#include "RenderBackendDX12.PipelineState.h"
#include "Dx12CachedPsoStore.h"
#include "RenderGraphTransientDX12.h"
#include "RenderDeviceDX12.h"
#include "Dx12BackbufferCapture.h"
#include "Dx12ResourceBuilder.h"
#include "Dx12GraphTransientPool.h"
#include "Dx12TextureRegistry.h"
#include "MeshDX12.h"
#include "ShaderDX12.h"
#include "FramebufferDX12.h"
#include "Dx12DescriptorHeaps.h"
#include "Dx12Diagnostics.h"
#include "Dx12ShaderDevelopment.h"
#include "Dx12FrameOwner.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "Dx12ImGuiRendererOwner.h"
#endif
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
    // Stable owner-issued identity only. A COM address may be recycled after
    // root-signature recreation, so it cannot prove PSO recipe compatibility.
    // Bytecode hashes keep recompiled identical shaders from exploding the
    // cache during scene reload stress.
    std::uint64_t rootSignatureIdentity;
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
    void BindResourceOwners( Dx12RenderDevice& device, Dx12FrameOwner& frame, Dx12PipelineOwner& pipeline );
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
    uint32_t
    CreateTexture2D( const uint8_t* data, int width, int height, int channels, bool generateMips, bool linearFilter );
    void BindTexture( uint32_t handle, int slot );
    void DeleteTexture( Dx12TextureCommands& commands, uint32_t handle );
    void DeleteTexture( uint32_t handle );
    UINT RegisterSRV( UINT srvIndex, ID3D12Resource* resource );
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
    ID3D12Resource* ResolveResource( uint32_t handle ) const;
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
    Dx12RenderDevice* m_resourceDevice = nullptr;
    Dx12FrameOwner* m_resourceFrame = nullptr;
    Dx12PipelineOwner* m_resourcePipeline = nullptr;
};

// Concept: a pipeline is the complete draw recipe, not a collection of backend
// flags. This owner retains root-signature identity, command bindings, target
// state, PSO cache, and the dirty-state fast path as one coherent lifetime.
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
                      const DynamicVBDX12* dynamicVertexBuffer,
                      const RasterStateDesc& rasterState );
    bool PrecompileDraw( ID3D12Device* device,
                         VertexFormat12 format,
                         bool instanced,
                         const InstancedMeshDX12* instancedMesh,
                         const DynamicVBDX12* dynamicVertexBuffer,
                         const RasterStateDesc& declaredRasterState );
    void SetActiveShader( const ShaderDX12* shader );
    void ReleaseShaderPipelinesForReload();
    void RestoreShaderPipelinesAfterReload();
    const ShaderDX12* ActiveShader() const;
    void SetCurrentTargets( D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv );
    void SetRenderingToFBO( bool rendering, DXGI_FORMAT rtvFormat );
    void SetViewport( const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor );
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
    uint64_t CacheHitCount() const;
    uint64_t CacheMissCount() const;
    uint64_t PrecompiledPsoCount() const;

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
                                    const DynamicVBDX12* dynamicVertexBuffer,
                                    const RasterStateDesc& rasterState );
    PSOKey12 BuildPSOKey( VertexFormat12 format, bool instanced, const RasterStateDesc& rasterState ) const;
    static size_t BuildPSOHash( const PSOKey12& key, const DynamicVBDX12* dynamicVertexBuffer );
    ID3D12PipelineState* FindOrCreatePSO( ID3D12Device* device,
                                          const PSOKey12& key,
                                          size_t psoHash,
                                          VertexFormat12 format,
                                          bool instanced,
                                          const InstancedMeshDX12* instancedMesh,
                                          const DynamicVBDX12* dynamicVertexBuffer,
                                          const RasterStateDesc& rasterState,
                                          bool precompile );
    void ResetCommandState();

    static constexpr size_t CACHE_CAPACITY = 96;
    static constexpr size_t ROOT_SIGNATURE_SERIALIZED_CAPACITY = 4096;
    Dx12CachedPsoStore m_persistentPsoCache;
    std::array<CachedPSODX12, CACHE_CAPACITY> m_psoCache = {};
    size_t m_psoCacheCount = 0;
    uint64_t m_psoCacheHitCount = 0;
    uint64_t m_psoCacheMissCount = 0;
    uint64_t m_precompiledPsoCount = 0;
    // Canonical UnifiedRaster bytes reopen the P4 cache after a changed manifest
    // without retaining the temporary root-signature serialization blob.
    std::array<std::uint8_t, ROOT_SIGNATURE_SERIALIZED_CAPACITY> m_rootSignatureSerialized = {};
    size_t m_rootSignatureSerializedSize = 0;
    ID3D12RootSignature* m_rootSignature = nullptr;
    // Invariant: zero means no published signature. Successful creations take
    // one monotonically increasing identity from this concrete owner; Shutdown
    // clears the active identity but never rewinds the issuance sequence.
    std::uint64_t m_rootSignatureIdentity = 0;
    std::uint64_t m_nextRootSignatureIdentity = 1;
    const ShaderDX12* m_activeShader = nullptr;
    D3D12_VIEWPORT m_viewport = {};
    D3D12_RECT m_scissorRect = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentRTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentDSV = {};
    DXGI_FORMAT m_currentRTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool m_renderingToFBO = false;
    size_t m_lastPSOHash = 0;
    bool m_pipelineBindingDirty = true;
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
    Dx12GeometryOwner();
    uint32_t CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices );
    void UploadAndDrawDynamicVB( uint32_t handle,
                                 std::span<const float> packedVertices,
                                 D3D12_GPU_VIRTUAL_ADDRESS address,
                                 uint8_t* uploadPointer,
                                 ID3D12GraphicsCommandList* commandList,
                                 Dx12DrawGate& drawGate,
                                 Dx12Diagnostics& diagnostics,
                                 const RasterStateDesc& rasterState );
    bool PrecompileDynamicVBRasterState( uint32_t handle,
                                         Dx12DrawGate& drawGate,
                                         const RasterStateDesc& declaredRasterState );
    void DestroyDynamicVB( uint32_t handle );
    void AdoptGridLineShader( std::unique_ptr<ShaderDX12> shader );
    bool EnsureGridLinePipeline( ID3D12Device* device, Dx12PipelineOwner& pipeline, DXGI_FORMAT rtvFormat );
    void AdoptTransientTriangleShader( TransientTriangleStyle style, std::unique_ptr<ShaderDX12> shader );
    static const char* TransientShaderBaseName( TransientTriangleStyle style );
    bool HasTransientTriangleShader( TransientTriangleStyle style ) const;
    void InvalidateGridLinePipelinesForShaderReload();
    UINT GridLineConstantBytes() const;
    UINT TransientConstantBytes( TransientTriangleStyle style ) const;
    void DrawLinesColored( std::span<const float> packedVertices,
                           const Math::Transformation::Matrix4& viewProjection,
                           D3D12_GPU_VIRTUAL_ADDRESS vertexAddress,
                           uint8_t* uploadPointer,
                           ID3D12GraphicsCommandList* commandList,
                           Dx12PipelineOwner& pipeline,
                           Dx12DrawGate& drawGate,
                           Dx12Diagnostics& diagnostics,
                           const RasterStateDesc& rasterState );
    void DrawTransientColoredTriangles( std::span<const float> packedVertices,
                                        const Math::Transformation::Matrix4& viewProjection,
                                        TransientTriangleStyle style,
                                        int viewportWidth,
                                        int viewportHeight,
                                        D3D12_GPU_VIRTUAL_ADDRESS vertexAddress,
                                        uint8_t* uploadPointer,
                                        ID3D12GraphicsCommandList* commandList,
                                        Dx12DrawGate& drawGate,
                                        Dx12Diagnostics& diagnostics,
                                        const RasterStateDesc& rasterState );
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
    void BindResourceOwners( Dx12RenderDevice& device, Dx12FrameOwner& frame );
    uint32_t CreateInstancedMesh( const float* staticData,
                                  int staticVertCount,
                                  int staticFloatsPerVert,
                                  int maxInstances,
                                  int instanceFloats,
                                  int instanceStartAttrib,
                                  const int* instanceAttribSizes,
                                  int numInstanceAttribs,
                                  const int* staticAttribSizes = nullptr,
                                  int numStaticAttribs = 0 );
    void UploadInstanceData( uint32_t handle,
                             std::span<const float> packedInstances,
                             D3D12_GPU_VIRTUAL_ADDRESS address,
                             uint8_t* uploadPointer );
    void DrawInstancedMesh( const InstancedMeshDrawDesc& draw,
                            ID3D12GraphicsCommandList* commandList,
                            Dx12DrawGate& drawGate,
                            Dx12Diagnostics& diagnostics );
    void DestroyInstancedMesh( uint32_t handle );
    uint64_t StaticVertexBufferAddress( uint32_t handle ) const;
    int StaticVertexStride( uint32_t handle ) const;
    size_t DynamicCount() const;
    size_t DynamicCapacity() const;
    UINT64 DynamicUploadBytes( uint32_t handle, std::span<const float> packedVertices ) const;
    size_t InstancedCount() const;
    size_t InstancedCapacity() const;
    void Shutdown();

  private:
    static constexpr size_t MAX_DYNAMIC_VERTEX_BUFFERS = 32;
    static constexpr size_t MAX_GRID_LINE_PSOS = 4;
    static constexpr size_t TRANSIENT_TRIANGLE_STYLE_COUNT = 4;
    std::vector<DynamicVBDX12> m_dynamicVBs;
    std::vector<InstancedMeshDX12> m_instancedMeshes;
    std::unique_ptr<ShaderDX12> m_gridLineShader;
    std::array<GridLinePSODX12, MAX_GRID_LINE_PSOS> m_gridLinePSOs = {};
    size_t m_gridLinePSOCount = 0;
    std::array<std::unique_ptr<ShaderDX12>, TRANSIENT_TRIANGLE_STYLE_COUNT> m_transientTriangleShaders;
    Dx12RenderDevice* m_resourceDevice = nullptr;
    Dx12FrameOwner* m_resourceFrame = nullptr;
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
    Dx12RaytracingOwner( Dx12RenderDevice& device,
                         Dx12DescriptorHeaps& descriptors,
                         Dx12FrameOwner& frame,
                         Dx12TextureOwner& textures,
                         Dx12PipelineOwner& pipeline,
                         Dx12GeometryOwner& geometry );

    SkullbonezCore::Core::SbResult InitDXR( const RaytracingSetupDesc& setup );
    void DispatchReflectionRays( const WaterReflectionRayDesc& reflection );
    void BuildTLAS( std::span<const Math::Transformation::Matrix4> instanceTransforms );
    uint32_t GetReflectionUAVTexture() const;
    void ShutdownDXR();
    uint64_t GetInstancedMeshStaticVBVA( uint32_t handle ) const;
    int GetInstancedMeshStaticStride( uint32_t handle ) const;

    void ProbeCapability( ID3D12Device* device );
    bool Supported() const;
    bool Initialized() const;
    const SkullbonezCore::Core::SbResult& FeatureResult() const;

    Dx12RaytracingSetupOutcome BeginSetup( ID3D12Device* device,
                                           ID3D12GraphicsCommandList* commandList,
                                           Dx12DescriptorHeaps& descriptors,
                                           int renderWidth,
                                           int renderHeight,
                                           const RaytracingSetupDesc& setup );
    SkullbonezCore::Core::SbResult CompleteSetup( ID3D12Device* device, int maxInstances );
    void AbortSetup( const SkullbonezCore::Core::SbResult& failure );
    SkullbonezCore::Core::SbResult BuildScene( std::span<const Math::Transformation::Matrix4> instanceTransforms );
    Dx12RaytracingDispatchOutcome DispatchReflections( ID3D12Device* device,
                                                       Dx12DescriptorHeaps& descriptors,
                                                       const Dx12TextureOwner& textures,
                                                       const WaterReflectionRayDesc& reflection );
    UINT ReflectionSrvIndex() const;
    ID3D12Resource* ReflectionResource() const;
    uint32_t ReflectionTextureHandle() const;
    void PublishReflectionTextureHandle( uint32_t handle );
    uint32_t TakeReflectionTextureHandle();
    void Shutdown();

  private:
    SkullbonezCore::Core::SbResult CreateRootSignature( ID3D12Device* device );
    SkullbonezCore::Core::SbResult CreatePipeline();
    SkullbonezCore::Core::SbResult
    CreateReflectionTexture( ID3D12Device* device, Dx12DescriptorHeaps& descriptors, int width, int height );
    Dx12RenderDevice& m_device;
    Dx12DescriptorHeaps& m_descriptors;
    Dx12FrameOwner& m_frame;
    Dx12TextureOwner& m_textures;
    Dx12PipelineOwner& m_rasterPipeline;
    Dx12GeometryOwner& m_geometry;
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
    uint32_t m_reflectionTextureHandle = 0;
    int m_reflectionWidth = 0;
    int m_reflectionHeight = 0;
    ID3D12Resource* m_constantBuffer = nullptr;
    uint8_t* m_constantBufferMapped = nullptr;
    int m_maxInstances = 0;
    std::array<D3D12_RAYTRACING_INSTANCE_DESC, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS + 1> m_instances = {};
    BLAS m_terrainBlas;
    BLAS m_sphereBlas;
    TLAS m_tlas;
    SBT m_sbt;
};

// Lifetime: graph transient slots own their DX12 resource until the backend
// releases the graph pool. Descriptor rows come from the backend descriptor
// allocators and are reused with the slot; they must not be mixed into
// material/object texture ownership.
// Concept: RenderBackendDX12 composes concrete DX12 owners and publishes each
// one through its narrow runtime seam.
//
// Public seams use engine verbs for command recording and device lifecycle;
// resource and diagnostics work reaches concrete owners. Internally, DX12 requires the backend to make every
// hidden GPU concept explicit: descriptor table rows, command allocators,
// resource states, fences, upload memory, and compiled pipeline state. Texture
// and pipeline lifetime belong to the named owners above; this class sequences
// their work with the device/frame command stream.
class RenderBackendDX12 : public IRenderDeviceLifecycle, public IRenderCommandContext
{

  private:
    // Concept: the composition root retains concrete owners, while domain state
    // and helper operations remain inside those owners.
    Dx12TextureOwner m_textureOwner;
    Dx12PipelineOwner m_pipelineOwner;
    Dx12GeometryOwner m_geometryOwner;
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
    // Cold-only owners borrow the stable renderer owners above and retain the
    // complete transaction authority published to runtime.
    Dx12ShaderDevelopment m_shaderDevelopment;
    Dx12ResourceBuilder m_resourceBuilder;
    Dx12RaytracingOwner m_raytracingOwner;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Lifetime: this development renderer borrows the preceding concrete
    // device/descriptor/frame owners and is unbound before their shutdown.
    Dx12ImGuiRendererOwner m_imguiRenderer;
#endif
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
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    Dx12ImGuiRendererOwner& DevelopmentUiRenderer() noexcept
    {
        return m_imguiRenderer;
    }
#endif
    Dx12ShaderDevelopment& ShaderDevelopment() noexcept
    {
        return m_shaderDevelopment;
    }
    Dx12ResourceBuilder& ResourceBuilder() noexcept
    {
        return m_resourceBuilder;
    }
    Dx12TextureOwner& Textures() noexcept
    {
        return m_textureOwner;
    }
    Dx12GeometryOwner& Geometry() noexcept
    {
        return m_geometryOwner;
    }
    Dx12Diagnostics& Diagnostics() noexcept
    {
        return m_diagnostics;
    }
    Dx12RaytracingOwner& Raytracing() noexcept
    {
        return m_raytracingOwner;
    }
    Dx12BackbufferCapture& BackbufferCapture() noexcept
    {
        return m_backbufferCapture;
    }

    void SetViewport( int x, int y, int w, int h ) override;
    void Clear( const ClearTargetDesc& target ) override;

    void BindTexture( uint32_t handle, int slot ) override;
    RenderGraphTransientMaterializationStats
    MaterializeGraphTransientResources( const RenderGraph& graph, const RenderGraphCompileResult& compiled ) override;
    RenderGraphTextureBinding ResolveGraphTextureBinding( RenderGraphResourceHandle resource ) const override;
    RenderGraphNativeResourceToken ResolveGraphResourceToken( uint32_t textureHandle ) const override;
    RenderGraphBackbufferBinding ResolveGraphBackbufferBinding() const override;
    size_t ExecuteGraphTransitions( const RenderGraph& graph,
                                    const RenderGraphCompileResult& compiled,
                                    uint32_t passIndex ) override;
    void BeginGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName ) override;
    void EndGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName ) override;

    int GetWidth() const override;
    int GetHeight() const override;

    bool PrecompileDynamicVBRasterState( uint32_t handle, const PassRasterStateBucket& bucket ) override;
    void UploadAndDrawDynamicVB( uint32_t handle,
                                 std::span<const float> packedVertices,
                                 const PassRasterStateBucket& bucket ) override;

    void DrawLinesColored( std::span<const float> packedVertices,
                           const Math::Transformation::Matrix4& viewProjection,
                           const PassRasterStateBucket& bucket ) override;
    void DrawTransientColoredTriangles( std::span<const float> packedVertices,
                                        const Math::Transformation::Matrix4& viewProjection,
                                        TransientTriangleStyle style,
                                        const PassRasterStateBucket& bucket ) override;

    void UploadInstanceData( uint32_t handle, std::span<const float> packedInstances ) override;
    void DrawInstancedMesh( const InstancedMeshDrawDesc& draw ) override;
};
} // namespace Rendering
} // namespace SkullbonezCore
