/*
File: SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h
Purpose:
  Declares replay overlay drawing entry points used by RuntimeRenderHost.

Mental model:
  RuntimeRenderHost still decides pass order, but replay owns the UI drawing
  logic for replay scrubber and cause-tree overlays.

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

namespace SkullbonezCore::Basics::ReplayOverlay
{
struct ReplayOverlayRenderContext
{
    ReplayRuntime& replayRuntime;
    const std::vector<GameObjects::GameModel>& models;
    bool editorModeEnabled = false;
    bool uiVisible = false;
    bool uiMinimized = false;
    int screenW = 0;
    int screenH = 0;
    double nowSeconds = 0.0;
};

void RenderReplayScrubberOverlay( const ReplayOverlayRenderContext& context );
void RenderReplayCauseTreeOverlay( const ReplayOverlayRenderContext& context );
} // namespace SkullbonezCore::Basics::ReplayOverlay
