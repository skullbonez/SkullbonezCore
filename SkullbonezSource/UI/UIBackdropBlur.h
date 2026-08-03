/*
File: SkullbonezSource/UI/UIBackdropBlur.h
Purpose:
  Draws the optional non-readback backdrop panel behind the in-engine controls.

Summary:
  The backdrop is ordinary UI geometry. It never captures the back buffer; the
  capture/readback renderer capability belongs to screenshot and validation
  paths, not per-frame UI decoration.

Invariants:
  - Backdrop drawing must stay on UIDrawContext so it cannot pull renderer
    capture/readback or resource-factory capabilities into the UI layout path.

Related:
  - SkullbonezSource/UI/UIBackdropBlur.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"
#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{

enum class UIBackdropBlurInvalidationReason : uint8_t
{
    Unknown,
    Visibility,
    WindowState,
    Bounds,
    Content,
    Toggle,
    ResourceReset
};

class UIBackdropBlur
{
  public:
    ~UIBackdropBlur();

    void Draw( const UIDrawContext& draw, const UIRect& bounds, int screenW, int screenH, int currentFrame, double now,
               bool enabled );
    void Invalidate( UIBackdropBlurInvalidationReason reason = UIBackdropBlurInvalidationReason::Unknown );
    void ResetResources();

  private:
    int m_lastScreenW = 0;
    int m_lastScreenH = 0;
    int m_lastX = -1;
    int m_lastY = -1;
    int m_lastW = 0;
    int m_lastH = 0;
    bool m_invalidated = true;
    UIBackdropBlurInvalidationReason m_lastInvalidationReason = UIBackdropBlurInvalidationReason::Unknown;
};

} // namespace UI
} // namespace SkullbonezCore
