/*
File: SkullbonezSource/Runtime/Scene/SceneController.h
Purpose:
  Owns the scene runtime boundary used by the main Run facade.

Mental model:
  SceneController is the narrow API around scene queue and scene-run state.
  Run coordinates broad side effects, while this controller owns the scene
  runtime object that those side effects reference.

Glossary:
  Scene runtime: Current scene state plus queue navigation data.
  Scene queue: Ordered authored scene list, with an empty path selecting the
  generated demo scene.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntime.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/physics-playground-refactor-and-file-prefix-cleanup-plan.md
*/
#pragma once

#include "SceneRuntime.h"

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
class SceneController
{
  public:
    SceneController() = default;
    explicit SceneController( std::vector<std::string> queue );

    RunSceneState& State();
    const RunSceneState& State() const;

    bool HasEntry( int index ) const;
    bool HasCurrentEntry() const;
    const std::string* CurrentPath() const;
    const std::string& PathAt( int index ) const;
    int QueueSize() const;
    int CurrentIndex() const;
    int NextIndex() const;
    const std::vector<std::string>& Queue() const;

    void BeginLoad( int index );
    void MarkManualReset();
    int FindNormalizedPath( const std::string& normalizedPath ) const;
    int FindGeneratedDemo() const;
    int Append( std::string path );
    bool CurrentQueueIsCinematicDeck() const;
    int AdjacentQueueIndex( int direction ) const;

    SceneRuntime& Runtime();
    const SceneRuntime& Runtime() const;

  private:
    SceneRuntime m_runtime; // Scene queue and active scene-run state
};
} // namespace Basics
} // namespace SkullbonezCore
