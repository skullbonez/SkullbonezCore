/*
File: SkullbonezSource/UI/UIRenderDiagnostics.h
Purpose:
  Defines renderer-independent diagnostic values displayed by UI surfaces.

Summary:
  Runtime/UI projects backend measurement snapshots into these bounded values.
  UI can then lay out memory, upload, descriptor, and visibility information
  without including Rendering headers or naming backend-owned types.

Glossary:
  Upload category: UI row identifying which render activity consumed transient
    upload bytes.
  Visibility view: One rendering viewpoint whose candidate/submitted/cull/draw
    counts are displayed by the profiler surface.
  High water: Largest observed usage retained for diagnostics.

Invariants:
  - Values contain measurements only; no backend owner, handle, or callback
    crosses into UI.
  - Runtime/UI maps every enum slot and field explicitly so backend additions
    require a deliberate presentation decision.
  - Array capacities are compile-time constants and never grow at runtime.

Related:
  - SkullbonezSource/Runtime/UI/RenderDiagnosticsProjection.h
  - SkullbonezSource/Rendering/RenderDiagnosticsTypes.h
  - SkullbonezSource/UI/UITabMemory.cpp
*/
#pragma once

#include <cstddef>
#include <cstdint>

namespace SkullbonezCore::UI
{
enum class UIRenderUploadCategory : std::size_t
{
    Constants = 0,
    DynamicVertex,
    InstanceData,
    TextureRows,
    RetainedGeometry,
    Count
};

constexpr std::size_t UI_RENDER_UPLOAD_CATEGORY_COUNT = static_cast<std::size_t>( UIRenderUploadCategory::Count );

struct UIRenderMemoryStats
{
    bool available = false;
    char backendName[32] = "unknown";
    uint64_t recreationGeneration = 0;
    bool adapterMemoryAvailable = false;
    uint64_t localBudgetBytes = 0;
    uint64_t localCurrentUsageBytes = 0;
    uint64_t localCurrentReservationBytes = 0;
    uint64_t localAvailableForReservationBytes = 0;
    uint64_t nonLocalBudgetBytes = 0;
    uint64_t nonLocalCurrentUsageBytes = 0;
    uint64_t nonLocalCurrentReservationBytes = 0;
    uint64_t nonLocalAvailableForReservationBytes = 0;
    uint64_t uploadCapacityBytes = 0;
    uint64_t uploadUsedBytes = 0;
    uint64_t uploadPeakBytes = 0;
    uint64_t uploadCategoryUsedBytes[UI_RENDER_UPLOAD_CATEGORY_COUNT] = {};
    uint64_t uploadCategoryPeakBytes[UI_RENDER_UPLOAD_CATEGORY_COUNT] = {};
    uint64_t uploadFlushCount = 0;
    uint64_t uploadDropCount = 0;
    uint64_t timerReadbackBytes = 0;
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

enum class UIRenderVisibilityView : uint8_t
{
    Main,
    Reflection,
    TerrainShadow,
    ObjectShadow,
    Count
};

struct UIRenderVisibilityViewStats
{
    int candidates = 0;
    int submitted = 0;
    int culled = 0;
    int draws = 0;
};

struct UIRenderVisibilityStats
{
    UIRenderVisibilityViewStats views[static_cast<int>( UIRenderVisibilityView::Count )] = {};
};
} // namespace SkullbonezCore::UI
