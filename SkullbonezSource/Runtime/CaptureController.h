/*
File: SkullbonezSource/Runtime/CaptureController.h
Purpose:
  Owns screenshot state, input-triggered request storage, and capture automation.

Mental model:
  CaptureController is the mutable runtime boundary for screenshots. The
  lower-level CaptureSystem still writes pixels, while this controller owns
  trigger state, a fixed request ring, and per-frame automation decisions.

Glossary:
  Capture sink: Value hook that performs the actual screenshot write.
  Auto-cycle: Screenshot automation that advances capture targets over time.
  Screenshot request: Runtime state describing when and where to capture pixels.
  Accepted capture: Queued screenshot whose complete readback/file write succeeded.
  Frame gate: Per-frame decision that says whether a capture is due now.

Invariants:
  - Controller state is runtime-owned; pixel IO stays in CaptureSystem.
  - Request paths are validated without truncation before they enter fixed storage.
  - Automation must remain stable for validation screenshot timing.

Related:
  - SkullbonezSource/Runtime/CaptureSystem.h
  - SkullbonezSource/Runtime/RunFrame.cpp
*/
#pragma once

#include "CaptureSystem.h"

#include <cstddef>

namespace SkullbonezCore
{
namespace Basics
{
constexpr int CAPTURE_REQUEST_PATH_CAPACITY = 260;
constexpr int CAPTURE_REQUEST_QUEUE_CAPACITY = 16;

struct CaptureRequest
{
    char path[CAPTURE_REQUEST_PATH_CAPACITY] = {};             // Validated, non-truncated BMP output path.
};

struct CaptureRequestBatchResult
{
    SbResult status = SbResult::Success();
    CaptureRequest saved[CAPTURE_REQUEST_QUEUE_CAPACITY];
    std::size_t savedCount = 0;
    std::size_t failedCount = 0;
};

class CaptureController
{
  public:
    RunScreenshotState& Screenshot();
    const RunScreenshotState& Screenshot() const;

    void ResetScreenshot();
    RuntimeCaptureResult TickScreenshots( const RuntimeCaptureSceneContext& context, const RuntimeCaptureSink& sink );
    RuntimeCaptureResult TickAutoCycle( bool isSceneMode,
                                        bool isInteractiveRun,
                                        int ballCount,
                                        float& autoCycleInterval,
                                        float& autoCycleAccum,
                                        int& autoCycleShotsTaken,
                                        int& trackBallIndex,
                                        const RuntimeCaptureSink& sink );

    // Accepts one bounded BMP path for the next input-frame capture checkpoint.
    // Invalid or truncating paths return Lane R failure without entering the queue.
    SbResult QueueScreenshot( const char* path );
    CaptureRequestBatchResult DrainScreenshotRequests( Rendering::IRenderCaptureBackend& backend );
    std::size_t PendingScreenshotCount() const;

    SbResult SaveScreenshot( Rendering::IRenderCaptureBackend& backend, const char* path );
    static SbResult SaveBackbufferBmp( Rendering::IRenderCaptureBackend& backend, const char* path );

  private:
    RunScreenshotState m_screenshot;                           // Scene and CLI screenshot trigger state
    CaptureRequest m_requests[CAPTURE_REQUEST_QUEUE_CAPACITY]; // Fixed input-triggered capture ring.
    int m_requestHead = 0;                                     // Oldest capture request.
    int m_requestCount = 0;                                    // Occupied capture request slots.
};
} // namespace Basics
} // namespace SkullbonezCore
