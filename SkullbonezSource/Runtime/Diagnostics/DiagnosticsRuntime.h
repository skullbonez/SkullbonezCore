/*
File: SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h
Purpose:
  Owns runtime diagnostics and capture controllers behind one diagnostics boundary.

Mental model:
  DiagnosticsRuntime is the Phase 7 compatibility owner. Capture, perf logs,
  and SkullScope state keep their existing controllers and artifact formats,
  while Run reaches them through one diagnostics runtime member.

Glossary:
  Capture controller: Screenshot trigger and automation state.
  Diagnostics controller: Perf CSV and queryable physics diagnostic state.
  Artifact path: Validation-facing output path that must stay stable.

Invariants:
  - Artifact formatting stays in RuntimeDiagnostics and CaptureSystem.
  - Existing output paths and command-line behavior must not drift here.

Related:
  - SkullbonezSource/Runtime/CaptureController.h
  - SkullbonezSource/Runtime/DiagnosticsController.h
*/
#pragma once

#include "../CaptureController.h"
#include "../DiagnosticsController.h"

namespace SkullbonezCore
{
namespace Basics
{
class DiagnosticsRuntime
{
  public:
    CaptureController& Capture();
    const CaptureController& Capture() const;

    DiagnosticsController& Diagnostics();
    const DiagnosticsController& Diagnostics() const;

  private:
    CaptureController m_capture;         // Screenshot trigger and capture automation
    DiagnosticsController m_diagnostics; // Perf/test logs and queryable physics diagnostic trace
};
} // namespace Basics
} // namespace SkullbonezCore
