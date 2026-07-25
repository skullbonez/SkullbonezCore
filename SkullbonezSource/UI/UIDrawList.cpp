/*
File: SkullbonezSource/UI/UIDrawList.cpp
Purpose:
  Implements UI DrawList widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UIDrawList.cpp implements UI DrawList widgets, layout, drawing, or UI state
  for the in-engine controls. As an implementation unit, keep edits anchored
  on UI request, layout, hit-test, and draw-command flow and on the
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
  - SkullbonezSource/UI/UIDrawList.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UIDrawList.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace UI
{

void UIDrawList::Clear()
{
    m_commandCount = 0;
    m_textBytes = 0;
    m_commandOverflow = false;
    m_textOverflow = false;
    m_clipOverflow = false;
    m_clipDepth = 0;
    m_maxClipDepth = 0;
    if ( MAX_TEXT_BYTES > 0 )
    {
        m_text[0] = '\0';
    }
}


void UIDrawList::AddRect( float x, float y, float w, float h, float r, float g, float b, float a )
{
    Command* cmd = PushCommand();
    if ( !cmd )
    {
        return;
    }

    cmd->type = CommandType::Rect;
    cmd->x0 = x;
    cmd->y0 = y;
    cmd->w = w;
    cmd->h = h;
    cmd->r = r;
    cmd->g = g;
    cmd->b = b;
    cmd->a = a;
}


void UIDrawList::AddRoundedRect( float x, float y, float w, float h, float radius, float r, float g, float b, float a )
{
    Command* cmd = PushCommand();
    if ( !cmd )
    {
        return;
    }

    cmd->type = CommandType::RoundedRect;
    cmd->x0 = x;
    cmd->y0 = y;
    cmd->w = w;
    cmd->h = h;
    cmd->radius = radius;
    cmd->r = r;
    cmd->g = g;
    cmd->b = b;
    cmd->a = a;
}


void UIDrawList::
    AddTriangle( float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a )
{
    Command* cmd = PushCommand();
    if ( !cmd )
    {
        return;
    }

    cmd->type = CommandType::Triangle;
    cmd->x0 = x0;
    cmd->y0 = y0;
    cmd->x1 = x1;
    cmd->y1 = y1;
    cmd->x2 = x2;
    cmd->y2 = y2;
    cmd->r = r;
    cmd->g = g;
    cmd->b = b;
    cmd->a = a;
}


void UIDrawList::AddText( float x, float y, float pxSize, float r, float g, float b, const char* value )
{
    Command* cmd = PushCommand();
    if ( !cmd )
    {
        return;
    }

    cmd->type = CommandType::Text;
    cmd->x0 = x;
    cmd->y0 = y;
    cmd->pxSize = pxSize;
    cmd->r = r;
    cmd->g = g;
    cmd->b = b;
    cmd->a = 1.0f;
    cmd->textOffset = StoreText( value );
}


void UIDrawList::PushClip( float x, float y, float w, float h )
{
    if ( m_clipDepth >= MAX_CLIP_DEPTH )
    {
        m_clipOverflow = true;
        return;
    }

    Command* command = PushCommand();
    if ( !command )
    {
        return;
    }
    command->type = CommandType::PushClip;
    command->x0 = x;
    command->y0 = y;
    command->w = w;
    command->h = h;
    ++m_clipDepth;
    m_maxClipDepth = (std::max)( m_maxClipDepth, m_clipDepth );
}


void UIDrawList::PopClip()
{
    if ( m_clipDepth <= 0 )
    {
        m_clipOverflow = true;
        return;
    }
    Command* command = PushCommand();
    if ( !command )
    {
        return;
    }
    command->type = CommandType::PopClip;
    --m_clipDepth;
}


void UIDrawList::AddPreviewImage(
    PreviewTargetId target,
    float x,
    float y,
    float w,
    float h,
    float fallbackR,
    float fallbackG,
    float fallbackB,
    float fallbackA,
    const char* fallbackLabel
)
{
    Command* command = PushCommand();
    if ( !command )
    {
        return;
    }
    command->type = CommandType::PreviewImage;
    command->preview = target;
    command->x0 = x;
    command->y0 = y;
    command->w = w;
    command->h = h;
    command->r = fallbackR;
    command->g = fallbackG;
    command->b = fallbackB;
    command->a = fallbackA;
    command->textOffset = StoreText( fallbackLabel );
}


bool UIDrawList::Empty() const
{
    return m_commandCount == 0;
}


UIDrawList::Stats UIDrawList::GetStats() const
{
    Stats stats;
    stats.commandCount = m_commandCount;
    stats.textBytes = m_textBytes;
    stats.commandOverflow = m_commandOverflow;
    stats.textOverflow = m_textOverflow;
    stats.clipOverflow = m_clipOverflow || m_clipDepth != 0;
    stats.maxClipDepth = m_maxClipDepth;
    return stats;
}


std::span<const UIDrawList::Command> UIDrawList::Commands() const
{
    return { m_commands, static_cast<size_t>( m_commandCount ) };
}


const char* UIDrawList::TextAt( int offset ) const
{
    return offset >= 0 && offset < m_textBytes ? m_text + offset : "";
}


UIDrawList::Command* UIDrawList::PushCommand()
{
    if ( m_commandCount >= MAX_COMMANDS )
    {
        m_commandOverflow = true;
        return nullptr;
    }
    return &m_commands[m_commandCount++];
}


int UIDrawList::StoreText( const char* value )
{
    if ( !value )
    {
        value = "";
    }

    const int offset = m_textBytes;
    int remaining = MAX_TEXT_BYTES - m_textBytes;
    if ( remaining <= 1 )
    {
        m_textOverflow = true;
        if ( MAX_TEXT_BYTES > 0 )
        {
            m_text[MAX_TEXT_BYTES - 1] = '\0';
            return MAX_TEXT_BYTES - 1;
        }
        return 0;
    }

    int copied = 0;
    while ( copied < remaining - 1 && value[copied] != '\0' )
    {
        m_text[offset + copied] = value[copied];
        ++copied;
    }
    if ( value[copied] != '\0' )
    {
        m_textOverflow = true;
    }
    m_text[offset + copied] = '\0';
    m_textBytes += copied + 1;
    return offset;
}

} // namespace UI
} // namespace SkullbonezCore
