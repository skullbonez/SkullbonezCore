/*
File: SkullbonezSource/Runtime/Startup/RunStartupState.cpp
Purpose:
  Captures the cold process defaults restored by later scene rebuilds.

Summary:
  Startup owns the immutable reset baseline. Scene and Capture consume this
  detached value without reaching back into the App composition root.

Invariants:
  - Scene capacity is clamped before any runtime owner reserves storage.
  - Worker-thread policy preserves the configured sentinel and is never
    recomputed from live scene state.

Related:
  - SkullbonezSource/Runtime/Startup/RunStartupState.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
*/
#include "RunStartupState.h"

#include <algorithm>

using namespace SkullbonezCore::Runtime;

void RunStartupState::ApplyStartupConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    sceneObjectCapacity = std::clamp( config.runtimeCapacity.sceneObjectCapacity, 1,
                                      SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    workerThreads = config.runtimeCapacity.workerThreads;
}
