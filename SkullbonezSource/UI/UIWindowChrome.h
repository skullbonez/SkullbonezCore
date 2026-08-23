/*
File: SkullbonezSource/UI/UIWindowChrome.h
Purpose:
  Declares window placement, clamping, title fitting, animation, and chrome
  drawing.

Summary:
  Owns window placement, clamping,
  maximize/minimize animation, title controls, and frame drawing.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIWindowChrome.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"
#include "UIState.h"

#include <cstddef>

namespace SkullbonezCore
{
namespace UI
{

namespace Chrome
{

struct TitleButtonRects
{
    UIRect minimize;
    UIRect maximize;
    UIRect close;
};

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
void DrawWindowFrame( const UIDrawContext& draw, const UIRect& bounds, float titleH, float tabH, bool blurEnabled,
                      const char* titleText );
void DrawTitleButtons( const UIDrawContext& draw, const TitleButtonRects& buttons, bool isMaximized, int mouseX,
                       int mouseY );

} // namespace Chrome
} // namespace UI
} // namespace SkullbonezCore
