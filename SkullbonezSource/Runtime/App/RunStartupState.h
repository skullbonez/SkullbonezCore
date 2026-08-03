/*
File: SkullbonezSource/Runtime/App/RunStartupState.h
Purpose:
  Defines startup-only runtime capacity and thread defaults.

Summary:
  Startup state is a cold snapshot of engine.cfg values that must survive later
  scene reloads. Generated demo rebuilds use it to restore model capacity and
  worker-thread policy after scene files or UI controls temporarily override
  those values.

Glossary:
  Startup capacity: Game-model capacity captured before scene loading mutates
    the active config.
  Worker-thread policy: The engine.cfg thread count restored when generated
    scenes are rebuilt.

Invariants:
  - Values are copied from config at process startup and then treated as the
    reset baseline for scene reload paths.
  - Capacity is clamped to the engine model-storage budget before owners reserve
    runtime arrays.

Related:
  - SkullbonezSource/Runtime/App/Run.cpp
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
*/
#pragma once

#include "../../Core/Common.h"
#include "../../Core/Config.h"

namespace SkullbonezCore
{
namespace Runtime
{
struct RunStartupState
{
    int sceneObjectCapacity = SkullbonezCore::Scene::Capacity::DEFAULT_SCENE_OBJECT_CAPACITY;
    int workerThreads = -1;

    void ApplyStartupConfig( const SkullbonezCore::Core::EngineConfig& config ); // Captures startup-only capacity/thread policy from config.
};
} // namespace Runtime
} // namespace SkullbonezCore
