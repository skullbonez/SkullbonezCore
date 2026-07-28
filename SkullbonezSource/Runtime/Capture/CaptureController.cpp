/*
File: SkullbonezSource/Runtime/Capture/CaptureController.cpp
Purpose:
  Implements screenshot request ownership, capture execution, and automation pass-throughs.

Summary:
  Run supplies frame context and the concrete capture owner. CaptureController
  owns mutable screenshot automation plus the fixed input-triggered request ring,
  then delegates pixel writing to CaptureSystem.

Glossary:
  Capture result: Value outcome folded into the fixed accepted-request batch.
  Auto-cycle: Screenshot automation that steps through tracked balls/scenes.
  Screenshot request: Runtime state describing when and where to capture pixels.
  Request ring: Fixed FIFO storage drained at the input-frame capture checkpoint.

Invariants:
  - CaptureController owns trigger state but not backbuffer readback.
  - Failed or rejected requests never appear in the accepted result batch.
  - Tick methods must be deterministic for suite and screenshot automation.

Related:
  - SkullbonezSource/Runtime/Capture/CaptureController.h
  - SkullbonezSource/Runtime/Capture/CaptureSystem.h
*/
#include "CaptureController.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/FatalError.h"

#include <cstdio>
#include <cstring>

namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace SkullbonezCore
{
namespace Runtime
{
CaptureController::CaptureController( SkullbonezCore::Core::SbDiagnosticStore& diagnostics ) noexcept
    : m_diagnostics( diagnostics )
{
}

RunScreenshotState& CaptureController::Screenshot()
{
    return m_screenshot;
}


const RunScreenshotState& CaptureController::Screenshot() const
{
    return m_screenshot;
}


void CaptureController::ResetScreenshot()
{
    m_screenshot = {};
}


void CaptureController::DisableAutomationExit()
{
    m_screenshot.isScreenshotAndExit = false;
}


bool CaptureController::IsScreenshotDue( bool isSceneMode, int currentFrame, double elapsedMs ) const
{
    return CaptureSystem::IsScreenshotDue( m_screenshot, isSceneMode, currentFrame, elapsedMs );
}


bool CaptureController::RequiresDeterministicPresentation( bool isSceneMode, int currentFrame, double elapsedMs ) const
{
    return CaptureSystem::RequiresDeterministicPresentation( m_screenshot, isSceneMode, currentFrame, elapsedMs );
}


#if defined( SKULLBONEZ_CAPTURE_EXECUTION )
RuntimeCaptureResult CaptureController::TickScreenshots( bool isSceneMode, bool isInteractiveRun, int currentFrame,
                                                         double elapsedMs, const char* currentScenePath,
                                                         Rendering::Dx12BackbufferCapture& backend )
{
    return CaptureSystem::TickScreenshots( m_screenshot, isSceneMode, isInteractiveRun, currentFrame, elapsedMs,
                                           currentScenePath, *this, backend );
}


RuntimeCaptureResult CaptureController::TickAutoCycle( bool isSceneMode, bool isInteractiveRun, int ballCount,
                                                       float& autoCycleInterval, float& autoCycleAccum,
                                                       int& autoCycleShotsTaken, int& trackBallIndex,
                                                       Rendering::Dx12BackbufferCapture& backend )
{
    return CaptureSystem::TickAutoCycle( isSceneMode, isInteractiveRun, ballCount, autoCycleInterval, autoCycleAccum,
                                         autoCycleShotsTaken, trackBallIndex, *this, backend );
}
#endif


SkullbonezCore::Core::SbResult CaptureController::QueueScreenshot( const char* path )
{
    const std::size_t pathLength = path ? strnlen_s( path, CAPTURE_REQUEST_PATH_CAPACITY ) : 0;

    if ( pathLength == 0 || pathLength >= CAPTURE_REQUEST_PATH_CAPACITY )
    {

        // Lane R: file paths originate at tool/input boundaries. Rejecting the
        // request before enqueue prevents the fixed record from truncating to a
        // different destination than the operator selected.
        return m_diagnostics.Failure( "Runtime/CaptureController",
                                      "Screenshot path must contain 1-%d bytes without truncation",
                                      CAPTURE_REQUEST_PATH_CAPACITY - 1 );
    }

    const char* extension = strrchr( path, '.' );

    if ( !extension || _stricmp( extension, ".bmp" ) != 0 )
    {
        return m_diagnostics.Failure( "Runtime/CaptureController", "Screenshot path must end in .bmp: %s", path );
    }

    if ( m_requestCount >= CAPTURE_REQUEST_QUEUE_CAPACITY )
    {

        // Lane F: this queue drains once per input frame. Exhaustion means a
        // producer violated the fixed owner budget; runtime growth is forbidden.
        SB_FATAL( "Runtime/CaptureController", "Capture request capacity exhausted. capacity=%d high_water=%d phase=input",
                  CAPTURE_REQUEST_QUEUE_CAPACITY, m_requestCount );
    }

    const int tail = ( m_requestHead + m_requestCount ) % CAPTURE_REQUEST_QUEUE_CAPACITY;
    strcpy_s( m_requests[tail].path, path );
    ++m_requestCount;
    return SkullbonezCore::Core::SbResult::Success();
}


void AccumulateCaptureRequestResult( CaptureRequestBatchResult& batch, const CaptureRequest& request,
                                     const SkullbonezCore::Core::SbResult& requestResult )
{

    if ( requestResult.Ok() )
    {
        batch.saved[batch.savedCount++] = request;
        return;
    }

    if ( batch.status.Ok() )
    {
        batch.status = requestResult;
    }

    ++batch.failedCount;
}


#if defined( SKULLBONEZ_CAPTURE_EXECUTION )
CaptureRequestBatchResult CaptureController::DrainScreenshotRequests( Rendering::Dx12BackbufferCapture& backend )
{
    CaptureRequestBatchResult result;

    while ( m_requestCount > 0 )
    {
        const CaptureRequest request = m_requests[m_requestHead];
        m_requests[m_requestHead] = {};

        m_requestHead = ( m_requestHead + 1 ) % CAPTURE_REQUEST_QUEUE_CAPACITY;
        --m_requestCount;

        const SkullbonezCore::Core::SbResult saveResult = SaveScreenshot( backend, request.path );
        AccumulateCaptureRequestResult( result, request, saveResult );
    }

    m_requestHead = 0;
    return result;
}
#endif


std::size_t CaptureController::PendingScreenshotCount() const
{
    return static_cast<std::size_t>( m_requestCount );
}


#if defined( SKULLBONEZ_CAPTURE_EXECUTION )
SkullbonezCore::Core::SbResult CaptureController::SaveScreenshot( Rendering::Dx12BackbufferCapture& backend,
                                                                  const char* path )
{
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Capture );
    const SkullbonezCore::Core::SbResult captureResult = CaptureSystem::SaveBackbufferBmp( m_diagnostics, backend, path );

    if ( !captureResult.Ok() )
    {
        return captureResult;
    }

    printf( "[capture] Screenshot taken: %s\n", path );
    fflush( stdout );
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult CaptureController::SaveBackbufferBmp( Rendering::Dx12BackbufferCapture& backend,
                                                                     const char* path )
{
    return CaptureSystem::SaveBackbufferBmp( m_diagnostics, backend, path );
}
#endif
} // namespace Runtime
} // namespace SkullbonezCore
