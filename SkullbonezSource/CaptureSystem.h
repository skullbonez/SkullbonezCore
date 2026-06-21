/*
File: SkullbonezSource/CaptureSystem.h
Purpose:
  Captures frame output to screenshots for scenes, validation, and look-dev.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - SkullbonezSource/CaptureSystem.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

namespace SkullbonezCore
{
namespace Rendering
{
class IRenderCaptureBackend;
}

namespace Basics
{
struct RunScreenshotState;

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

class RuntimeCaptureSink
{
  public:
    virtual ~RuntimeCaptureSink() = default;
    virtual void SaveScreenshot( const char* path ) = 0;
};

class CaptureSystem
{
  public:
    static void SaveBackbufferBmp( Rendering::IRenderCaptureBackend& backend, const char* path );
    static RuntimeCaptureResult TickScreenshots( RunScreenshotState& screenshot,
                                                 const RuntimeCaptureSceneContext& context,
                                                 RuntimeCaptureSink& sink );
    static RuntimeCaptureResult TickAutoCycle( bool isSceneMode,
                                               bool isInteractiveRun,
                                               int ballCount,
                                               float& autoCycleInterval,
                                               float& autoCycleAccum,
                                               int& autoCycleShotsTaken,
                                               int& trackBallIndex,
                                               RuntimeCaptureSink& sink );
};
} // namespace Basics
} // namespace SkullbonezCore
