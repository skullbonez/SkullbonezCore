/*
File: SkullbonezSource/Runtime/Replay/RunReplayQueryTools.cpp
Purpose:
  Routes replay path-pick results through composition-level prediction and camera side effects.

Mental model:
  ReplayPresentation owns the query and selected-target state. ReplayRuntime
  reacts to its small result value by invalidating prediction or exiting the
  inspection camera; it does not reimplement the pick.

Glossary:
  Path pick: World-ray query that resolves a stable ReplayBodyId target.
  Composition side effect: Cross-owner reaction, such as dirtying prediction
    or asking the camera owner to leave inspection mode.

Invariants:
  - ReplayPresentation is the only mutable path-target owner.
  - A successful target change always invalidates prediction caches.
  - Clearing on miss exits camera focus and clears path/prediction state once.

Related:
  - ReplayPresentation.cpp
  - ReplayRuntime.cpp
*/
#include "ReplayPresentation.h"
#include "ReplayRuntime.h"

using namespace SkullbonezCore::Runtime;

ReplayRuntime::PathPickResult
ReplayRuntime::TryPickPathTarget( const PathPickInput& input,
                                  const SceneEntityStore& entities,
                                  const Physics::PhysicsBodyStore& bodyStore,
                                  const Physics::ColliderStore& colliderStore,
                                  std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords )
{
    const PathPickResult result = m_visualPresentation.TryPickPathTarget( input,
                                                                          entities,
                                                                          bodyStore,
                                                                          colliderStore,
                                                                          presentationRecords,
                                                                          CurrentSolverScrubSample() );
    if ( result.picked )
    {
        ClearPredictionCache();
        MarkPredictionDirty();
    }
    else if ( result.exitInspectionCamera )
    {
        ClearCameraFocusForRestore();
        ClearPathVisualizerState();
    }
    return result;
}


bool ReplayRuntime::RouteWorldPointer( const WorldPointerInput& input )
{
    if ( !input.leftPressed || input.suppressWorldAction || input.editorMode || input.uiWantsNativeCursor ||
         ( !input.controlDown && input.launcherMode ) )
    {
        return false;
    }

    const PathPickResult pickResult =
        TryPickPathTarget( input.pick, input.entities, input.bodyStore, input.colliderStore, input.presentation );
    if ( pickResult.exitInspectionCamera )
    {
        ExitInspectionCamera( input.cameras,
                              input.terrain,
                              input.camera,
                              input.restoreCameraMode,
                              input.attachedCameraFollow,
                              input.directorGrabbed,
                              input.interaction,
                              input.inputRouter );
    }
    return true;
}
