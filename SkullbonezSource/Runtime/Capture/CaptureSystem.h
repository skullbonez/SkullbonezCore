/*
File: SkullbonezSource/Runtime/Capture/CaptureSystem.h
Purpose:
  Captures frame output to screenshots for scenes, validation, and look-dev.

Summary:
  Runtime code owns screenshot trigger state, while renderer code owns pixel
  readback. Pure trigger, PNG encoding, and result-folding rules remain value
  seams so tests do not need to impersonate a renderer.

Glossary:
  Due predictor: Side-effect-free trigger query used before simulation so the
    eventual captured frame can pin presentation to committed solver state.
  Post-render PNG: A capture encoded after UI/world draw submission so an
    authoring bundle contains the presentation applied during that input turn.

Invariants:
  - Screenshot state is per-run state; interval counters and one-shot flags are
    consumed by TickScreenshots rather than by render backends.
  - One-shot and interval triggers are independent; both may publish on one
    frame without one completion consuming the other.
  - CaptureController receives the concrete capture owner explicitly; tests
    exercise the pure result policy instead of a renderer-shaped test double.
  - The due predictor and TickScreenshots use identical frame/time trigger rules.
  - Screenshot files replace their final destination only after every byte is
    written, flushed, and closed successfully.

Related:
  - SkullbonezSource/Runtime/Capture/CaptureSystem.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/SbResult.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12BackbufferCapture;
}

namespace Runtime
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

struct RuntimeCaptureResult
{
    bool restartFrame = false;
    RuntimeCaptureCompletion completion = RuntimeCaptureCompletion::None;
    RuntimeCaptureAutomation automation = RuntimeCaptureAutomation::None;
    SkullbonezCore::Core::SbResult captureResult; // recoverable result from screenshot readback/write side effects.
};

struct ScreenshotCapturePlan
{
    bool oneShotDue = false;
    bool intervalDue = false;
};

struct ScreenshotFrameInput
{
    bool sceneMode = false;
    bool interactiveRun = false;
    int frame = 0;
    double elapsedMs = 0.0;
    const char* scenePath = nullptr;
};

struct AutoCycleCaptureInput
{
    bool sceneMode = false;
    bool interactiveRun = false;
    int ballCount = 0;
    float intervalSeconds = 0.0f;
    float accumulatedSeconds = 0.0f;
    int shotsTaken = 0;
    int trackedBallIndex = 0;

    bool Due() const noexcept
    {
        return sceneMode && ballCount > 0 && intervalSeconds > 0.0f && accumulatedSeconds >= intervalSeconds;
    }
};

struct AutoCycleCaptureUpdate
{
    bool apply = false;
    float accumulatedSeconds = 0.0f;
    int shotsTaken = 0;
    int trackedBallIndex = 0;
};

class CaptureController;
class CaptureSystem
{
  public:
    static bool IsScreenshotDue( const RunScreenshotState& screenshot, bool isSceneMode, int currentFrame,
                                 double elapsedMs );
    static ScreenshotCapturePlan BuildScreenshotCapturePlan( const RunScreenshotState& screenshot, bool isSceneMode,
                                                             int currentFrame, double elapsedMs );
    static bool RequiresDeterministicPresentation( const RunScreenshotState& screenshot, bool isSceneMode, int currentFrame,
                                                   double elapsedMs );
    static bool TryBuildScreenshotAndExitPath( const char* scenePath, char* outPath, size_t outPathSize ) noexcept;
    static SkullbonezCore::Core::SbResult SaveScreenshotBytesAtomic( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                     const char* path, std::span<const uint8_t> bytes );
    static SkullbonezCore::Core::SbResult SaveBackbufferBmp( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                             Rendering::Dx12BackbufferCapture& backend, const char* path );
    static SkullbonezCore::Core::SbResult BuildPngBytes( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                         std::span<const uint8_t> bottomUpBgr, int width, int height,
                                                         std::vector<uint8_t>& output );
    static SkullbonezCore::Core::SbResult SaveBackbufferPng( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                             Rendering::Dx12BackbufferCapture& backend, const char* path );
    static RuntimeCaptureResult TickScreenshots( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                 RunScreenshotState& screenshot, const ScreenshotFrameInput& input,
                                                 CaptureController& capture, Rendering::Dx12BackbufferCapture& backend );
    static RuntimeCaptureResult TickAutoCycle( const AutoCycleCaptureInput& input, AutoCycleCaptureUpdate& update,
                                               CaptureController& capture, Rendering::Dx12BackbufferCapture& backend );
};
} // namespace Runtime
} // namespace SkullbonezCore
