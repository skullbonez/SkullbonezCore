/*
File: SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h
Purpose:
  Declares replay overlay drawing entry points used by RuntimeRenderHost.

Mental model:
  RuntimeRenderHost still decides pass order, but replay owns the UI drawing
  logic for replay scrubber and cause-tree overlays.

Glossary:
  UI (User Interface): Runtime controls and overlays drawn over the 3D scene.
  RuntimeRenderHost: Runtime rendering coordinator that invokes replay overlay
    drawing after scene rendering.
  Replay overlay: UI draw pass for replay timeline, prediction controls, and
    cause-tree inspection.
  Render context: Borrowed state bundle for one overlay draw call.

Invariants:
  - The context borrows live runtime state for one frame only.
  - Overlay functions must not store references from the context.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderHost.cpp
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#pragma once

#include "ReplayRuntime.h"

#include <vector>

namespace SkullbonezCore::GameObjects
{
class GameModel;
}

namespace SkullbonezCore::Rendering
{
class IRenderCommandContext;
}

namespace SkullbonezCore::Physics
{
class PhysicsBodyStore;
}

namespace SkullbonezCore::Basics::ReplayOverlay
{
struct ReplayOverlayRenderContext
{
    // Lifetime: borrowed from the current UI/text pass; overlay code must not
    // store it after the draw call returns.
    Rendering::IRenderCommandContext& renderCommands;
    ReplayRuntime& replayRuntime;
    const std::vector<GameObjects::GameModel>& models;
    const Physics::PhysicsBodyStore& bodyStore;
    bool editorModeEnabled = false;
    bool uiVisible = false;
    bool uiMinimized = false;
    bool scenePhysicsEnabled = false;
    int screenW = 0;
    int screenH = 0;
    double nowSeconds = 0.0;
};

void RenderReplayScrubberOverlay( const ReplayOverlayRenderContext& context );
void RenderReplayCauseTreeOverlay( const ReplayOverlayRenderContext& context );
} // namespace SkullbonezCore::Basics::ReplayOverlay
