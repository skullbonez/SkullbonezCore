/*
File: SkullbonezSource/Runtime/Scene/SceneController.cpp
Purpose:
  Implements the scene runtime controller pass-throughs.

Mental model:
  The controller deliberately starts as a behavior-preserving boundary. It
  provides a named ownership point for scene state before deeper scene-loading
  side effects move out of RunScene.cpp.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/SceneRuntime.cpp
*/
#include "SceneController.h"

#include <utility>

namespace SkullbonezCore
{
namespace Basics
{
SceneController::SceneController( std::vector<std::string> queue ) : m_runtime( std::move( queue ) )
{
}


RunSceneState& SceneController::State()
{
    return m_runtime.State();
}


const RunSceneState& SceneController::State() const
{
    return m_runtime.State();
}


bool SceneController::HasEntry( int index ) const
{
    return m_runtime.HasEntry( index );
}


bool SceneController::HasCurrentEntry() const
{
    return m_runtime.HasCurrentEntry();
}


const std::string* SceneController::CurrentPath() const
{
    return m_runtime.CurrentPath();
}


const std::string& SceneController::PathAt( int index ) const
{
    return m_runtime.PathAt( index );
}


int SceneController::QueueSize() const
{
    return m_runtime.QueueSize();
}


int SceneController::CurrentIndex() const
{
    return m_runtime.CurrentIndex();
}


int SceneController::NextIndex() const
{
    return m_runtime.NextIndex();
}


const std::vector<std::string>& SceneController::Queue() const
{
    return m_runtime.Queue();
}


void SceneController::BeginLoad( int index )
{
    m_runtime.BeginLoad( index );
}


void SceneController::MarkManualReset()
{
    m_runtime.MarkManualReset();
}


int SceneController::FindNormalizedPath( const std::string& normalizedPath ) const
{
    return m_runtime.FindNormalizedPath( normalizedPath );
}


int SceneController::FindGeneratedDemo() const
{
    return m_runtime.FindGeneratedDemo();
}


int SceneController::Append( std::string path )
{
    return m_runtime.Append( std::move( path ) );
}


bool SceneController::CurrentQueueIsCinematicDeck() const
{
    return m_runtime.CurrentQueueIsCinematicDeck();
}


int SceneController::AdjacentQueueIndex( int direction ) const
{
    return m_runtime.AdjacentQueueIndex( direction );
}


SceneRuntime& SceneController::Runtime()
{
    return m_runtime;
}


const SceneRuntime& SceneController::Runtime() const
{
    return m_runtime;
}
} // namespace Basics
} // namespace SkullbonezCore
