/*
File: SkullbonezSource/Runtime/CaptureController.cpp
Purpose:
  Implements runtime screenshot ownership and capture automation pass-throughs.

Mental model:
  Run supplies frame context and a sink. CaptureController owns the mutable
  screenshot request state and delegates pixel writing to CaptureSystem.

Glossary:
  Capture sink: Value hook that performs the actual screenshot write.
  Auto-cycle: Screenshot automation that steps through tracked balls/scenes.
  Screenshot request: Runtime state describing when and where to capture pixels.

Invariants:
  - CaptureController owns trigger state but not backbuffer readback.
  - Tick methods must be deterministic for suite and screenshot automation.

Related:
  - SkullbonezSource/Runtime/CaptureController.h
  - SkullbonezSource/Runtime/CaptureSystem.h
*/
#include "CaptureController.h"

namespace SkullbonezCore
{
namespace Basics
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


RuntimeCaptureResult CaptureController::TickScreenshots( const RuntimeCaptureSceneContext& context,
                                                         const RuntimeCaptureSink& sink )
{
    return CaptureSystem::TickScreenshots( m_screenshot, context, sink );
}


RuntimeCaptureResult CaptureController::TickAutoCycle( bool isSceneMode,
                                                       bool isInteractiveRun,
                                                       int ballCount,
                                                       float& autoCycleInterval,
                                                       float& autoCycleAccum,
                                                       int& autoCycleShotsTaken,
                                                       int& trackBallIndex,
                                                       const RuntimeCaptureSink& sink )
{
    return CaptureSystem::TickAutoCycle( isSceneMode,
                                         isInteractiveRun,
                                         ballCount,
                                         autoCycleInterval,
                                         autoCycleAccum,
                                         autoCycleShotsTaken,
                                         trackBallIndex,
                                         sink );
}


void CaptureController::SaveBackbufferBmp( Rendering::IRenderCaptureBackend& backend, const char* path )
{
    CaptureSystem::SaveBackbufferBmp( backend, path );
}
} // namespace Basics
} // namespace SkullbonezCore
