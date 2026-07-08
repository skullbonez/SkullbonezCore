/*
File: SkullbonezSource/Runtime/Scene/SceneController.cpp
Purpose:
  Implements the scene runtime controller pass-throughs.

Mental model:
  The controller deliberately starts as a behavior-preserving boundary. It
  provides a named ownership point for scene state before deeper scene-loading
  side effects move out of RunScene.cpp.

Glossary:
  Scene runtime: Mutable per-scene queue, completion, and automation state.
  Scene queue: Ordered list of authored scenes or demo entries to run.
  Pass-through boundary: Wrapper that names ownership before moving behavior.

Invariants:
  - Controller accessors must preserve the existing SceneRuntime semantics.
  - No scene load side effects live here yet; RunScene.cpp still applies them.

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


RunSceneBrowserState& SceneController::Browser()
{
    // Invariant: Scene browser arrays live for the whole run so UI/render host
    // name-pointer views remain stable until the next explicit browser refresh.
    return m_browser;
}


const RunSceneBrowserState& SceneController::Browser() const
{
    return m_browser;
}


RunSceneUIOverrideState& SceneController::UIOverrides()
{
    return m_uiOverrides;
}


const RunSceneUIOverrideState& SceneController::UIOverrides() const
{
    return m_uiOverrides;
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


void SceneController::RecordLifecycleEvent( SceneRuntimeLifecycleEvent event )
{
    m_runtime.RecordLifecycleEvent( event );
}


SceneRuntimeLifecycleEvent SceneController::LastLifecycleEvent() const
{
    return m_runtime.LastLifecycleEvent();
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


std::vector<RunRequiredContactState>& SceneController::RequiredContacts()
{
    return m_runtime.RequiredContacts();
}


const std::vector<RunRequiredContactState>& SceneController::RequiredContacts() const
{
    return m_runtime.RequiredContacts();
}


std::vector<RunRequiredBroadphaseXCellsState>& SceneController::RequiredBroadphaseXCells()
{
    return m_runtime.RequiredBroadphaseXCells();
}


const std::vector<RunRequiredBroadphaseXCellsState>& SceneController::RequiredBroadphaseXCells() const
{
    return m_runtime.RequiredBroadphaseXCells();
}


void SceneController::ClearRequiredAutomationGates()
{
    m_runtime.ClearRequiredAutomationGates();
}


void SceneController::UpdateRequiredContacts( SkullbonezCore::GameObjects::GameModelCollection& models,
                                              float contactEpsilon )
{
    m_runtime.UpdateRequiredContacts( models, contactEpsilon );
}


bool SceneController::RequiredContactsComplete() const
{
    return m_runtime.RequiredContactsComplete();
}


void SceneController::UpdateRequiredBroadphaseXCells(
    const Math::CollisionDetection::SpatialGrid::ActiveCell* activeCells,
    int activeCellCount )
{
    m_runtime.UpdateRequiredBroadphaseXCells( activeCells, activeCellCount );
}


bool SceneController::RequiredBroadphaseXCellsComplete() const
{
    return m_runtime.RequiredBroadphaseXCellsComplete();
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
