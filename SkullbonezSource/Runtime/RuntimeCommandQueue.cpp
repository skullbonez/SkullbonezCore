/*
File: SkullbonezSource/Runtime/RuntimeCommandQueue.cpp
Purpose:
  Implements the runtime command queue.

Mental model:
  Commands are ordinary FIFO intent records. The queue does not execute them;
  controllers or Run decide how to apply each command.
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
