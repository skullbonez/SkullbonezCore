/*
File: SkullbonezSource/Runtime/RuntimeCommandQueue.cpp
Purpose:
  Implements the runtime command queue.

Mental model:
  Commands are ordinary FIFO intent records. The queue does not execute them;
  controllers or Run decide how to apply each command.

Glossary:
  FIFO (First In, First Out): Queue order where the oldest command is consumed
    first.
  Runtime command: Typed intent record emitted by input, UI, or tools.
  Command consumer: Runtime code that applies a queued command to live state.

Invariants:
  - Queue order is observable behavior for same-frame command handling.
  - Queue capacity is fixed for steady gameplay; overflow is a policy failure,
    not a growth path.
  - TryPop moves exactly one command and leaves empty queues unchanged.

Related:
  - SkullbonezSource/Runtime/RuntimeCommandQueue.h
  - SkullbonezSource/Runtime/RunInput.cpp
*/
#include "RuntimeCommandQueue.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
void ReportRuntimeCommandQueueOverflow( int capacity )
{
    std::fprintf( stderr, "FATAL: RuntimeCommandQueue capacity exhausted (capacity=%d).\n", capacity );
    std::fprintf( stdout, "FATAL: RuntimeCommandQueue capacity exhausted (capacity=%d).\n", capacity );
    std::fflush( stderr );
    std::fflush( stdout );
    assert( false && "RuntimeCommandQueue capacity exhausted." );
    std::abort();
}
} // namespace

namespace SkullbonezCore
{
namespace Basics
{
void RuntimeCommandText::Assign( const char* value )
{
    text[0] = '\0';
    if ( !value )
    {
        return;
    }
    strncpy_s( text, sizeof( text ), value, _TRUNCATE );
}


const char* RuntimeCommandText::c_str() const
{
    return text;
}


bool RuntimeCommandText::empty() const
{
    return text[0] == '\0';
}


void RuntimeCommandText::clear()
{
    text[0] = '\0';
}


void RuntimeCommandQueue::Push( RuntimeCommand command )
{
    if ( m_count >= RUNTIME_COMMAND_QUEUE_CAPACITY )
    {
        ReportRuntimeCommandQueueOverflow( RUNTIME_COMMAND_QUEUE_CAPACITY );
    }

    const int tail = ( m_head + m_count ) % RUNTIME_COMMAND_QUEUE_CAPACITY;
    m_commands[tail] = command;
    ++m_count;
}


bool RuntimeCommandQueue::TryPop( RuntimeCommand& outCommand )
{
    if ( m_count <= 0 )
    {
        return false;
    }
    outCommand = m_commands[m_head];
    m_commands[m_head] = RuntimeCommand();
    m_head = ( m_head + 1 ) % RUNTIME_COMMAND_QUEUE_CAPACITY;
    --m_count;
    return true;
}


void RuntimeCommandQueue::Clear()
{
    while ( m_count > 0 )
    {
        m_commands[m_head] = RuntimeCommand();
        m_head = ( m_head + 1 ) % RUNTIME_COMMAND_QUEUE_CAPACITY;
        --m_count;
    }
    m_head = 0;
}


bool RuntimeCommandQueue::Empty() const
{
    return m_count == 0;
}


std::size_t RuntimeCommandQueue::Size() const
{
    return static_cast<std::size_t>( m_count );
}
} // namespace Basics
} // namespace SkullbonezCore
