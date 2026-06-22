/*
File: SkullbonezSource/UI/UICache.cpp
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
  - SkullbonezSource/UI/UICache.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UICache.h"

#include <cmath>

namespace SkullbonezCore
{
namespace UI
{

UICacheState::UICacheState() : m_drawList( std::make_unique<UIDrawList>() )
{
}


void UICacheState::Reset()
{
    m_drawList->Clear();
    m_lastKey = {};
    m_dirtyFlags = UI_DIRTY_CONTENT | UI_DIRTY_LAYOUT | UI_DIRTY_STYLE | UI_DIRTY_INTERACTION | UI_DIRTY_VIEWPORT |
                   UI_DIRTY_BLUR_SOURCE;
    m_hasFrame = false;
}


uint32_t UICacheState::BeginFrame( const UICacheFrameKey& key )
{
    uint32_t flags = UI_DIRTY_NONE;
    if ( !m_hasFrame )
    {
        flags = UI_DIRTY_CONTENT | UI_DIRTY_LAYOUT | UI_DIRTY_STYLE | UI_DIRTY_INTERACTION | UI_DIRTY_VIEWPORT |
                UI_DIRTY_BLUR_SOURCE;
    }
    else
    {
        if ( key.screenW != m_lastKey.screenW || key.screenH != m_lastKey.screenH )
        {
            flags |= UI_DIRTY_VIEWPORT;
        }
        if ( !SameSize( key.windowBounds, m_lastKey.windowBounds ) )
        {
            flags |= UI_DIRTY_LAYOUT;
        }
        if ( !SamePosition( key.windowBounds, m_lastKey.windowBounds ) )
        {
            flags |= UI_DIRTY_POSITION;
            if ( key.blurEnabled )
            {
                flags |= UI_DIRTY_BLUR_SOURCE;
            }
        }
        if ( key.activeTab != m_lastKey.activeTab || !SameFloat( key.scrollY, m_lastKey.scrollY ) ||
             key.contentSignature != m_lastKey.contentSignature )
        {
            flags |= UI_DIRTY_CONTENT;
        }
        if ( key.styleSignature != m_lastKey.styleSignature )
        {
            flags |= UI_DIRTY_STYLE;
        }
        if ( key.interactionSignature != m_lastKey.interactionSignature )
        {
            flags |= UI_DIRTY_INTERACTION;
        }
        if ( key.blurEnabled != m_lastKey.blurEnabled )
        {
            flags |= UI_DIRTY_BLUR_SOURCE;
        }
    }

    m_dirtyFlags = flags;
    return m_dirtyFlags;
}


bool UICacheState::CanReplayPositionOnly( const UICacheFrameKey& key ) const
{
    return m_hasFrame && !m_drawList->Empty() && m_dirtyFlags == UI_DIRTY_POSITION &&
           key.screenW == m_lastKey.screenW && key.screenH == m_lastKey.screenH &&
           SameSize( key.windowBounds, m_lastKey.windowBounds ) && key.activeTab == m_lastKey.activeTab &&
           SameFloat( key.scrollY, m_lastKey.scrollY ) && key.blurEnabled == m_lastKey.blurEnabled &&
           key.contentSignature == m_lastKey.contentSignature && key.styleSignature == m_lastKey.styleSignature &&
           key.interactionSignature == m_lastKey.interactionSignature;
}


void UICacheState::StoreFrame( const UICacheFrameKey& key )
{
    m_lastKey = key;
    m_dirtyFlags = UI_DIRTY_NONE;
    m_hasFrame = true;
}


UIDrawList& UICacheState::MutableDrawList()
{
    return *m_drawList;
}


const UIDrawList& UICacheState::DrawList() const
{
    return *m_drawList;
}


uint32_t UICacheState::DirtyFlags() const
{
    return m_dirtyFlags;
}


float UICacheState::ReplayOffsetX( const UICacheFrameKey& key ) const
{
    return key.windowBounds.x - m_lastKey.windowBounds.x;
}


float UICacheState::ReplayOffsetY( const UICacheFrameKey& key ) const
{
    return key.windowBounds.y - m_lastKey.windowBounds.y;
}


bool UICacheState::SameFloat( float a, float b )
{
    return std::fabs( a - b ) < 0.001f;
}


bool UICacheState::SameSize( const UIRect& a, const UIRect& b )
{
    return SameFloat( a.w, b.w ) && SameFloat( a.h, b.h );
}


bool UICacheState::SamePosition( const UIRect& a, const UIRect& b )
{
    return SameFloat( a.x, b.x ) && SameFloat( a.y, b.y );
}

} // namespace UI
} // namespace SkullbonezCore
