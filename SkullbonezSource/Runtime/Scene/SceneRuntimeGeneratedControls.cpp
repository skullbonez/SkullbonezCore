/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.cpp
Purpose:
  Implements live generated-scene rebuild helpers outside Run.

Mental model:
  Generated-scene UI rebuilds clear the active generated objects, reset fixed
  step simulation state, reseed the deterministic setup path, and request the
  caller-owned replay/profiler resets that still sit on Run.

Glossary:
  Generated override: UI-selected model count or solver ball/box counts.
  Follow-up action: Returned flags that tell Run to reset replay and profiler
    state after rebuilding generated objects.
  Model capacity: Maximum active model count that generated rebuilds must obey.

Invariants:
  - Rebuilds clear generated runtime state before reseeding object setup.
  - Replay/profiler resets are returned as actions, not performed here.
  - Camera tracking must be clamped after model-count changes.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/run-composition-root-shrink-plan.md
*/
#include "SceneRuntimeGeneratedControls.h"
#include "SceneController.h"
#include "../SimulationController.h"
#include "../Tools/RuntimeTools.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Rendering/IRenderBackend.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
SceneRuntimeGeneratedControlAction RequestReplayAndProfileReset()
{
    SceneRuntimeGeneratedControlAction action;
    action.resetReplayTimeline = true;
    action.scheduleProfileReset = true;
    return action;
}

void ResetGeneratedRuntimeState( SceneRuntimeGeneratedControlContext context )
{
    // Hazard: Generated rebuilds destroy model/render state. Flush GPU work
    // first, then clear objects and reset simulation/tool state together.
    if ( context.renderer )
    {
        context.renderer->FlushGPU();
    }
    context.models.Clear();
    context.tools.ClearRayCastTestLines();
    context.simulation.Reset();
    context.scene.currentFrame = 0;
    context.scene.isTestComplete = false;
}

SceneGeneratedModelContext BuildGeneratedModelContext( SceneRuntimeGeneratedControlContext context )
{
    return SceneGeneratedModelContext{ context.scene,
                                       context.config,
                                       context.world,
                                       context.terrain,
                                       context.models,
                                       context.models.GetPhysicsEngine(),
                                       context.objectTypeOverride };
}
} // namespace

SceneRuntimeGeneratedControlAction ApplyUIModelCountOverride( SceneRuntimeGeneratedControlContext context, int count )
{
    // Invariant: A model-count override is mutually exclusive with solver exact
    // ball/box overrides; only one generated setup mode owns the rebuild.
    context.uiOverrides.modelCountOverride = std::clamp( count, 0, context.modelCapacity );
    context.uiOverrides.solverBallCountOverride = -1;
    context.uiOverrides.solverBoxCountOverride = -1;
    if ( !context.controller.HasCurrentEntry() )
    {
        return SceneRuntimeGeneratedControlAction{};
    }

    ResetGeneratedRuntimeState( context );
    if ( context.uiOverrides.modelCountOverride <= 0 )
    {
        context.scene.modelCount = 0;
        context.camera.trackBallIndex = -1;
        return RequestReplayAndProfileReset();
    }

    const unsigned int seed = context.scene.rngSeed > 0 ? context.scene.rngSeed : 1u;
    context.scene.rngState = seed;
    SceneGeneratedSetup::SetUpGameModels( BuildGeneratedModelContext( context ),
                                          context.uiOverrides.modelCountOverride );
    if ( context.camera.trackBallIndex >= context.uiOverrides.modelCountOverride )
    {
        context.camera.trackBallIndex = context.uiOverrides.modelCountOverride - 1;
    }
    return RequestReplayAndProfileReset();
}

SceneRuntimeGeneratedControlAction
ApplyUISolverObjectCounts( SceneRuntimeGeneratedControlContext context, int balls, int boxes )
{
    // Concept: Solver overrides are exact-count generated scenes used by physics
    // diagnostics and baseline runs, so model-count UI override is cleared.
    balls = std::clamp( balls, 0, context.modelCapacity );
    boxes = std::clamp( boxes, 0, context.modelCapacity );
    if ( balls + boxes > context.modelCapacity )
    {
        boxes = (std::max)( 0, context.modelCapacity - balls );
    }
    context.uiOverrides.solverBallCountOverride = balls;
    context.uiOverrides.solverBoxCountOverride = boxes;
    context.uiOverrides.modelCountOverride = -1;
    if ( !context.controller.HasCurrentEntry() )
    {
        return SceneRuntimeGeneratedControlAction{};
    }

    ResetGeneratedRuntimeState( context );

    const unsigned int seed = context.scene.rngSeed > 0 ? context.scene.rngSeed : 1u;
    context.scene.rngState = seed;
    SceneGeneratedSetup::SetUpSolverObjects( BuildGeneratedModelContext( context ),
                                             context.uiOverrides.solverBallCountOverride,
                                             context.uiOverrides.solverBoxCountOverride );
    if ( context.scene.modelCount <= 0 )
    {
        context.camera.trackBallIndex = -1;
    }
    else if ( context.camera.trackBallIndex >= context.scene.modelCount )
    {
        context.camera.trackBallIndex = context.scene.modelCount - 1;
    }
    return RequestReplayAndProfileReset();
}

} // namespace Basics
} // namespace SkullbonezCore
