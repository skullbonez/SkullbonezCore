/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h
Purpose:
  Defines the allocation-free command-recording epoch and checked Map result
  used by the DX12 backend's recoverable failure paths.

Mental model:
  A command list is either logically open for recording or closed for
  submission. The state changes only after the matching DX12 operation
  succeeds. The first failure is sticky because later successes cannot prove
  that an earlier failed close, reset, or wait was harmless.

Glossary:
  Recording epoch: Interval in which one command list is known to be open or
    closed according to the last successful state-changing operation.
  GPU drain: Ordered close, submit, fence wait, and command-list reopen that
    must finish before a runtime owner destroys resources.
  Submitted work: Command stream passed to ExecuteCommandLists whose completion
    must be proven by a later fence before allocator or resource reuse.
  Sticky failure: First recoverable error retained until a new device
    initialization establishes a fresh command-list lifetime.
  Mapped pointer: CPU address returned by a successful resource Map operation.

Invariants:
  - Failed Close or Reset operations never change the logical epoch.
  - A failed wait never changes the epoch and prevents allocator/resource reuse.
  - Submitted work without a successful covering fence can never be treated as
    complete merely because the command list is closed.
  - A GPU drain exposes resource-mutation safety only after close, submission,
    wait, and reopen have committed in their legal order.
  - Only ResetForDevice clears a sticky failure.
  - The type owns no heap memory and invokes no callbacks.

Related:
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp
  - Agentic/Plans/TODO/dx12-failure-propagation.md
*/
#pragma once

#include "../../Core/SbResult.h"

#include <windows.h>


namespace SkullbonezCore
{
namespace Rendering
{

enum class Dx12CommandRecordingEpoch
{
    Closed,
    Open
};


class Dx12CommandRecordingState
{
  public:
    // A successfully initialized Dx12RenderDevice creates its command list and
    // closes it before handing ownership to the backend.
    void ResetForDevice()
    {
        m_epoch = Dx12CommandRecordingEpoch::Closed;
        m_firstFailure = Basics::SbResult::Success();
        m_allocatorResetCommitted = false;
    }

    Basics::SbResult CommitClose( HRESULT result, const char* operation = "command list Close" )
    {
        const char* operationName = operation ? operation : "command list Close";
        if ( HasFailure() )
        {
            return m_firstFailure;
        }
        if ( FAILED( result ) )
        {
            return RetainFailure( Basics::SbResult::Failure( "Rendering/DX12",
                                                             "%s failed (HRESULT 0x%08X)",
                                                             operationName,
                                                             static_cast<unsigned int>( result ) ) );
        }
        if ( m_epoch != Dx12CommandRecordingEpoch::Open )
        {
            return RetainFailure(
                Basics::SbResult::Failure( "Rendering/DX12",
                                           "%s succeeded while the command list was not logically open",
                                           operationName ) );
        }

        m_epoch = Dx12CommandRecordingEpoch::Closed;
        m_allocatorResetCommitted = false;
        return Basics::SbResult::Success();
    }

    Basics::SbResult CommitAllocatorReset( HRESULT result, const char* operation = "command allocator Reset" )
    {
        const char* operationName = operation ? operation : "command allocator Reset";
        if ( HasFailure() )
        {
            return m_firstFailure;
        }
        if ( m_epoch != Dx12CommandRecordingEpoch::Closed )
        {
            return RetainFailure( Basics::SbResult::Failure( "Rendering/DX12",
                                                             "%s attempted while the command list was logically open",
                                                             operationName ) );
        }
        if ( FAILED( result ) )
        {
            return RetainFailure( Basics::SbResult::Failure( "Rendering/DX12",
                                                             "%s failed (HRESULT 0x%08X)",
                                                             operationName,
                                                             static_cast<unsigned int>( result ) ) );
        }

        m_allocatorResetCommitted = true;
        return Basics::SbResult::Success();
    }

    Basics::SbResult CommitListReset( HRESULT result, const char* operation = "command list Reset" )
    {
        const char* operationName = operation ? operation : "command list Reset";
        if ( HasFailure() )
        {
            return m_firstFailure;
        }
        if ( m_epoch != Dx12CommandRecordingEpoch::Closed || !m_allocatorResetCommitted )
        {
            return RetainFailure( Basics::SbResult::Failure( "Rendering/DX12",
                                                             "%s attempted before a successful allocator Reset",
                                                             operationName ) );
        }
        if ( FAILED( result ) )
        {
            return RetainFailure( Basics::SbResult::Failure( "Rendering/DX12",
                                                             "%s failed (HRESULT 0x%08X)",
                                                             operationName,
                                                             static_cast<unsigned int>( result ) ) );
        }

        m_epoch = Dx12CommandRecordingEpoch::Open;
        m_allocatorResetCommitted = false;
        return Basics::SbResult::Success();
    }

    Basics::SbResult CommitWait( const Basics::SbResult& result )
    {
        return result.ok ? CurrentResult() : RetainFailure( result );
    }

    Basics::SbResult RetainFailure( const Basics::SbResult& result )
    {
        if ( !result.ok && !HasFailure() )
        {
            m_firstFailure = result;
        }
        return CurrentResult();
    }

    bool IsOpen() const
    {
        return m_epoch == Dx12CommandRecordingEpoch::Open;
    }

    bool IsClosed() const
    {
        return m_epoch == Dx12CommandRecordingEpoch::Closed;
    }

    bool CanRecord() const
    {
        return IsOpen() && !HasFailure();
    }

    bool HasFailure() const
    {
        return !m_firstFailure.ok;
    }

    const Basics::SbResult& CurrentResult() const
    {
        return m_firstFailure;
    }

  private:
    Dx12CommandRecordingEpoch m_epoch = Dx12CommandRecordingEpoch::Closed;
    Basics::SbResult m_firstFailure = Basics::SbResult::Success();
    bool m_allocatorResetCommitted = false;
};


enum class Dx12SubmittedWorkPhase
{
    Idle,
    SubmittedUnfenced,
    SubmittedFenced,
    CompletionUncertain
};


// Concept: tracks the completion proof for queue submissions independently of
// the command-list recording epoch. ExecuteCommandLists has no HRESULT, so a
// submission is conservatively live until a successful covering Signal and
// completed Wait (or a later CompletedValue observation) proves it finished.
class Dx12SubmittedWorkState
{
  public:
    void ResetForDevice()
    {
        m_phase = Dx12SubmittedWorkPhase::Idle;
        m_completionFence = 0;
    }

    void MarkSubmitted()
    {
        // A new queue submission is not covered by an older fence even if that
        // older marker is still pending, so discard the old proof immediately.
        m_phase = Dx12SubmittedWorkPhase::SubmittedUnfenced;
        m_completionFence = 0;
    }

    void CommitSignal( const Basics::SbResult& result, UINT64 fenceValue )
    {
        if ( !result.ok )
        {
            if ( HasSubmittedWork() )
            {
                // Preserve a previously known fence when a later drain Signal
                // fails; MarkSubmitted already clears it for genuinely new work.
                m_phase = Dx12SubmittedWorkPhase::CompletionUncertain;
            }
            return;
        }

        if ( !HasSubmittedWork() )
        {
            return;
        }
        if ( fenceValue == 0 )
        {
            m_phase = Dx12SubmittedWorkPhase::CompletionUncertain;
            m_completionFence = 0;
            return;
        }

        m_phase = Dx12SubmittedWorkPhase::SubmittedFenced;
        m_completionFence = fenceValue;
    }

    void CommitWait( const Basics::SbResult& result, UINT64 waitedFence )
    {
        if ( !result.ok )
        {
            if ( HasSubmittedWork() )
            {
                m_phase = Dx12SubmittedWorkPhase::CompletionUncertain;
            }
            return;
        }
        ObserveCompletedFence( waitedFence );
    }

    void ObserveCompletedFence( UINT64 completedFence )
    {
        if ( HasSubmittedWork() && m_completionFence != 0 && completedFence >= m_completionFence )
        {
            m_phase = Dx12SubmittedWorkPhase::Idle;
            m_completionFence = 0;
        }
    }

    bool HasSubmittedWork() const
    {
        return m_phase != Dx12SubmittedWorkPhase::Idle;
    }

    bool HasUnfencedOrUncertainWork() const
    {
        return m_phase == Dx12SubmittedWorkPhase::SubmittedUnfenced ||
               m_phase == Dx12SubmittedWorkPhase::CompletionUncertain;
    }

    bool CanReleaseWithoutFence() const
    {
        return m_phase == Dx12SubmittedWorkPhase::Idle;
    }

    bool HasKnownCompletionFence() const
    {
        return m_completionFence != 0;
    }

    UINT64 CompletionFence() const
    {
        return m_completionFence;
    }

    Dx12SubmittedWorkPhase Phase() const
    {
        return m_phase;
    }

  private:
    Dx12SubmittedWorkPhase m_phase = Dx12SubmittedWorkPhase::Idle;
    UINT64 m_completionFence = 0;
};


enum class Dx12GpuDrainStage
{
    CloseRequired,
    SubmitRequired,
    WaitRequired,
    ReopenRequired,
    MutationSafe
};


// Concept: Dx12GpuDrainProgress is the CPU-testable ordering proof used by
// FlushGPU. It does not perform DX12 calls; each transition is committed only
// after the backend has successfully completed the corresponding operation.
// A failed operation therefore leaves later stages inaccessible.
class Dx12GpuDrainProgress
{
  public:
    explicit Dx12GpuDrainProgress( bool commandListOpen )
        : m_stage( commandListOpen ? Dx12GpuDrainStage::CloseRequired : Dx12GpuDrainStage::WaitRequired )
    {
    }

    bool RequiresClose() const
    {
        return m_stage == Dx12GpuDrainStage::CloseRequired;
    }

    bool CommitClose()
    {
        if ( !RequiresClose() )
        {
            return false;
        }
        m_stage = Dx12GpuDrainStage::SubmitRequired;
        return true;
    }

    bool CanSubmit() const
    {
        return m_stage == Dx12GpuDrainStage::SubmitRequired;
    }

    bool CommitSubmission()
    {
        if ( !CanSubmit() )
        {
            return false;
        }
        m_stage = Dx12GpuDrainStage::WaitRequired;
        return true;
    }

    bool CanWait() const
    {
        return m_stage == Dx12GpuDrainStage::WaitRequired;
    }

    bool CommitWait()
    {
        if ( !CanWait() )
        {
            return false;
        }
        m_stage = Dx12GpuDrainStage::ReopenRequired;
        return true;
    }

    bool CanReopen() const
    {
        return m_stage == Dx12GpuDrainStage::ReopenRequired;
    }

    bool CommitReopen()
    {
        if ( !CanReopen() )
        {
            return false;
        }
        m_stage = Dx12GpuDrainStage::MutationSafe;
        return true;
    }

    bool IsMutationSafe() const
    {
        return m_stage == Dx12GpuDrainStage::MutationSafe;
    }

  private:
    Dx12GpuDrainStage m_stage;
};


struct Dx12MappedPointerResult
{
    Basics::SbResult result = Basics::SbResult::Success();
    void* pointer = nullptr;
};


inline Dx12MappedPointerResult
ValidateDx12MappedPointer( HRESULT mapResult, void* mappedPointer, const char* operation )
{
    Dx12MappedPointerResult checked;
    if ( FAILED( mapResult ) )
    {
        checked.result = Basics::SbResult::Failure( "Rendering/DX12",
                                                    "%s failed (HRESULT 0x%08X)",
                                                    operation ? operation : "resource Map",
                                                    static_cast<unsigned int>( mapResult ) );
        return checked;
    }
    if ( !mappedPointer )
    {
        checked.result = Basics::SbResult::Failure( "Rendering/DX12",
                                                    "%s succeeded without returning a mapped pointer",
                                                    operation ? operation : "resource Map" );
        return checked;
    }

    checked.pointer = mappedPointer;
    return checked;
}

} // namespace Rendering
} // namespace SkullbonezCore
