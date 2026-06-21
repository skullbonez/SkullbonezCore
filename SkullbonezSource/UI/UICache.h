/*
File: SkullbonezSource/UI/UICache.h
Purpose:
  Implements UI Cache widgets, layout, drawing, or UI state for the in-engine controls.

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
  - SkullbonezSource/UI/UICache.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "UIDraw.h"
#include "UIDrawList.h"

#include <cstdint>
#include <memory>

namespace SkullbonezCore
{
namespace UI
{

enum UIDirtyFlags : uint32_t
{
    UI_DIRTY_NONE = 0,
    UI_DIRTY_CONTENT = 1u << 0,
    UI_DIRTY_LAYOUT = 1u << 1,
    UI_DIRTY_STYLE = 1u << 2,
    UI_DIRTY_INTERACTION = 1u << 3,
    UI_DIRTY_VIEWPORT = 1u << 4,
    UI_DIRTY_BLUR_SOURCE = 1u << 5,
    UI_DIRTY_POSITION = 1u << 6
};

struct UICacheFrameKey
{
    int screenW = 1;
    int screenH = 1;
    UIRect windowBounds;
    int activeTab = 0;
    float scrollY = 0.0f;
    bool blurEnabled = false;
    uint32_t contentSignature = 0;
    uint32_t styleSignature = 0;
    uint32_t interactionSignature = 0;
};

class UICacheState
{
  public:
    UICacheState();

    void Reset();

    uint32_t BeginFrame( const UICacheFrameKey& key );
    bool CanReplayPositionOnly( const UICacheFrameKey& key ) const;
    void StoreFrame( const UICacheFrameKey& key );

    UIDrawList& MutableDrawList();
    const UIDrawList& DrawList() const;
    uint32_t DirtyFlags() const;
    float ReplayOffsetX( const UICacheFrameKey& key ) const;
    float ReplayOffsetY( const UICacheFrameKey& key ) const;

  private:
    static bool SameFloat( float a, float b );
    static bool SameSize( const UIRect& a, const UIRect& b );
    static bool SamePosition( const UIRect& a, const UIRect& b );

    std::unique_ptr<UIDrawList> m_drawList;
    UICacheFrameKey m_lastKey;
    uint32_t m_dirtyFlags = UI_DIRTY_CONTENT | UI_DIRTY_LAYOUT | UI_DIRTY_STYLE | UI_DIRTY_INTERACTION |
                            UI_DIRTY_VIEWPORT | UI_DIRTY_BLUR_SOURCE;
    bool m_hasFrame = false;
};

} // namespace UI
} // namespace SkullbonezCore
