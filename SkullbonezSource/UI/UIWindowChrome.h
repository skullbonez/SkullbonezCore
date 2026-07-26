/*
File: SkullbonezSource/UI/UIWindowChrome.h
Purpose:
  Implements UI WindowChrome widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UIWindowChrome.h implements UI WindowChrome widgets, layout, drawing, or UI
  state for the in-engine controls. As a public header, keep edits anchored on
  UI request, layout, hit-test, and draw-command flow and on the
  glossary/invariants below.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIWindowChrome.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "UIDraw.h"
#include "UIState.h"

#include <cstddef>

namespace SkullbonezCore
{
namespace UI
{

struct InGameUIFrameData;

namespace Chrome
{

struct TitleButtonRects
{
    UIRect minimize;
    UIRect maximize;
    UIRect close;
};

void BuildWindowTitle( const InGameUIFrameData& data, char* out, size_t outSize );
void FitTitleText( char* text, size_t textSize, float fontSize, float maxWidth );

UIRect WindowRect( const UIWindowState& window );
void ApplyDefaultWindowPlacement( UIWindowState& window, int screenW, int screenH );
void ClampWindowToScreen( UIWindowState& window, int screenW, int screenH, int minW, int minH, int margin );
bool SetMaximized( UIWindowState& window, bool maximized, int screenW, int screenH, double now );

void BeginWindowAnimation( UIWindowState& window, const UIRect& from, const UIRect& to, double now, bool toMinimized );
UIRect CurrentWindowRect( UIWindowState& window, double now );

TitleButtonRects GetTitleButtonRects( const UIRect& windowBounds );
bool IsResizeHotspot( const UIRect& windowBounds, int mouseX, int mouseY );

void DrawWindowAnimationShell( const UIDrawContext& draw, const UIRect& bounds );
void DrawMinimizedWindow( const UIDrawContext& draw, const UIRect& minimized, const char* titleText );
void DrawWindowFrame( const UIDrawContext& draw,
                      const UIRect& bounds,
                      float titleH,
                      float tabH,
                      bool blurEnabled,
                      const char* titleText );
void DrawTitleButtons( const UIDrawContext& draw,
                       const TitleButtonRects& buttons,
                       bool isMaximized,
                       int mouseX,
                       int mouseY );

} // namespace Chrome
} // namespace UI
} // namespace SkullbonezCore
