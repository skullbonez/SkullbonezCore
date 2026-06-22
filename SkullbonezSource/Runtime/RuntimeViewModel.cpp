/*
File: SkullbonezSource/Runtime/RuntimeViewModel.cpp
Purpose:
  Builds runtime presentation snapshots from EngineContext.

Mental model:
  The builder reads existing subsystem owners and copies only scalar UI-facing
  state, so no renderer, scene, or physics owner is exposed to presentation.

Glossary:
  View model: Read-only presentation snapshot assembled from runtime owners.
  EngineContext: Bound view over subsystems owned by Run.
  Scalar state: Small copyable values such as counts, flags, and indices.

Invariants:
  - Building the view model must not mutate subsystems.
  - Missing or unbound context returns a default snapshot.

Related:
  - SkullbonezSource/Runtime/RuntimeViewModel.h
  - SkullbonezSource/Runtime/EngineContext.h
*/
#include "RuntimeViewModel.h"

#include "CaptureController.h"
#include "EngineContext.h"
#include "Scene/SceneController.h"
#include "../GameObjects/GameModelCollection.h"

namespace SkullbonezCore
{
namespace Basics
{
RuntimeViewModel RuntimeViewModelBuilder::Build( const EngineContext& context )
{
    RuntimeViewModel view;
    if ( !context.IsBound() )
    {
        return view;
    }

    const EngineContextBindings& bindings = context.Bindings();
    const RunSceneState& scene = bindings.scene->State();
    const RunScreenshotState& screenshot = bindings.capture->Screenshot();
    const bool screenshotConfigured = screenshot.isScreenshotAndExit || screenshot.screenshotFrame >= 0 ||
                                      screenshot.screenshotMs >= 0 || screenshot.screenshotPath[0] != '\0' ||
                                      screenshot.screenshotInterval > 0;

    view.sceneMode = scene.isSceneMode;
    view.scenePhysics = scene.isScenePhysics;
    view.sceneText = scene.isSceneText;
    view.fixedStep = scene.isFixedStep;
    view.screenshotPending = screenshotConfigured && !screenshot.isScreenshotSaved;
    view.sceneIndex = scene.currentSceneIndex;
    view.sceneCount = bindings.scene->QueueSize();
    view.frame = scene.currentFrame;
    view.targetFrameCount = scene.targetFrameCount;
    view.modelCount = bindings.models ? bindings.models->GetModelCount() : 0;
    view.timeScale = scene.timeScale;
    return view;
}
} // namespace Basics
} // namespace SkullbonezCore
