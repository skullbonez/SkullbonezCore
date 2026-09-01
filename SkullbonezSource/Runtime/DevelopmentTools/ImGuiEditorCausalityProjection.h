// Derives a compact, bounded editor view from replay's published cause rows.
#pragma once

#include "../Planning/ReplayOverlayPackets.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

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

struct ImGuiEditorSelectedCause
{
    const RunReplayCauseTreeRow* selectedRow = nullptr;
    const RunReplayCauseTreeRow* selectedObjectRow = nullptr;
    const RunReplayCauseTreeRow* immediateCauseRow = nullptr;
    int selectedRowIndex = -1;
};

class ImGuiEditorRelatedCauseRows
{
  public:
    bool Append( const RunReplayCauseTreeRow& row ) noexcept
    {
        for ( const RunReplayCauseTreeRow* existing : Rows() )
        {
            if ( existing == &row )
            {
                return true;
            }
        }

        if ( m_count >= m_rows.size() )
        {
            return false;
        }

        m_rows[m_count++] = &row;
        return true;
    }

    std::span<const RunReplayCauseTreeRow* const> Rows() const noexcept
    {
        return std::span<const RunReplayCauseTreeRow* const>( m_rows.data(), m_count );
    }

  private:
    // Lifetime: these pointers borrow ReplayAuthoring rows for the current editor frame.
    std::array<const RunReplayCauseTreeRow*, IMGUI_CAUSALITY_RELEVANT_LINK_CAPACITY> m_rows = {};
    std::size_t m_count = 0u;
};

struct ImGuiEditorCausalityStatus
{
    std::size_t totalRowCount = 0u;
    ReplayFrameIndex replayTick = 0;
    ImGuiEditorCausalityState state = ImGuiEditorCausalityState::Empty;
    ImGuiEditorPredictionState predictionState = ImGuiEditorPredictionState::Disabled;
    bool hasReplayTick = false;
    bool compactScanTruncated = false;
};

struct ImGuiEditorCausalityProjection
{
    ImGuiEditorSelectedCause selected;
    ImGuiEditorRelatedCauseRows related;
    ImGuiEditorCausalityStatus status;
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

inline ImGuiEditorCausalityProjection
BuildImGuiEditorCausalityProjection( const ReplayOverlay::ReplayOverlayStateView& replay ) noexcept
{
    ImGuiEditorCausalityProjection projection;
    const RunReplayCauseTreeState& tree = replay.causeTree;
    const ReplayPresentationSelection& selection = replay.selection;
    projection.status.totalRowCount = tree.rows.size();

    const auto setTick = [&]( const auto* sample ) -> bool
    {
        if ( !sample )
        {
            return false;
        }

        projection.status.replayTick = sample->frameIndex;
        projection.status.hasReplayTick = true;
        return true;
    };

    if ( !setTick( replay.selectedPrediction ) && !setTick( selection.selectedSolver ) &&
         !setTick( selection.selectedPresentation ) && !setTick( selection.currentSolver ) &&
         !setTick( selection.currentPresentation ) && !setTick( selection.latestSolver ) )
    {
        (void)setTick( selection.latestPresentation );
    }

    if ( !projection.status.hasReplayTick && replay.prediction.timeline.sourceFrame != 0 )
    {
        projection.status.replayTick = replay.prediction.timeline.sourceFrame;
        projection.status.hasReplayTick = true;
    }

    if ( !replay.prediction.controls.enabled )
    {
        projection.status.predictionState = ImGuiEditorPredictionState::Disabled;
    }
    else if ( replay.prediction.topology.targetId.value == 0u )
    {
        projection.status.predictionState = ImGuiEditorPredictionState::AwaitingTarget;
    }
    else if ( replay.prediction.controls.building || !replay.prediction.timeline.complete )
    {
        projection.status.predictionState = ImGuiEditorPredictionState::Building;
    }
    else
    {
        projection.status.predictionState = ImGuiEditorPredictionState::Ready;
    }

    // Why: replay fails the full build closed when the pre-reserved row budget
    // cannot hold its estimate. Recompute only that constant-time size equation
    // so the compact UI can distinguish exhaustion from an ordinary empty tree.
    const bool usePredictionNodes = replay.prediction.controls.enabled && replay.prediction.topology.targetId.value != 0u &&
                                    replay.prediction.topology.targetId.value == replay.pathVisualizer.targetId.value;
    const std::size_t nodeCount = usePredictionNodes ? replay.prediction.topology.futureNodes.size() : std::size_t { 0u };
    const std::size_t contactCount = selection.currentSolver
                                         ? selection.currentSolver->worldSnapshot.physics.persistentContacts.size()
                                         : std::size_t { 0u };
    const std::size_t estimatedRows = 1u + ( usePredictionNodes ? nodeCount * 2u : nodeCount ) + contactCount * 3u;
    const bool capacityLimited = replay.pathVisualizer.hasTarget && tree.rows.empty() &&
                                 estimatedRows > tree.rows.capacity();

    if ( capacityLimited )
    {
        projection.status.state = ImGuiEditorCausalityState::CapacityLimited;
        return projection;
    }

    if ( tree.rows.empty() )
    {
        projection.status.state = tree.focusedId.value != 0u ? ImGuiEditorCausalityState::Stale
                                                             : ImGuiEditorCausalityState::Empty;
        return projection;
    }

    projection.selected.selectedRowIndex = tree.selectedRow >= 0 && tree.selectedRow < static_cast<int>( tree.rows.size() )
                                               ? tree.selectedRow
                                               : 0;
    projection.selected.selectedRow = &tree.rows[static_cast<std::size_t>( projection.selected.selectedRowIndex )];
    const std::size_t halfWindow = IMGUI_CAUSALITY_COMPACT_SCAN_CAPACITY / 2u;
    std::size_t scanBegin = projection.selected.selectedRowIndex > static_cast<int>( halfWindow )
                                ? static_cast<std::size_t>( projection.selected.selectedRowIndex ) - halfWindow
                                : 0u;
    const std::size_t scanEnd = (std::min)( tree.rows.size(), scanBegin + IMGUI_CAUSALITY_COMPACT_SCAN_CAPACITY );

    if ( scanEnd - scanBegin < IMGUI_CAUSALITY_COMPACT_SCAN_CAPACITY && scanEnd == tree.rows.size() &&
         scanEnd > IMGUI_CAUSALITY_COMPACT_SCAN_CAPACITY )
    {
        scanBegin = scanEnd - IMGUI_CAUSALITY_COMPACT_SCAN_CAPACITY;
    }

    projection.status.compactScanTruncated = scanBegin != 0u || scanEnd != tree.rows.size();

    const auto appendRelevant = [&]( const RunReplayCauseTreeRow& row )
    {
        if ( !projection.related.Append( row ) )
        {
            projection.status.compactScanTruncated = true;
        }
    };

    appendRelevant( *projection.selected.selectedRow );

    for ( std::size_t index = scanBegin; index < scanEnd; ++index )
    {
        const RunReplayCauseTreeRow& row = tree.rows[index];

        if ( !projection.selected.selectedObjectRow && row.kind == RunReplayCauseTreeRowKind::Body &&
             row.id.value == projection.selected.selectedRow->id.value )
        {
            projection.selected.selectedObjectRow = &row;
        }

        if ( !projection.selected.immediateCauseRow && projection.selected.selectedRow->parentId.value != 0u &&
             row.kind == RunReplayCauseTreeRowKind::Body && row.id.value == projection.selected.selectedRow->parentId.value )
        {
            projection.selected.immediateCauseRow = &row;
        }

        const bool isParent = projection.selected.selectedRow->parentId.value != 0u &&
                              row.id.value == projection.selected.selectedRow->parentId.value;
        const bool isChild = row.parentId.value != 0u && row.parentId.value == projection.selected.selectedRow->id.value;
        const bool isSameBodyDetail = row.id.value == projection.selected.selectedRow->id.value &&
                                      row.kind != RunReplayCauseTreeRowKind::Body;

        if ( isParent || isChild || isSameBodyDetail )
        {
            appendRelevant( row );
        }
    }

    if ( !projection.selected.selectedObjectRow && projection.selected.selectedRow->kind == RunReplayCauseTreeRowKind::Body )
    {
        projection.selected.selectedObjectRow = projection.selected.selectedRow;
    }

    if ( !projection.selected.selectedObjectRow )
    {
        projection.selected.selectedObjectRow = projection.selected.selectedRow;
    }

    const bool staleFocus = tree.focusedId.value != 0u && tree.selectedRow < 0;
    projection.status.state = staleFocus ? ImGuiEditorCausalityState::Stale
                                         : ( projection.status.compactScanTruncated ? ImGuiEditorCausalityState::Truncated
                                                                                    : ImGuiEditorCausalityState::Ready );
    return projection;
}
} // namespace SkullbonezCore::Runtime::DevelopmentTools
