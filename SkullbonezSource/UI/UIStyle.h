/*
File: SkullbonezSource/UI/UIStyle.h
Purpose:
  Implements UI Style widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UIStyle.h implements UI Style widgets, layout, drawing, or UI state for the
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
  - SkullbonezSource/UI/UIStyle.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

namespace SkullbonezCore
{
namespace UI
{
namespace Style
{

struct UIColor
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct FooterToggleStyle
{
    float labelTextSize = 10.5f;
    UIColor label;
    float switchW = 28.0f;
    float switchH = 14.0f;
    float knobW = 10.0f;
    float knobH = 10.0f;
};

struct UIPalette
{
    UIColor window;
    UIColor windowRaised;
    UIColor windowSubtle;
    UIColor control;
    UIColor controlHover;
    UIColor textPrimary;
    UIColor textSecondary;
    UIColor textMuted;
    UIColor border;
    UIColor innerBorder;
    UIColor lineSoft;
    UIColor shadow;
    UIColor accent;
    UIColor accentStrong;
    UIColor warningAccent;
};

struct UIRadii
{
    float window = 12.0f;
    float control = 8.0f;
    float smallButton = 7.0f;
    float switchPill = 999.0f;
};

struct UITextStyle
{
    float titlePx = 13.0f;
    float tabPx = 11.0f;
    float labelPx = 10.5f;
    float sectionPx = 13.0f;
    float valuePx = 10.5f;
};

struct UIControlStyle
{
    float sliderTrackHeight = 6.0f;
    float switchW = 30.0f;
    float switchH = 16.0f;
};

const UIPalette& Palette();
const UIRadii& Radii();
const UITextStyle& Text();
const UIControlStyle& Control();
const UIColor& Accent();
const UIColor& AccentCyan();
const FooterToggleStyle& FooterToggle();

} // namespace Style
} // namespace UI
} // namespace SkullbonezCore
