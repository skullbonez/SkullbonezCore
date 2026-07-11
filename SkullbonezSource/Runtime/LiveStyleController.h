/*
File: SkullbonezSource/Runtime/LiveStyleController.h
Purpose:
  Owns the live style-harness control-folder state.

Mental model:
  The live style controller watches a small opt-in folder for `live.style.json`
  and screenshot requests. It borrows the capture owner after rendering to save
  the requested image and owns the matching harness status transition.

Glossary:
  Control folder: Directory containing live.style.json, capture.txt, and
    status.txt for the style harness.
  Style stamp: Timestamp/size fingerprint used to avoid rereading unchanged
    control files every frame.
  Pending capture: Screenshot path decoded from capture.txt and consumed after
    the current render pass.

Invariants:
  - File paths are fixed when the controller directory is configured.
  - Style polling is style-only; it must not reload scene physics or replace
    runtime-owned bodies.
  - Pending capture text is bounded and cleared after the controller consumes the save
    request, whether the screenshot succeeds or reports a Lane R failure.

Related:
  - SkullbonezSource/Runtime/LiveStyleController.cpp
  - SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "Scene/SceneRuntimeStyle.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Rendering
{
class IRenderCaptureBackend;
}
namespace Basics
{
class CaptureController;
class LiveStyleController
{
  public:
    bool ConfigureDirectory( const char* path );
    void MarkReady();
    void Tick( SceneRuntimeStyleContext context );
    bool HasPendingCapture() const;
    void SavePendingCapture( CaptureController& capture, Rendering::IRenderCaptureBackend& backend );
    const char* PendingScreenshotPath() const;
    void MarkCaptureSaved();
    void MarkCaptureFailed( const char* message );

  private:
    void WriteStatus( const char* status, const char* detail ) const;

    bool m_enabled = false;                 // Polls a small control folder for live style JSON and screenshot requests.
    char m_directory[260] = {};             // Folder containing live.style.json, capture.txt, and status.txt.
    char m_stylePath[300] = {};             // Style descriptor applied without reloading the scene.
    char m_capturePath[300] = {};           // Text command file used to request one screenshot.
    char m_statusPath[300] = {};            // Latest harness status for scripts/humans.
    char m_pendingScreenshotPath[512] = {}; // Screenshot path requested by capture.txt.
    uint64_t m_styleStamp = 0;              // Last applied live.style.json write stamp.
    uint64_t m_captureStamp = 0;            // Last consumed capture.txt write stamp.
    int m_styleApplyCount = 0;              // Successful live style applications.
    int m_captureCount = 0;                 // Successful live screenshots.
    bool m_hasPendingScreenshot = false;    // Capture should run after render/UI this frame.
};
} // namespace Basics
} // namespace SkullbonezCore
