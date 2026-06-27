/*
File: SkullbonezSource/Rendering/RenderGraph.h
Purpose:
  Records render pass/resource intent and feeds DX12 graph-owned barrier
  diagnostics.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

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

Related:
  - SkullbonezSource/Rendering/RenderGraph.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Rendering
{

inline constexpr uint32_t RENDER_GRAPH_ALL_SUBRESOURCES = 0xFFFFFFFFu;

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

    Without a graph, barrier decisions get scattered across the codebase. Each
    pass has to remember what state every texture was in before it starts. That
    is fragile. The render graph changes the contract: every pass declares what
    it reads and writes, and a future compiler can derive the needed barriers.

    This class is intentionally small. It can execute callback-owned pass
    bodies, but it still does not allocate transient textures. It gives the
    engine a concrete place to name:

    - render resources,
    - render passes,
    - whether each pass reads or writes a resource,
    - what DX12-style access the pass expects,
    - whether a reviewed pass body is called by the graph.

    The current renderer keeps pass bodies in the existing runtime order while
    DX12 transition and UAV barriers route through graph-owned helper APIs.
    Selected pass bodies can now move into the same pass/resource callback
    model without changing the lower-level DX12 transition executor.
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

// Barrier policy names whether a pass is plain diagnostics or a reviewed
// handoff marker. Live transition emission now goes through the DX12 graph
// executor helpers. Callback execution ownership is tracked separately because
// a pass can have reviewed barrier declarations before its body is graph-owned.
enum class RenderGraphBarrierPolicy
{
    DiagnosticOnly,  // The graph documents intent; hand-written backend barriers still own execution.
    HandoffValidated // The pass/resource declaration is reviewed as a migration handoff marker, not executed by the
                     // graph.
};

// Execution ownership names whether a pass is only declared for diagnostics or
// whether the graph owns calling its command-recording body. Callback-owned
// passes still use the same read/write declarations as barrier-only passes.
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

using RenderGraphPassCallback = void ( * )( const RenderGraphPassContext& context, void* userData );

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
    PixelShaderResource,
    NonPixelShaderResource,
    UnorderedAccess,
    CopySource,
    CopyDest,
    VertexAndNonPixelShaderResource,
    Present
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

// A named resource in the graph.
//
// For this first slice, resources are just names and "external" markers.
// External means the object is owned outside the graph, such as the swap-chain
// back buffer or an existing backend texture. Later graph-owned transient
// resources can use the same declaration shape but set external=false.
//
// Lifetime: nativeResource is optional diagnostic identity only. It is a
// borrowed backend pointer used to match graph transitions against live DX12
// barrier logs; the graph must never dereference, retain ownership of, or
// release that object.
struct RenderGraphResourceDesc
{
    std::string name;
    bool external = true;
    RenderGraphResourceAccess initialAccess = RenderGraphResourceAccess::Unknown;
    const void* nativeResource = nullptr;
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

// A pass is one named phase of the frame.
//
// A pass declaration answers three questions:
//
// - What is this phase called? Example: "MainScene" or "WaterReflection".
// - Which resources does it read?
// - Which resources does it write?
//
// Callback fields are optional. Declaration-only passes still serve diagnostics
// and barrier compilation; callback-owned passes use the same read/write records
// before the graph invokes their command-recording body.
struct RenderGraphPassDesc
{
    std::string name;
    std::string debugLabel;
    RenderGraphQueueType queue = RenderGraphQueueType::Graphics;
    RenderGraphBarrierPolicy barrierPolicy = RenderGraphBarrierPolicy::DiagnosticOnly;
    RenderGraphPassExecutionOwner executionOwner = RenderGraphPassExecutionOwner::DeclarationOnly;
    RenderGraphPassCallback callback = nullptr;
    void* callbackUserData = nullptr;
    bool callbackEnabled = true;
    std::vector<RenderGraphResourceUse> reads;
    std::vector<RenderGraphResourceUse> writes;
};

// A graph transition is the API-neutral version of a future DX12 resource
// barrier.
//
// In DX12 terms, a barrier says "before this pass uses the resource, transition
// it from state A to state B." This struct avoids D3D12_RESOURCE_STATES on
// purpose. The render graph should speak in engine access concepts; the DX12
// backend can translate those concepts into concrete D3D12 barrier flags later.
// nativeResource is copied from the resource declaration only so diagnostics can
// prefer exact pointer identity over name-only matching.
struct RenderGraphTransitionDesc
{
    uint32_t passIndex = 0;
    RenderGraphResourceHandle resource;
    const void* nativeResource = nullptr;
    RenderGraphResourceAccess before = RenderGraphResourceAccess::Unknown;
    RenderGraphResourceAccess after = RenderGraphResourceAccess::Unknown;
    uint32_t subresource = RENDER_GRAPH_ALL_SUBRESOURCES;
};

// Result of the first simple graph compile step.
//
// This is intentionally only a transition list for now. The DX12 graph executor
// can translate these records into barrier candidates, while direct live helper
// calls emit production barriers for current pass code. Later compile output can
// add transient texture allocation, queue ownership, and callback execution
// order without changing the pass/resource declarations.
struct RenderGraphCompileResult
{
    std::vector<RenderGraphTransitionDesc> transitions;
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

class RenderGraph
{
  public:
    void Clear();

    RenderGraphResourceHandle AddExternalResource( const char* name,
                                                   RenderGraphResourceAccess initialAccess,
                                                   const void* nativeResource = nullptr );
    uint32_t AddPass( const char* name,
                      RenderGraphQueueType queue = RenderGraphQueueType::Graphics,
                      RenderGraphBarrierPolicy barrierPolicy = RenderGraphBarrierPolicy::DiagnosticOnly );

    void AddRead( uint32_t passIndex,
                  RenderGraphResourceHandle resource,
                  RenderGraphResourceAccess access,
                  uint32_t subresource = RENDER_GRAPH_ALL_SUBRESOURCES );
    void AddWrite( uint32_t passIndex,
                   RenderGraphResourceHandle resource,
                   RenderGraphResourceAccess access,
                   uint32_t subresource = RENDER_GRAPH_ALL_SUBRESOURCES );
    void SetPassCallback( uint32_t passIndex,
                          RenderGraphPassCallback callback,
                          void* userData = nullptr,
                          bool enabled = true,
                          const char* debugLabel = nullptr );

    const std::vector<RenderGraphResourceDesc>& Resources() const
    {
        return m_resources;
    }

    const std::vector<RenderGraphPassDesc>& Passes() const
    {
        return m_passes;
    }

    std::string DumpText() const;
    RenderGraphCompileResult Compile() const;
    RenderGraphCallbackExecutionResult ExecuteCallbacks( RenderGraphCallbackExecutionMode mode ) const;

  private:
    const RenderGraphResourceDesc& CheckedResource( RenderGraphResourceHandle handle ) const;
    RenderGraphPassDesc& CheckedPass( uint32_t passIndex );
    void CheckedConcreteAccess( RenderGraphResourceAccess access ) const;

    std::vector<RenderGraphResourceDesc> m_resources;
    std::vector<RenderGraphPassDesc> m_passes;
};

const char* ToString( RenderGraphQueueType queue );
const char* ToString( RenderGraphBarrierPolicy policy );
const char* ToString( RenderGraphPassExecutionOwner owner );
const char* ToString( RenderGraphResourceAccess access );

} // namespace Rendering
} // namespace SkullbonezCore
