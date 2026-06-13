#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Rendering
{

/* -- RenderGraph: first DX12 render-architecture contract -----------------------------------------------------------------------------------------------

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

    This first class is intentionally small. It does not yet record GPU commands
    or allocate transient textures. It gives the engine a concrete place to name:

    - render resources,
    - render passes,
    - whether each pass reads or writes a resource,
    - what DX12-style access the pass expects.

    The current renderer can keep drawing exactly as it does today while future
    slices move pass setup and barrier ownership into this graph.
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
struct RenderGraphResourceDesc
{
    std::string name;
    bool external = true;
    RenderGraphResourceAccess initialAccess = RenderGraphResourceAccess::Unknown;
};

// A single declared use of a resource by one pass. For example: "WaterPass reads
// ReflectionColor as PixelShaderResource" or "ToneMap writes Backbuffer as
// RenderTarget." Later, these records become the input to barrier scheduling.
struct RenderGraphResourceUse
{
    RenderGraphResourceHandle resource;
    RenderGraphResourceAccess access = RenderGraphResourceAccess::Unknown;
};

// A pass is one named phase of the frame.
//
// A pass declaration answers three questions:
//
// - What is this phase called? Example: "MainScene" or "WaterReflection".
// - Which resources does it read?
// - Which resources does it write?
//
// It does not yet store a callback. That is deliberate. The first architecture
// step is to make pass/resource intent explicit without moving draw code at the
// same time. Command recording will come after the current backend's pass
// boundaries are visible.
struct RenderGraphPassDesc
{
    std::string name;
    RenderGraphQueueType queue = RenderGraphQueueType::Graphics;
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
struct RenderGraphTransitionDesc
{
    uint32_t passIndex = 0;
    RenderGraphResourceHandle resource;
    RenderGraphResourceAccess before = RenderGraphResourceAccess::Unknown;
    RenderGraphResourceAccess after = RenderGraphResourceAccess::Unknown;
};

// Result of the first simple graph compile step.
//
// This is intentionally only a transition list for now. Later compile output can
// add transient texture allocation, last-writer diagnostics, queue ownership,
// and callback execution order without changing the pass/resource declarations.
struct RenderGraphCompileResult
{
    std::vector<RenderGraphTransitionDesc> transitions;
};

class RenderGraph
{
  public:
    void Clear();

    RenderGraphResourceHandle AddExternalResource( const char* name, RenderGraphResourceAccess initialAccess = RenderGraphResourceAccess::Unknown );
    uint32_t AddPass( const char* name, RenderGraphQueueType queue = RenderGraphQueueType::Graphics );

    void AddRead( uint32_t passIndex, RenderGraphResourceHandle resource, RenderGraphResourceAccess access );
    void AddWrite( uint32_t passIndex, RenderGraphResourceHandle resource, RenderGraphResourceAccess access );

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

  private:
    const RenderGraphResourceDesc& CheckedResource( RenderGraphResourceHandle handle ) const;
    RenderGraphPassDesc& CheckedPass( uint32_t passIndex );
    void CheckedConcreteAccess( RenderGraphResourceAccess access ) const;

    std::vector<RenderGraphResourceDesc> m_resources;
    std::vector<RenderGraphPassDesc> m_passes;
};

const char* ToString( RenderGraphQueueType queue );
const char* ToString( RenderGraphResourceAccess access );

} // namespace Rendering
} // namespace SkullbonezCore
