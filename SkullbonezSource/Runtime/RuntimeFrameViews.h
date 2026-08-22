/*
File: RuntimeFrameViews.h
Purpose:
  Defines the value-only facts selected for the late runtime UI pass.

Summary:
  Runtime frame work uses concrete operands or direct `Run` coordinator member
  reach. This header retains only the immutable value snapshot shared by the
  GameUI and ImGui presentation paths.

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
} // namespace Runtime
} // namespace SkullbonezCore
