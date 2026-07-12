/*
File: SkullbonezSource/UI/UIState.h
Purpose:
  Implements UI State widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UIState.h implements UI State widgets, layout, drawing, or UI state for the
  in-engine controls. As a public header, keep edits anchored on UI request,
  layout, hit-test, and draw-command flow and on the glossary/invariants
  below.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

struct UIWindowState
{
    bool isVisible = true;
    bool isMinimized = true;
    bool isMaximized = false;
    bool hasAppliedDefaultPlacement = false;

    int x = 34;
    int y = 56;
    int width = 760;
    int height = 540;
    float minimizedWidth = 176.0f;

    int restoreX = 34;
    int restoreY = 56;
    int restoreW = 760;
    int restoreH = 540;

    bool animationActive = false;
    bool animationToMinimized = false;
    double animationStart = 0.0;
    double animationEnd = 0.0;
    UIRect animationFrom;
    UIRect animationTo;
};

struct UIInteractionState
{
    bool isDragging = false;
    bool isResizing = false;
    bool blocksCameraMouse = false;

    int dragOffsetX = 0;
    int dragOffsetY = 0;
    int resizeStartMouseX = 0;
    int resizeStartMouseY = 0;
    int resizeStartW = 0;
    int resizeStartH = 0;
};

} // namespace UI
} // namespace SkullbonezCore
