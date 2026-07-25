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

#include "UIDraw.h"

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


void UIDrawList::Flush( const UIDrawContext& draw, float offsetX, float offsetY ) const
{
    for ( int i = 0; i < m_commandCount; ++i )
    {
        const Command& cmd = m_commands[i];
        switch ( cmd.type )
        {
        case CommandType::Rect:
            draw.Rect( cmd.x0 + offsetX, cmd.y0 + offsetY, cmd.w, cmd.h, cmd.r, cmd.g, cmd.b, cmd.a );
            break;
        case CommandType::RoundedRect:
            draw.RoundedRect(
                cmd.x0 + offsetX,
                cmd.y0 + offsetY,
                cmd.w,
                cmd.h,
                cmd.radius,
                cmd.r,
                cmd.g,
                cmd.b,
                cmd.a
            );
            break;
        case CommandType::Triangle:
            draw.Triangle(
                cmd.x0 + offsetX,
                cmd.y0 + offsetY,
                cmd.x1 + offsetX,
                cmd.y1 + offsetY,
                cmd.x2 + offsetX,
                cmd.y2 + offsetY,
                cmd.r,
                cmd.g,
                cmd.b,
                cmd.a
            );
            break;
        case CommandType::Text:
            draw.Text( cmd.x0 + offsetX, cmd.y0 + offsetY, cmd.pxSize, cmd.r, cmd.g, cmd.b, m_text + cmd.textOffset );
            break;
        }
    }
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
    return stats;
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
