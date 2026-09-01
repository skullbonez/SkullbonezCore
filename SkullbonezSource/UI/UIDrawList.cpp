/*
File: SkullbonezSource/UI/UIDrawList.cpp
Purpose:
  Implements bounded GameUI command storage, composition, and fingerprints.

Summary:
  Every authoring operation initializes one plain command. Append rebuilds
  commands through the public bounded operations so text offsets and clip
  diagnostics remain owned by the destination list.

Invariants:
  - Capacity exhaustion sets diagnostics and never allocates or reorders.
  - Reused command slots are zero-initialized before semantic hashing.

Related:
  - SkullbonezSource/UI/UIDrawList.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UIDrawList.h"
#include "UIStyle.h"

#include <algorithm>
#include <bit>

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
    m_suppressedClipDepth = 0;
    m_maxClipDepth = 0;

    m_text[0] = '\0';
}


void UIDrawList::AddRect( const UIRect& bounds, const Style::UIColor& color )
{
    Command* cmd = PushCommand();

    if ( !cmd )
    {
        return;
    }

    cmd->type = CommandType::Rect;
    cmd->x0 = bounds.x;
    cmd->y0 = bounds.y;
    cmd->w = bounds.w;
    cmd->h = bounds.h;
    cmd->r = color.r;
    cmd->g = color.g;
    cmd->b = color.b;
    cmd->a = color.a;
}


void UIDrawList::AddRoundedRect( const UIRect& bounds, float radius, const Style::UIColor& color )
{
    Command* cmd = PushCommand();

    if ( !cmd )
    {
        return;
    }

    cmd->type = CommandType::RoundedRect;
    cmd->x0 = bounds.x;
    cmd->y0 = bounds.y;
    cmd->w = bounds.w;
    cmd->h = bounds.h;
    cmd->radius = radius;
    cmd->r = color.r;
    cmd->g = color.g;
    cmd->b = color.b;
    cmd->a = color.a;
}


void UIDrawList::AddTriangle( const UITriangle& triangle, const Style::UIColor& color )
{
    Command* cmd = PushCommand();

    if ( !cmd )
    {
        return;
    }

    cmd->type = CommandType::Triangle;
    cmd->x0 = triangle.first.x;
    cmd->y0 = triangle.first.y;
    cmd->x1 = triangle.second.x;
    cmd->y1 = triangle.second.y;
    cmd->x2 = triangle.third.x;
    cmd->y2 = triangle.third.y;
    cmd->r = color.r;
    cmd->g = color.g;
    cmd->b = color.b;
    cmd->a = color.a;
}


void UIDrawList::AddText( UIPoint position, float pxSize, const Style::UIColor& color, const char* value )
{
    Command* cmd = PushCommand();

    if ( !cmd )
    {
        return;
    }

    cmd->type = CommandType::Text;
    cmd->x0 = position.x;
    cmd->y0 = position.y;
    cmd->pxSize = pxSize;
    cmd->r = color.r;
    cmd->g = color.g;
    cmd->b = color.b;
    cmd->a = 1.0f;
    cmd->textOffset = StoreText( value );
}


void UIDrawList::PushClip( const UIRect& bounds )
{
    if ( m_clipDepth >= MAX_CLIP_DEPTH )
    {
        // Invariant: every rejected push still owns its matching pop. Tracking
        // that logical depth prevents an overflow pair from un-clipping the
        // deepest retained outer rectangle.
        ++m_suppressedClipDepth;
        m_clipOverflow = true;
        return;
    }

    Command* command = PushCommand();

    if ( !command )
    {
        return;
    }

    command->type = CommandType::PushClip;
    command->x0 = bounds.x;
    command->y0 = bounds.y;
    command->w = bounds.w;
    command->h = bounds.h;
    ++m_clipDepth;
    m_maxClipDepth = (std::max)( m_maxClipDepth, m_clipDepth );
}


void UIDrawList::PopClip()
{
    if ( m_suppressedClipDepth > 0 )
    {
        --m_suppressedClipDepth;
        return;
    }

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


void UIDrawList::AddPreviewImage( PreviewTargetId target, const UIRect& bounds, const Style::UIColor& fallbackColor,
                                  const char* fallbackLabel )
{
    Command* command = PushCommand();

    if ( !command )
    {
        return;
    }

    command->type = CommandType::PreviewImage;
    command->preview = target;
    command->x0 = bounds.x;
    command->y0 = bounds.y;
    command->w = bounds.w;
    command->h = bounds.h;
    command->r = fallbackColor.r;
    command->g = fallbackColor.g;
    command->b = fallbackColor.b;
    command->a = fallbackColor.a;
    command->textOffset = StoreText( fallbackLabel );
}


void UIDrawList::Append( const UIDrawList& source, float offsetX, float offsetY )
{
    for ( const Command& command : source.Commands() )
    {
        switch ( command.type )
        {
        case CommandType::Rect:
            AddRect( { command.x0 + offsetX, command.y0 + offsetY, command.w, command.h },
                     { command.r, command.g, command.b, command.a } );

            break;
        case CommandType::RoundedRect:
            AddRoundedRect( { command.x0 + offsetX, command.y0 + offsetY, command.w, command.h }, command.radius,
                            { command.r, command.g, command.b, command.a } );

            break;
        case CommandType::Triangle:
            AddTriangle( { { command.x0 + offsetX, command.y0 + offsetY },
                           { command.x1 + offsetX, command.y1 + offsetY },
                           { command.x2 + offsetX, command.y2 + offsetY } },
                         { command.r, command.g, command.b, command.a } );

            break;
        case CommandType::Text:
            AddText( { command.x0 + offsetX, command.y0 + offsetY }, command.pxSize,
                     { command.r, command.g, command.b, command.a }, source.TextAt( command.textOffset ) );

            break;
        case CommandType::PushClip:
            PushClip( { command.x0 + offsetX, command.y0 + offsetY, command.w, command.h } );
            break;
        case CommandType::PopClip:
            PopClip();
            break;
        case CommandType::PreviewImage:
            AddPreviewImage( command.preview, { command.x0 + offsetX, command.y0 + offsetY, command.w, command.h },
                             { command.r, command.g, command.b, command.a }, source.TextAt( command.textOffset ) );

            break;
        }
    }

    const Stats sourceStats = source.GetStats();
    m_commandOverflow = m_commandOverflow || sourceStats.commandOverflow;
    m_textOverflow = m_textOverflow || sourceStats.textOverflow;
    m_clipOverflow = m_clipOverflow || sourceStats.clipOverflow;
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
    stats.clipOverflow = m_clipOverflow || m_clipDepth != 0 || m_suppressedClipDepth != 0;
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


uint64_t UIDrawList::Fingerprint() const
{
    constexpr uint64_t OFFSET_BASIS = 14695981039346656037ull;
    constexpr uint64_t PRIME = 1099511628211ull;
    uint64_t hash = OFFSET_BASIS;
    auto addByte = [&]( uint8_t value )
    {
        hash ^= value;

        hash *= PRIME;
    };

    auto addUint32 = [&]( uint32_t value )
    {
        addByte( static_cast<uint8_t>( value ) );

        addByte( static_cast<uint8_t>( value >> 8 ) );
        addByte( static_cast<uint8_t>( value >> 16 ) );
        addByte( static_cast<uint8_t>( value >> 24 ) );
    };

    auto addFloat = [&]( float value ) { addUint32( std::bit_cast<uint32_t>( value ) ); };

    auto addText = [&]( const char* value )
    {
        for ( const unsigned char* cursor = reinterpret_cast<const unsigned char*>( value ); *cursor; ++cursor )
        {
            addByte( *cursor );
        }

        addByte( 0 );
    };

    addUint32( static_cast<uint32_t>( m_commandCount ) );

    for ( const Command& command : Commands() )
    {
        addByte( static_cast<uint8_t>( command.type ) );
        addFloat( command.x0 );
        addFloat( command.y0 );
        addFloat( command.x1 );
        addFloat( command.y1 );
        addFloat( command.x2 );
        addFloat( command.y2 );
        addFloat( command.w );
        addFloat( command.h );
        addFloat( command.radius );
        addFloat( command.pxSize );
        addFloat( command.r );
        addFloat( command.g );
        addFloat( command.b );
        addFloat( command.a );
        addUint32( command.preview.catalogIndex );
        addByte( command.preview.valid ? 1u : 0u );

        if ( command.type == CommandType::Text || command.type == CommandType::PreviewImage )
        {
            addText( TextAt( command.textOffset ) );
        }
    }

    return hash;
}


UIDrawList::Command* UIDrawList::PushCommand()
{
    if ( m_commandCount >= MAX_COMMANDS )
    {
        m_commandOverflow = true;
        return nullptr;
    }

    Command& command = m_commands[m_commandCount++];
    command = {};

    return &command;
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

        m_text[MAX_TEXT_BYTES - 1] = '\0';
        return MAX_TEXT_BYTES - 1;
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
