/*
File: SkullbonezSource/Rendering/RenderDiagnosticsTypes.h
Purpose:
  Declares value records shared by concrete render diagnostics owners and consumers.

Summary:
  Runtime and DX12 diagnostics exchange bounded value snapshots for capabilities,
  memory, visibility, and draw traces. The records own no backend authority.

Glossary:
  Draw-call trace: Per-frame list of named draw events used by overlays and
    validation diagnostics.
  GPU timer: Backend measurement of elapsed GPU time for a marked render region.
  Platform profiler marker: Named event emitted for external profiling tools.
  Render memory snapshot: Coarse counters that separate engine renderer caches
    from platform-reported adapter memory during stress runs.
  PSO cache counters: Monotonic per-device-epoch hit, miss, and pass-precompile
    totals paired with the current fixed cache entry count.
  Visibility counters: Per-view candidate, cull, submission, and draw totals
    accumulated between frame-diagnostics resets.
  DXGI (DirectX Graphics Infrastructure) adapter memory: Graphics-kernel
    budget and usage counters for the adapter that owns the active device.

Invariants:
  - Value records never create resources, record commands, or retain owners.
  - Capability flags describe the active backend lifetime; callers must not
    cache them across backend teardown and replacement.
  - Visibility snapshots describe only the current frame and never own a
    visible-index list or influence render decisions.
  - PSO hit/miss/precompile totals diagnose pipeline behavior only; they never
    choose state, resize the cache, or alter pass submission.

Related:
  - SkullbonezSource/Rendering/DrawCallTrace.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Core/Common.h"

#include <cstddef>
#include <cstdint>

#include "DrawCallTrace.h"

namespace SkullbonezCore
{
namespace Rendering
{

struct RenderCapabilities
{
    bool supportsBackbufferCapture = true;
    bool supportsGpuTimers = false;
    bool supportsDxrReflection = false;
    bool supportsDebugLines = false;
};

// Concept: upload attribution follows the owner that generated the bytes. The
// categories are stable diagnostic rows shared by the Memory tab and stress log.
enum class RenderUploadCategory : std::size_t
{
    Constants = 0,
    DynamicVertex,
    InstanceData,
    TextureRows,
    RetainedGeometry,
    Count
};

constexpr std::size_t RENDER_UPLOAD_CATEGORY_COUNT = static_cast<std::size_t>( RenderUploadCategory::Count );

struct RenderMemoryStats
{
    bool available = false;                                              // False when the backend is not initialized enough to answer.
    char backendName[32] = "unknown";                                    // Short renderer name for CSV/JSON diagnostics.
    uint64_t recreationGeneration = 0;                                   // Advances after a complete backend resize publication.
    bool adapterMemoryAvailable = false;                                 // True when DXGI adapter memory counters were sampled.
    uint64_t localBudgetBytes = 0;                                       // Adapter-local budget reported by DXGI.
    uint64_t localCurrentUsageBytes = 0;                                 // Adapter-local bytes currently charged to this process.
    uint64_t localCurrentReservationBytes = 0;                           // Adapter-local reservation bytes currently held by this process.
    uint64_t localAvailableForReservationBytes = 0;
    uint64_t nonLocalBudgetBytes = 0;                                    // Shared/system-memory budget reported by DXGI.
    uint64_t nonLocalCurrentUsageBytes = 0;                              // Non-local bytes currently charged to this process.
    uint64_t nonLocalCurrentReservationBytes = 0;                        // Non-local reservation bytes currently held by this process.
    uint64_t nonLocalAvailableForReservationBytes = 0;
    uint64_t uploadCapacityBytes = 0;                                    // Sum of persistent per-frame upload-buffer resources.
    uint64_t uploadUsedBytes = 0;                                        // Bytes used in the currently sampled upload arenas.
    uint64_t uploadPeakBytes = 0;                                        // Highest one-frame arena water mark for this run.
    uint64_t uploadCategoryUsedBytes[RENDER_UPLOAD_CATEGORY_COUNT] = {}; // Current bytes by upload owner.
    uint64_t uploadCategoryPeakBytes[RENDER_UPLOAD_CATEGORY_COUNT] = {}; // Highest one-frame category totals.
    uint64_t uploadFlushCount = 0;                                       // Cold-phase mid-frame drains since backend initialization.
    uint64_t uploadDropCount = 0;                                        // Steady-phase reservations rejected at their draw boundary.
    uint64_t timerReadbackBytes = 0;                                     // CPU-readable timer readback resource, when allocated.
    std::size_t textureRegistryCount = 0;
    std::size_t textureRegistryCapacity = 0;
    std::size_t dynamicVertexBufferCount = 0;
    std::size_t dynamicVertexBufferCapacity = 0;
    std::size_t instancedMeshCount = 0;
    std::size_t instancedMeshCapacity = 0;
    std::size_t psoCacheCount = 0;
    uint64_t psoCacheHitCount = 0;
    uint64_t psoCacheMissCount = 0;
    uint64_t precompiledPsoCount = 0;
    std::size_t graphTransientCount = 0;
    std::size_t graphTransientCapacity = 0;
    uint32_t rtvDescriptorsUsed = 0;
    uint32_t rtvDescriptorsCapacity = 0;
    uint32_t dsvDescriptorsUsed = 0;
    uint32_t dsvDescriptorsCapacity = 0;
    uint32_t srvStaticDescriptorsUsed = 0;
    uint32_t srvStaticDescriptorsCapacity = 0;
    uint32_t srvStaticDescriptorsHighWater = 0;
    uint32_t srvTransientDescriptorsUsedThisFrame = 0;
    uint32_t srvTransientDescriptorsCapacityPerFrame = 0;
    uint32_t srvTransientDescriptorsPeakThisRun = 0;
};

enum class RenderVisibilityView : uint8_t
{
    Main,
    Reflection,
    TerrainShadow,
    ObjectShadow,
    Count
};

struct RenderVisibilityViewStats
{
    int candidates = 0;
    int submitted = 0;
    int culled = 0;
    int draws = 0;
};

struct RenderVisibilityStats
{
    RenderVisibilityViewStats views[static_cast<int>( RenderVisibilityView::Count )] = {};
};

} // namespace Rendering
} // namespace SkullbonezCore
