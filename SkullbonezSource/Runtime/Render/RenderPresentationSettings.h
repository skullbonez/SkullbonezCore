/*
File: SkullbonezSource/Runtime/Render/RenderPresentationSettings.h
Purpose:
  Defines renderer-owned live presentation policy that survives backend rebuilds.

Summary:
  RuntimeRenderer owns these values alongside its render passes. Scene loading,
  UI, and stress tools may edit them at explicit cold/frame boundaries, while
  physics keeps its own state in its domain owner.

Glossary:
  Pipeline sync: Diagnostic mode that forces CPU/GPU synchronization before a
    frame is rendered.

Invariants:
  - These values never change deterministic physics state.
  - Vsync is retained while the backend is absent and applied when it is live.
  - No physics, input, or scene-lifecycle state belongs here.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - SkullbonezSource/Runtime/OperatorCommandApplier.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

namespace SkullbonezCore
{
namespace Runtime
{
struct RenderPresentationSettings
{
    bool vsyncEnabled = true;
    bool pipelineSyncEnabled = false;
};
} // namespace Runtime
} // namespace SkullbonezCore
