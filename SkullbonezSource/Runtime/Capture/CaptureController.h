/*
File: SkullbonezSource/Runtime/Capture/CaptureController.h
Purpose:
  Owns screenshot state, input-triggered request storage, and capture automation.

Summary:
  CaptureController is the mutable runtime boundary for screenshots. The
  lower-level CaptureSystem still writes pixels, while this controller owns
  trigger state, a fixed request ring, and per-frame automation decisions.

Glossary:
  Capture result: Value outcome folded into the fixed accepted-request batch.
  Auto-cycle: Screenshot automation that advances capture targets over time.
  Screenshot request: Runtime state describing when and where to capture pixels.
  Accepted capture: Queued screenshot whose complete readback/file write succeeded.
  Frame gate: Per-frame decision that says whether a capture is due now.

Invariants:
  - Controller state is runtime-owned; pixel IO stays in CaptureSystem.
  - Request paths are validated without truncation before they enter fixed storage.
  - Automation must remain stable for validation screenshot timing.

Related:
  - SkullbonezSource/Runtime/Capture/CaptureSystem.h
  - SkullbonezSource/Runtime/App/RunFrame.cpp
*/
#pragma once

#include "CaptureSystem.h"

#include <cstddef>

namespace SkullbonezCore
{
namespace Runtime
{
constexpr int CAPTURE_REQUEST_PATH_CAPACITY = 260;
constexpr int CAPTURE_REQUEST_QUEUE_CAPACITY = 16;

struct CaptureRequest
{
    char path[CAPTURE_REQUEST_PATH_CAPACITY] = {};             // Validated, non-truncated BMP output path.
};

struct CaptureRequestBatchResult
{
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    CaptureRequest saved[CAPTURE_REQUEST_QUEUE_CAPACITY];
    std::size_t savedCount = 0;
    std::size_t failedCount = 0;
};

// Applies one completed screenshot result to the bounded batch projection.
// Tests use this value seam to exercise failure ownership without manufacturing
// an alternate renderer implementation.
void AccumulateCaptureRequestResult( CaptureRequestBatchResult& batch, const CaptureRequest& request,
                                     const SkullbonezCore::Core::SbResult& requestResult );

class CaptureController
{
  public:
    explicit CaptureController( SkullbonezCore::Core::SbDiagnosticStore& diagnostics ) noexcept;
    RunScreenshotState& Screenshot();
    const RunScreenshotState& Screenshot() const;

    void ResetScreenshot();
    void DisableAutomationExit();
    bool IsScreenshotDue( bool isSceneMode, int currentFrame, double elapsedMs ) const;
    bool RequiresDeterministicPresentation( bool isSceneMode, int currentFrame, double elapsedMs ) const;
    RuntimeCaptureResult TickScreenshots( bool isSceneMode, bool isInteractiveRun, int currentFrame, double elapsedMs,
                                          const char* currentScenePath, Rendering::Dx12BackbufferCapture& backend );
    RuntimeCaptureResult TickAutoCycle( bool isSceneMode, bool isInteractiveRun, int ballCount, float& autoCycleInterval,
                                        float& autoCycleAccum, int& autoCycleShotsTaken, int& trackBallIndex,
                                        Rendering::Dx12BackbufferCapture& backend );

    // Accepts one bounded BMP path for the next input-frame capture checkpoint.
    // Invalid or truncating paths return Lane R failure without entering the queue.
    SkullbonezCore::Core::SbResult QueueScreenshot( const char* path );
    CaptureRequestBatchResult DrainScreenshotRequests( Rendering::Dx12BackbufferCapture& backend );
    std::size_t PendingScreenshotCount() const;

    SkullbonezCore::Core::SbResult SaveScreenshot( Rendering::Dx12BackbufferCapture& backend, const char* path );
    SkullbonezCore::Core::SbResult SaveBackbufferBmp( Rendering::Dx12BackbufferCapture& backend, const char* path );

  private:
    SkullbonezCore::Core::SbDiagnosticStore& m_diagnostics;
    RunScreenshotState m_screenshot;                           // Scene and CLI screenshot trigger state
    CaptureRequest m_requests[CAPTURE_REQUEST_QUEUE_CAPACITY]; // Fixed input-triggered capture ring.
    int m_requestHead = 0;                                     // Oldest capture request.
    int m_requestCount = 0;                                    // Occupied capture request slots.
};
} // namespace Runtime
} // namespace SkullbonezCore
