/*
File: SkullbonezSource/Runtime/CaptureController.cpp
Purpose:
  Implements screenshot request ownership, capture execution, and automation pass-throughs.

Summary:
  Run supplies frame context and narrow render capabilities. CaptureController
  owns mutable screenshot automation plus the fixed input-triggered request ring,
  then delegates pixel writing to CaptureSystem.

Glossary:
  Capture sink: Value hook that performs the actual screenshot write.
  Auto-cycle: Screenshot automation that steps through tracked balls/scenes.
  Screenshot request: Runtime state describing when and where to capture pixels.
  Request ring: Fixed FIFO storage drained at the input-frame capture checkpoint.

Invariants:
  - CaptureController owns trigger state but not backbuffer readback.
  - Failed or rejected requests never appear in the accepted result batch.
  - Tick methods must be deterministic for suite and screenshot automation.

Related:
  - SkullbonezSource/Runtime/CaptureController.h
  - SkullbonezSource/Runtime/CaptureSystem.h
*/
#include "CaptureController.h"
#include "../Core/Allocation/RuntimeAllocationTracker.h"
#include "../Core/FatalError.h"

#include <cstdio>
#include <cstring>

namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace SkullbonezCore
{
namespace Runtime
{
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


bool CaptureController::IsScreenshotDue( const RuntimeCaptureSceneContext& context ) const
{
    return CaptureSystem::IsScreenshotDue( m_screenshot, context );
}


bool CaptureController::RequiresDeterministicPresentation( const RuntimeCaptureSceneContext& context ) const
{
    return CaptureSystem::RequiresDeterministicPresentation( m_screenshot, context );
}


RuntimeCaptureResult CaptureController::TickScreenshots( const RuntimeCaptureSceneContext& context,
                                                         Rendering::IRenderCaptureBackend& backend )
{
    return CaptureSystem::TickScreenshots( m_screenshot, context, *this, backend );
}


RuntimeCaptureResult CaptureController::TickAutoCycle( bool isSceneMode,
                                                       bool isInteractiveRun,
                                                       int ballCount,
                                                       float& autoCycleInterval,
                                                       float& autoCycleAccum,
                                                       int& autoCycleShotsTaken,
                                                       int& trackBallIndex,
                                                       Rendering::IRenderCaptureBackend& backend )
{
    return CaptureSystem::TickAutoCycle( isSceneMode,
                                         isInteractiveRun,
                                         ballCount,
                                         autoCycleInterval,
                                         autoCycleAccum,
                                         autoCycleShotsTaken,
                                         trackBallIndex,
                                         *this,
                                         backend );
}


SkullbonezCore::Core::SbResult CaptureController::QueueScreenshot( const char* path )
{
    const std::size_t pathLength = path ? strnlen_s( path, CAPTURE_REQUEST_PATH_CAPACITY ) : 0;
    if ( pathLength == 0 || pathLength >= CAPTURE_REQUEST_PATH_CAPACITY )
    {
        // Lane R: file paths originate at tool/input boundaries. Rejecting the
        // request before enqueue prevents the fixed record from truncating to a
        // different destination than the operator selected.
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/CaptureController",
                                                        "Screenshot path must contain 1-%d bytes without truncation",
                                                        CAPTURE_REQUEST_PATH_CAPACITY - 1 );
    }

    const char* extension = strrchr( path, '.' );
    if ( !extension || _stricmp( extension, ".bmp" ) != 0 )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/CaptureController",
                                                        "Screenshot path must end in .bmp: %s",
                                                        path );
    }

    if ( m_requestCount >= CAPTURE_REQUEST_QUEUE_CAPACITY )
    {
        // Lane F: this queue drains once per input frame. Exhaustion means a
        // producer violated the fixed owner budget; runtime growth is forbidden.
        SB_FATAL( "Runtime/CaptureController",
                  "Capture request capacity exhausted. capacity=%d high_water=%d phase=input",
                  CAPTURE_REQUEST_QUEUE_CAPACITY,
                  m_requestCount );
    }

    const int tail = ( m_requestHead + m_requestCount ) % CAPTURE_REQUEST_QUEUE_CAPACITY;
    strcpy_s( m_requests[tail].path, path );
    ++m_requestCount;
    return SkullbonezCore::Core::SbResult::Success();
}


CaptureRequestBatchResult CaptureController::DrainScreenshotRequests( Rendering::IRenderCaptureBackend& backend )
{
    CaptureRequestBatchResult result;
    while ( m_requestCount > 0 )
    {
        const CaptureRequest request = m_requests[m_requestHead];
        m_requests[m_requestHead] = {};
        m_requestHead = ( m_requestHead + 1 ) % CAPTURE_REQUEST_QUEUE_CAPACITY;
        --m_requestCount;

        const SkullbonezCore::Core::SbResult saveResult = SaveScreenshot( backend, request.path );
        if ( saveResult.ok )
        {
            result.saved[result.savedCount++] = request;
        }
        else
        {
            if ( result.status.ok )
            {
                result.status = saveResult;
            }
            ++result.failedCount;
        }
    }
    m_requestHead = 0;
    return result;
}


std::size_t CaptureController::PendingScreenshotCount() const
{
    return static_cast<std::size_t>( m_requestCount );
}


SkullbonezCore::Core::SbResult CaptureController::SaveScreenshot( Rendering::IRenderCaptureBackend& backend,
                                                                  const char* path )
{
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Capture );
    const SkullbonezCore::Core::SbResult captureResult = CaptureSystem::SaveBackbufferBmp( backend, path );
    if ( !captureResult.ok )
    {
        return captureResult;
    }
    printf( "[capture] Screenshot taken: %s\n", path );
    fflush( stdout );
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult CaptureController::SaveBackbufferBmp( Rendering::IRenderCaptureBackend& backend,
                                                                     const char* path )
{
    return CaptureSystem::SaveBackbufferBmp( backend, path );
}
} // namespace Runtime
} // namespace SkullbonezCore
