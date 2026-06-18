/*
File: SkullbonezSource/SkullbonezSceneRuntime.cpp
Purpose:
  Owns scene queue and scene-run state for the application runtime.

Mental model:
  SceneRuntime is intentionally narrow. It owns queue/index bookkeeping and
  scene-run state, while SkullbonezRun still performs the current object,
  camera, terrain, UI, and renderer side effects around scene loads.

Glossary:
  Scene queue: Ordered list of authored scene paths, where an empty path means
  the generated demo scene.
  Scene-run state: Counters, flags, and overrides that describe the currently
  loaded scene.

Related:
  - SkullbonezSource/SkullbonezSceneRuntime.h
  - Agentic/Reference/runtime-reference.md
*/
#include "SkullbonezSceneRuntime.h"

#include <algorithm>
#include <cstring>
#include <utility>

using namespace SkullbonezCore::Basics;

namespace
{
const char* FileNameFromPath( const char* path )
{
    if ( !path )
    {
        return "";
    }

    const char* slash = strrchr( path, '/' );
    const char* backslash = strrchr( path, '\\' );
    const char* separator = slash;
    if ( backslash && ( !separator || backslash > separator ) )
    {
        separator = backslash;
    }
    return separator ? separator + 1 : path;
}

std::string NormalizeScenePath( const std::string& path )
{
    std::string normalized = path;
    std::replace( normalized.begin(), normalized.end(), '\\', '/' );
    return normalized;
}

bool IsCineScenePath( const std::string& path )
{
    const char* name = FileNameFromPath( path.c_str() );
    return strncmp( name, "concept_", 8 ) == 0 ||
           strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr ||
           strstr( name, "cine_" ) == name;
}
} // namespace

SceneRuntime::SceneRuntime( std::vector<std::string> queue )
    : m_queue( std::move( queue ) )
{
}


RunSceneState& SceneRuntime::State()
{
    return m_state;
}


const RunSceneState& SceneRuntime::State() const
{
    return m_state;
}


bool SceneRuntime::HasEntry( int index ) const
{
    return index >= 0 && index < static_cast<int>( m_queue.size() );
}


bool SceneRuntime::HasCurrentEntry() const
{
    return HasEntry( m_state.currentSceneIndex );
}


const std::string* SceneRuntime::CurrentPath() const
{
    return HasCurrentEntry() ? &m_queue[m_state.currentSceneIndex] : nullptr;
}


const std::string& SceneRuntime::PathAt( int index ) const
{
    return m_queue[index];
}


int SceneRuntime::QueueSize() const
{
    return static_cast<int>( m_queue.size() );
}


int SceneRuntime::CurrentIndex() const
{
    return m_state.currentSceneIndex;
}


int SceneRuntime::NextIndex() const
{
    return m_state.currentSceneIndex + 1;
}


const std::vector<std::string>& SceneRuntime::Queue() const
{
    return m_queue;
}


void SceneRuntime::BeginLoad( int index )
{
    m_state.currentSceneIndex = index;
    ++m_state.loadCount;
}


void SceneRuntime::MarkManualReset()
{
    ++m_state.manualResetCount;
}


int SceneRuntime::FindNormalizedPath( const std::string& normalizedPath ) const
{
    for ( int i = 0; i < QueueSize(); ++i )
    {
        if ( NormalizeScenePath( m_queue[i] ) == normalizedPath )
        {
            return i;
        }
    }
    return -1;
}


int SceneRuntime::FindGeneratedDemo() const
{
    for ( int i = 0; i < QueueSize(); ++i )
    {
        if ( m_queue[i].empty() )
        {
            return i;
        }
    }
    return -1;
}


int SceneRuntime::Append( std::string path )
{
    m_queue.push_back( std::move( path ) );
    return QueueSize() - 1;
}


bool SceneRuntime::CurrentQueueIsCinematicDeck() const
{
    if ( !HasCurrentEntry() || m_queue.size() <= 1 )
    {
        return false;
    }
    for ( const std::string& queuedPath : m_queue )
    {
        if ( queuedPath.empty() || !IsCineScenePath( queuedPath ) )
        {
            return false;
        }
    }
    return true;
}


int SceneRuntime::AdjacentQueueIndex( int direction ) const
{
    const int queueCount = QueueSize();
    if ( queueCount <= 0 )
    {
        return -1;
    }
    return ( m_state.currentSceneIndex + ( direction < 0 ? -1 : 1 ) + queueCount ) % queueCount;
}
