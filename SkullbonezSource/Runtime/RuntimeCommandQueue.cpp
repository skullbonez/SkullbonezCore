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
  - TryPop moves exactly one command and leaves empty queues unchanged.

Related:
  - SkullbonezSource/Runtime/RuntimeCommandQueue.h
  - SkullbonezSource/Runtime/RunInput.cpp
*/
#include "RuntimeCommandQueue.h"

#include <utility>

namespace SkullbonezCore
{
namespace Basics
{
void RuntimeCommandQueue::Push( RuntimeCommand command )
{
    m_commands.push_back( std::move( command ) );
}


bool RuntimeCommandQueue::TryPop( RuntimeCommand& outCommand )
{
    if ( m_commands.empty() )
    {
        return false;
    }
    outCommand = std::move( m_commands.front() );
    m_commands.pop_front();
    return true;
}


void RuntimeCommandQueue::Clear()
{
    m_commands.clear();
}


bool RuntimeCommandQueue::Empty() const
{
    return m_commands.empty();
}


std::size_t RuntimeCommandQueue::Size() const
{
    return m_commands.size();
}
} // namespace Basics
} // namespace SkullbonezCore
