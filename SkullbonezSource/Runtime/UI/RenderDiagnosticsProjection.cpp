/*
File: SkullbonezSource/Runtime/UI/RenderDiagnosticsProjection.cpp
Purpose:
  Implements field-complete renderer-to-UI diagnostic projection.

Summary:
  Renderer owners keep measurement and accounting authority. This file copies
  their immutable snapshots into UI-owned values at the Runtime/UI composition
  boundary, preventing either package from adopting the other's types.

Glossary:
  Transient: Frame-scoped storage whose current and peak use are diagnostic
    values rather than durable ownership.

Invariants:
  - Enum counts must match before indexed category/view loops can compile.
  - Character data is copied into the fixed destination buffer with termination.
  - No renderer pointer or reference survives the return statement.

Related:
  - SkullbonezSource/Runtime/UI/RenderDiagnosticsProjection.h
  - SkullbonezSource/Rendering/RenderDiagnosticsTypes.h
  - Agentic/Reference/engine-glossary.md
*/
#include "RenderDiagnosticsProjection.h"

#include "../../Rendering/RenderDiagnosticsTypes.h"

#include <cstring>

namespace SkullbonezCore::Runtime
{
UI::UIRenderMemoryStats ProjectRenderMemoryDiagnostics( const Rendering::RenderMemoryStats& source )
{
    static_assert( UI::UI_RENDER_UPLOAD_CATEGORY_COUNT == Rendering::RENDER_UPLOAD_CATEGORY_COUNT,
                   "Render upload categories require an explicit UI projection update." );

    UI::UIRenderMemoryStats result;
    result.available = source.available;
    strcpy_s( result.backendName, source.backendName );
    result.recreationGeneration = source.recreationGeneration;
    result.adapterMemoryAvailable = source.adapterMemoryAvailable;
    result.localBudgetBytes = source.localBudgetBytes;
    result.localCurrentUsageBytes = source.localCurrentUsageBytes;
    result.localCurrentReservationBytes = source.localCurrentReservationBytes;
    result.localAvailableForReservationBytes = source.localAvailableForReservationBytes;
    result.nonLocalBudgetBytes = source.nonLocalBudgetBytes;
    result.nonLocalCurrentUsageBytes = source.nonLocalCurrentUsageBytes;
    result.nonLocalCurrentReservationBytes = source.nonLocalCurrentReservationBytes;
    result.nonLocalAvailableForReservationBytes = source.nonLocalAvailableForReservationBytes;
    result.uploadCapacityBytes = source.uploadCapacityBytes;
    result.uploadUsedBytes = source.uploadUsedBytes;
    result.uploadPeakBytes = source.uploadPeakBytes;

    for ( std::size_t index = 0; index < UI::UI_RENDER_UPLOAD_CATEGORY_COUNT; ++index )
    {
        result.uploadCategoryUsedBytes[index] = source.uploadCategoryUsedBytes[index];
        result.uploadCategoryPeakBytes[index] = source.uploadCategoryPeakBytes[index];
    }

    result.uploadFlushCount = source.uploadFlushCount;
    result.uploadDropCount = source.uploadDropCount;
    result.timerReadbackBytes = source.timerReadbackBytes;
    result.textureRegistryCount = source.textureRegistryCount;
    result.textureRegistryCapacity = source.textureRegistryCapacity;
    result.dynamicVertexBufferCount = source.dynamicVertexBufferCount;
    result.dynamicVertexBufferCapacity = source.dynamicVertexBufferCapacity;
    result.instancedMeshCount = source.instancedMeshCount;
    result.instancedMeshCapacity = source.instancedMeshCapacity;
    result.psoCacheCount = source.psoCacheCount;
    result.psoCacheHitCount = source.psoCacheHitCount;
    result.psoCacheMissCount = source.psoCacheMissCount;
    result.precompiledPsoCount = source.precompiledPsoCount;
    result.graphTransientCount = source.graphTransientCount;
    result.graphTransientCapacity = source.graphTransientCapacity;
    result.rtvDescriptorsUsed = source.rtvDescriptorsUsed;
    result.rtvDescriptorsCapacity = source.rtvDescriptorsCapacity;
    result.dsvDescriptorsUsed = source.dsvDescriptorsUsed;
    result.dsvDescriptorsCapacity = source.dsvDescriptorsCapacity;
    result.srvStaticDescriptorsUsed = source.srvStaticDescriptorsUsed;
    result.srvStaticDescriptorsCapacity = source.srvStaticDescriptorsCapacity;
    result.srvStaticDescriptorsHighWater = source.srvStaticDescriptorsHighWater;
    result.srvTransientDescriptorsUsedThisFrame = source.srvTransientDescriptorsUsedThisFrame;
    result.srvTransientDescriptorsCapacityPerFrame = source.srvTransientDescriptorsCapacityPerFrame;
    result.srvTransientDescriptorsPeakThisRun = source.srvTransientDescriptorsPeakThisRun;
    return result;
}

UI::UIRenderVisibilityStats ProjectRenderVisibilityDiagnostics( const Rendering::RenderVisibilityStats& source )
{
    static_assert( static_cast<int>( UI::UIRenderVisibilityView::Count ) ==
                       static_cast<int>( Rendering::RenderVisibilityView::Count ),
                   "Render visibility views require an explicit UI projection update." );

    UI::UIRenderVisibilityStats result;

    for ( int index = 0; index < static_cast<int>( UI::UIRenderVisibilityView::Count ); ++index )
    {
        const Rendering::RenderVisibilityViewStats& sourceView = source.views[index];
        UI::UIRenderVisibilityViewStats& resultView = result.views[index];
        resultView.candidates = sourceView.candidates;
        resultView.submitted = sourceView.submitted;
        resultView.culled = sourceView.culled;
        resultView.draws = sourceView.draws;
    }

    return result;
}
} // namespace SkullbonezCore::Runtime
