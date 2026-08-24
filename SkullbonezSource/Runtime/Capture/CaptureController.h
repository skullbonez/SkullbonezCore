/*
File: SkullbonezSource/Runtime/Capture/CaptureController.h
Purpose:
  Owns screenshot state, input-triggered request storage, and capture automation.

Summary:
  CaptureController is the mutable runtime boundary for screenshots. The
  lower-level CaptureSystem still writes pixels, while this controller owns
  trigger state, fixed input and post-render request stores, and per-frame
  automation decisions.

Glossary:
  Accepted capture: Queued screenshot whose complete readback/file write succeeded.
  Frame gate: Per-frame decision that says whether a capture is due now.
  Post-render capture: Typed image request whose token returns completion to a
    retained authoring transaction after the frame's draw submission.

Invariants:
  - Controller state is runtime-owned; pixel IO stays in CaptureSystem.
  - Request paths are validated without truncation before they enter fixed storage.
  - Post-render requests retain owner/token identity through success or failure.
  - Automation must remain stable for validation screenshot timing.

Related:
  - SkullbonezSource/Runtime/Capture/CaptureSystem.h
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "CaptureSystem.h"

#include <cstddef>

namespace SkullbonezCore
{
namespace Runtime
{
constexpr int CAPTURE_REQUEST_PATH_CAPACITY = 512;
constexpr int CAPTURE_REQUEST_QUEUE_CAPACITY = 16;
constexpr int POST_RENDER_CAPTURE_REQUEST_CAPACITY = 4;

struct CaptureRequest
{
    char path[CAPTURE_REQUEST_PATH_CAPACITY] = {};             // Validated, non-truncated BMP output path.
};

enum class PostRenderCaptureOwner : uint8_t
{
    LookLab = 0
};

struct PostRenderCaptureRequest
{
    // Invariant: the token identifies one request to its retained transaction
    // owner; Capture never interprets or manufactures transaction identity.
    char path[CAPTURE_REQUEST_PATH_CAPACITY] = {};
    PostRenderCaptureOwner owner = PostRenderCaptureOwner::LookLab;
    uint64_t token = 0;
};

struct PostRenderCaptureResult
{
    // TestOwnerRequestQueues.cpp proves fixed owner/token identity independently
    // of the renderer-backed drain.
    PostRenderCaptureRequest request;
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
};

struct PostRenderCaptureBatchResult
{
    // Invariant: count names the initialized FIFO prefix; Capture fills one row
    // for every removed request, including failed image writes.
    PostRenderCaptureResult results[POST_RENDER_CAPTURE_REQUEST_CAPACITY];
    std::size_t count = 0;
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

    void ResetScreenshot();
    void DisableAutomationExit();
    void ApplySceneAutomation( int screenshotFrame, int screenshotMs, bool screenshotAndExit, const char* screenshotPath,
                               int screenshotInterval, const char* screenshotDirectory );
    bool RequiresDeterministicPresentation( bool isSceneMode, int currentFrame, double elapsedMs ) const;
    RuntimeCaptureResult TickScreenshots( bool isSceneMode, bool isInteractiveRun, int currentFrame, double elapsedMs,
                                          const char* currentScenePath, Rendering::Dx12BackbufferCapture& backend );
    RuntimeCaptureResult TickAutoCycle( bool isSceneMode, bool isInteractiveRun, int ballCount, float& autoCycleInterval,
                                        float& autoCycleAccum, int& autoCycleShotsTaken, int& trackBallIndex,
                                        Rendering::Dx12BackbufferCapture& backend );

    // Accepts one bounded BMP path for the next input-frame capture checkpoint.
    // Invalid or truncating paths return recoverable failure without entering the queue.
    SkullbonezCore::Core::SbResult QueueScreenshot( const char* path );
    CaptureRequestBatchResult DrainScreenshotRequests( Rendering::Dx12BackbufferCapture& backend );
    std::size_t PendingScreenshotCount() const;

    // Post-render requests are separate from F3's input checkpoint so their
    // image necessarily contains presentation applied during the current turn.
    SkullbonezCore::Core::SbResult QueuePostRenderPng( const char* path, PostRenderCaptureOwner owner, uint64_t token );
    bool CancelPostRenderRequest( PostRenderCaptureOwner owner, uint64_t token );
    PostRenderCaptureBatchResult DrainPostRenderRequests( Rendering::Dx12BackbufferCapture& backend );
    std::size_t PendingPostRenderCount() const;

    SkullbonezCore::Core::SbResult SaveScreenshot( Rendering::Dx12BackbufferCapture& backend, const char* path );

  private:
    SkullbonezCore::Core::SbDiagnosticStore& m_diagnostics;
    RunScreenshotState m_screenshot;                           // Scene and CLI screenshot trigger state
    CaptureRequest m_requests[CAPTURE_REQUEST_QUEUE_CAPACITY]; // Fixed input-triggered capture ring.
    int m_requestHead = 0;                                     // Oldest capture request.
    int m_requestCount = 0;                                    // Occupied capture request slots.
    PostRenderCaptureRequest m_postRenderRequests[POST_RENDER_CAPTURE_REQUEST_CAPACITY];
    int m_postRenderRequestCount = 0;
};
} // namespace Runtime
} // namespace SkullbonezCore
