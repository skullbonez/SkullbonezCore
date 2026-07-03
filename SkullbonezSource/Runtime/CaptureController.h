/*
File: SkullbonezSource/Runtime/CaptureController.h
Purpose:
  Owns screenshot state and capture automation for the runtime shell.

Mental model:
  CaptureController is the mutable runtime boundary for screenshots. The
  lower-level CaptureSystem still writes pixels, while this controller owns
  trigger state and per-frame automation decisions.

Glossary:
  Capture sink: Value hook that performs the actual screenshot write.
  Auto-cycle: Screenshot automation that advances capture targets over time.
  Screenshot request: Runtime state describing when and where to capture pixels.
  Frame gate: Per-frame decision that says whether a capture is due now.

Invariants:
  - Controller state is runtime-owned; pixel IO stays in CaptureSystem.
  - Automation must remain stable for validation screenshot timing.

Related:
  - SkullbonezSource/Runtime/CaptureSystem.h
  - SkullbonezSource/Runtime/RunFrame.cpp
*/
#pragma once

#include "CaptureSystem.h"

namespace SkullbonezCore
{
namespace Basics
{
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

    static void SaveBackbufferBmp( Rendering::IRenderCaptureBackend& backend, const char* path );

  private:
    RunScreenshotState m_screenshot; // Scene and CLI screenshot trigger state
};
} // namespace Basics
} // namespace SkullbonezCore
