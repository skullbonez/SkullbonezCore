# Runtime Shell Settings/Context Comment Audit

Scope: every source-bearing file touched while deleting `RunRuntimeSettings`,
moving its values to renderer/physics/audio owners, removing stress/automation
context bags, deleting the remaining interaction-automation Run forwarders,
and repairing the final adversarial-review findings in render/UI/replay boundaries.

Guide: `Agentic/Reference/comment-style-guide.md`

Checked: 47. Deferred: 0. Unchecked: 0.

- [x] `SkullbonezSource/GameObjects/GameModelCollection.h`
- [x] `SkullbonezSource/Physics/PhysicsEngine.cpp`
- [x] `SkullbonezSource/Physics/PhysicsEngine.h`
- [x] `SkullbonezSource/Physics/PhysicsScene.cpp`
- [x] `SkullbonezSource/Physics/PhysicsScene.h`
- [x] `SkullbonezSource/Runtime/Audio/ContactAudioService.cpp`
- [x] `SkullbonezSource/Runtime/Audio/ContactAudioService.h`
- [x] `SkullbonezSource/Runtime/Editor/LauncherTools.cpp`
- [x] `SkullbonezSource/Runtime/Init.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.h`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/InteractionAutomationController.cpp`
- [x] `SkullbonezSource/Runtime/Render/RenderPresentationSettings.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderResources.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRestoreService.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntimeOwnerViews.h`
- [x] `SkullbonezSource/Runtime/Replay/RunReplayProbes.cpp`
- [x] `SkullbonezSource/Runtime/Run.cpp`
- [x] `SkullbonezSource/Runtime/Run.h`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RunLaunchOptions.h`
- [x] `SkullbonezSource/Runtime/RunRender.cpp`
- [x] `SkullbonezSource/Runtime/RunUiTextPass.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeStressController.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeStressController.h`
- [x] `SkullbonezSource/Runtime/RuntimeTuning.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeTuning.h`
- [x] `SkullbonezSource/Runtime/RuntimeViewModel.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeViewModel.h`
- [x] `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeReset.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeReset.h`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.h`
- [x] `SkullbonezTests/TestDeterminism.cpp`
- [x] `tools/validate_project_filters.py`

All files retain complete learning headers. New comments identify the owner of
render presentation, audio diagnostics, and physics sleep/tornado state; the
reset transaction documents its value-only copies; render/UI comments explain
the value-only frame-policy and preview contracts; replay probe comments forbid
reintroducing a whole-world Debug fixture; and stress/automation helpers document
that explicit borrows cannot be retained as replacement shell contexts. No
wording is deferred for human approval.
