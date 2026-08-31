/*
File: SkullbonezSource/Runtime/Capture/CaptureController.cpp
Purpose:
  Implements screenshot request ownership, capture execution, and automation pass-throughs.

Summary:
  Run supplies frame context and the concrete capture owner. CaptureController
  owns mutable screenshot automation plus fixed input-checkpoint and post-render
  request stores, then delegates pixel writing to CaptureSystem.

Glossary:
  Request ring: Fixed FIFO storage drained at the input-frame capture checkpoint.
  Post-render request: Typed PNG path and transaction token drained after draw.

Invariants:
  - CaptureController owns trigger state but not backbuffer readback.
  - Failed or rejected requests never appear in the accepted result batch.
  - Post-render completion preserves every request token even when capture fails.
  - Tick methods must be deterministic for suite and screenshot automation.

Related:
  - SkullbonezSource/Runtime/Capture/CaptureController.h
  - SkullbonezSource/Runtime/Capture/CaptureSystem.h
  - Agentic/Reference/engine-glossary.md
*/
#include "CaptureController.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/FatalError.h"

#include <Windows.h>
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

void CaptureController::ResetScreenshot()
{
    m_screenshot = {};
}


void CaptureController::DisableAutomationExit()
{
    m_screenshot.isScreenshotAndExit = false;
}


void CaptureController::ApplySceneAutomation( int screenshotFrame, int screenshotMs, bool screenshotAndExit,
                                              const char* screenshotPath, int screenshotInterval,
                                              const char* screenshotDirectory )
{
    m_screenshot.screenshotFrame = screenshotFrame;
    m_screenshot.screenshotMs = screenshotMs;
    m_screenshot.isScreenshotAndExit = screenshotAndExit;
    m_screenshot.screenshotInterval = screenshotInterval;

    if ( screenshotPath && screenshotPath[0] != '\0' )
    {
        strcpy_s( m_screenshot.screenshotPath, sizeof( m_screenshot.screenshotPath ), screenshotPath );
    }

    if ( screenshotDirectory && screenshotDirectory[0] != '\0' )
    {
        strcpy_s( m_screenshot.screenshotDir, sizeof( m_screenshot.screenshotDir ), screenshotDirectory );
        CreateDirectoryA( m_screenshot.screenshotDir, nullptr );
    }
}


bool CaptureController::RequiresDeterministicPresentation( bool isSceneMode, int currentFrame, double elapsedMs ) const
{
    return CaptureSystem::RequiresDeterministicPresentation( m_screenshot, isSceneMode, currentFrame, elapsedMs );
}


#if defined( SKULLBONEZ_CAPTURE_EXECUTION )
RuntimeCaptureResult CaptureController::TickScreenshots( const ScreenshotFrameInput& input,
                                                         Rendering::Dx12BackbufferCapture& backend )
{
    return CaptureSystem::TickScreenshots( m_diagnostics, m_screenshot, input, *this, backend );
}


RuntimeCaptureResult CaptureController::TickAutoCycle( const AutoCycleCaptureInput& input, AutoCycleCaptureUpdate& update,
                                                       Rendering::Dx12BackbufferCapture& backend )
{
    return CaptureSystem::TickAutoCycle( input, update, *this, backend );
}
#endif


SkullbonezCore::Core::SbResult CaptureController::QueueScreenshot( const char* path )
{
    const std::size_t pathLength = path ? strnlen_s( path, CAPTURE_REQUEST_PATH_CAPACITY ) : 0;

    if ( pathLength == 0 || pathLength >= CAPTURE_REQUEST_PATH_CAPACITY )
    {
        // Recoverable error: file paths originate at tool/input boundaries. Rejecting the
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
        // Fatal invariant: this queue drains once per input frame. Exhaustion means a
        // producer violated the fixed owner budget; runtime growth is forbidden.
        SB_FATAL( "Runtime/CaptureController", "Capture request capacity exhausted. capacity=%d high_water=%d phase=input",
                  CAPTURE_REQUEST_QUEUE_CAPACITY, m_requestCount );
    }

    const int tail = ( m_requestHead + m_requestCount ) % CAPTURE_REQUEST_QUEUE_CAPACITY;
    strcpy_s( m_requests[tail].path, path );
    ++m_requestCount;
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult CaptureController::QueuePostRenderPng( const char* path, PostRenderCaptureOwner owner,
                                                                      uint64_t token )
{
    const std::size_t pathLength = path ? strnlen_s( path, CAPTURE_REQUEST_PATH_CAPACITY ) : 0;
    const char* extension = path ? strrchr( path, '.' ) : nullptr;

    if ( pathLength == 0 || pathLength >= CAPTURE_REQUEST_PATH_CAPACITY || !extension ||
         _stricmp( extension, ".png" ) != 0 || token == 0 )
    {
        return m_diagnostics.Failure( "Runtime/CaptureController",
                                      "Post-render capture requires a bounded PNG path and nonzero token." );
    }

    if ( m_postRenderRequestCount >= POST_RENDER_CAPTURE_REQUEST_CAPACITY )
    {
        SB_FATAL( "Runtime/CaptureController",
                  "Post-render capture capacity exhausted. capacity=%d high_water=%d phase=post_render",
                  POST_RENDER_CAPTURE_REQUEST_CAPACITY, m_postRenderRequestCount );
    }

    PostRenderCaptureRequest& request = m_postRenderRequests[m_postRenderRequestCount++];
    strcpy_s( request.path, path );
    request.owner = owner;
    request.token = token;
    return SkullbonezCore::Core::SbResult::Success();
}


bool CaptureController::CancelPostRenderRequest( PostRenderCaptureOwner owner, uint64_t token )
{
    for ( int index = 0; index < m_postRenderRequestCount; ++index )
    {
        if ( m_postRenderRequests[index].owner != owner || m_postRenderRequests[index].token != token )
        {
            continue;
        }

        for ( int moveIndex = index + 1; moveIndex < m_postRenderRequestCount; ++moveIndex )
        {
            m_postRenderRequests[moveIndex - 1] = m_postRenderRequests[moveIndex];
        }

        m_postRenderRequests[--m_postRenderRequestCount] = {};
        return true;
    }

    return false;
}


std::size_t CaptureController::PendingPostRenderCount() const
{
    return static_cast<std::size_t>( m_postRenderRequestCount );
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


PostRenderCaptureBatchResult CaptureController::DrainPostRenderRequests( Rendering::Dx12BackbufferCapture& backend )
{
    PostRenderCaptureBatchResult batch;

    while ( m_postRenderRequestCount > 0 )
    {
        PostRenderCaptureResult& completed = batch.results[batch.count++];
        completed.request = m_postRenderRequests[0];

        for ( int moveIndex = 1; moveIndex < m_postRenderRequestCount; ++moveIndex )
        {
            m_postRenderRequests[moveIndex - 1] = m_postRenderRequests[moveIndex];
        }

        m_postRenderRequests[--m_postRenderRequestCount] = {};
        completed.status = SaveScreenshot( backend, completed.request.path );
    }

    return batch;
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
    const char* extension = path ? strrchr( path, '.' ) : nullptr;
    SkullbonezCore::Core::SbResult captureResult;

    if ( extension && _stricmp( extension, ".bmp" ) == 0 )
    {
        captureResult = CaptureSystem::SaveBackbufferBmp( m_diagnostics, backend, path );
    }
    else if ( extension && _stricmp( extension, ".png" ) == 0 )
    {
        captureResult = CaptureSystem::SaveBackbufferPng( m_diagnostics, backend, path );
    }
    else
    {
        captureResult = m_diagnostics.Failure( "Runtime/CaptureController", "Unsupported screenshot extension: %s",
                                               path ? path : "<null>" );
    }

    if ( !captureResult.Ok() )
    {
        return captureResult;
    }

    printf( "[capture] Screenshot taken: %s\n", path );
    fflush( stdout );
    return SkullbonezCore::Core::SbResult::Success();
}


#endif
} // namespace Runtime
} // namespace SkullbonezCore
