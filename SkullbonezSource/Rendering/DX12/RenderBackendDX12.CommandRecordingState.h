/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h
Purpose:
  Defines the allocation-free command-recording epoch and checked Map result
  used by the DX12 backend's recoverable failure paths.

Summary:
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
  Recreation transaction: Staged replacement of swap-chain resources whose
    public generation advances only after every candidate exists.
  Fault injection: Debug-only synthetic failure used to prove that queue work
    stops before the first unsafe submission.
  Platform profiler GPU stack: Allocation-free nesting state mirrored by PIX
    ranges while a command list is open.

Invariants:
  - Failed Close or Reset operations never change the logical epoch.
  - A failed wait never changes the epoch and prevents allocator/resource reuse.
  - Submitted work without a successful covering fence can never be treated as
    complete merely because the command list is closed.
  - Only confirmed device removal may abandon submitted-work tracking without
    a covering fence, and only for terminal resource release.
  - A GPU drain exposes resource-mutation safety only after close, submission,
    wait, and reopen have committed in their legal order.
  - Only ResetForDevice clears a sticky failure.
  - Device loss remains sticky across command epochs until ResetForDevice.
  - Recreation failure never advances the published resource generation.
  - Suspending platform-profiler ranges for submission preserves their nesting
    depth so the replacement command list can restore the same stack.
  - The type owns no heap memory and invokes no callbacks.

Related:
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp
  - Agentic/Reports/dx12_failure_inventory_20260710.md
*/
#pragma once

#include "../../Core/SbResult.h"

#include "../../Core/PlatformWin32.h"

#include <cstdint>
#include <cstring>


namespace SkullbonezCore
{
namespace Rendering
{

enum class Dx12CommandRecordingEpoch
{
    Closed,
    Open
};


// Concept: command-list submission temporarily closes platform GPU ranges.
//
// PIX ranges cannot span command lists, but engine profiler scopes can. This
// value tracks only the nesting proof: the backend retains marker names in its
// fixed array, ends the ranges before submission, then restores the same depth
// on the replacement list. Keeping the bookkeeping independent makes failure-
// path unwinding testable without a device or PIX runtime.
class Dx12PlatformProfilerGpuStackState
{
  public:
    void Reset()
    {
        m_depth = 0;
    }

    bool CommitBegin( int capacity )
    {

        if ( capacity <= 0 || m_depth >= capacity )
        {
            return false;
        }

        ++m_depth;
        return true;
    }

    bool CommitEnd()
    {

        if ( m_depth <= 0 )
        {
            return false;
        }

        --m_depth;
        return true;
    }

    int SuspendForSubmit()
    {
        const int suspendedDepth = m_depth;
        m_depth = 0;
        return suspendedDepth;
    }

    bool RestoreAfterSubmit( int suspendedDepth, int capacity )
    {

        if ( suspendedDepth < 0 || suspendedDepth > capacity )
        {
            return false;
        }

        m_depth = suspendedDepth;
        return true;
    }

    int Depth() const
    {
        return m_depth;
    }

  private:
    int m_depth = 0;
};


class Dx12CommandRecordingState
{
  public:

    // A successfully initialized Dx12RenderDevice creates its command list and
    // closes it before handing ownership to the backend.
    void ResetForDevice()
    {
        m_epoch = Dx12CommandRecordingEpoch::Closed;
        m_firstFailure = SkullbonezCore::Core::SbResult::Success();
        m_allocatorResetCommitted = false;
    }

    SkullbonezCore::Core::SbResult CommitClose( HRESULT result, const char* operation = "command list Close" )
    {
        const char* operationName = operation ? operation : "command list Close";

        if ( HasFailure() )
        {
            return m_firstFailure;
        }

        if ( FAILED( result ) )
        {
            return RetainFailure( SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12", "%s failed (HRESULT 0x%08X)",
                                                                           operationName,
                                                                           static_cast<unsigned int>( result ) ) );
        }

        if ( m_epoch != Dx12CommandRecordingEpoch::Open )
        {
            return RetainFailure( SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                                           "%s succeeded while the command list was not logically open",
                                                                           operationName ) );
        }

        m_epoch = Dx12CommandRecordingEpoch::Closed;
        m_allocatorResetCommitted = false;
        return SkullbonezCore::Core::SbResult::Success();
    }

    SkullbonezCore::Core::SbResult CommitAllocatorReset( HRESULT result, const char* operation = "command allocator Reset" )
    {
        const char* operationName = operation ? operation : "command allocator Reset";

        if ( HasFailure() )
        {
            return m_firstFailure;
        }

        if ( m_epoch != Dx12CommandRecordingEpoch::Closed )
        {
            return RetainFailure( SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                                           "%s attempted while the command list was logically open",
                                                                           operationName ) );
        }

        if ( FAILED( result ) )
        {
            return RetainFailure( SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12", "%s failed (HRESULT 0x%08X)",
                                                                           operationName,
                                                                           static_cast<unsigned int>( result ) ) );
        }

        m_allocatorResetCommitted = true;
        return SkullbonezCore::Core::SbResult::Success();
    }

    SkullbonezCore::Core::SbResult CommitListReset( HRESULT result, const char* operation = "command list Reset" )
    {
        const char* operationName = operation ? operation : "command list Reset";

        if ( HasFailure() )
        {
            return m_firstFailure;
        }

        if ( m_epoch != Dx12CommandRecordingEpoch::Closed || !m_allocatorResetCommitted )
        {
            return RetainFailure( SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                                           "%s attempted before a successful allocator Reset",
                                                                           operationName ) );
        }

        if ( FAILED( result ) )
        {
            return RetainFailure( SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12", "%s failed (HRESULT 0x%08X)",
                                                                           operationName,
                                                                           static_cast<unsigned int>( result ) ) );
        }

        m_epoch = Dx12CommandRecordingEpoch::Open;
        m_allocatorResetCommitted = false;
        return SkullbonezCore::Core::SbResult::Success();
    }

    SkullbonezCore::Core::SbResult CommitWait( const SkullbonezCore::Core::SbResult& result )
    {
        return result.ok ? CurrentResult() : RetainFailure( result );
    }

    SkullbonezCore::Core::SbResult RetainFailure( const SkullbonezCore::Core::SbResult& result )
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

    const SkullbonezCore::Core::SbResult& CurrentResult() const
    {
        return m_firstFailure;
    }

  private:
    Dx12CommandRecordingEpoch m_epoch = Dx12CommandRecordingEpoch::Closed;
    SkullbonezCore::Core::SbResult m_firstFailure = SkullbonezCore::Core::SbResult::Success();
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

    void AbandonForRemovedDevice()
    {

        // Lifetime: device removal cancels the device lifetime itself, so no
        // command from this queue can execute against resources after terminal
        // COM teardown. This is not a reusable completion proof.
        m_phase = Dx12SubmittedWorkPhase::Idle;
        m_completionFence = 0;
    }

    void CommitSignal( const SkullbonezCore::Core::SbResult& result, UINT64 fenceValue )
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

    void CommitWait( const SkullbonezCore::Core::SbResult& result, UINT64 waitedFence )
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
    SkullbonezCore::Core::SbResult result = SkullbonezCore::Core::SbResult::Success();
    uint8_t* bytes = nullptr;
};


inline Dx12MappedPointerResult ValidateDx12MappedPointer( HRESULT mapResult, void* mappedPointer, const char* operation )
{

    // Why: ID3D12Resource::Map is a native void-pointer ABI. Validate it at
    // this immediate seam and publish only typed mapped bytes to owners.
    Dx12MappedPointerResult checked;

    if ( FAILED( mapResult ) )
    {
        checked.result = SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12", "%s failed (HRESULT 0x%08X)",
                                                                  operation ? operation : "resource Map",
                                                                  static_cast<unsigned int>( mapResult ) );
        return checked;
    }

    if ( !mappedPointer )
    {
        checked.result = SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                                  "%s succeeded without returning a mapped pointer",
                                                                  operation ? operation : "resource Map" );
        return checked;
    }

    checked.bytes = static_cast<uint8_t*>( mappedPointer );
    return checked;
}


// Concept: device health is separate from one command-list epoch. A removed
// device invalidates every resource owner and must remain sticky until a full
// backend initialization establishes a new device lifetime.
class Dx12DeviceHealthState
{
  public:
    void ResetForDevice()
    {
        m_lost = false;
        m_firstFailure = SkullbonezCore::Core::SbResult::Success();
    }

    SkullbonezCore::Core::SbResult RetainDeviceLoss( const char* operation, HRESULT result )
    {

        if ( !m_lost )
        {
            m_lost = true;
            m_firstFailure = SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                                      "DX12 device lost during %s (HRESULT 0x%08X)",
                                                                      operation ? operation : "unknown operation",
                                                                      static_cast<unsigned int>( result ) );
        }

        return m_firstFailure;
    }

    bool CanIssueDeviceWork() const
    {
        return !m_lost;
    }

    bool IsLost() const
    {
        return m_lost;
    }

    const SkullbonezCore::Core::SbResult& CurrentResult() const
    {
        return m_firstFailure;
    }

  private:
    SkullbonezCore::Core::SbResult m_firstFailure = SkullbonezCore::Core::SbResult::Success();
    bool m_lost = false;
};


enum class Dx12RecreationStage
{
    Idle,
    CandidateReady,
    OldReferencesReleased,
    SwapChainResized,
    BackBuffersReady,
    Published,
    Failed
};


// Concept: a recreation transaction separates native calls from publication.
// The backend may have to release DXGI back-buffer references before resize,
// but width/height/current-target state advances only after every replacement
// resource exists.
class Dx12RecreationTransaction
{
  public:
    void Begin( uint64_t publishedGeneration )
    {
        m_stage = Dx12RecreationStage::Idle;
        m_publishedGeneration = publishedGeneration;
        m_firstFailure = SkullbonezCore::Core::SbResult::Success();
    }

    bool CommitCandidateReady()
    {
        return Advance( Dx12RecreationStage::Idle, Dx12RecreationStage::CandidateReady );
    }

    bool CommitOldReferencesReleased()
    {
        return Advance( Dx12RecreationStage::CandidateReady, Dx12RecreationStage::OldReferencesReleased );
    }

    bool CommitSwapChainResized()
    {
        return Advance( Dx12RecreationStage::OldReferencesReleased, Dx12RecreationStage::SwapChainResized );
    }

    bool CommitBackBuffersReady()
    {
        return Advance( Dx12RecreationStage::SwapChainResized, Dx12RecreationStage::BackBuffersReady );
    }

    bool CommitPublished( uint64_t generation )
    {

        if ( !Advance( Dx12RecreationStage::BackBuffersReady, Dx12RecreationStage::Published ) )
        {
            return false;
        }

        m_publishedGeneration = generation;
        return true;
    }

    SkullbonezCore::Core::SbResult Fail( const SkullbonezCore::Core::SbResult& result )
    {

        if ( result.ok )
        {
            return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                            "Recreation failure requires a failed result" );
        }

        if ( m_stage != Dx12RecreationStage::Failed )
        {
            m_firstFailure = result;
            m_stage = Dx12RecreationStage::Failed;
        }

        return m_firstFailure;
    }

    bool IsPublished() const
    {
        return m_stage == Dx12RecreationStage::Published;
    }

    bool HasFailed() const
    {
        return m_stage == Dx12RecreationStage::Failed;
    }

    uint64_t PublishedGeneration() const
    {
        return m_publishedGeneration;
    }

    Dx12RecreationStage Stage() const
    {
        return m_stage;
    }

  private:
    bool Advance( Dx12RecreationStage expected, Dx12RecreationStage next )
    {

        if ( m_stage != expected || !m_firstFailure.ok )
        {
            return false;
        }

        m_stage = next;
        return true;
    }

    SkullbonezCore::Core::SbResult m_firstFailure = SkullbonezCore::Core::SbResult::Success();
    Dx12RecreationStage m_stage = Dx12RecreationStage::Idle;
    uint64_t m_publishedGeneration = 0;
};


// Debug-only runtime fault injection uses this allocation-free state before
// ExecuteCommandLists. The first injected error is sticky and every later
// submission attempt is counted as blocked rather than reaching the queue.
class Dx12FaultInjectionState
{
  public:
    void Configure( const char* token )
    {
        m_armedBeforeFirstSubmission = token && std::strcmp( token, "before-first-submit" ) == 0;
        m_injected = false;
        m_submissionCount = 0;
        m_blockedSubmissionCount = 0;
        m_firstFailure = SkullbonezCore::Core::SbResult::Success();
    }

    SkullbonezCore::Core::SbResult BeforeSubmission()
    {

        if ( !m_armedBeforeFirstSubmission )
        {
            return SkullbonezCore::Core::SbResult::Success();
        }

        if ( m_injected )
        {
            ++m_blockedSubmissionCount;
            return m_firstFailure;
        }

        m_injected = true;
        m_firstFailure = SkullbonezCore::Core::SbResult::
            Failure( "Rendering/DX12FaultInjection", "Injected failure before first ExecuteCommandLists submission" );
        return m_firstFailure;
    }

    void CommitSubmission()
    {
        ++m_submissionCount;
    }

    bool IsArmed() const
    {
        return m_armedBeforeFirstSubmission;
    }

    bool WasInjected() const
    {
        return m_injected;
    }

    uint32_t SubmissionCount() const
    {
        return m_submissionCount;
    }

    uint32_t BlockedSubmissionCount() const
    {
        return m_blockedSubmissionCount;
    }

  private:
    SkullbonezCore::Core::SbResult m_firstFailure = SkullbonezCore::Core::SbResult::Success();
    uint32_t m_submissionCount = 0;
    uint32_t m_blockedSubmissionCount = 0;
    bool m_armedBeforeFirstSubmission = false;
    bool m_injected = false;
};

} // namespace Rendering
} // namespace SkullbonezCore
