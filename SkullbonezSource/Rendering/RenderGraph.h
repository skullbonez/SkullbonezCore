/*
File: SkullbonezSource/Rendering/RenderGraph.h
Purpose:
  Records render pass/resource intent, callback ordering, and transient texture
  lifetime plans for the renderer.

Summary:
  RenderGraph.h records render pass/resource intent, callback ordering, and
  transient texture lifetime plans for the renderer. As a public header, keep
  edits anchored on render submission and resource lifetime and on the
  glossary/invariants below.

Glossary:
  UAV (Unordered Access View): Descriptor row used when compute or raytracing
  shaders write textures or buffers.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Render graph handles are graph-local ids, not CPU pointers or GPU descriptor
    handles.
  - External resources are borrowed backend-owned resources; the graph records
    usage intent without taking lifetime ownership.
  - Transient resources are graph-declared texture requests. The graph compiler
    plans aliasing and descriptor lifetime; a backend executor is the only layer
    that may turn that plan into API objects.

Related:
  - SkullbonezSource/Rendering/RenderGraph.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/FatalError.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

namespace SkullbonezCore
{
namespace Rendering
{

inline constexpr uint32_t RENDER_GRAPH_ALL_SUBRESOURCES = 0xFFFFFFFFu;
inline constexpr size_t RENDER_GRAPH_MAX_RESOURCES = 24;
inline constexpr size_t RENDER_GRAPH_MAX_PASSES = 24;
inline constexpr size_t RENDER_GRAPH_MAX_PASS_RESOURCE_USES = 8;
inline constexpr size_t RENDER_GRAPH_MAX_SUBRESOURCE_STATES_PER_RESOURCE = 8;
inline constexpr size_t RENDER_GRAPH_MAX_TRANSITIONS = 96;
inline constexpr size_t RENDER_GRAPH_MAX_TRANSIENT_ALLOCATIONS = 16;

/* -- RenderGraph: first DX12 render-architecture contract
-----------------------------------------------------------------------------------------------

    What problem does a render graph solve?

    A frame is not one draw call. A frame is a sequence of passes:

    - draw the shadow map,
    - draw reflections,
    - draw the main scene color,
    - read scene color/depth for water, post effects, and UI,
    - finally present the back buffer.

    Each pass reads or writes resources. A resource can be a texture, depth
    buffer, back buffer, UAV target, or future transient render target. DirectX
    12 requires the engine to say exactly how each resource is being used before
    the GPU touches it. If a texture was just a render target and the next pass
    wants to sample it as a shader texture, the engine must insert a resource
    barrier between those uses.

    Without a shared declaration layer, resource intent gets scattered across
    the codebase. Each pass has to remember what state every texture was in
    before it starts. That is fragile. The render graph keeps the declarations
    together, while the DX12 backend remains the explicit owner of live
    transition and UAV barrier emission on this branch.

    This class is intentionally small. It can execute callback-owned pass
    bodies and plan graph-managed transient resource lifetimes. It gives the
    engine a concrete place to name:

    - render resources,
    - render passes,
    - whether each pass reads or writes a resource,
    - which transient resources may alias in one frame,
    - what DX12-style access the pass expects,
    - whether a reviewed pass body is called by the graph.

    The current renderer keeps DX12 transitions explicit in the backend.
    Selected pass bodies can use the same pass/resource callback model without
    transferring barrier derivation to RenderGraph.
----------------------------------------------------------------------------------------------------------------------------------------------------------*/

// A queue is a lane of GPU work.
//
// Graphics queue:
//   Traditional rendering: draw triangles, clear render targets, use depth
//   testing, and present to the screen.
//
// Compute queue:
//   General shader work that is not a triangle draw. Examples include mip
//   generation, GPU culling, particle simulation, and future post effects.
//
// Copy queue:
//   Dedicated resource copies, often used for moving texture/buffer data from
//   CPU upload memory into faster GPU-only memory.
//
// The first live graph will use Graphics only, but naming the queue now keeps
// the contract ready for later copy/compute work without changing pass
// declarations again.
enum class RenderGraphQueueType
{
    Graphics,
    Compute,
    Copy
};

// Execution ownership distinguishes executable frame work from the few
// callback-free rows that document external frame edges such as Present.
enum class RenderGraphPassExecutionOwner
{
    DeclarationOnly,
    Callback
};

// Callback execution can run as a dry validation pass or as the live command
// callback path. DryRun is deliberately side-effect free and exists so
// architecture tests can validate callback ordering and declaration coverage.
enum class RenderGraphCallbackExecutionMode
{
    DryRun,
    Execute
};

class RenderGraph;
struct RenderGraphPassDesc;

// Context passed to a callback-owned pass. It exposes graph vocabulary and the
// current pass description, not broad runtime state. Runtime-specific state must
// be held by the pass object that the callback invokes.
struct RenderGraphPassContext
{
    const RenderGraph* graph = nullptr;
    const RenderGraphPassDesc* pass = nullptr;
    uint32_t passIndex = 0;
    const char* debugLabel = nullptr;
    bool dryRun = false;
};

// Resource access is the plain-English form of a future DX12 resource state.
//
// A resource is the actual image/buffer memory. Access describes what one pass
// plans to do with that memory. The same texture might be written as a render
// target in one pass, then read as a PixelShaderResource in the next pass.
//
// DX12 requires explicit transitions between many of these uses. The graph
// records intent in engine terms, then the DX12 implementation can map that
// intent to D3D12_RESOURCE_STATES when it inserts barriers.
//
// Unknown is allowed only as a resource's initial state when the graph does not
// yet know what the backend-owned object is doing before the first pass. Pass
// reads and writes should use a concrete access value so future barrier output
// is meaningful.
enum class RenderGraphResourceAccess
{
    Unknown,
    RenderTarget,
    DepthRead,
    DepthWrite,
    ShaderResource,
    PixelShaderResource,
    NonPixelShaderResource,
    UnorderedAccess,
    CopySource,
    CopyDest,
    VertexAndNonPixelShaderResource,
    Present
};

enum class RenderGraphResourceKind
{
    Texture2D,
    Buffer
};

enum class RenderGraphResourceFormat
{
    Unknown,
    RGBA8,
    RGBA16F,
    Depth24Stencil8
};

// Descriptor ownership is graph vocabulary, not a DX12 descriptor handle. It
// names what view rows a graph-created resource needs so descriptor lifetime can
// follow the resource instead of the material/object binding tables.
struct RenderGraphDescriptorNeeds
{
    bool renderTarget = false;
    bool depthStencil = false;
    bool shaderResource = false;
    bool unorderedAccess = false;
};

// API-neutral description of a graph-declared transient resource. Width/height are
// concrete because aliasing is only safe when descriptor shape is compatible.
struct RenderGraphTransientResourceDesc
{
    RenderGraphResourceKind kind = RenderGraphResourceKind::Texture2D;
    RenderGraphResourceFormat format = RenderGraphResourceFormat::Unknown;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t mipLevels = 1;
    RenderGraphDescriptorNeeds descriptors;
};

// Small typed handle for graph resources.
//
// A handle is an index into the graph's resource table. It is deliberately not a
// raw pointer and not a descriptor handle. That distinction matters:
//
// - C++ pointer: address of an engine or DX12 object in CPU memory.
// - GPU handle: address-like token the GPU can follow in a descriptor heap.
// - RenderGraphResourceHandle: small engine ID used only to talk about a named
//   graph resource while building pass/resource declarations.
//
// Keeping graph handles separate from DX12 handles lets the graph stay API
// neutral and lets future validation catch invalid resource IDs early.
struct RenderGraphResourceHandle
{
    static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;

    uint32_t index = INVALID_INDEX;

    bool IsValid() const
    {
        return index != INVALID_INDEX;
    }
};

// Engine-level view of a backend materialized graph texture. Runtime passes can
// bind the texture through ordinary renderer handles and dimensions without
// learning native descriptor or resource ownership.
struct RenderGraphTextureBinding
{
    RenderGraphResourceHandle resource;
    uint32_t textureHandle = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool renderTarget = false;
    bool shaderResource = false;

    bool IsValid() const
    {
        return resource.IsValid() && textureHandle != 0;
    }
};

// Typed token for optional native resource identity. The graph transports the
// token to the backend executor but cannot dereference it or assume an API.
struct RenderGraphNativeResourceToken
{
    template <typename T> static RenderGraphNativeResourceToken From( T* resource ) noexcept
    {
        static_assert( sizeof( T* ) == sizeof( std::uintptr_t ) );
        return { std::bit_cast<std::uintptr_t>( resource ) };
    }

    template <typename T> T* As() const noexcept
    {
        static_assert( sizeof( T* ) == sizeof( std::uintptr_t ) );
        return std::bit_cast<T*>( value );
    }

    explicit operator bool() const
    {
        return value != 0;
    }

    std::uintptr_t value = 0;
};

// Typed view of the current swap-chain image and the backend's tracked access.
// The graph borrows this identity for one accumulated frame schedule; the
// backend remains the resource lifetime owner.
struct RenderGraphBackbufferBinding
{
    RenderGraphNativeResourceToken nativeResource;
    RenderGraphResourceAccess currentAccess = RenderGraphResourceAccess::Unknown;

    bool IsValid() const
    {
        return static_cast<bool>( nativeResource ) && currentAccess != RenderGraphResourceAccess::Unknown;
    }
};

// A named resource in the graph.
//
// For this first slice, resources are just names and "external" markers.
// External means the object is owned outside the graph, such as the swap-chain
// back buffer or an existing backend texture. Later graph-declared transient
// resources can use the same declaration shape but set external=false.
//
// Lifetime: nativeResource is a typed non-owning token used by the backend to
// emit compiled transitions. The graph must never dereference or release it.
struct RenderGraphResourceDesc
{
    const char* name = "UnnamedResource";
    bool external = true;
    RenderGraphResourceAccess initialAccess = RenderGraphResourceAccess::Unknown;
    RenderGraphNativeResourceToken nativeResource;
    RenderGraphTransientResourceDesc transient;
};

// A single declared use of a resource by one pass. For example: "WaterPass reads
// ReflectionColor as PixelShaderResource" or "ToneMap writes Backbuffer as
// RenderTarget." Later, these records become the input to barrier scheduling.
struct RenderGraphResourceUse
{
    RenderGraphResourceHandle resource;
    RenderGraphResourceAccess access = RenderGraphResourceAccess::Unknown;
    uint32_t subresource = RENDER_GRAPH_ALL_SUBRESOURCES;
};

struct RenderGraphResourceUseList
{
    bool empty() const
    {
        return count == 0;
    }

    size_t size() const
    {
        return count;
    }

    const RenderGraphResourceUse* begin() const
    {
        return uses.data();
    }

    const RenderGraphResourceUse* end() const
    {
        return uses.data() + count;
    }

    void clear()
    {
        count = 0;
    }

    void push_back( const RenderGraphResourceUse& use );

    std::array<RenderGraphResourceUse, RENDER_GRAPH_MAX_PASS_RESOURCE_USES> uses = {};
    size_t count = 0;
};

// A pass is one named phase of the frame.
//
// A pass declaration answers three questions:
//
// - What is this phase called? Example: "MainScene" or "WaterReflection".
// - Which resources does it read?
// - Which resources does it write?
//
// Callback fields are optional only for declared frame-edge bookkeeping such as
// Present. Every command-recording frame pass installs a callback.
struct RenderGraphPassDesc
{
    const char* name = "UnnamedPass";
    const char* debugLabel = "UnnamedPass";
    RenderGraphQueueType queue = RenderGraphQueueType::Graphics;
    RenderGraphPassExecutionOwner executionOwner = RenderGraphPassExecutionOwner::DeclarationOnly;
    bool callbackEnabled = true;
    RenderGraphResourceUseList reads;
    RenderGraphResourceUseList writes;
};

// A graph transition is the API-neutral version of a future DX12 resource
// barrier.
//
// In DX12 terms, a barrier says "before this pass uses the resource, transition
// it from state A to state B." This struct avoids D3D12_RESOURCE_STATES on
// purpose. The render graph should speak in engine access concepts; the DX12
// backend can translate those concepts into concrete D3D12 barrier flags later.
// nativeResource is copied from the declaration so the backend executor can
// bind the compiled edge to the exact physical resource.
struct RenderGraphTransitionDesc
{
    uint32_t passIndex = 0;
    RenderGraphResourceHandle resource;
    RenderGraphNativeResourceToken nativeResource;
    RenderGraphResourceAccess before = RenderGraphResourceAccess::Unknown;
    RenderGraphResourceAccess after = RenderGraphResourceAccess::Unknown;
    uint32_t subresource = RENDER_GRAPH_ALL_SUBRESOURCES;
};

struct RenderGraphResourceLifetimeDesc
{
    RenderGraphResourceHandle resource;
    uint32_t firstPass = 0;
    uint32_t lastPass = 0;
    bool used = false;
};

struct RenderGraphTransientAllocationDesc
{
    RenderGraphResourceHandle resource;
    uint32_t poolSlot = 0;
    uint32_t firstPass = 0;
    uint32_t lastPass = 0;
    uint32_t descriptorCount = 0;
    bool reused = false;
    bool releasedAtFrameEnd = false;
};

struct RenderGraphTransientAllocationDiagnostics
{
    size_t allocationCount = 0;
    size_t reuseCount = 0;
    size_t releaseCount = 0;
    size_t highWaterResources = 0;
    size_t highWaterDescriptors = 0;
};

struct RenderGraphTransientMaterializationStats
{
    size_t poolSize = 0;
    size_t createdThisCompile = 0;
    size_t reusedThisCompile = 0;
    size_t releasedAtFrameEnd = 0;
    size_t descriptorRowsOwned = 0;
    // Lane R: backend materialization can fail because the device rejects an
    // allocation. Keep fixed diagnostics here so frame reports can describe the
    // skipped graph resource without allocating another string.
    bool failed = false;
    unsigned int failureHresult = 0;
    char failureStage[64] = {};
    char failureResource[64] = {};
};

template <typename T, size_t Capacity> struct RenderGraphFixedList
{
    bool empty() const
    {
        return m_count == 0;
    }

    size_t size() const
    {
        return m_count;
    }

    constexpr size_t capacity() const
    {
        return Capacity;
    }

    void reserve( size_t requested )
    {
        if ( requested > Capacity )
        {
            SB_FATAL( "RenderGraph",
                      "Fixed-list reserve capacity exceeded. requested=%zu capacity=%zu",
                      requested,
                      Capacity );
        }
    }

    void clear()
    {
        for ( size_t index = 0; index < m_count; ++index )
        {
            m_values[index] = T();
        }
        m_count = 0;
    }

    void resize( size_t count )
    {
        if ( count > Capacity )
        {
            SB_FATAL( "RenderGraph", "Fixed-list resize capacity exceeded. count=%zu capacity=%zu", count, Capacity );
        }
        if ( count < m_count )
        {
            for ( size_t index = count; index < m_count; ++index )
            {
                m_values[index] = T();
            }
        }
        else
        {
            for ( size_t index = m_count; index < count; ++index )
            {
                m_values[index] = T();
            }
        }
        m_count = count;
    }

    void push_back( const T& value )
    {
        if ( m_count >= Capacity )
        {
            SB_FATAL( "RenderGraph", "Fixed-list push capacity exceeded. count=%zu capacity=%zu", m_count, Capacity );
        }
        m_values[m_count++] = value;
    }

    T& operator[]( size_t index )
    {
        return m_values[index];
    }

    const T& operator[]( size_t index ) const
    {
        return m_values[index];
    }

    T* begin()
    {
        return m_values.data();
    }

    T* end()
    {
        return m_values.data() + m_count;
    }

    const T* begin() const
    {
        return m_values.data();
    }

    const T* end() const
    {
        return m_values.data() + m_count;
    }

    T* data()
    {
        return m_values.data();
    }

    const T* data() const
    {
        return m_values.data();
    }

    std::array<T, Capacity> m_values = {};
    size_t m_count = 0;
};

// Result of the first simple graph compile step.
//
// The transition list is the live barrier authority consumed immediately before
// each callback pass. Compile output also carries transient allocation/lifetime
// facts; future queue ownership can extend the same declarations.
struct RenderGraphCompileResult
{
    void Clear();
    void ReserveForRuntimePassGraph();

    RenderGraphFixedList<RenderGraphTransitionDesc, RENDER_GRAPH_MAX_TRANSITIONS> transitions;
    RenderGraphFixedList<RenderGraphResourceLifetimeDesc, RENDER_GRAPH_MAX_RESOURCES> resourceLifetimes;
    RenderGraphFixedList<RenderGraphTransientAllocationDesc, RENDER_GRAPH_MAX_TRANSIENT_ALLOCATIONS>
        transientAllocations;
    RenderGraphTransientAllocationDiagnostics transientDiagnostics;
};

// Result of callback validation/execution. The graph still compiles barriers
// separately; these counts only describe command callback ownership.
struct RenderGraphCallbackExecutionResult
{
    size_t declarationOnlyPassCount = 0;
    size_t callbackPassCount = 0;
    size_t dryRunValidatedPassCount = 0;
    size_t executedPassCount = 0;
    size_t disabledCallbackPassCount = 0;
};

struct RenderGraphExecutionContractResult
{
    size_t callbackPassCount = 0;
    size_t declarationOnlyPassCount = 0;
    size_t expectedDeclarationOnlyPassCount = 1;
    bool declarationOnlyNameMatches = true;
    bool allCallbacksEnabled = true;

    bool IsValid() const
    {
        return declarationOnlyPassCount == expectedDeclarationOnlyPassCount && declarationOnlyNameMatches &&
               allCallbacksEnabled;
    }
};

class RenderGraph
{
  private:
    // Lifetime: this is the graph's sole erased callback record. payload is a
    // non-owning borrow of a concrete stack payload and is consumed synchronously
    // before the registration scope ends; typed public trampolines recover it.
    // Why: one private erased slot permits heterogeneous fixed-capacity passes
    // without allocation; no caller can register or recover an untyped payload.
    struct CallbackRecord
    {
        using Invoke = void ( * )( const RenderGraphPassContext&, const CallbackRecord& );
        Invoke invoke = nullptr;
        void* payload = nullptr;
    };

    void SetPassCallbackRecord( uint32_t passIndex, CallbackRecord record, bool enabled, const char* debugLabel );

  public:
    void Clear();
    void ReserveForRuntimePassGraph();

    RenderGraphResourceHandle AddExternalResource( const char* name,
                                                   RenderGraphResourceAccess initialAccess,
                                                   RenderGraphNativeResourceToken nativeResource = {} );
    RenderGraphResourceHandle
    AddTransientResource( const char* name,
                          const RenderGraphTransientResourceDesc& desc,
                          RenderGraphResourceAccess initialAccess = RenderGraphResourceAccess::Unknown );
    uint32_t AddPass( const char* name, RenderGraphQueueType queue = RenderGraphQueueType::Graphics );

    void AddRead( uint32_t passIndex,
                  RenderGraphResourceHandle resource,
                  RenderGraphResourceAccess access,
                  uint32_t subresource = RENDER_GRAPH_ALL_SUBRESOURCES );
    void AddWrite( uint32_t passIndex,
                   RenderGraphResourceHandle resource,
                   RenderGraphResourceAccess access,
                   uint32_t subresource = RENDER_GRAPH_ALL_SUBRESOURCES );
    template <auto Callback, typename Payload>
    void SetPassCallback( uint32_t passIndex, Payload& payload, bool enabled = true, const char* debugLabel = nullptr )
    {
        CallbackRecord record;
        record.invoke = []( const RenderGraphPassContext& context, const CallbackRecord& erased )
        { Callback( context, *static_cast<Payload*>( erased.payload ) ); };
        record.payload = std::addressof( payload );
        SetPassCallbackRecord( passIndex, record, enabled, debugLabel );
    }

    template <auto Callback>
    void SetPassCallback( uint32_t passIndex, bool enabled = true, const char* debugLabel = nullptr )
    {
        CallbackRecord record;
        record.invoke = []( const RenderGraphPassContext& context, const CallbackRecord& ) { Callback( context ); };
        SetPassCallbackRecord( passIndex, record, enabled, debugLabel );
    }

    const RenderGraphFixedList<RenderGraphResourceDesc, RENDER_GRAPH_MAX_RESOURCES>& Resources() const
    {
        return m_resources;
    }

    const RenderGraphFixedList<RenderGraphPassDesc, RENDER_GRAPH_MAX_PASSES>& Passes() const
    {
        return m_passes;
    }

    std::string DumpText() const;
    RenderGraphCompileResult Compile() const;
    void Compile( RenderGraphCompileResult& result ) const;
    // Executes callback-owned passes in declaration order. The range overload
    // is the production path for newly appended one-shot callback payloads.
    RenderGraphCallbackExecutionResult ExecuteCallbacks( RenderGraphCallbackExecutionMode mode ) const;
    RenderGraphCallbackExecutionResult
    ExecuteCallbacks( RenderGraphCallbackExecutionMode mode, uint32_t firstPass, uint32_t passCount ) const;
    // Passing a null/empty declaration name validates a capture-only graph with
    // no declaration-only edge; ordinary submitted frames pass "Present".
    RenderGraphExecutionContractResult ValidateFrameExecutionContract( const char* declarationOnlyPassName ) const;
    // Poisons erased one-frame callback borrows after execution and diagnostics.
    // Declarations remain available until Clear for frame-edge inspection.
    void ReleaseCallbackPayloadBorrows();

  private:
    const RenderGraphResourceDesc& CheckedResource( RenderGraphResourceHandle handle ) const;
    RenderGraphPassDesc& CheckedPass( uint32_t passIndex );
    void CheckedConcreteAccess( RenderGraphResourceAccess access ) const;

    RenderGraphFixedList<RenderGraphResourceDesc, RENDER_GRAPH_MAX_RESOURCES> m_resources;
    RenderGraphFixedList<RenderGraphPassDesc, RENDER_GRAPH_MAX_PASSES> m_passes;
    std::array<CallbackRecord, RENDER_GRAPH_MAX_PASSES> m_callbackRecords = {};
};

const char* ToString( RenderGraphQueueType queue );
const char* ToString( RenderGraphPassExecutionOwner owner );
const char* ToString( RenderGraphResourceAccess access );
const char* ToString( RenderGraphResourceKind kind );
const char* ToString( RenderGraphResourceFormat format );

} // namespace Rendering
} // namespace SkullbonezCore
