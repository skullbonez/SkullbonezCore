/*
File: SkullbonezSource/Runtime/Render/RenderPresentationSettings.h
Purpose:
  Defines renderer-owned live presentation policy that survives backend rebuilds.

Summary:
  RuntimeRenderer owns these values alongside its render passes. Scene loading,
  UI, and stress tools may edit them at explicit cold/frame boundaries, while
  physics and audio keep their own state in their domain owners.

Glossary:
  Pipeline sync: Diagnostic mode that forces CPU/GPU synchronization before a
    frame is rendered.
  Tornado visual: Non-deterministic sparse funnel art paired with, but separate
    from, the physics-owned tornado force configuration.

Invariants:
  - These values never change deterministic physics state.
  - Vsync is retained while the backend is absent and applied when it is live.
  - No physics, audio, input, or scene-lifecycle state belongs here.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - SkullbonezSource/Runtime/RuntimeTuning.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

namespace SkullbonezCore
{
namespace Basics
{
struct TornadoVisualSettings
{
    bool enabled = true;
    bool autoEnableWithTornado = true;
    float shellAlpha = 0.14f;
    float dustAlpha = 0.20f;
    float ribbonWidth = 5.5f;
    int ribbonCount = 7;
    int ribbonSegments = 48;
    int particleCount = 96;
    float rotationSpeed = 1.25f;
};

struct RenderPresentationSettings
{
    bool vsyncEnabled = true;
    bool pipelineSyncEnabled = false;
    TornadoVisualSettings tornadoVisual;
};
} // namespace Basics
} // namespace SkullbonezCore
