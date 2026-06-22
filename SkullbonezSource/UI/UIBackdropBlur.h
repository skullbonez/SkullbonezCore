/*
File: SkullbonezSource/UI/UIBackdropBlur.h
Purpose:
  Implements UI BackdropBlur widgets, layout, drawing, or UI state for the in-engine controls.

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
  - SkullbonezSource/UI/UIBackdropBlur.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "UIDraw.h"
#include "../Rendering/IShader.h"
#include <cstdint>
#include <memory>
#include <vector>

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

    void Draw( const UIDrawContext& draw,
               const UIRect& bounds,
               int screenW,
               int screenH,
               int currentFrame,
               double now,
               bool enabled );
    void Invalidate( UIBackdropBlurInvalidationReason reason = UIBackdropBlurInvalidationReason::Unknown );
    void ResetResources();
    UIBackdropBlurInvalidationReason LastInvalidationReason() const;

  private:
    void EnsureDrawResources();
    void RefreshSourceTexture( const UIRect& bounds, int screenW, int screenH );

    std::unique_ptr<Rendering::IShader> m_shader;
    uint32_t m_dynamicVB = 0;
    uint32_t m_texture = 0;
    std::vector<uint8_t> m_sourcePixels;

    int m_textureW = 0;
    int m_textureH = 0;
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
