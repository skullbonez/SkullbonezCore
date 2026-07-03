/*
File: SkullbonezSource/Runtime/CaptureSystem.h
Purpose:
  Captures frame output to screenshots for scenes, validation, and look-dev.

Mental model:
  Runtime code owns screenshot trigger state, while renderer code owns pixel
  readback. This header keeps the trigger, write hook, and readback capability
  narrow enough to test without a full renderer.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
    resource.
  Back buffer: Swap-chain image that will be presented to the window.
  Capture sink: Value hook that performs the screenshot write side effect.

Invariants:
  - Screenshot state is per-run state; interval counters and one-shot flags are
    consumed by TickScreenshots rather than by render backends.
  - RuntimeCaptureSink carries the actual write side effect so capture policy
    can be tested without a renderer or a virtual callback object.

Related:
  - SkullbonezSource/Runtime/CaptureSystem.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cassert>

namespace SkullbonezCore
{
namespace Rendering
{
class IRenderCaptureBackend;
}

namespace Basics
{
struct RunScreenshotState
{
    bool isScreenshotSaved = false;   // Screenshot already written this run
    bool isScreenshotAndExit = false; // Capture frame 1 as SCENENAME.bmp then exit
    int screenshotFrame = -1;         // Save screenshot at this frame (-1 = unused)
    int screenshotMs = -1;            // Save screenshot at this elapsed ms (-1 = unused)
    char screenshotPath[256] = {};    // Output path for screenshot (empty = none)
    int screenshotInterval = -1;      // Save screenshot every N frames (-1 = disabled)
    int intervalCaptureCount = 0;     // Sequential counter for interval captures
    char screenshotDir[256] = {};     // Output directory for interval captures
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
};

struct RuntimeCaptureSink
{
    using SaveScreenshotFn = void ( * )( void* context, const char* path );

    void* context = nullptr;
    SaveScreenshotFn saveScreenshot = nullptr;

    void SaveScreenshot( const char* path ) const
    {
        // Concept: capture automation needs one explicit side-effect hook, not
        // an inherited service object on the frame path.
        assert( saveScreenshot != nullptr );
        saveScreenshot( context, path );
    }
};

class CaptureSystem
{
  public:
    static void SaveBackbufferBmp( Rendering::IRenderCaptureBackend& backend, const char* path );
    static RuntimeCaptureResult TickScreenshots( RunScreenshotState& screenshot,
                                                 const RuntimeCaptureSceneContext& context,
                                                 const RuntimeCaptureSink& sink );
    static RuntimeCaptureResult TickAutoCycle( bool isSceneMode,
                                               bool isInteractiveRun,
                                               int ballCount,
                                               float& autoCycleInterval,
                                               float& autoCycleAccum,
                                               int& autoCycleShotsTaken,
                                               int& trackBallIndex,
                                               const RuntimeCaptureSink& sink );
};
} // namespace Basics
} // namespace SkullbonezCore
