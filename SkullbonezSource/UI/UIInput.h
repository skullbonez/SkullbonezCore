/*
File: SkullbonezSource/UI/UIInput.h
Purpose:
  Implements UI Input widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  The UI is immediate-mode-style: each frame reads engine state, computes hit
  boxes, emits draw commands, and returns requests for the run loop to apply.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIInput.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Common.h"

namespace SkullbonezCore
{
namespace UI
{
namespace InputControl
{

struct UIInputSnapshot
{
    int mouseX = 0;
    int mouseY = 0;
    int wheelDelta = 0;
    bool leftDown = false;
    bool leftPressed = false;
    bool leftReleased = false;
};

UIInputSnapshot CaptureSnapshot( bool previousLeftDown, bool hasMouseOverride, int overrideX, int overrideY );

void CaptureKeyStates( bool keyWasDown[256] );
bool ConsumeKeyPress( bool keyWasDown[256], int virtualKey );
bool IsVirtualKeyDown( int virtualKey );

void BeginMouseCapture( HWND hwnd );
void EndMouseCapture();

} // namespace InputControl
} // namespace UI
} // namespace SkullbonezCore
