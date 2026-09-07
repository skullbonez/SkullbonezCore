/*
File: RuntimeFrameViews.h
Purpose:
  Defines the value-only facts selected for the late runtime UI pass.

Invariants:
  - UI facts contain no mutable owner references.
  - Stable label pointers name process-lifetime vocabulary.
  - The record is produced and consumed within one frame turn.

Related:
  - Runtime/App/RunFrame.cpp selects the facts.
  - Runtime/Render/UiTextPass.cpp consumes them.
*/

#pragma once

#include <cstdint>

namespace SkullbonezCore
{
namespace Runtime
{
enum class RuntimeGizmoDragKind;
enum class RuntimeInteractionGestureKind;

// Value snapshots selected for one late UI pass. Label pointers name stable
// owner vocabulary; mutable output storage is a separate Render parameter and
// is deliberately not hidden inside this facts record.
struct RuntimeUiTextFrameFacts
{
    uint32_t cameraModeEnabledMask = 0u;
    const char* cameraModeLabel = nullptr;
    const char* launcherFireModeLabel = nullptr;
    bool isLauncherCameraMode = false;
    RuntimeInteractionGestureKind interactionGestureKind {};
    RuntimeGizmoDragKind interactionGizmoKind {};
    float presentationAlpha = 0.0f;
    bool presentationPinned = false;
    double secondsPerFrame = 0.0;
    bool gameUiActive = true;
};

// Immutable publication from the Diagnostics frame-metrics owner. All values
// are copied, so UI, Render, Capture, and Automation cannot retain or mutate
// the clocks and aggregation state that produced them.
struct RuntimeFrameMetricsSnapshot
{
    double secondsPerFrame = 0.0;
    double simulationTotalSeconds = 0.0;
    double sceneElapsedSeconds = 0.0;
    float physicsSeconds = 0.0f;
    float renderSeconds = 0.0f;
    float rollingPhysicsSeconds = 0.0f;
    float rollingRenderSeconds = 0.0f;
    float rollingFrameSeconds = 0.0f;
    float sceneEnergy = 0.0f;
    float cpuFrameWorkMs = 0.0f;
    float gpuFrameWorkMs = 0.0f;
    int uiDrawCalls = 0;
};
} // namespace Runtime
} // namespace SkullbonezCore
