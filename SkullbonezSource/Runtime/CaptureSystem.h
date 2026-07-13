/*
File: SkullbonezSource/Runtime/CaptureSystem.h
Purpose:
  Captures frame output to screenshots for scenes, validation, and look-dev.

Summary:
  Runtime code owns screenshot trigger state, while renderer code owns pixel
  readback. This header keeps the trigger, capture owner, and readback capability
  narrow enough to test without a full renderer.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
    resource.
  Back buffer: Swap-chain image that will be presented to the window.
  Capture backend: Narrow renderer facet that supplies screenshot readback.
  Due predictor: Side-effect-free trigger query used before simulation so the
    eventual captured frame can pin presentation to committed solver state.

Invariants:
  - Screenshot state is per-run state; interval counters and one-shot flags are
    consumed by TickScreenshots rather than by render backends.
  - CaptureController owns the write side effect and receives the backend facet
    explicitly, so no callback can recover the application shell.
  - The due predictor and TickScreenshots use identical frame/time trigger rules.

Related:
  - SkullbonezSource/Runtime/CaptureSystem.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/SbResult.h"

#include <cassert>

namespace SkullbonezCore
{
namespace Rendering
{
class IRenderCaptureBackend;
}

namespace Runtime
{
struct RunScreenshotState
{
    bool isScreenshotSaved = false;               // Screenshot already written this run
    bool isScreenshotAndExit = false;             // Capture frame 1 as SCENENAME.bmp then exit
    int screenshotFrame = -1;                     // Save screenshot at this frame (-1 = unused)
    int screenshotMs = -1;                        // Save screenshot at this elapsed ms (-1 = unused)
    char screenshotPath[256] = {};                // Output path for screenshot (empty = none)
    int screenshotInterval = -1;                  // Save screenshot every N frames (-1 = disabled)
    int intervalCaptureCount = 0;                 // Sequential counter for interval captures
    char screenshotDir[256] = {};                 // Output directory for interval captures
};

enum class RuntimeCaptureCompletion
{
    None,
    ScreenshotAndExit,
    Screenshot,
    AutoCycle
};

enum class RuntimeCaptureAutomation
{
    None,
    Quit,
    AdvanceSceneOrQuit,
    HoldInteractive
};

struct RuntimeCaptureSceneContext
{
    bool isSceneMode = false;
    bool isInteractiveRun = false;
    int currentFrame = 0;
    double elapsedMs = 0.0;
    const char* currentScenePath = nullptr;
};

struct RuntimeCaptureResult
{
    bool restartFrame = false;
    RuntimeCaptureCompletion completion = RuntimeCaptureCompletion::None;
    RuntimeCaptureAutomation automation = RuntimeCaptureAutomation::None;
    SkullbonezCore::Core::SbResult captureResult; // Lane R result from screenshot readback/write side effects.
};

class CaptureController;
class CaptureSystem
{
  public:
    static bool IsScreenshotDue( const RunScreenshotState& screenshot, const RuntimeCaptureSceneContext& context );
    static bool RequiresDeterministicPresentation( const RunScreenshotState& screenshot,
                                                   const RuntimeCaptureSceneContext& context );
    static SkullbonezCore::Core::SbResult SaveBackbufferBmp( Rendering::IRenderCaptureBackend& backend,
                                                             const char* path );
    static RuntimeCaptureResult TickScreenshots( RunScreenshotState& screenshot,
                                                 const RuntimeCaptureSceneContext& context,
                                                 CaptureController& capture,
                                                 Rendering::IRenderCaptureBackend& backend );
    static RuntimeCaptureResult TickAutoCycle( bool isSceneMode,
                                               bool isInteractiveRun,
                                               int ballCount,
                                               float& autoCycleInterval,
                                               float& autoCycleAccum,
                                               int& autoCycleShotsTaken,
                                               int& trackBallIndex,
                                               CaptureController& capture,
                                               Rendering::IRenderCaptureBackend& backend );
};
} // namespace Runtime
} // namespace SkullbonezCore
