/*
File: SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorCausalityProjection.h
Purpose:
  Derives a compact, bounded ImGui causality summary from replay's published rows.

Summary:
  The replay owner retains the complete preallocated cause tree. This value-only
  projection borrows those immutable rows for one editor frame, selects a local
  context, and publishes at most eight relevant links without rebuilding or
  copying the full tree.

Glossary:
  Context row: The replay row currently focused by the cause camera, or the root
    body row when no detail row is focused.
  Relevant link: A parent, selected row, direct child, or same-body detail near
    the context row.
  Scan window: A fixed 512-row neighborhood used to find compact links.

Invariants:
  - Returned pointers borrow ReplayAuthoring storage only for the current frame.
  - Compact projection scans at most 512 rows and retains at most eight links.
  - The complete tree is not copied; the separately dockable detail panel reads
    the original published rows through ImGuiListClipper.
  - Empty, stale, scan-truncated, and capacity-limited states remain distinct.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayAuthoring.h
  - SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp
*/
#pragma once

#include "../Planning/ReplayOverlayPackets.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore::Runtime::DevelopmentTools
{
inline constexpr std::size_t IMGUI_CAUSALITY_RELEVANT_LINK_CAPACITY = 8u;
inline constexpr std::size_t IMGUI_CAUSALITY_COMPACT_SCAN_CAPACITY = 512u;

enum class ImGuiEditorCausalityState : uint8_t
{
    Ready,
    Empty,
    Stale,
    Truncated,
    CapacityLimited
};

enum class ImGuiEditorPredictionState : uint8_t
{
    Disabled,
    AwaitingTarget,
    Building,
    Ready
};

struct ImGuiEditorCausalityContext
{
    const RunReplayCauseTreeRow* selectedRow = nullptr;
    const RunReplayCauseTreeRow* selectedObjectRow = nullptr;
    const RunReplayCauseTreeRow* immediateCauseRow = nullptr;
    const RunReplayCauseTreeRow* relevantLinks[IMGUI_CAUSALITY_RELEVANT_LINK_CAPACITY] = {};
    std::size_t relevantLinkCount = 0u;
    std::size_t totalRowCount = 0u;
    ReplayFrameIndex replayTick = 0;
    int selectedRowIndex = -1;
    ImGuiEditorCausalityState state = ImGuiEditorCausalityState::Empty;
    ImGuiEditorPredictionState predictionState = ImGuiEditorPredictionState::Disabled;
    bool hasReplayTick = false;
    bool compactScanTruncated = false;
};

inline const char* ImGuiEditorCausalityStateName( ImGuiEditorCausalityState state ) noexcept
{

    switch ( state )
    {
    case ImGuiEditorCausalityState::Ready:
        return "ready";
    case ImGuiEditorCausalityState::Empty:
        return "empty";
    case ImGuiEditorCausalityState::Stale:
        return "stale selection";
    case ImGuiEditorCausalityState::Truncated:
        return "compact list truncated";
    case ImGuiEditorCausalityState::CapacityLimited:
        return "cause capacity exceeded";
    default:
        return "unknown";
    }
}

inline const char* ImGuiEditorPredictionStateName( ImGuiEditorPredictionState state ) noexcept
{

    switch ( state )
    {
    case ImGuiEditorPredictionState::Disabled:
        return "disabled";
    case ImGuiEditorPredictionState::AwaitingTarget:
        return "awaiting target";
    case ImGuiEditorPredictionState::Building:
        return "building";
    case ImGuiEditorPredictionState::Ready:
        return "ready";
    default:
        return "unknown";
    }
}

inline const char* ImGuiEditorCauseRowKindName( RunReplayCauseTreeRowKind kind ) noexcept
{

    switch ( kind )
    {
    case RunReplayCauseTreeRowKind::Body:
        return "Body";
    case RunReplayCauseTreeRowKind::Manifold:
        return "Manifold";
    case RunReplayCauseTreeRowKind::SolverRow:
        return "Solver";
    case RunReplayCauseTreeRowKind::PredictionContact:
        return "Predicted contact";
    case RunReplayCauseTreeRowKind::PredictionMotion:
        return "Predicted motion";
    default:
        return "Unknown";
    }
}

inline ImGuiEditorCausalityContext
BuildImGuiEditorCausalityContext( const ReplayOverlay::ReplayOverlayStateView& replay ) noexcept
{
    ImGuiEditorCausalityContext context;
    const RunReplayCauseTreeState& tree = replay.causeTree;
    const ReplayPresentationSelection& selection = replay.selection;
    context.totalRowCount = tree.rows.size();

    const auto setTick = [&]( const auto* sample ) -> bool
    {

        if ( !sample )
        {
            return false;
        }

        context.replayTick = sample->frameIndex;
        context.hasReplayTick = true;
        return true;
    };

    if ( !setTick( replay.selectedPrediction ) && !setTick( selection.selectedSolver ) &&
         !setTick( selection.selectedPresentation ) && !setTick( selection.currentSolver ) &&
         !setTick( selection.currentPresentation ) && !setTick( selection.latestSolver ) )
    {
        (void)setTick( selection.latestPresentation );
    }

    if ( !context.hasReplayTick && replay.prediction.sourceFrame != 0 )
    {
        context.replayTick = replay.prediction.sourceFrame;
        context.hasReplayTick = true;
    }

    if ( !replay.prediction.enabled )
    {
        context.predictionState = ImGuiEditorPredictionState::Disabled;
    }
    else if ( replay.prediction.targetId.value == 0u )
    {
        context.predictionState = ImGuiEditorPredictionState::AwaitingTarget;
    }
    else if ( replay.prediction.building || !replay.prediction.complete )
    {
        context.predictionState = ImGuiEditorPredictionState::Building;
    }
    else
    {
        context.predictionState = ImGuiEditorPredictionState::Ready;
    }

    // Why: replay fails the full build closed when the pre-reserved row budget
    // cannot hold its estimate. Recompute only that constant-time size equation
    // so the compact UI can distinguish exhaustion from an ordinary empty tree.
    const bool usePredictionNodes = replay.prediction.enabled && replay.prediction.targetId.value != 0u &&
                                    replay.prediction.targetId.value == replay.pathVisualizer.targetId.value;
    const std::size_t nodeCount = usePredictionNodes ? replay.prediction.futureNodes.size() : std::size_t { 0u };
    const std::size_t contactCount = selection.currentSolver
                                         ? selection.currentSolver->worldSnapshot.physics.persistentContacts.size()
                                         : std::size_t { 0u };
    const std::size_t estimatedRows = 1u + ( usePredictionNodes ? nodeCount * 2u : nodeCount ) + contactCount * 3u;
    const bool capacityLimited = replay.pathVisualizer.hasTarget && tree.rows.empty() &&
                                 estimatedRows > tree.rows.capacity();

    if ( capacityLimited )
    {
        context.state = ImGuiEditorCausalityState::CapacityLimited;
        return context;
    }

    if ( tree.rows.empty() )
    {
        context.state = tree.focusedId.value != 0u ? ImGuiEditorCausalityState::Stale : ImGuiEditorCausalityState::Empty;
        return context;
    }

    context.selectedRowIndex = tree.selectedRow >= 0 && tree.selectedRow < static_cast<int>( tree.rows.size() )
                                   ? tree.selectedRow
                                   : 0;
    context.selectedRow = &tree.rows[static_cast<std::size_t>( context.selectedRowIndex )];
    const std::size_t halfWindow = IMGUI_CAUSALITY_COMPACT_SCAN_CAPACITY / 2u;
    std::size_t scanBegin = context.selectedRowIndex > static_cast<int>( halfWindow )
                                ? static_cast<std::size_t>( context.selectedRowIndex ) - halfWindow
                                : 0u;
    const std::size_t scanEnd = (std::min)( tree.rows.size(), scanBegin + IMGUI_CAUSALITY_COMPACT_SCAN_CAPACITY );

    if ( scanEnd - scanBegin < IMGUI_CAUSALITY_COMPACT_SCAN_CAPACITY && scanEnd == tree.rows.size() &&
         scanEnd > IMGUI_CAUSALITY_COMPACT_SCAN_CAPACITY )
    {
        scanBegin = scanEnd - IMGUI_CAUSALITY_COMPACT_SCAN_CAPACITY;
    }

    context.compactScanTruncated = scanBegin != 0u || scanEnd != tree.rows.size();

    const auto appendRelevant = [&]( const RunReplayCauseTreeRow& row )
    {

        for ( std::size_t index = 0u; index < context.relevantLinkCount; ++index )
        {

            if ( context.relevantLinks[index] == &row )
            {
                return;
            }
        }

        if ( context.relevantLinkCount < IMGUI_CAUSALITY_RELEVANT_LINK_CAPACITY )
        {
            context.relevantLinks[context.relevantLinkCount++] = &row;
        }
        else
        {
            context.compactScanTruncated = true;
        }
    };

    appendRelevant( *context.selectedRow );

    for ( std::size_t index = scanBegin; index < scanEnd; ++index )
    {
        const RunReplayCauseTreeRow& row = tree.rows[index];

        if ( !context.selectedObjectRow && row.kind == RunReplayCauseTreeRowKind::Body &&
             row.id.value == context.selectedRow->id.value )
        {
            context.selectedObjectRow = &row;
        }

        if ( !context.immediateCauseRow && context.selectedRow->parentId.value != 0u &&
             row.kind == RunReplayCauseTreeRowKind::Body && row.id.value == context.selectedRow->parentId.value )
        {
            context.immediateCauseRow = &row;
        }

        const bool isParent = context.selectedRow->parentId.value != 0u &&
                              row.id.value == context.selectedRow->parentId.value;
        const bool isChild = row.parentId.value != 0u && row.parentId.value == context.selectedRow->id.value;
        const bool isSameBodyDetail = row.id.value == context.selectedRow->id.value &&
                                      row.kind != RunReplayCauseTreeRowKind::Body;

        if ( isParent || isChild || isSameBodyDetail )
        {
            appendRelevant( row );
        }
    }

    if ( !context.selectedObjectRow && context.selectedRow->kind == RunReplayCauseTreeRowKind::Body )
    {
        context.selectedObjectRow = context.selectedRow;
    }

    if ( !context.selectedObjectRow )
    {
        context.selectedObjectRow = context.selectedRow;
    }

    const bool staleFocus = tree.focusedId.value != 0u && tree.selectedRow < 0;
    context.state = staleFocus ? ImGuiEditorCausalityState::Stale
                               : ( context.compactScanTruncated ? ImGuiEditorCausalityState::Truncated
                                                                : ImGuiEditorCausalityState::Ready );
    return context;
}
} // namespace SkullbonezCore::Runtime::DevelopmentTools
