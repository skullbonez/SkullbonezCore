#!/usr/bin/env python3
#
# File: tools/check_runtime_boundaries.py
# Purpose:
#   Check that Run.h stays a runtime composition root instead of regrowing
#   extracted subsystem ownership, and prevent new physics dependencies on the
#   legacy GameModelCollection world container or raytracing calls on the wide
#   render backend facade. It also blocks direct scheduling regressions for
#   passes that already moved to render graph callback ownership.
#
# Mental model:
#   Runtime decomposition is easy to regress by adding one convenient field or
#   helper back to Run. Physics data ownership is similarly easy to regress by
#   threading GameModelCollection into one more API. Render ownership can regress
#   when DXR reflection calls creep back onto Gfx(), or when a graph-owned pass
#   is called directly from the runtime frame loop again. This check is
#   intentionally small: it watches the boundaries named by the active
#   architecture plans.
#
# Glossary:
#   Composition root: Top-level owner that wires subsystems together.
#   Boundary guardrail: Static check that blocks architecture drift.
#   Allowlist: Explicit set of legacy references accepted during migration.
#
# Invariants:
#   - Run.h may own subsystem objects, but not their extracted transient state.
#   - Subsystems may borrow explicit service/context structs, but not store Run.
#   - Physics may only keep the current GameModelCollection compatibility
#     surface while stores and handles become authoritative.
#   - Raytracing calls go through IRenderRayTracing/GfxRayTracing instead of the
#     wide IRenderBackend/Gfx facade.
#   - Graph-owned render passes stay scheduled through render graph callback
#     helpers after migration.
#
# Related:
#   - Agentic/Plans/runtime-run-decomposition-plan.md
#   - Agentic/Plans/engine-architecture-next-steps-plan.md
#   - tools/validate_fast.bat
#
"""Validate runtime Run-boundary guardrails."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


RUN_HEADER = Path("SkullbonezSource/Runtime/Run.h")
RUN_INTERNAL_HEADER = Path("SkullbonezSource/Runtime/RunInternal.h")
RUNTIME_ROOT = Path("SkullbonezSource/Runtime")
PHYSICS_ROOT = Path("SkullbonezSource/Physics")
IRENDER_BACKEND_HEADER = Path("SkullbonezSource/Rendering/IRenderBackend.h")
RUN_RENDER_SOURCE = Path("SkullbonezSource/Runtime/RunRender.cpp")
RUN_INPUT_SOURCE = Path("SkullbonezSource/Runtime/RunInput.cpp")
RUN_SCENE_SOURCE = Path("SkullbonezSource/Runtime/Scene/RunScene.cpp")
RUNTIME_RENDER_HOST_HEADER = Path("SkullbonezSource/Runtime/Render/RuntimeRenderHost.h")
FIELD_TAIL_PATTERN = r"(?=[^;{}]*\bm_[A-Za-z_]\w*)[^;{}]*;"
RUN_NAME_PATTERN = r"(?:(?:[A-Za-z_]\w*::)*Run)\b"
RUN_CV_PATTERN = rf"(?:const\s+{RUN_NAME_PATTERN}|{RUN_NAME_PATTERN}\s+const|{RUN_NAME_PATTERN})"
GAME_MODEL_COLLECTION_PATTERN = re.compile(r"\bGameModelCollection\b")
PHYSICS_DELETED_MODEL_VIEW_PATTERN = re.compile(r"\b(?:MakePhysicsModelView|PhysicsModelView)\b")
PHYSICS_MODELS_ACCESS_PATTERN = re.compile(r"\bPhysicsModels\s*\(")
DIRECT_GFX_RAYTRACING_PATTERN = re.compile(
    r"\bGfx\s*\(\s*\)\s*\.\s*(?:InitDXR|DispatchReflectionRays|BuildTLAS|GetReflectionUAVTexture|"
    r"ShutdownDXR|GetInstancedMeshStaticVBVA|GetInstancedMeshStaticStride)\s*\("
)
IRENDER_BACKEND_RAYTRACING_DECLARATION_PATTERN = re.compile(
    r"\b(?:virtual\s+)?(?:void|uint32_t|uint64_t|int)\s+"
    r"(?:InitDXR|DispatchReflectionRays|BuildTLAS|GetReflectionUAVTexture|ShutdownDXR|"
    r"GetInstancedMeshStaticVBVA|GetInstancedMeshStaticStride)\s*\("
)
GRAPH_OWNED_RENDER_PASS_DIRECT_CALL_PATTERN = re.compile(
    r"\bm_(?:debugOverlayPass|volumetricPass|tonemapPass)\s*\.\s*Render\s*\("
)
MAX_RUN_PRIVATE_METHOD_DECLARATIONS = 129
RUN_PRIVATE_METHOD_DECLARATION_PATTERN = re.compile(
    r"(?m)^\s*(?:static\s+)?(?:[A-Za-z_][\w:<>,~]*\s*(?:[&*]\s*)?\s+)+"
    r"(?:[A-Za-z_][\w:]*)\s*\([^;{}]*\)\s*(?:const\s*)?"
    r"(?:=\s*(?:delete|default)\s*)?;",
    re.S,
)


def normalize_boundary_line(line: str) -> str:
    return " ".join(line.strip().split())


PHYSICS_GAME_MODEL_COLLECTION_ALLOWLIST: Counter[tuple[Path, str]] = Counter(
    ( Path(path), normalize_boundary_line(line) )
    for path, line in (
        # Legacy debug visualizers still inspect GameModelCollection state while
        # body/collider/render/entity stores become authoritative.
        ( "SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp", '#include "../../GameObjects/GameModelCollection.h"' ),
        ( "SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp", "void CollisionVisualizer::Update( float dt, GameModelCollection& models )" ),
        ( "SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp", "void CollisionVisualizer::BuildSleepGroupSizes( GameModelCollection& models )" ),
        (
            "SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp",
            "CollisionVisualizer::Color CollisionVisualizer::ComputeModelColor( int modelIndex, GameModelCollection& models ) const",
        ),
        ( "SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp", "void CollisionVisualizer::Render( GameModelCollection& models," ),
        ( "SkullbonezSource/Physics/Debug/CollisionVisualizer.h", "class GameModelCollection;" ),
        (
            "SkullbonezSource/Physics/Debug/CollisionVisualizer.h",
            "Color ComputeModelColor( int modelIndex, GameObjects::GameModelCollection& models ) const;",
        ),
        (
            "SkullbonezSource/Physics/Debug/CollisionVisualizer.h",
            "void BuildSleepGroupSizes( GameObjects::GameModelCollection& models );",
        ),
        ( "SkullbonezSource/Physics/Debug/CollisionVisualizer.h", "void Update( float dt, GameObjects::GameModelCollection& models );" ),
        ( "SkullbonezSource/Physics/Debug/CollisionVisualizer.h", "void Render( GameObjects::GameModelCollection& models," ),
        ( "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp", '#include "../../GameObjects/GameModelCollection.h"' ),
        ( "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp", "void PhysicsDebugVisualizer::EmitObjectAxes( GameModelCollection& models )" ),
        (
            "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp",
            "void PhysicsDebugVisualizer::EmitConvexHullWireframes( GameModelCollection& models )",
        ),
        ( "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp", "void PhysicsDebugVisualizer::EmitContacts( GameModelCollection& models )" ),
        ( "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp", "void PhysicsDebugVisualizer::EmitSleepState( GameModelCollection& models )" ),
        (
            "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp",
            "void PhysicsDebugVisualizer::EmitPipelineStage( GameModelCollection& models )",
        ),
        (
            "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp",
            "void PhysicsDebugVisualizer::EmitTerrainContactProbe( GameModelCollection& models, Geometry::Terrain* terrain )",
        ),
        ( "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp", "void PhysicsDebugVisualizer::Update( float dt, GameModelCollection& models )" ),
        (
            "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp",
            "void PhysicsDebugVisualizer::Render( GameModelCollection& models, const Matrix4& viewProj, Geometry::Terrain* terrain )",
        ),
        ( "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.h", "class GameModelCollection;" ),
        ( "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.h", "void EmitObjectAxes( GameObjects::GameModelCollection& models );" ),
        (
            "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.h",
            "void EmitConvexHullWireframes( GameObjects::GameModelCollection& models );",
        ),
        ( "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.h", "void EmitContacts( GameObjects::GameModelCollection& models );" ),
        ( "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.h", "void EmitSleepState( GameObjects::GameModelCollection& models );" ),
        ( "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.h", "void EmitPipelineStage( GameObjects::GameModelCollection& models );" ),
        (
            "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.h",
            "void EmitTerrainContactProbe( GameObjects::GameModelCollection& models, Geometry::Terrain* terrain );",
        ),
        ( "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.h", "void Update( float dt, GameObjects::GameModelCollection& models );" ),
        ( "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.h", "void Render( GameObjects::GameModelCollection& models," ),
        # Creation still lives on the legacy scene/model facade; the solver
        # path uses PhysicsModelAccess and handles after creation.
        ( "SkullbonezSource/Physics/Ragdoll.cpp", '#include "../GameObjects/GameModelCollection.h"' ),
        ( "SkullbonezSource/Physics/Ragdoll.cpp", "void Ragdoll::AddSimpleHumanoid( GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/Ragdoll.h", "class GameModelCollection;" ),
        ( "SkullbonezSource/Physics/Ragdoll.h", "static void AddSimpleHumanoid( GameObjects::GameModelCollection& collection," ),
        # SimulationSystem is a compatibility input boundary and is not used by
        # the active PhysicsEngine/PhysicsWorld step path.
        ( "SkullbonezSource/Physics/SimulationSystem.cpp", '#include "../GameObjects/GameModelCollection.h"' ),
        ( "SkullbonezSource/Physics/SimulationSystem.h", "class GameModelCollection;" ),
        ( "SkullbonezSource/Physics/SimulationSystem.h", "GameObjects::GameModelCollection* models = nullptr;" ),
    )
)

# Counted allowlist: duplicate entries are intentional. They preserve the exact
# current compatibility debt while failing any new direct PhysicsModels() call.
PHYSICS_MODELS_ACCESS_ALLOWLIST: Counter[tuple[Path, str]] = Counter(
    ( Path(path), normalize_boundary_line(line) )
    for path, line in (
        ( "SkullbonezSource/GameObjects/GameModelCollection.h", "std::vector<GameModel>& PhysicsModels();" ),
        ( "SkullbonezSource/GameObjects/GameModelCollection.h", "const std::vector<GameModel>& PhysicsModels() const;" ),
        (
            "SkullbonezSource/GameObjects/GameModelCollection.cpp",
            "std::vector<GameModel>& GameModelCollection::PhysicsModels()",
        ),
        (
            "SkullbonezSource/GameObjects/GameModelCollection.cpp",
            "const std::vector<GameModel>& GameModelCollection::PhysicsModels() const",
        ),
        (
            "SkullbonezSource/Runtime/Run.cpp",
            "std::vector<GameObjects::GameModel>& models = m_cGameModelCollection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/RunFrame.cpp",
            "std::vector<SkullbonezCore::GameObjects::GameModel>& physicsModels = m_cGameModelCollection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/RunFrame.cpp",
            "std::vector<SkullbonezCore::GameObjects::GameModel>& physicsModels = m_cGameModelCollection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/RunFrame.cpp",
            "std::vector<SkullbonezCore::GameObjects::GameModel>& physicsModels = m_cGameModelCollection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/RunFrame.cpp",
            "const std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/RunFrame.cpp",
            "const std::vector<GameModel>& restoredModels = m_cGameModelCollection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/Editor/RunMousePickupTools.inl",
            "std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/Editor/RunMousePickupTools.inl",
            "std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp",
            "std::vector<GameObjects::GameModel>& models = collection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp",
            "std::vector<GameObjects::GameModel>& models = collection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp",
            "std::vector<GameObjects::GameModel>& models = collection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp",
            "std::vector<GameObjects::GameModel>& models = collection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp",
            "std::vector<GameModel>& physicsModels = models.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp",
            "std::vector<GameModel>& physicsModels = models.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl",
            "std::vector<GameModel>& models = modelCollection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl",
            "std::vector<GameModel>& models = modelCollection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl",
            "std::vector<GameModel>& models = modelCollection.PhysicsModels();",
        ),
        (
            "SkullbonezSource/Runtime/Replay/RunReplayPredictionVisualizer.inl",
            "std::vector<GameModel>& models = modelCollection.PhysicsModels();",
        ),
    )
)

RUN_HEADER_RULES: tuple[tuple[str, str, str], ...] = (
    (
        "render pass class definitions must stay out of Run.h",
        r"\b(?:class|struct)\s+(?:FullscreenQuadPass|SkyPass|SceneTargetPass|ShadowPass|ReflectionPass|ObjectPass|"
        r"TerrainPass|WaterPass|TornadoVisualPass|DebugOverlayPass|VolumetricPass|TonemapPass|UiTextPass)\b",
        "Put render pass types in Runtime/Render/RuntimeRenderPasses.h.",
    ),
    (
        "DXR reflection state must stay out of Run.h",
        r"\b(?:m_dxrReflectionTransforms|dxrReflectionTransforms|DxrReflection|InitDXR|DispatchReflectionRays|BuildTLAS)\b",
        "RuntimeRenderHost or a renderer-owned DXR capability owns reflection scratch state and backend calls.",
    ),
    (
        "replay recorder fields must stay out of Run.h",
        r"\b(?:ReplayRecorder|ReplaySolverRecorder|ReplayEventRecorder|ReplayBranchInfo)\b" + FIELD_TAIL_PATTERN,
        "ReplayRuntime owns recorder and branch state.",
    ),
    (
        "tool transient fields must stay out of Run.h",
        r"\b(?:RunRayCastTestState|LauncherLaser|RunMousePickupState|RunEditorPlacementState|RunEditorTracer)\b"
        + FIELD_TAIL_PATTERN,
        "RuntimeTools owns launcher, manipulator, and editor transient state.",
    ),
    (
        "scene object population helpers must stay out of Run.h",
        r"\b(?:SetUpGameModels|SetUpSolverObjects|SetUpCamerasFromScene|SetUpGameModelsFromScene|"
        r"SetUpRequiredContactsFromScene|SetUpRequiredBroadphaseXCellsFromScene)\s*\(",
        "Call SceneGeneratedSetup or SceneAuthoredSetup through explicit context builders.",
    ),
    (
        "scene context builders must stay out of Run.h",
        r"\bBuildScene(?:Authored|Generated)(?:Camera|Model)Context\s*\(",
        "Keep scene setup contexts file-local with explicit borrowed dependencies.",
    ),
    (
        "generated camera setup wrapper must stay out of Run.h",
        r"\bSetUpCameras\s*\(",
        "Call SceneGeneratedSetup::SetUpCameras through an explicit scene setup context.",
    ),
    (
        "scene terrain/world setup wrappers must stay out of Run.h",
        r"\b(?:ApplyConfiguredWorldEnvironment|ApplyNoWaterOverride|UseDefaultTerrain|"
        r"UseFlatSlopeTerrain|UpdateWorldTerrainBounds)\s*\(",
        "Keep scene terrain and world setup policy file-local or in Runtime/Scene helpers.",
    ),
    (
        "editable scene snapshot helper must stay out of Run.h",
        r"\bSaveCurrentEditableSceneSnapshot\s*\(",
        "Keep editable-scene snapshot persistence file-local or in Runtime/Scene helpers.",
    ),
    (
        "tornado defaults helper must stay out of Run.h",
        r"\bApplyTornadoDefaultsForActiveScene\s*\(",
        "Keep scene tornado default policy file-local or in Runtime/Scene helpers.",
    ),
    (
        "tornado physics sync wrapper must stay out of Run.h",
        r"\bSyncTornadoFieldToPhysics\s*\(",
        "Sync tornado runtime settings to physics through an explicit helper.",
    ),
    (
        "scene runtime reset helpers must stay out of Run.h",
        r"\b(?:CaptureSceneRuntimeResetSnapshot|RestoreSceneRuntimeResetSnapshot|ClearSceneRuntimeUIOverrides)\s*\(",
        "Keep scene reload preserve/reset policy in Runtime/Scene scene runtime helpers.",
    ),
    (
        "scene queue wrappers must stay out of Run.h",
        r"\b(?:HasSceneQueueEntry|HasCurrentSceneQueueEntry|CurrentSceneQueuePath)\s*\(",
        "Use SceneController/SceneRuntime accessors directly while scene load ownership moves out of Run.",
    ),
    (
        "diagnostics perf-memory wrappers must stay out of Run.h",
        r"\bLogPerfMemory\s*\(",
        "Call DiagnosticsRuntime::LogPerfMemory directly so diagnostics owns perf-log policy.",
    ),
    (
        "diagnostics perf-log tick wrappers must stay out of Run.h",
        r"\bTickPerfLog\s*\(",
        "Call DiagnosticsRuntime::TickPerfLog directly so diagnostics owns perf-log lifecycle.",
    ),
    (
        "diagnostics memory-dump wrapper must stay out of Run.h",
        r"\bWriteMainMemoryDump\s*\(",
        "Call DiagnosticsRuntime::WriteMainMemoryDump directly so diagnostics owns memory dump policy.",
    ),
    (
        "diagnostics UI stress RNG helpers must stay out of Run.h",
        r"\bNextUIStress(?:Random|Int|Float)\s*\(",
        "Keep deterministic UI stress RNG helpers file-local or in DiagnosticsRuntime.",
    ),
    (
        "scene-control wrappers must stay out of Run.h",
        r"\b(?:LoadSceneFromBrowserIndex|LoadDemoSceneFromUI|ApplyAdjacentCinematicMode|"
        r"LoadAdjacentSceneFromBrowser|ResetCurrentScene|AdvanceScene)\s*\(",
        "Call SceneRuntimeCoordinator directly while scene load ownership moves out of Run.",
    ),
    (
        "scene browser refresh wrapper must stay out of Run.h",
        r"\bRefreshSceneBrowserList\s*\(",
        "Refresh scene browser discovery through SceneRuntimeLoad helpers.",
    ),
    (
        "scene browser index wrapper must stay out of Run.h",
        r"\bCurrentSceneBrowserIndex\s*\(",
        "Resolve current scene browser selection through SceneRuntimeLoad helpers.",
    ),
    (
        "scene default persistence wrappers must stay out of Run.h",
        r"\bSave(?:Render|Sky)Defaults\s*\(",
        "Persist render defaults through SceneRuntimeDefaults helpers with explicit config payloads.",
    ),
    (
        "scene world override wrapper must stay out of Run.h",
        r"\bApplyUIWorldOverride\s*\(",
        "Apply live world overrides through RuntimeTuning helpers with explicit world/replay dependencies.",
    ),
    (
        "scene generated control wrappers must stay out of Run.h",
        r"\bApplyUI(?:ModelCountOverride|SolverObjectCounts)\s*\(",
        "Apply generated scene UI rebuilds through SceneRuntimeGeneratedControls helpers.",
    ),
    (
        "scene create wrapper must stay out of Run.h",
        r"\bCreateSceneFromUI\s*\(",
        "Create starter scene files through SceneRuntimeCreate helpers.",
    ),
    (
        "scene style wrappers must stay out of Run.h",
        r"\b(?:ApplyCinematicModeFromBrowserIndex|ApplyLiveStyleScene|ApplyDemoHeroStyleOverride)\s*\(",
        "Apply live style and cinematic scene overrides through SceneRuntimeStyle helpers.",
    ),
    (
        "scene coordinator callback builders must stay out of Run.h",
        r"\bBuildSceneRuntimeCoordinatorCallbacks\s*\(",
        "Return explicit SceneRuntimeCoordinator actions instead of callback-bouncing through Run.",
    ),
    (
        "editor gizmo mechanics must stay out of Run.h",
        r"\b(?:[A-Za-z_]\w*EditorGizmo[A-Za-z_]\w*|"
        r"[A-Za-z_]\w*SelectedEditorObject[A-Za-z_]\w*)\s*\(",
        "Keep editor transform math and mutation in Runtime/Editor/EditorTools helpers.",
    ),
    (
        "editor UI/mode command helpers must stay out of Run.h",
        r"\b(?:ResetEditorUnfocusedInputState|ClearEditorManipulationState|ToggleEditorPlacementMode|"
        r"HandleEditorKeyboardShortcuts|ApplyEditorUICommands)\s*\(",
        "Keep editor input-mode state mechanics in Runtime/Editor/EditorTools helpers.",
    ),
    (
        "editor overlay/preview helpers must stay out of Run.h",
        r"\b(?:UpdateEditorInteractionPreview|RenderEditorOverlay|(?=[A-Za-z_]\w*Editor)(?=[A-Za-z_]\w*Overlay)"
        r"[A-Za-z_]\w*|(?=[A-Za-z_]\w*InteractionPreview)[A-Za-z_]\w*)\s*\(",
        "Keep editor preview and overlay trace construction in Runtime/Editor overlay helpers.",
    ),
    (
        "replay render-query helpers must stay out of Run.h",
        r"\b(?:HasLoadedReplayPresentation|LoadedReplayPresentation[A-Za-z_]\w*|IsReplayScrubPaused|"
        r"CurrentReplay[A-Za-z_]\w*|BuildReplayFocusModelMask|ShouldRenderReplayScrubber|"
        r"RenderReplayPredictionGhosts)\s*\(",
        "Keep replay render queries and focus masks in ReplayRuntime.",
    ),
    (
        "replay cause-tree row builders must stay out of Run.h",
        r"\bBuild(?:Replay)?CauseTreeRows\s*\(",
        "Build replay cause-tree rows in ReplayRuntime.",
    ),
    (
        "replay prediction job-state helpers must stay out of Run.h",
        r"\b(?:MarkReplayPredictionDirty|ClearReplayPredictionCache|CancelReplayPredictionJob)\s*\(",
        "Keep replay prediction job and cache state mutation in ReplayRuntime.",
    ),
    (
        "replay prediction capture helpers must stay out of Run.h",
        r"\b(?:CaptureReplayPredictionBodyState|ApplyReplayPredictionBodyState|CaptureReplayPredictionFrame)\s*\(",
        "Keep replay prediction capture and restore helpers file-local or in ReplayRuntime.",
    ),
    (
        "replay prediction lifecycle helpers must stay out of Run.h",
        r"\b(?:BeginReplayPredictionJob|StepReplayPredictionJob|RenderReplayPredictionVisualizer)\s*\(",
        "Keep replay prediction lifecycle orchestration file-local or in ReplayRuntime.",
    ),
    (
        "replay path-state helpers must stay out of Run.h",
        r"\bClearReplayPathVisualizer\s*\(",
        "Clear replay path visualizer state through ReplayRuntime.",
    ),
    (
        "replay cause-tree body lookup helpers must stay out of Run.h",
        r"\bTryResolveReplayCauseTreeBodyPosition\s*\(",
        "Resolve replay cause-tree body positions through ReplayRuntime.",
    ),
    (
        "replay cause-tree body focus wrappers must stay out of Run.h",
        r"\bFocusReplayCauseTreeBody\s*\(",
        "Route cause-tree focus through explicit row activation instead of private Run wrappers.",
    ),
    (
        "replay cause-tree camera activation helper must stay out of Run.h",
        r"\bActivateReplayCameraForCauseRow\s*\(",
        "Keep replay cause-tree camera activation scoped to the input path or replay helpers.",
    ),
    (
        "replay render-state helpers must stay out of Run.h",
        r"\b(?:ApplyReplayRenderStateForFrame|RestoreReplayRenderStateForFrame|"
        r"ApplyReplayLauncherVisualSampleForRender|RestoreReplayLauncherVisualForRender)\s*\(",
        "Keep replay render-state sampling scoped to the render frame or replay render helpers.",
    ),
    (
        "render host texture wrappers must stay out of Run.h",
        r"\b(?:Textures|TextureHandle|SelectRenderTexture)\s*\(",
        "Route render-host texture access through RuntimeRenderHost callbacks.",
    ),
    (
        "render input builders must stay out of Run.h",
        r"\bBuildRuntimeRender(?:Services|Inputs)\s*\(",
        "Keep render input assembly local to Runtime/RunRender.cpp.",
    ),
    (
        "runtime window-size wrappers must stay out of Run.h",
        r"\bWindowScreen(?:Width|Height)\s*\(",
        "Use RunInternal runtime window-size helpers with explicit subsystem/config inputs.",
    ),
    (
        "runtime cinematic config wrappers must stay out of Run.h",
        r"\b(?:ActiveCinematicConfig|IsCinematicRenderingEnabled)\s*\(",
        "Use RunInternal cinematic helpers with explicit scene/config/launch/debug inputs.",
    ),
    (
        "replay launcher visual sample helpers must stay out of Run.h",
        r"\b(?:BuildReplayLauncherVisualSample|RestoreReplayLauncherVisualSample)\s*\(",
        "Build and restore replay launcher visual samples through RuntimeTools.",
    ),
    (
        "replay sample comparison helper must stay out of Run.h",
        r"\bCompareLatestReplaySamples\s*\(",
        "Keep replay sample mismatch diagnostics file-local to replay capture.",
    ),
    (
        "replay presentation artifact picker must stay out of Run.h",
        r"\bPromptLoadReplayPresentationArtifact\s*\(",
        "Keep replay artifact picker prompts scoped to replay scrubber input.",
    ),
    (
        "replay scrubber save helper must stay out of Run.h",
        r"\bSaveReplayBufferFromScrubber\s*\(",
        "Keep replay scrubber save behavior scoped to replay tools.",
    ),
    (
        "replay restore event helper must stay out of Run.h",
        r"\bApplyReplayEventForRestoreTarget\s*\(",
        "Keep replay event restore application scoped to the v2 target restore path.",
    ),
    (
        "replay inspection camera update wrapper must stay out of Run.h",
        r"\bUpdateReplayInspectionCamera\s*\(",
        "Keep replay inspection camera activation driven by ReplayRuntime state.",
    ),
    (
        "replay inspection query wrappers must stay out of Run.h",
        r"\bReplayInspection(?:MouseLook)?Active\s*\(",
        "Query replay inspection state through ReplayRuntime.",
    ),
    (
        "replay live-advance wrapper must stay out of Run.h",
        r"\bSetReplayLiveAdvanceHeld\s*\(",
        "Set replay live-advance state through ReplayRuntime.",
    ),
    (
        "replay scrubber reset wrapper must stay out of Run.h",
        r"\bResetReplayScrubber\s*\(",
        "Reset replay scrubber state through ReplayRuntime.",
    ),
    (
        "replay loaded-presentation scrubber arming wrapper must stay out of Run.h",
        r"\bArmLoadedReplayPresentationScrubber\s*\(",
        "Arm loaded replay presentation scrubber state through ReplayRuntime.",
    ),
    (
        "replay camera focus clear wrapper must stay out of Run.h",
        r"\bClearReplayCameraFocus\s*\(",
        "Clear replay camera focus state through ReplayRuntime.",
    ),
    (
        "replay event frame cursor wrapper must stay out of Run.h",
        r"\bNextReplayEventFrameIndex\s*\(",
        "Read replay event frame cursors from ReplayRuntime.",
    ),
    (
        "replay event record wrapper must stay out of Run.h",
        r"\bRecordReplayEvent\s*\(",
        "Append replay events through ReplayRuntime.",
    ),
    (
        "replay generated-scene config wrapper must stay out of Run.h",
        r"\bRecordReplayGeneratedSceneConfigEvent\s*\(",
        "Keep generated-scene replay config appends local to timeline reset.",
    ),
    (
        "replay physics capture wrappers must stay out of Run.h",
        r"\bCaptureReplayPhysicsStep(?:Thunk)?\s*\(",
        "Keep replay capture in the post-physics hook.",
    ),
    (
        "replay world override event wrapper must stay out of Run.h",
        r"\bRecordReplayWorldOverrideEvent\s*\(",
        "Record world override replay events through ReplayRuntime.",
    ),
    (
        "replay launcher config event wrapper must stay out of Run.h",
        r"\bRecordReplayLauncherConfigEvent\s*\(",
        "Record launcher config replay events through ReplayRuntime.",
    ),
    (
        "replay launcher fire event wrapper must stay out of Run.h",
        r"\bRecordReplayLauncherFireEvent\s*\(",
        "Record launcher fire replay events through ReplayRuntime.",
    ),
    (
        "replay editor place event wrapper must stay out of Run.h",
        r"\bRecordReplayEditorPlaceEvent\s*\(",
        "Record editor placement replay events through ReplayRuntime.",
    ),
    (
        "replay editor transform event wrapper must stay out of Run.h",
        r"\bRecordReplayEditorTransformEvent\s*\(",
        "Record editor transform replay events through ReplayRuntime.",
    ),
    (
        "replay velocity target lookup helpers must stay out of Run.h",
        r"\bResolveReplayVelocityEditModelIndex\s*\(",
        "Resolve replay velocity edit targets through ReplayRuntime.",
    ),
    (
        "replay velocity hit helpers must stay out of Run.h",
        r"\b(?:HitReplayVelocity(?:Linear|Angular)Axis|TryReplayVelocity(?:AxisRayParameter|AngularRayAngle))\s*\(",
        "Keep replay velocity hit and ray helpers file-local or in ReplayRuntime.",
    ),
    (
        "replay velocity edit toggle helper must stay out of Run.h",
        r"\bSetReplayVelocityEditEnabled\s*\(",
        "Set replay velocity edit state through ReplayRuntime and keep interaction transitions at the call site.",
    ),
    (
        "replay velocity apply helper must stay out of Run.h",
        r"\bApplyReplayVelocityEdit(?:ToModel|Drag)\s*\(",
        "Keep replay velocity model mutation helpers file-local or in ReplayRuntime.",
    ),
    (
        "replay overlay render helpers must stay out of Run.h",
        r"\bRenderReplay(?:Scrubber|CauseTree)Overlay\s*\(",
        "Draw replay overlays from RuntimeRenderHost or a replay-owned render service.",
    ),
)

RUN_INTERNAL_SCRUBBER_HELPER_RULE = (
    "replay scrubber timeline helpers must stay out of RunInternal.h",
    r"\b(?:(?:static|inline|constexpr|consteval)\s+)*(?:float|bool|void)\s+"
    r"(?:ReplayScrubberRetainedPastSeconds|ReplayPredictionAvailableFutureSeconds|"
    r"ReplayScrubberPresentTrackPosition|ReplayScrubberTimelineHasFuture|"
    r"ReplayScrubberAtPresentTrackPosition|ReplayScrubberTrackPositionIsFuture|"
    r"ReplayScrubberSolverNormalizedFromTrack|ReplayScrubberPredictionNormalizedFromTrack|"
    r"ReplayScrubberTrackPosition|ReplayScrubberSetTrackPosition|"
    r"ReplayScrubberSyncActivePosition|ReplayScrubberSetAllTrackPositions)\s*\(",
    "Keep replay scrubber timeline math and position mutation in ReplayRuntime.",
)

RUN_INTERNAL_REPLAY_LAYOUT_HELPER_RULE = (
    "replay overlay layout helpers must stay out of RunInternal.h",
    r"\b(?:(?:static|inline|constexpr|consteval)\s+)*(?:UI::UIRect|float|int|void)\s+"
    r"(?:ReplayScrubber(?:PanelRect|RowCenterY|TrackRect|SaveButtonRect|LoadButtonRect|"
    r"BranchButtonRect|PauseButtonRect|VelocityEditToggleRect|PredictControlRect|"
    r"PredictToggleRect|PredictHorizonRect|RagdollVisualToggleRect|HotZoneRect|PositionFromMouse)|"
    r"ReplayPredictionHorizon(?:T|FromMouse)|ReplayCause(?:TreePanelRect|TreeRowRect|"
    r"TreeVisibleRowCapacity|WindowRect|WindowTitleRect|WindowContentRect|WindowResizeRect|"
    r"WindowContentHeight|WindowMaxScroll)|ClampReplayCauseWindow|EnsureReplayCauseWindowPlacement)\s*\(",
    "Keep replay overlay layout in Runtime/Replay/ReplayOverlayLayout.",
)

RUN_INTERNAL_SCENE_RUNTIME_RULE = (
    "scene runtime reset snapshot must stay out of RunInternal.h",
    r"\bstruct\s+SceneRuntimeResetSnapshot\b",
    "Keep scene reset/load policy data in Runtime/Scene scene runtime helpers.",
)

RUN_INTERNAL_SCENE_STYLE_RULE = (
    "cinematic override helpers must stay out of RunInternal.h",
    r"\bApplyCinematicSceneOverrides\s*\(",
    "Keep cinematic override merge policy in Runtime/Scene scene style helpers.",
)

RUN_UI_TEXT_PASS_REPLAY_OVERLAY_RULE = (
    "replay overlay renderer definitions must stay out of RunUiTextPass.cpp",
    r"\b(?:(?:static|inline)\s+)*void\s+(?:(?:RuntimeRenderHost|ReplayOverlay)::)?"
    r"RenderReplay(?:Scrubber|CauseTree)Overlay\s*\(",
    "Keep replay overlay drawing in Runtime/Replay/ReplayOverlayRenderer.",
)

RUN_STORAGE_RULE = (
    "subsystems must not store Run by pointer or reference",
    rf"(?:\b{RUN_CV_PATTERN}\s*(?:[&*]\s*(?:const\s*)?)?|"
    rf"\bstd::(?:reference_wrapper|unique_ptr|shared_ptr|weak_ptr|optional)<\s*{RUN_CV_PATTERN}\s*>\s*)"
    + FIELD_TAIL_PATTERN,
    "Use EngineContext, RuntimeRenderHost, or a subsystem-specific service struct instead.",
)

CAMERA_MODE_WRITE_RULE = (
    "camera mode writes must stay behind the interaction bridge",
    r"\bm_camera\.mode\s*(?<![=!<>])=(?!=)",
    "Route camera label changes through Run::SetCameraModeLabelAfterInteractionTransition(...).",
)

WORLD_OWNER_WRITE_RULE = (
    "world interaction owner writes must stay behind the transition bridge",
    r"\bm_interaction\.SetWorldInteractionOwner(?:InWorkspace)?\s*\(",
    "Route owner changes through Run::SetWorldInteractionOwnerAfterInteractionTransition(...).",
)

PICK_HELPER_RULE = (
    "duplicated runtime pick helper wrappers are blocked",
    r"\b(?:bool|int|float|double|void|auto|[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s*[&*])?)\s+"
    r"(?:Run::)?TryPick(?:[A-Za-z_]\w*)?Model(?:[A-Za-z_]\w*)?\s*\(",
    "Use RuntimePickService::TryPickModel(...) with an explicit RuntimePickPurpose instead.",
)

RUN_REPLAY_CAUSE_TREE_SOURCE_RULE = (
    "Run replay cause-tree row builders are blocked",
    r"\bRun::Build(?:Replay)?CauseTreeRows\s*\(",
    "Build replay cause-tree rows in ReplayRuntime instead of Run.",
)

RUN_REPLAY_PREDICTION_JOB_SOURCE_RULE = (
    "Run replay prediction job-state helpers are blocked",
    r"\bRun::(?:MarkReplayPredictionDirty|ClearReplayPredictionCache|CancelReplayPredictionJob)\s*\(",
    "Keep replay prediction job and cache state mutation in ReplayRuntime instead of Run.",
)

RUN_REPLAY_PREDICTION_CAPTURE_SOURCE_RULE = (
    "Run replay prediction capture helpers are blocked",
    r"\bRun::(?:CaptureReplayPredictionBodyState|ApplyReplayPredictionBodyState|CaptureReplayPredictionFrame)\s*\(",
    "Keep replay prediction capture and restore helpers file-local or in ReplayRuntime.",
)

RUN_REPLAY_PREDICTION_LIFECYCLE_SOURCE_RULE = (
    "Run replay prediction lifecycle helpers are blocked",
    r"\bRun::(?:BeginReplayPredictionJob|StepReplayPredictionJob|RenderReplayPredictionVisualizer)\s*\(",
    "Keep replay prediction lifecycle orchestration file-local or in ReplayRuntime.",
)

RUN_REPLAY_PATH_STATE_SOURCE_RULE = (
    "Run replay path-state helpers are blocked",
    r"\bRun::ClearReplayPathVisualizer\s*\(",
    "Clear replay path visualizer state through ReplayRuntime instead of Run.",
)

RUN_REPLAY_CAUSE_TREE_LOOKUP_SOURCE_RULE = (
    "Run replay cause-tree lookup helpers are blocked",
    r"\bRun::TryResolveReplayCauseTreeBodyPosition\s*\(",
    "Resolve replay cause-tree body positions through ReplayRuntime instead of Run.",
)

RUN_REPLAY_CAUSE_TREE_FOCUS_SOURCE_RULE = (
    "Run replay cause-tree focus wrappers are blocked",
    r"\bRun::FocusReplayCauseTreeBody\s*\(",
    "Use explicit cause-tree row activation instead of private Run focus wrappers.",
)

RUN_REPLAY_CAUSE_TREE_CAMERA_ACTIVATION_SOURCE_RULE = (
    "Run replay cause-tree camera activation helper is blocked",
    r"\bRun::ActivateReplayCameraForCauseRow\s*\(",
    "Keep replay cause-tree camera activation scoped to the input path or replay helpers.",
)

RUN_REPLAY_RENDER_STATE_SOURCE_RULE = (
    "Run replay render-state helpers are blocked",
    r"\bRun::(?:ApplyReplayRenderStateForFrame|RestoreReplayRenderStateForFrame|"
    r"ApplyReplayLauncherVisualSampleForRender|RestoreReplayLauncherVisualForRender)\s*\(",
    "Keep replay render-state sampling scoped to the render frame or replay render helpers.",
)

RUN_REPLAY_LAUNCHER_VISUAL_SAMPLE_SOURCE_RULE = (
    "Run replay launcher visual sample helpers are blocked",
    r"\bRun::(?:BuildReplayLauncherVisualSample|RestoreReplayLauncherVisualSample)\s*\(",
    "Build and restore replay launcher visual samples through RuntimeTools instead of Run.",
)

RUN_REPLAY_SAMPLE_COMPARISON_SOURCE_RULE = (
    "Run replay sample comparison helper is blocked",
    r"\bRun::CompareLatestReplaySamples\s*\(",
    "Keep replay sample mismatch diagnostics file-local to replay capture.",
)

RUN_RENDER_HOST_TEXTURE_SOURCE_RULE = (
    "Run render host texture wrappers are blocked",
    r"\bRun::(?:Textures|TextureHandle|SelectRenderTexture)\s*\(",
    "Route render-host texture access through RuntimeRenderHost callbacks.",
)

RUN_REPLAY_PRESENTATION_PICKER_SOURCE_RULE = (
    "Run replay presentation artifact picker is blocked",
    r"\bRun::PromptLoadReplayPresentationArtifact\s*\(",
    "Keep replay artifact picker prompts scoped to replay scrubber input.",
)

RUN_REPLAY_SCRUBBER_SAVE_SOURCE_RULE = (
    "Run replay scrubber save helper is blocked",
    r"\bRun::SaveReplayBufferFromScrubber\s*\(",
    "Keep replay scrubber save behavior scoped to replay tools.",
)

RUN_REPLAY_RESTORE_EVENT_SOURCE_RULE = (
    "Run replay restore event helper is blocked",
    r"\bRun::ApplyReplayEventForRestoreTarget\s*\(",
    "Keep replay event restore application scoped to the v2 target restore path.",
)

RUN_REPLAY_INSPECTION_CAMERA_SOURCE_RULE = (
    "Run replay inspection camera update wrapper is blocked",
    r"\bRun::UpdateReplayInspectionCamera\s*\(",
    "Keep replay inspection camera activation driven by ReplayRuntime state.",
)

RUN_REPLAY_INSPECTION_QUERY_SOURCE_RULE = (
    "Run replay inspection query wrappers are blocked",
    r"\bRun::ReplayInspection(?:MouseLook)?Active\s*\(",
    "Query replay inspection state through ReplayRuntime.",
)

RUN_REPLAY_LIVE_ADVANCE_SOURCE_RULE = (
    "Run replay live-advance wrapper is blocked",
    r"\bRun::SetReplayLiveAdvanceHeld\s*\(",
    "Set replay live-advance state through ReplayRuntime.",
)

RUN_REPLAY_SCRUBBER_RESET_SOURCE_RULE = (
    "Run replay scrubber reset wrapper is blocked",
    r"\bRun::ResetReplayScrubber\s*\(",
    "Reset replay scrubber state through ReplayRuntime.",
)

RUN_REPLAY_EVENT_FRAME_CURSOR_SOURCE_RULE = (
    "Run replay event frame cursor wrapper is blocked",
    r"\bRun::NextReplayEventFrameIndex\s*\(",
    "Read replay event frame cursors from ReplayRuntime.",
)

RUN_REPLAY_EVENT_RECORD_SOURCE_RULE = (
    "Run replay event record wrapper is blocked",
    r"\bRun::RecordReplayEvent\s*\(",
    "Append replay events through ReplayRuntime.",
)

RUN_REPLAY_GENERATED_SCENE_CONFIG_SOURCE_RULE = (
    "Run replay generated-scene config wrapper is blocked",
    r"\bRun::RecordReplayGeneratedSceneConfigEvent\s*\(",
    "Keep generated-scene replay config appends local to timeline reset.",
)

RUN_REPLAY_PHYSICS_CAPTURE_SOURCE_RULE = (
    "Run replay physics capture wrappers are blocked",
    r"\bRun::CaptureReplayPhysicsStep(?:Thunk)?\s*\(",
    "Keep replay capture in the post-physics hook.",
)

RUN_REPLAY_WORLD_OVERRIDE_EVENT_SOURCE_RULE = (
    "Run replay world override event wrapper is blocked",
    r"\bRun::RecordReplayWorldOverrideEvent\s*\(",
    "Record world override replay events through ReplayRuntime.",
)

RUN_REPLAY_LAUNCHER_CONFIG_EVENT_SOURCE_RULE = (
    "Run replay launcher config event wrapper is blocked",
    r"\bRun::RecordReplayLauncherConfigEvent\s*\(",
    "Record launcher config replay events through ReplayRuntime.",
)

RUN_REPLAY_LAUNCHER_FIRE_EVENT_SOURCE_RULE = (
    "Run replay launcher fire event wrapper is blocked",
    r"\bRun::RecordReplayLauncherFireEvent\s*\(",
    "Record launcher fire replay events through ReplayRuntime.",
)

RUN_REPLAY_EDITOR_PLACE_EVENT_SOURCE_RULE = (
    "Run replay editor place event wrapper is blocked",
    r"\bRun::RecordReplayEditorPlaceEvent\s*\(",
    "Record editor placement replay events through ReplayRuntime.",
)

RUN_REPLAY_EDITOR_TRANSFORM_EVENT_SOURCE_RULE = (
    "Run replay editor transform event wrapper is blocked",
    r"\bRun::RecordReplayEditorTransformEvent\s*\(",
    "Record editor transform replay events through ReplayRuntime.",
)

RUN_REPLAY_LOADED_PRESENTATION_SCRUBBER_SOURCE_RULE = (
    "Run loaded-presentation scrubber arming wrapper is blocked",
    r"\bRun::ArmLoadedReplayPresentationScrubber\s*\(",
    "Arm loaded replay presentation scrubber state through ReplayRuntime.",
)

RUN_REPLAY_CAMERA_FOCUS_CLEAR_SOURCE_RULE = (
    "Run replay camera focus clear wrapper is blocked",
    r"\bRun::ClearReplayCameraFocus\s*\(",
    "Clear replay camera focus state through ReplayRuntime.",
)

RUN_REPLAY_VELOCITY_TARGET_SOURCE_RULE = (
    "Run replay velocity target lookup helpers are blocked",
    r"\bRun::ResolveReplayVelocityEditModelIndex\s*\(",
    "Resolve replay velocity edit targets through ReplayRuntime instead of Run.",
)

RUN_REPLAY_VELOCITY_HIT_SOURCE_RULE = (
    "Run replay velocity hit helpers are blocked",
    r"\bRun::(?:HitReplayVelocity(?:Linear|Angular)Axis|TryReplayVelocity(?:AxisRayParameter|AngularRayAngle))\s*\(",
    "Keep replay velocity hit and ray helpers file-local or in ReplayRuntime instead of Run.",
)

RUN_REPLAY_VELOCITY_TOGGLE_SOURCE_RULE = (
    "Run replay velocity edit toggle helper is blocked",
    r"\bRun::SetReplayVelocityEditEnabled\s*\(",
    "Set replay velocity edit state through ReplayRuntime and keep interaction transitions at the call site.",
)

RUN_REPLAY_VELOCITY_APPLY_SOURCE_RULE = (
    "Run replay velocity apply helper is blocked",
    r"\bRun::ApplyReplayVelocityEdit(?:ToModel|Drag)\s*\(",
    "Keep replay velocity model mutation helpers file-local or in ReplayRuntime instead of Run.",
)

RUN_REPLAY_OVERLAY_SOURCE_RULE = (
    "Run replay overlay render helpers are blocked",
    r"\bRun::RenderReplay(?:Scrubber|CauseTree)Overlay\s*\(",
    "Draw replay overlays from RuntimeRenderHost or a replay-owned render service instead of Run.",
)

RUN_SCENE_RESET_SOURCE_RULE = (
    "Run scene runtime reset helpers are blocked",
    r"\bRun::(?:CaptureSceneRuntimeResetSnapshot|RestoreSceneRuntimeResetSnapshot|ClearSceneRuntimeUIOverrides)\s*\(",
    "Keep scene reload preserve/reset policy in Runtime/Scene scene runtime helpers.",
)

RUN_SCENE_QUEUE_SOURCE_RULE = (
    "Run scene queue wrappers are blocked",
    r"\bRun::(?:HasSceneQueueEntry|HasCurrentSceneQueueEntry|CurrentSceneQueuePath)\s*\(",
    "Use SceneController/SceneRuntime accessors directly while scene load ownership moves out of Run.",
)

RUN_SCENE_CONTEXT_BUILDER_SOURCE_RULE = (
    "Run scene context builders are blocked",
    r"\bRun::BuildScene(?:Authored|Generated)(?:Camera|Model)Context\s*\(",
    "Keep scene setup contexts file-local with explicit borrowed dependencies.",
)

RUN_GENERATED_CAMERA_SETUP_SOURCE_RULE = (
    "Run generated camera setup wrapper is blocked",
    r"\bRun::SetUpCameras\s*\(",
    "Call SceneGeneratedSetup::SetUpCameras through an explicit scene setup context.",
)

RUN_SCENE_TERRAIN_WORLD_SOURCE_RULE = (
    "Run scene terrain/world setup wrappers are blocked",
    r"\bRun::(?:ApplyConfiguredWorldEnvironment|ApplyNoWaterOverride|UseDefaultTerrain|"
    r"UseFlatSlopeTerrain|UpdateWorldTerrainBounds)\s*\(",
    "Keep scene terrain and world setup policy file-local or in Runtime/Scene helpers.",
)

RUN_EDITABLE_SCENE_SNAPSHOT_SOURCE_RULE = (
    "Run editable scene snapshot helper is blocked",
    r"\bRun::SaveCurrentEditableSceneSnapshot\s*\(",
    "Keep editable-scene snapshot persistence file-local or in Runtime/Scene helpers.",
)

RUN_TORNADO_DEFAULTS_SOURCE_RULE = (
    "Run tornado defaults helper is blocked",
    r"\bRun::ApplyTornadoDefaultsForActiveScene\s*\(",
    "Keep scene tornado default policy file-local or in Runtime/Scene helpers.",
)

RUN_TORNADO_SYNC_SOURCE_RULE = (
    "Run tornado physics sync wrapper is blocked",
    r"\bRun::SyncTornadoFieldToPhysics\s*\(",
    "Sync tornado runtime settings to physics through an explicit helper instead of Run.",
)

RUN_DIAGNOSTICS_SOURCE_RULE = (
    "Run diagnostics perf-memory wrappers are blocked",
    r"\bRun::LogPerfMemory\s*\(",
    "Call DiagnosticsRuntime::LogPerfMemory directly so diagnostics owns perf-log policy.",
)

RUN_DIAGNOSTICS_PERF_TICK_SOURCE_RULE = (
    "Run diagnostics perf-log tick wrappers are blocked",
    r"\bRun::TickPerfLog\s*\(",
    "Call DiagnosticsRuntime::TickPerfLog directly so diagnostics owns perf-log lifecycle.",
)

RUN_DIAGNOSTICS_MEMORY_DUMP_SOURCE_RULE = (
    "Run diagnostics memory-dump wrapper is blocked",
    r"\bRun::WriteMainMemoryDump\s*\(",
    "Call DiagnosticsRuntime::WriteMainMemoryDump directly so diagnostics owns memory dump policy.",
)

RUN_DIAGNOSTICS_UI_STRESS_RNG_SOURCE_RULE = (
    "Run diagnostics UI stress RNG helpers are blocked",
    r"\bRun::NextUIStress(?:Random|Int|Float)\s*\(",
    "Keep deterministic UI stress RNG helpers file-local or in DiagnosticsRuntime.",
)

RUN_SCENE_PERF_LOG_LIFECYCLE_RULE = (
    "RunScene direct perf-log lifecycle access is blocked",
    r"\bm_diagnosticsRuntime\.PerfLog\(\)\.(?:isPerfTest|perfHeaderWritten|perfLogPath|perfLogFile|"
    r"isPerfLogFlushEnabled|perfLogFlushInterval|perfLogWritesSinceFlush)\b|"
    r"\bfopen_s\s*\([^;{}]*\bm_diagnosticsRuntime\.PerfLog\(\)\.",
    "Use DiagnosticsRuntime perf-log lifecycle APIs instead of mutating/opening perf logs from RunScene.",
)

RUN_SCENE_CONTROL_SOURCE_RULE = (
    "Run scene-control wrappers are blocked",
    r"\bRun::(?:LoadSceneFromBrowserIndex|LoadDemoSceneFromUI|ApplyAdjacentCinematicMode|"
    r"LoadAdjacentSceneFromBrowser|ResetCurrentScene|AdvanceScene)\s*\(",
    "Call SceneRuntimeCoordinator directly while scene load ownership moves out of Run.",
)

RUN_SCENE_BROWSER_INDEX_SOURCE_RULE = (
    "Run scene browser index wrapper is blocked",
    r"\bRun::CurrentSceneBrowserIndex\s*\(",
    "Resolve current scene browser selection through SceneRuntimeLoad helpers.",
)

RUN_SCENE_BROWSER_REFRESH_SOURCE_RULE = (
    "Run scene browser refresh wrapper is blocked",
    r"\bRun::RefreshSceneBrowserList\s*\(",
    "Refresh scene browser discovery through SceneRuntimeLoad helpers.",
)

RUN_SCENE_DEFAULTS_SOURCE_RULE = (
    "Run scene default persistence wrappers are blocked",
    r"\bRun::Save(?:Render|Sky)Defaults\s*\(",
    "Persist render defaults through SceneRuntimeDefaults helpers with explicit config payloads.",
)

RUN_SCENE_CREATE_SOURCE_RULE = (
    "Run scene create wrapper is blocked",
    r"\bRun::CreateSceneFromUI\s*\(",
    "Create starter scene files through SceneRuntimeCreate helpers.",
)

RUN_SCENE_WORLD_OVERRIDE_SOURCE_RULE = (
    "Run scene world override wrapper is blocked",
    r"\bRun::ApplyUIWorldOverride\s*\(",
    "Apply live world overrides through RuntimeTuning helpers with explicit world/replay dependencies.",
)

RUN_SCENE_GENERATED_CONTROL_SOURCE_RULE = (
    "Run scene generated control wrappers are blocked",
    r"\bRun::ApplyUI(?:ModelCountOverride|SolverObjectCounts)\s*\(",
    "Apply generated scene UI rebuilds through SceneRuntimeGeneratedControls helpers.",
)

RUN_SCENE_STYLE_SOURCE_RULE = (
    "Run scene style wrappers are blocked",
    r"\bRun::(?:ApplyCinematicModeFromBrowserIndex|ApplyLiveStyleScene|ApplyDemoHeroStyleOverride)\s*\(",
    "Apply live style and cinematic scene overrides through SceneRuntimeStyle helpers.",
)

RUN_SCENE_COORDINATOR_CALLBACK_SOURCE_RULE = (
    "Run scene coordinator callback builders are blocked",
    r"\bRun::BuildSceneRuntimeCoordinatorCallbacks\s*\(",
    "Return explicit SceneRuntimeCoordinator actions instead of callback-bouncing through Run.",
)

SCENE_RUNTIME_COORDINATOR_CALLBACK_RULE = (
    "SceneRuntimeCoordinator callbacks are blocked",
    r"\b(?:SceneRuntimeCoordinatorCallbacks|m_callbacks)\b",
    "Return explicit SceneRuntimeCoordinator actions instead of storing callback dispatch state.",
)

ALLOWED_CAMERA_MODE_WRITE_FUNCTIONS = {
    ( RUN_INPUT_SOURCE, "SetCameraModeLabelAfterInteractionTransition" ),
}

ALLOWED_WORLD_OWNER_WRITE_FUNCTIONS = {
    ( RUN_INPUT_SOURCE, "SetWorldInteractionOwnerAfterInteractionTransition" ),
}

ALLOWED_RENDER_HOST_BINDINGS = {
    "runtime",
    "world",
    "scene",
    "replayOverlay",
    "toolOverlay",
    "ui",
    "diagnostics",
}

# Concept: RuntimeRenderHost views are narrow borrowed seams, not escape hatches.
# Each view gets its own field allowlist so broad state cannot hide under a
# legitimate top-level binding root.
ALLOWED_RENDER_HOST_VIEW_FIELDS = {
    "RenderRuntimeView": {
        "systems",
        "launchOptions",
        "runtimeSettings",
    },
    "RenderWorldView": {
        "gameModelCollection",
        "worldEnvironment",
        "collisionVisualizer",
        "broadphaseVisualizer",
        "physicsDebugVisualizer",
    },
    "RenderSceneView": {
        "sceneController",
        "sceneBrowser",
    },
    "RenderReplayOverlayView": {
        "replayRuntime",
    },
    "RenderToolOverlayView": {
        "tools",
    },
    "RenderUiView": {
        "ui",
        "runtimeInput",
        "camera",
        "runtimeViewModel",
    },
    "RenderDiagnosticsView": {
        "diagnosticsRuntime",
        "debug",
        "timers",
    },
}

ALLOWED_RENDER_HOST_CALLBACK_TYPEDEFS = {
    "LogLifecycleStepFn",
    "RenderEditorOverlayFn",
    "VoidFn",
    "CameraModeEnabledMaskFn",
    "CameraModeLabelFn",
}

ALLOWED_MUTABLE_RENDER_HOST_CALLBACK_TYPEDEFS = set()

ALLOWED_RENDER_HOST_CALLBACK_FIELDS = {
    "user",
    "logRenderResourceLifecycleStep",
    "renderEditorOverlay",
    "refreshRuntimeViewModel",
    "cameraModeEnabledMask",
    "cameraModeLabel",
}


@dataclass(frozen=True)
class BoundaryError:
    path: Path
    line: int
    message: str
    detail: str


def repo_relative(repo: Path, path: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo.resolve())).replace("/", "\\")
    except ValueError:
        return str(path)


def strip_cpp_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda match: "\n" * match.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//.*", "", text)


def line_for_offset(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def extract_struct_body(stripped: str, struct_name: str) -> tuple[int, str] | None:
    match = re.search(rf"\bstruct\s+{re.escape(struct_name)}\s*\{{", stripped)
    if not match:
        return None
    open_brace_offset = stripped.find("{", match.start(), match.end())
    if open_brace_offset < 0:
        return None
    close_brace_offset = find_matching_close_brace(stripped, open_brace_offset)
    return open_brace_offset + 1, stripped[open_brace_offset + 1 : close_brace_offset]


def line_for_struct_offset(stripped: str, body_start_offset: int, local_offset: int) -> int:
    return line_for_offset(stripped, body_start_offset + local_offset)


def extract_run_private_section(stripped: str) -> tuple[int, str] | None:
    match = re.search(r"\bclass\s+Run\s*\{", stripped)
    if not match:
        return None
    open_brace_offset = stripped.find("{", match.start(), match.end())
    if open_brace_offset < 0:
        return None
    close_brace_offset = find_matching_close_brace(stripped, open_brace_offset)
    body_start_offset = open_brace_offset + 1
    body = stripped[body_start_offset:close_brace_offset]
    private_match = re.search(r"\bprivate\s*:", body)
    if not private_match:
        return None
    private_start = body_start_offset + private_match.end()
    public_match = re.search(r"\bpublic\s*:", body[private_match.end() :])
    private_end = (
        body_start_offset + private_match.end() + public_match.start()
        if public_match
        else close_brace_offset
    )
    return private_start, stripped[private_start:private_end]


def check_run_private_method_count_text(
    path: Path,
    text: str,
    max_allowed: int = MAX_RUN_PRIVATE_METHOD_DECLARATIONS,
) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    private_section = extract_run_private_section(stripped)
    if private_section is None:
        return []

    private_start, private_body = private_section
    method_count = len(RUN_PRIVATE_METHOD_DECLARATION_PATTERN.findall(private_body))
    if method_count <= max_allowed:
        return []

    return [
        BoundaryError(
            path,
            line_for_offset(stripped, private_start),
            "Run.h private method count exceeds ratchet",
            f"Found {method_count}; maximum is {max_allowed}. Move behavior to a subsystem or update the ratchet intentionally.",
        )
    ]


def check_text_rules(path: Path, text: str, rules: tuple[tuple[str, str, str], ...]) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    errors: list[BoundaryError] = []
    for message, pattern, detail in rules:
        for match in re.finditer(pattern, stripped):
            errors.append(BoundaryError(path, line_for_offset(stripped, match.start()), message, detail))
    return errors


def check_run_internal_scrubber_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_INTERNAL_SCRUBBER_HELPER_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_internal_scrubber_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / RUN_INTERNAL_HEADER
    return check_run_internal_scrubber_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_run_internal_replay_layout_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_INTERNAL_REPLAY_LAYOUT_HELPER_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_internal_replay_layout_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / RUN_INTERNAL_HEADER
    return check_run_internal_replay_layout_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_run_internal_scene_runtime_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_INTERNAL_SCENE_RUNTIME_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_internal_scene_runtime_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / RUN_INTERNAL_HEADER
    return check_run_internal_scene_runtime_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_run_internal_scene_style_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_INTERNAL_SCENE_STYLE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_internal_scene_style_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / RUN_INTERNAL_HEADER
    return check_run_internal_scene_style_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_run_storage(repo: Path) -> list[BoundaryError]:
    message, pattern, detail = RUN_STORAGE_RULE
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*.h")):
        if path.name == "Run.h":
            continue
        text = path.read_text(encoding="utf-8")
        stripped = strip_cpp_comments(text)
        for match in re.finditer(pattern, stripped):
            errors.append(BoundaryError(path, line_for_offset(stripped, match.start()), message, detail))
    return errors


def check_physics_game_model_collection_guardrails_text(
    path: Path,
    text: str,
    relative_path: Path | None = None,
    allowlist: Counter[tuple[Path, str]] | None = None,
) -> list[BoundaryError]:
    key_path = relative_path or path
    allowed = PHYSICS_GAME_MODEL_COLLECTION_ALLOWLIST if allowlist is None else allowlist
    stripped = strip_cpp_comments(text)
    seen: Counter[tuple[Path, str]] = Counter()
    errors: list[BoundaryError] = []

    for line_no, line in enumerate(stripped.splitlines(), start=1):
        if not GAME_MODEL_COLLECTION_PATTERN.search(line):
            continue

        normalized = normalize_boundary_line(line)
        key = ( key_path, normalized )
        seen[key] += 1
        if seen[key] > allowed.get(key, 0):
            errors.append(
                BoundaryError(
                    path,
                    line_no,
                    "new physics GameModelCollection dependencies are blocked",
                    "Use physics stores, stable handles, diagnostics views, or a named compatibility adapter instead.",
                )
            )

    return errors


def check_physics_game_model_collection_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / PHYSICS_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(
            check_physics_game_model_collection_guardrails_text(
                path,
                path.read_text(encoding="utf-8"),
                path.relative_to(repo),
            )
        )
    return errors


def check_deleted_physics_model_view_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_DELETED_MODEL_VIEW_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "deleted PhysicsModelView boundary is blocked",
                "Use PhysicsModelAccess plus stores/handles instead of recreating MakePhysicsModelView or PhysicsModelView.",
            )
        )
    return errors


def check_deleted_physics_model_view_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for root in (repo / PHYSICS_ROOT, repo / Path("SkullbonezSource/GameObjects")):
        for path in sorted(root.rglob("*")):
            if path.suffix not in { ".cpp", ".h", ".hpp", ".inl" }:
                continue
            errors.extend(check_deleted_physics_model_view_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_physics_models_access_guardrails_text(
    path: Path,
    text: str,
    relative_path: Path | None = None,
    allowlist: Counter[tuple[Path, str]] | None = None,
) -> list[BoundaryError]:
    key_path = relative_path or path
    allowed = PHYSICS_MODELS_ACCESS_ALLOWLIST if allowlist is None else allowlist
    stripped = strip_cpp_comments(text)
    seen: Counter[tuple[Path, str]] = Counter()
    errors: list[BoundaryError] = []

    for line_no, line in enumerate(stripped.splitlines(), start=1):
        if not PHYSICS_MODELS_ACCESS_PATTERN.search(line):
            continue

        normalized = normalize_boundary_line(line)
        key = ( key_path, normalized )
        seen[key] += 1
        if seen[key] > allowed.get(key, 0):
            errors.append(
                BoundaryError(
                    path,
                    line_no,
                    "direct PhysicsModels() compatibility access is blocked",
                    "Use PhysicsModelAccess, stores, stable handles, or a named compatibility adapter instead.",
                )
            )

    return errors


def check_physics_models_access_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / Path("SkullbonezSource")).rglob("*")):
        if path.suffix not in { ".cpp", ".h", ".hpp", ".inl" }:
            continue
        errors.extend(
            check_physics_models_access_guardrails_text(
                path,
                path.read_text(encoding="utf-8"),
                path.relative_to(repo),
            )
        )
    return errors


def check_direct_gfx_raytracing_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    errors: list[BoundaryError] = []
    for match in DIRECT_GFX_RAYTRACING_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "direct Gfx() raytracing calls are blocked",
                "Use GfxRayTracing()/IRenderRayTracing so DXR reflection does not live on the wide IRenderBackend facade.",
            )
        )
    return errors


def check_direct_gfx_raytracing_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / Path("SkullbonezSource")).rglob("*")):
        if path.suffix not in { ".cpp", ".h", ".hpp", ".inl" }:
            continue
        errors.extend(check_direct_gfx_raytracing_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_irender_backend_raytracing_declarations_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    errors: list[BoundaryError] = []
    for match in IRENDER_BACKEND_RAYTRACING_DECLARATION_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "raytracing declarations are blocked on IRenderBackend",
                "Put raytracing methods on IRenderRayTracing so the wide backend facade does not regrow DXR reflection ownership.",
            )
        )
    return errors


def check_irender_backend_raytracing_declarations(repo: Path) -> list[BoundaryError]:
    path = repo / IRENDER_BACKEND_HEADER
    return check_irender_backend_raytracing_declarations_text(path, path.read_text(encoding="utf-8"))


def check_graph_owned_render_pass_scheduling_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    errors: list[BoundaryError] = []
    for match in GRAPH_OWNED_RENDER_PASS_DIRECT_CALL_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "graph-owned render pass direct scheduling is blocked",
                "Use the RuntimeRenderer render graph helper so migrated pass callbacks keep resource declarations and dry-run checks.",
            )
        )
    return errors


def check_graph_owned_render_pass_scheduling(repo: Path) -> list[BoundaryError]:
    path = repo / RUN_RENDER_SOURCE
    return check_graph_owned_render_pass_scheduling_text(path, path.read_text(encoding="utf-8"))


def render_host_member_declarations(body: str) -> list[tuple[str, int]]:
    declarations: list[tuple[str, int]] = []
    pattern = re.compile(
        r"^[ \t]*(?:[A-Za-z0-9_:<>*,\s]+?)\s+([A-Za-z_]\w*)\s*(?:=\s*nullptr|\{\s*nullptr\s*\})?\s*;",
        re.M,
    )
    for match in pattern.finditer(body):
        declarations.append(( match.group(1), match.start() ))
    return declarations


def render_host_callback_typedefs(body: str) -> list[tuple[str, str, int]]:
    typedefs: list[tuple[str, str, int]] = []
    pattern = re.compile(r"^\s*using\s+([A-Za-z_]\w*)\s*=\s*([^;]+);", re.M)
    for match in pattern.finditer(body):
        typedefs.append(( match.group(1), match.group(2), match.start() ))
    return typedefs


def callback_returns_mutable_reference(signature: str) -> bool:
    match = re.search(r"(?P<return_type>.*?)\(\s*\*\s*\)", signature)
    if not match:
        return False
    return_type = match.group("return_type")
    if "&" in return_type:
        return "const" not in return_type.split("&", 1)[0].split()
    if "*" in return_type:
        return "const" not in return_type.split("*", 1)[0].split()
    return False


def check_runtime_render_host_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    errors: list[BoundaryError] = []

    for view_name, allowed_fields in ALLOWED_RENDER_HOST_VIEW_FIELDS.items():
        view = extract_struct_body(stripped, view_name)
        if view is None:
            continue

        body_start, body = view
        for field_name, local_offset in render_host_member_declarations(body):
            if field_name not in allowed_fields:
                errors.append(
                    BoundaryError(
                        path,
                        line_for_struct_offset(stripped, body_start, local_offset),
                        f"new {view_name} fields are blocked",
                        "Plan engine-evaluation-fix-01-runtime-ownership-plan.md: add render host dependencies through an owner-specific API and update the view allowlist deliberately.",
                    )
                )

    bindings = extract_struct_body(stripped, "RuntimeRenderHostBindings")
    if bindings is not None:
        body_start, body = bindings
        for field_name, local_offset in render_host_member_declarations(body):
            if field_name not in ALLOWED_RENDER_HOST_BINDINGS:
                errors.append(
                    BoundaryError(
                        path,
                        line_for_struct_offset(stripped, body_start, local_offset),
                        "new RuntimeRenderHostBindings fields are blocked",
                        "Plan engine-evaluation-fix-01-runtime-ownership-plan.md: split render-facing state into a narrow view instead of growing RuntimeRenderHost.",
                    )
                )

    callbacks = extract_struct_body(stripped, "RuntimeRenderHostCallbacks")
    if callbacks is not None:
        body_start, body = callbacks
        for typedef_name, signature, local_offset in render_host_callback_typedefs(body):
            if typedef_name not in ALLOWED_RENDER_HOST_CALLBACK_TYPEDEFS:
                errors.append(
                    BoundaryError(
                        path,
                        line_for_struct_offset(stripped, body_start, local_offset),
                        "new RuntimeRenderHostCallbacks typedefs are blocked",
                        "Plan engine-evaluation-fix-01-runtime-ownership-plan.md: move the callback behind a subsystem-owned service before wiring render host access.",
                    )
                )
            if (
                callback_returns_mutable_reference(signature)
                and typedef_name not in ALLOWED_MUTABLE_RENDER_HOST_CALLBACK_TYPEDEFS
            ):
                errors.append(
                    BoundaryError(
                        path,
                        line_for_struct_offset(stripped, body_start, local_offset),
                        "mutable RuntimeRenderHostCallbacks returns are blocked",
                        "Render callbacks may observe state; new mutable subsystem access should move to an owner API.",
                    )
                )
        for field_name, local_offset in render_host_member_declarations(body):
            if field_name not in ALLOWED_RENDER_HOST_CALLBACK_FIELDS:
                errors.append(
                    BoundaryError(
                        path,
                        line_for_struct_offset(stripped, body_start, local_offset),
                        "new RuntimeRenderHostCallbacks fields are blocked",
                        "Plan engine-evaluation-fix-01-runtime-ownership-plan.md: narrow the render service surface instead of adding another Run callback.",
                    )
                )

    return errors


def check_runtime_render_host_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / RUNTIME_RENDER_HOST_HEADER
    return check_runtime_render_host_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_pick_helper_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = PICK_HELPER_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_pick_helper_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        if path.name in { "RuntimePickService.cpp", "RuntimePickService.h" }:
            continue
        errors.extend(check_pick_helper_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_cause_tree_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_CAUSE_TREE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_cause_tree_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_cause_tree_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_prediction_job_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_PREDICTION_JOB_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_prediction_job_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_prediction_job_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_prediction_capture_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_PREDICTION_CAPTURE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_prediction_capture_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_prediction_capture_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_prediction_lifecycle_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_PREDICTION_LIFECYCLE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_prediction_lifecycle_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_prediction_lifecycle_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_path_state_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_PATH_STATE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_path_state_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_path_state_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_cause_tree_lookup_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_CAUSE_TREE_LOOKUP_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_cause_tree_lookup_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(
            check_run_replay_cause_tree_lookup_source_guardrails_text(path, path.read_text(encoding="utf-8"))
        )
    return errors


def check_run_replay_cause_tree_focus_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_CAUSE_TREE_FOCUS_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_cause_tree_focus_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_cause_tree_focus_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_cause_tree_camera_activation_source_guardrails_text(path: Path,
                                                                         text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_CAUSE_TREE_CAMERA_ACTIVATION_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_cause_tree_camera_activation_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(
            check_run_replay_cause_tree_camera_activation_source_guardrails_text(path, path.read_text(encoding="utf-8"))
        )
    return errors


def check_run_replay_render_state_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_RENDER_STATE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_render_state_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_render_state_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_launcher_visual_sample_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_LAUNCHER_VISUAL_SAMPLE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_launcher_visual_sample_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(
            check_run_replay_launcher_visual_sample_source_guardrails_text(path, path.read_text(encoding="utf-8"))
        )
    return errors


def check_run_replay_sample_comparison_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_SAMPLE_COMPARISON_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_sample_comparison_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_sample_comparison_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_render_host_texture_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_RENDER_HOST_TEXTURE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_render_host_texture_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_render_host_texture_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_presentation_picker_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_PRESENTATION_PICKER_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_presentation_picker_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_presentation_picker_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_scrubber_save_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_SCRUBBER_SAVE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_scrubber_save_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_scrubber_save_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_restore_event_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_RESTORE_EVENT_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_restore_event_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_restore_event_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_inspection_camera_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_INSPECTION_CAMERA_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_inspection_camera_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_inspection_camera_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_inspection_query_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_INSPECTION_QUERY_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_inspection_query_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_inspection_query_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_live_advance_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_LIVE_ADVANCE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_live_advance_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_live_advance_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_scrubber_reset_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_SCRUBBER_RESET_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_scrubber_reset_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_scrubber_reset_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_event_frame_cursor_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_EVENT_FRAME_CURSOR_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_event_frame_cursor_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_event_frame_cursor_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_event_record_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_EVENT_RECORD_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_event_record_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_event_record_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_generated_scene_config_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_GENERATED_SCENE_CONFIG_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_generated_scene_config_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(
            check_run_replay_generated_scene_config_source_guardrails_text(path, path.read_text(encoding="utf-8"))
        )
    return errors


def check_run_replay_physics_capture_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_PHYSICS_CAPTURE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_physics_capture_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_physics_capture_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_world_override_event_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_WORLD_OVERRIDE_EVENT_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_world_override_event_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_world_override_event_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_launcher_config_event_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_LAUNCHER_CONFIG_EVENT_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_launcher_config_event_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(
            check_run_replay_launcher_config_event_source_guardrails_text(path, path.read_text(encoding="utf-8"))
        )
    return errors


def check_run_replay_launcher_fire_event_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_LAUNCHER_FIRE_EVENT_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_launcher_fire_event_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_launcher_fire_event_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_editor_place_event_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_EDITOR_PLACE_EVENT_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_editor_place_event_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_editor_place_event_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_editor_transform_event_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_EDITOR_TRANSFORM_EVENT_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_editor_transform_event_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(
            check_run_replay_editor_transform_event_source_guardrails_text(path, path.read_text(encoding="utf-8"))
        )
    return errors


def check_run_replay_loaded_presentation_scrubber_source_guardrails_text(
    path: Path, text: str
) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_LOADED_PRESENTATION_SCRUBBER_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_loaded_presentation_scrubber_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(
            check_run_replay_loaded_presentation_scrubber_source_guardrails_text(
                path, path.read_text(encoding="utf-8")
            )
        )
    return errors


def check_run_replay_camera_focus_clear_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_CAMERA_FOCUS_CLEAR_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_camera_focus_clear_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_camera_focus_clear_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_velocity_target_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_VELOCITY_TARGET_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_velocity_target_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_velocity_target_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_velocity_hit_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_VELOCITY_HIT_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_velocity_hit_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_velocity_hit_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_velocity_toggle_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_VELOCITY_TOGGLE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_velocity_toggle_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_velocity_toggle_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_velocity_apply_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_VELOCITY_APPLY_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_velocity_apply_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_velocity_apply_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_replay_overlay_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_REPLAY_OVERLAY_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_replay_overlay_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_replay_overlay_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_scene_reset_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_RESET_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_reset_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_scene_reset_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_scene_queue_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_QUEUE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_queue_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_scene_queue_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_scene_context_builder_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_CONTEXT_BUILDER_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_context_builder_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_scene_context_builder_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_generated_camera_setup_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_GENERATED_CAMERA_SETUP_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_generated_camera_setup_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_generated_camera_setup_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_scene_terrain_world_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_TERRAIN_WORLD_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_terrain_world_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_scene_terrain_world_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_editable_scene_snapshot_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_EDITABLE_SCENE_SNAPSHOT_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_editable_scene_snapshot_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_editable_scene_snapshot_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_tornado_defaults_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_TORNADO_DEFAULTS_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_tornado_defaults_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_tornado_defaults_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_tornado_sync_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_TORNADO_SYNC_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_tornado_sync_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_tornado_sync_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_diagnostics_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_DIAGNOSTICS_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_diagnostics_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_diagnostics_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_diagnostics_perf_tick_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_DIAGNOSTICS_PERF_TICK_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_diagnostics_perf_tick_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(
            check_run_diagnostics_perf_tick_source_guardrails_text(path, path.read_text(encoding="utf-8"))
        )
    return errors


def check_run_diagnostics_memory_dump_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_DIAGNOSTICS_MEMORY_DUMP_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_diagnostics_memory_dump_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(
            check_run_diagnostics_memory_dump_source_guardrails_text(path, path.read_text(encoding="utf-8"))
        )
    return errors


def check_run_diagnostics_ui_stress_rng_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_DIAGNOSTICS_UI_STRESS_RNG_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_diagnostics_ui_stress_rng_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(
            check_run_diagnostics_ui_stress_rng_source_guardrails_text(path, path.read_text(encoding="utf-8"))
        )
    return errors


def check_run_scene_perf_log_lifecycle_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_PERF_LOG_LIFECYCLE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_perf_log_lifecycle_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / RUN_SCENE_SOURCE
    if not path.exists():
        return []
    return check_run_scene_perf_log_lifecycle_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_run_scene_control_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_CONTROL_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_control_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_scene_control_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_scene_browser_index_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_BROWSER_INDEX_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_browser_index_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_scene_browser_index_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_scene_browser_refresh_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_BROWSER_REFRESH_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_browser_refresh_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_scene_browser_refresh_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_scene_defaults_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_DEFAULTS_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_defaults_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_scene_defaults_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_scene_create_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_CREATE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_create_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_scene_create_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_scene_world_override_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_WORLD_OVERRIDE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_world_override_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_scene_world_override_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_scene_generated_control_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_GENERATED_CONTROL_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_generated_control_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_scene_generated_control_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_scene_style_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_STYLE_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_style_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_run_scene_style_source_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_scene_coordinator_callback_source_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_SCENE_COORDINATOR_CALLBACK_SOURCE_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_scene_coordinator_callback_source_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(
            check_run_scene_coordinator_callback_source_guardrails_text(path, path.read_text(encoding="utf-8"))
        )
    return errors


def check_scene_runtime_coordinator_callback_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = SCENE_RUNTIME_COORDINATOR_CALLBACK_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_scene_runtime_coordinator_callback_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    scene_runtime_dir = repo / RUNTIME_ROOT / "Scene"
    for path in sorted(scene_runtime_dir.glob("SceneRuntimeCoordinator.*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        errors.extend(check_scene_runtime_coordinator_callback_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_run_ui_text_pass_replay_overlay_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    message, pattern, detail = RUN_UI_TEXT_PASS_REPLAY_OVERLAY_RULE
    return [
        BoundaryError(path, line_for_offset(stripped, match.start()), message, detail)
        for match in re.finditer(pattern, stripped)
    ]


def check_run_ui_text_pass_replay_overlay_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / RUNTIME_ROOT / "RunUiTextPass.cpp"
    return check_run_ui_text_pass_replay_overlay_guardrails_text(path, path.read_text(encoding="utf-8"))


def run_self_tests() -> list[str]:
    failures: list[str] = []
    synthetic_path = Path("synthetic/RuntimeRenderHost.h")
    allowed_physics_path = Path("SkullbonezSource/Physics/PhysicsEngine.h")
    allowed_physics_dependency = "class GameModelCollection;"
    synthetic_physics_allowlist: Counter[tuple[Path, str]] = Counter(
        { ( allowed_physics_path, allowed_physics_dependency ): 1 }
    )

    allowed_host = """
    struct RenderRuntimeView
    {
        RunSubsystemState* systems = nullptr;
        const RunLaunchOptions* launchOptions = nullptr;
        RunRuntimeSettings* runtimeSettings = nullptr;
    };
    struct RenderWorldView
    {
        GameModelCollection* gameModelCollection = nullptr;
        WorldEnvironment* worldEnvironment = nullptr;
        CollisionVisualizer* collisionVisualizer = nullptr;
        BroadphaseVisualizer* broadphaseVisualizer = nullptr;
        PhysicsDebugVisualizer* physicsDebugVisualizer = nullptr;
    };
    struct RenderSceneView
    {
        SceneController* sceneController = nullptr;
        RunSceneBrowserState* sceneBrowser = nullptr;
    };
    struct RenderReplayOverlayView
    {
        ReplayRuntime* replayRuntime = nullptr;
    };
    struct RenderToolOverlayView
    {
        RuntimeTools* tools = nullptr;
    };
    struct RenderUiView
    {
        InGameUI* ui = nullptr;
        RuntimeInputContext* runtimeInput = nullptr;
        RunCameraState* camera = nullptr;
        RuntimeViewModel* runtimeViewModel = nullptr;
    };
    struct RenderDiagnosticsView
    {
        DiagnosticsRuntime* diagnosticsRuntime = nullptr;
        RunDebugState* debug = nullptr;
        RunTimerState* timers = nullptr;
    };
    struct RuntimeRenderHostBindings
    {
        RenderRuntimeView runtime;
        RenderWorldView world;
        RenderSceneView scene;
        RenderReplayOverlayView replayOverlay;
        RenderToolOverlayView toolOverlay;
        RenderUiView ui;
        RenderDiagnosticsView diagnostics;
    };
    struct RuntimeRenderHostCallbacks
    {
        using VoidFn = void ( * )( void* user );
        void* user = nullptr;
        VoidFn refreshRuntimeViewModel = nullptr;
    };
    """
    if check_runtime_render_host_guardrails_text(synthetic_path, allowed_host):
        failures.append("allowed RuntimeRenderHost synthetic surface failed")

    allowed_run_header = """
    class Run
    {
      private:
        void Render();
      public:
        void Execute();
    };
    """
    if check_run_private_method_count_text(Path("synthetic/Run.h"), allowed_run_header, max_allowed=1):
        failures.append("allowed Run.h private method count synthetic surface failed")

    old_run_dxr_reflection_state = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        std::array<float, MAX_GAME_MODELS * 16> m_dxrReflectionTransforms = {};",
    )
    if not any(
        error.message == "DXR reflection state must stay out of Run.h"
        for error in check_text_rules(Path("synthetic/Run.h"), old_run_dxr_reflection_state, RUN_HEADER_RULES)
    ):
        failures.append("old Run.h DXR reflection state synthetic surface was not rejected")

    allowed_raytracing_accessor = "void Render() { GfxRayTracing().BuildTLAS( matrices, count, 0, 0 ); }"
    if check_direct_gfx_raytracing_guardrails_text(
        Path("synthetic/RunPasses.cpp"),
        allowed_raytracing_accessor,
    ):
        failures.append("allowed GfxRayTracing synthetic call was rejected")

    old_direct_gfx_raytracing_call = "void Render() { Gfx().BuildTLAS( matrices, count, 0, 0 ); }"
    if not any(
        error.message == "direct Gfx() raytracing calls are blocked"
        for error in check_direct_gfx_raytracing_guardrails_text(
            Path("synthetic/RunPasses.cpp"),
            old_direct_gfx_raytracing_call,
        )
    ):
        failures.append("direct Gfx raytracing synthetic call was not rejected")

    old_irender_backend_raytracing_surface = """
    class IRenderBackend
    {
      public:
        virtual void InitDXR( uint64_t terrainVBVA, int terrainVertCount, int terrainStride ) = 0;
        virtual uint32_t GetReflectionUAVTexture() const = 0;
    };
    """
    if not any(
        error.message == "raytracing declarations are blocked on IRenderBackend"
        for error in check_irender_backend_raytracing_declarations_text(
            Path("synthetic/IRenderBackend.h"),
            old_irender_backend_raytracing_surface,
        )
    ):
        failures.append("IRenderBackend raytracing declaration synthetic surface was not rejected")

    allowed_graph_owned_pass_scheduling = """
    void RuntimeRenderer::RenderFrame()
    {
        ExecuteDebugOverlayThroughRenderGraph( frame, useCinematicTarget );
        ExecuteCinematicPostThroughRenderGraph( frame );
    }
    """
    if check_graph_owned_render_pass_scheduling_text(
        Path("synthetic/RunRender.cpp"),
        allowed_graph_owned_pass_scheduling,
    ):
        failures.append("allowed graph-owned pass helper scheduling synthetic surface failed")

    old_direct_graph_owned_pass_scheduling = """
    void RuntimeRenderer::RenderFrame()
    {
        m_debugOverlayPass.Render( frame );
        m_volumetricPass.Render( frame );
        m_tonemapPass.Render( frame, false, true );
    }
    """
    if not any(
        error.message == "graph-owned render pass direct scheduling is blocked"
        for error in check_graph_owned_render_pass_scheduling_text(
            Path("synthetic/RunRender.cpp"),
            old_direct_graph_owned_pass_scheduling,
        )
    ):
        failures.append("direct graph-owned pass scheduling synthetic surface was not rejected")

    grown_run_header = allowed_run_header.replace("void Render();", "void Render();\n        void NewHelper();")
    if not any(
        error.message == "Run.h private method count exceeds ratchet"
        for error in check_run_private_method_count_text(Path("synthetic/Run.h"), grown_run_header, max_allowed=1)
    ):
        failures.append("grown Run.h private method count synthetic surface was not rejected")

    pointer_return_run_header = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        const ReplayPresentationSample* CurrentReplayScrubSample() const;",
    )
    if not any(
        error.message == "Run.h private method count exceeds ratchet"
        for error in check_run_private_method_count_text(
            Path("synthetic/Run.h"), pointer_return_run_header, max_allowed=1
        )
    ):
        failures.append("pointer-return Run.h private method count synthetic surface was not rejected")

    short_editor_overlay_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void BuildEditorOverlay();",
    )
    if not any(
        error.message == "editor overlay/preview helpers must stay out of Run.h"
        for error in check_text_rules(Path("synthetic/Run.h"), short_editor_overlay_helper, RUN_HEADER_RULES)
    ):
        failures.append("short editor overlay helper synthetic surface was not rejected")

    short_interaction_preview_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void RefreshInteractionPreview();",
    )
    if not any(
        error.message == "editor overlay/preview helpers must stay out of Run.h"
        for error in check_text_rules(Path("synthetic/Run.h"), short_interaction_preview_helper, RUN_HEADER_RULES)
    ):
        failures.append("short interaction preview helper synthetic surface was not rejected")

    old_replay_render_query_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        const ReplayPresentationSample* CurrentReplayScrubSample() const;",
    )
    if not any(
        error.message == "replay render-query helpers must stay out of Run.h"
        for error in check_text_rules(Path("synthetic/Run.h"), old_replay_render_query_helper, RUN_HEADER_RULES)
    ):
        failures.append("replay render-query helper synthetic surface was not rejected")

    old_scrubber_visibility_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool ShouldRenderReplayScrubber() const;",
    )
    if not any(
        error.message == "replay render-query helpers must stay out of Run.h"
        for error in check_text_rules(Path("synthetic/Run.h"), old_scrubber_visibility_helper, RUN_HEADER_RULES)
    ):
        failures.append("replay scrubber visibility helper synthetic surface was not rejected")

    old_replay_prediction_ghost_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void RenderReplayPredictionGhosts( const RenderFrameContext& frame );",
    )
    if not any(
        error.message == "replay render-query helpers must stay out of Run.h"
        for error in check_text_rules(Path("synthetic/Run.h"), old_replay_prediction_ghost_helper, RUN_HEADER_RULES)
    ):
        failures.append("replay prediction ghost helper synthetic surface was not rejected")

    old_replay_cause_tree_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool BuildReplayCauseTreeRows();",
    )
    if not any(
        error.message == "replay cause-tree row builders must stay out of Run.h"
        for error in check_text_rules(Path("synthetic/Run.h"), old_replay_cause_tree_header_helper, RUN_HEADER_RULES)
    ):
        failures.append("replay cause-tree header helper synthetic surface was not rejected")

    renamed_replay_cause_tree_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool BuildCauseTreeRows();",
    )
    if not any(
        error.message == "replay cause-tree row builders must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            renamed_replay_cause_tree_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("renamed replay cause-tree header helper synthetic surface was not rejected")

    old_replay_prediction_job_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void CancelReplayPredictionJob( bool clearSamples );",
    )
    if not any(
        error.message == "replay prediction job-state helpers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_prediction_job_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay prediction job-state header helper synthetic surface was not rejected")

    old_replay_prediction_capture_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void CaptureReplayPredictionFrame( ReplayFrameIndex frameIndex );",
    )
    if not any(
        error.message == "replay prediction capture helpers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_prediction_capture_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay prediction capture header helper synthetic surface was not rejected")

    old_replay_prediction_lifecycle_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool StepReplayPredictionJob();",
    )
    if not any(
        error.message == "replay prediction lifecycle helpers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_prediction_lifecycle_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay prediction lifecycle header helper synthetic surface was not rejected")

    old_replay_path_state_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void ClearReplayPathVisualizer();",
    )
    if not any(
        error.message == "replay path-state helpers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_path_state_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay path-state header helper synthetic surface was not rejected")

    old_replay_cause_tree_lookup_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool TryResolveReplayCauseTreeBodyPosition( ReplayBodyId id );",
    )
    if not any(
        error.message == "replay cause-tree body lookup helpers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_cause_tree_lookup_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay cause-tree body lookup header helper synthetic surface was not rejected")

    old_replay_cause_tree_focus_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool FocusReplayCauseTreeBody( ReplayBodyId id );",
    )
    if not any(
        error.message == "replay cause-tree body focus wrappers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_cause_tree_focus_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay cause-tree body focus header helper synthetic surface was not rejected")

    old_replay_cause_tree_camera_activation_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void ActivateReplayCameraForCauseRow( const RunReplayCauseTreeRow& row, int rowIndex );",
    )
    if not any(
        error.message == "replay cause-tree camera activation helper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_cause_tree_camera_activation_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay cause-tree camera activation header helper synthetic surface was not rejected")

    old_replay_render_state_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void ApplyReplayRenderStateForFrame();",
    )
    if not any(
        error.message == "replay render-state helpers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_render_state_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay render-state header helper synthetic surface was not rejected")

    old_replay_launcher_visual_sample_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void BuildReplayLauncherVisualSample( ReplayLauncherVisualSample& outSample ) const;",
    )
    if not any(
        error.message == "replay launcher visual sample helpers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_launcher_visual_sample_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay launcher visual sample header helper synthetic surface was not rejected")

    old_replay_sample_comparison_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void CompareLatestReplaySamples();",
    )
    if not any(
        error.message == "replay sample comparison helper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_sample_comparison_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay sample comparison header helper synthetic surface was not rejected")

    old_render_host_texture_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n"
        "        Textures::TextureCollection& Textures();\n"
        "        uint32_t TextureHandle( uint32_t textureHash );\n"
        "        void SelectRenderTexture( uint32_t textureHash );",
    )
    if not any(
        error.message == "render host texture wrappers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_render_host_texture_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("render host texture header helpers synthetic surface was not rejected")

    old_replay_presentation_picker_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool PromptLoadReplayPresentationArtifact( HWND hwnd );",
    )
    if not any(
        error.message == "replay presentation artifact picker must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_presentation_picker_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay presentation picker header helper synthetic surface was not rejected")

    old_replay_scrubber_save_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool SaveReplayBufferFromScrubber( RunReplayTrack track );",
    )
    if not any(
        error.message == "replay scrubber save helper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_scrubber_save_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay scrubber save header helper synthetic surface was not rejected")

    old_replay_restore_event_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool ApplyReplayEventForRestoreTarget( const ReplayEventSample& event );",
    )
    if not any(
        error.message == "replay restore event helper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_restore_event_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay restore event header helper synthetic surface was not rejected")

    old_replay_inspection_camera_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void UpdateReplayInspectionCamera();",
    )
    if not any(
        error.message == "replay inspection camera update wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_inspection_camera_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay inspection camera header helper synthetic surface was not rejected")

    old_replay_inspection_query_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n"
        "        bool ReplayInspectionActive() const;\n"
        "        bool ReplayInspectionMouseLookActive() const;",
    )
    if not any(
        error.message == "replay inspection query wrappers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_inspection_query_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay inspection query header helpers synthetic surface was not rejected")

    old_replay_live_advance_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void SetReplayLiveAdvanceHeld( bool held );",
    )
    if not any(
        error.message == "replay live-advance wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_live_advance_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay live-advance header helper synthetic surface was not rejected")

    old_replay_scrubber_reset_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void ResetReplayScrubber();",
    )
    if not any(
        error.message == "replay scrubber reset wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_scrubber_reset_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay scrubber reset header helper synthetic surface was not rejected")

    old_replay_loaded_presentation_scrubber_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void ArmLoadedReplayPresentationScrubber( float normalized );",
    )
    if not any(
        error.message == "replay loaded-presentation scrubber arming wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_loaded_presentation_scrubber_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay loaded-presentation scrubber header helper synthetic surface was not rejected")

    old_replay_camera_focus_clear_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void ClearReplayCameraFocus( bool restoreCamera );",
    )
    if not any(
        error.message == "replay camera focus clear wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_camera_focus_clear_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay camera focus clear header helper synthetic surface was not rejected")

    old_replay_event_frame_cursor_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        ReplayFrameIndex NextReplayEventFrameIndex() const;",
    )
    if not any(
        error.message == "replay event frame cursor wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_event_frame_cursor_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay event frame cursor header helper synthetic surface was not rejected")

    old_replay_event_record_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void RecordReplayEvent( ReplayEventKind kind );",
    )
    if not any(
        error.message == "replay event record wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_event_record_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay event record header helper synthetic surface was not rejected")

    old_replay_generated_scene_config_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void RecordReplayGeneratedSceneConfigEvent();",
    )
    if not any(
        error.message == "replay generated-scene config wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_generated_scene_config_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay generated-scene config header helper synthetic surface was not rejected")

    old_replay_physics_capture_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void CaptureReplayPhysicsStep();\n        static void CaptureReplayPhysicsStepThunk( void* userData );",
    )
    if not any(
        error.message == "replay physics capture wrappers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_physics_capture_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay physics capture header helper synthetic surface was not rejected")

    old_replay_world_override_event_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void RecordReplayWorldOverrideEvent( float gravity );",
    )
    if not any(
        error.message == "replay world override event wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_world_override_event_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay world override event header helper synthetic surface was not rejected")

    old_replay_launcher_config_event_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void RecordReplayLauncherConfigEvent( uint32_t changedFlags );",
    )
    if not any(
        error.message == "replay launcher config event wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_launcher_config_event_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay launcher config event header helper synthetic surface was not rejected")

    old_replay_launcher_fire_event_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void RecordReplayLauncherFireEvent( const Vector3& rayOrigin );",
    )
    if not any(
        error.message == "replay launcher fire event wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_launcher_fire_event_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay launcher fire event header helper synthetic surface was not rejected")

    old_replay_editor_place_event_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void RecordReplayEditorPlaceEvent( int objectType );",
    )
    if not any(
        error.message == "replay editor place event wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_editor_place_event_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay editor place event header helper synthetic surface was not rejected")

    old_replay_editor_transform_event_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void RecordReplayEditorTransformEvent( int modelIndex );",
    )
    if not any(
        error.message == "replay editor transform event wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_editor_transform_event_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay editor transform event header helper synthetic surface was not rejected")

    old_replay_velocity_target_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        int ResolveReplayVelocityEditModelIndex() const;",
    )
    if not any(
        error.message == "replay velocity target lookup helpers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_velocity_target_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay velocity target header helper synthetic surface was not rejected")

    old_replay_velocity_hit_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        int HitReplayVelocityLinearAxis();",
    )
    if not any(
        error.message == "replay velocity hit helpers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_velocity_hit_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay velocity hit header helper synthetic surface was not rejected")

    old_replay_velocity_toggle_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void SetReplayVelocityEditEnabled(bool enabled);",
    )
    if not any(
        error.message == "replay velocity edit toggle helper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_velocity_toggle_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay velocity edit toggle header helper synthetic surface was not rejected")

    old_replay_velocity_apply_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void ApplyReplayVelocityEditDrag();",
    )
    if not any(
        error.message == "replay velocity apply helper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_velocity_apply_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay velocity apply header helper synthetic surface was not rejected")

    old_replay_scrubber_overlay_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void RenderReplayScrubberOverlay();",
    )
    if not any(
        error.message == "replay overlay render helpers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_scrubber_overlay_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay scrubber overlay header helper synthetic surface was not rejected")

    old_replay_cause_tree_overlay_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void RenderReplayCauseTreeOverlay();",
    )
    if not any(
        error.message == "replay overlay render helpers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_replay_cause_tree_overlay_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("replay cause-tree overlay header helper synthetic surface was not rejected")

    old_scene_reset_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void RestoreSceneRuntimeResetSnapshot( const SceneRuntimeResetSnapshot& snapshot );",
    )
    if not any(
        error.message == "scene runtime reset helpers must stay out of Run.h"
        for error in check_text_rules(Path("synthetic/Run.h"), old_scene_reset_header_helper, RUN_HEADER_RULES)
    ):
        failures.append("scene runtime reset header helper synthetic surface was not rejected")

    old_scene_queue_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        const std::string* CurrentSceneQueuePath() const;",
    )
    if not any(
        error.message == "scene queue wrappers must stay out of Run.h"
        for error in check_text_rules(Path("synthetic/Run.h"), old_scene_queue_header_helper, RUN_HEADER_RULES)
    ):
        failures.append("scene queue header wrapper synthetic surface was not rejected")

    old_scene_context_builder_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        SceneGeneratedModelContext BuildSceneGeneratedModelContext();",
    )
    if not any(
        error.message == "scene context builders must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_scene_context_builder_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("scene context builder header synthetic surface was not rejected")

    old_generated_camera_setup_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void SetUpCameras();",
    )
    if not any(
        error.message == "generated camera setup wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_generated_camera_setup_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("generated camera setup header synthetic surface was not rejected")

    old_scene_terrain_world_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void UseDefaultTerrain();",
    )
    if not any(
        error.message == "scene terrain/world setup wrappers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_scene_terrain_world_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("scene terrain/world header synthetic surface was not rejected")

    old_editable_scene_snapshot_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool SaveCurrentEditableSceneSnapshot();",
    )
    if not any(
        error.message == "editable scene snapshot helper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_editable_scene_snapshot_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("editable scene snapshot header helper synthetic surface was not rejected")

    old_tornado_defaults_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void ApplyTornadoDefaultsForActiveScene();",
    )
    if not any(
        error.message == "tornado defaults helper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_tornado_defaults_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("tornado defaults header helper synthetic surface was not rejected")

    old_tornado_sync_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void SyncTornadoFieldToPhysics();",
    )
    if not any(
        error.message == "tornado physics sync wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_tornado_sync_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("tornado sync header helper synthetic surface was not rejected")

    old_diagnostics_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void LogPerfMemory( const char* checkpoint );",
    )
    if not any(
        error.message == "diagnostics perf-memory wrappers must stay out of Run.h"
        for error in check_text_rules(Path("synthetic/Run.h"), old_diagnostics_header_helper, RUN_HEADER_RULES)
    ):
        failures.append("diagnostics perf-memory header wrapper synthetic surface was not rejected")

    old_diagnostics_tick_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void TickPerfLog();",
    )
    if not any(
        error.message == "diagnostics perf-log tick wrappers must stay out of Run.h"
        for error in check_text_rules(Path("synthetic/Run.h"), old_diagnostics_tick_header_helper, RUN_HEADER_RULES)
    ):
        failures.append("diagnostics perf-log tick header wrapper synthetic surface was not rejected")

    old_diagnostics_memory_dump_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool WriteMainMemoryDump( const char* checkpoint );",
    )
    if not any(
        error.message == "diagnostics memory-dump wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"), old_diagnostics_memory_dump_header_helper, RUN_HEADER_RULES
        )
    ):
        failures.append("diagnostics memory-dump header wrapper synthetic surface was not rejected")

    old_diagnostics_ui_stress_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n"
        "        unsigned int NextUIStressRandom();\n"
        "        int NextUIStressInt( int maxExclusive );\n"
        "        float NextUIStressFloat( float minValue, float maxValue );",
    )
    if not any(
        error.message == "diagnostics UI stress RNG helpers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"), old_diagnostics_ui_stress_header_helper, RUN_HEADER_RULES
        )
    ):
        failures.append("diagnostics UI stress RNG header helpers synthetic surface was not rejected")

    old_scene_control_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool AdvanceScene();",
    )
    if not any(
        error.message == "scene-control wrappers must stay out of Run.h"
        for error in check_text_rules(Path("synthetic/Run.h"), old_scene_control_header_helper, RUN_HEADER_RULES)
    ):
        failures.append("scene-control header wrapper synthetic surface was not rejected")

    old_scene_browser_refresh_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void RefreshSceneBrowserList();",
    )
    if not any(
        error.message == "scene browser refresh wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_scene_browser_refresh_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("scene browser refresh header wrapper synthetic surface was not rejected")

    old_scene_browser_index_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        int CurrentSceneBrowserIndex() const;",
    )
    if not any(
        error.message == "scene browser index wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_scene_browser_index_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("scene browser index header wrapper synthetic surface was not rejected")

    old_scene_defaults_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool SaveRenderDefaults();\n        bool SaveSkyDefaults();",
    )
    if not any(
        error.message == "scene default persistence wrappers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_scene_defaults_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("scene default persistence header wrappers synthetic surface was not rejected")

    old_scene_create_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool CreateSceneFromUI( const char* requestedName );",
    )
    if not any(
        error.message == "scene create wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_scene_create_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("scene create header wrapper synthetic surface was not rejected")

    old_scene_world_override_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        void ApplyUIWorldOverride( float gravity, float fluidHeight, float fluidDensity );",
    )
    if not any(
        error.message == "scene world override wrapper must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_scene_world_override_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("scene world override header wrapper synthetic surface was not rejected")

    old_scene_generated_control_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n"
        "        void ApplyUIModelCountOverride( int count );\n"
        "        void ApplyUISolverObjectCounts( int balls, int boxes );",
    )
    if not any(
        error.message == "scene generated control wrappers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_scene_generated_control_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("scene generated control header wrappers synthetic surface was not rejected")

    old_scene_style_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool ApplyCinematicModeFromBrowserIndex( int index );",
    )
    if not any(
        error.message == "scene style wrappers must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"),
            old_scene_style_header_helper,
            RUN_HEADER_RULES,
        )
    ):
        failures.append("scene style header wrapper synthetic surface was not rejected")

    old_scene_coordinator_callback_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        SceneRuntimeCoordinatorCallbacks BuildSceneRuntimeCoordinatorCallbacks();",
    )
    if not any(
        error.message == "scene coordinator callback builders must stay out of Run.h"
        for error in check_text_rules(
            Path("synthetic/Run.h"), old_scene_coordinator_callback_header_helper, RUN_HEADER_RULES
        )
    ):
        failures.append("scene coordinator callback builder header synthetic surface was not rejected")

    old_run_internal_scrubber_helper = """
    static inline float ReplayScrubberTrackPosition( const RunReplayScrubberState& state, RunReplayTrack track )
    {
        return state.position;
    }
    """
    if not any(
        error.message == "replay scrubber timeline helpers must stay out of RunInternal.h"
        for error in check_run_internal_scrubber_guardrails_text(
            Path("synthetic/RunInternal.h"), old_run_internal_scrubber_helper
        )
    ):
        failures.append("RunInternal scrubber timeline helper synthetic surface was not rejected")

    old_run_internal_panel_layout_helper = """
    inline UI::UIRect ReplayScrubberPanelRect( int screenW, int screenH )
    {
        return { 0.0f, 0.0f, 1.0f, 1.0f };
    }
    """
    if not any(
        error.message == "replay overlay layout helpers must stay out of RunInternal.h"
        for error in check_run_internal_replay_layout_guardrails_text(
            Path("synthetic/RunInternal.h"), old_run_internal_panel_layout_helper
        )
    ):
        failures.append("RunInternal replay scrubber panel layout helper synthetic surface was not rejected")

    old_run_internal_cause_window_layout_helper = """
    inline UI::UIRect ReplayCauseWindowRect( const RunReplayCauseTreeState& state )
    {
        return { 0.0f, 0.0f, 1.0f, 1.0f };
    }
    """
    if not any(
        error.message == "replay overlay layout helpers must stay out of RunInternal.h"
        for error in check_run_internal_replay_layout_guardrails_text(
            Path("synthetic/RunInternal.h"), old_run_internal_cause_window_layout_helper
        )
    ):
        failures.append("RunInternal replay cause window layout helper synthetic surface was not rejected")

    old_run_internal_cause_window_placement_helper = """
    inline void EnsureReplayCauseWindowPlacement( RunReplayCauseTreeState& state, int screenW, int screenH )
    {
    }
    """
    if not any(
        error.message == "replay overlay layout helpers must stay out of RunInternal.h"
        for error in check_run_internal_replay_layout_guardrails_text(
            Path("synthetic/RunInternal.h"), old_run_internal_cause_window_placement_helper
        )
    ):
        failures.append("RunInternal replay cause window placement helper synthetic surface was not rejected")

    old_run_internal_scrubber_geometry = """
    inline float ReplayScrubberPositionFromMouse( int mouseX, int screenW, int screenH, RunReplayTrack trackName )
    {
        return 1.0f;
    }
    """
    if not any(
        error.message == "replay overlay layout helpers must stay out of RunInternal.h"
        for error in check_run_internal_replay_layout_guardrails_text(
            Path("synthetic/RunInternal.h"), old_run_internal_scrubber_geometry
        )
    ):
        failures.append("RunInternal replay scrubber geometry helper synthetic surface was not rejected")

    old_run_internal_scene_runtime_snapshot = "struct SceneRuntimeResetSnapshot { int value = 0; };"
    if not any(
        error.message == "scene runtime reset snapshot must stay out of RunInternal.h"
        for error in check_run_internal_scene_runtime_guardrails_text(
            Path("synthetic/RunInternal.h"), old_run_internal_scene_runtime_snapshot
        )
    ):
        failures.append("RunInternal scene runtime reset snapshot synthetic surface was not rejected")

    old_run_internal_scene_style_helper = """
    inline void ApplyCinematicSceneOverrides( CinematicRenderConfig& target,
                                              uint64_t mask,
                                              const CinematicRenderConfig& source )
    {
    }
    """
    if not any(
        error.message == "cinematic override helpers must stay out of RunInternal.h"
        for error in check_run_internal_scene_style_guardrails_text(
            Path("synthetic/RunInternal.h"), old_run_internal_scene_style_helper
        )
    ):
        failures.append("RunInternal cinematic override helper synthetic surface was not rejected")

    new_binding = allowed_host.replace(
        "RenderDiagnosticsView diagnostics;",
        "RenderDiagnosticsView diagnostics;\n        RunSceneState* newSceneState = nullptr;",
    )
    if not any(
        error.message == "new RuntimeRenderHostBindings fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, new_binding)
    ):
        failures.append("new RuntimeRenderHostBindings synthetic field was not rejected")

    bare_new_binding = allowed_host.replace(
        "RenderDiagnosticsView diagnostics;",
        "RenderDiagnosticsView diagnostics;\n        RunSceneState* bareSceneState;",
    )
    if not any(
        error.message == "new RuntimeRenderHostBindings fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, bare_new_binding)
    ):
        failures.append("bare RuntimeRenderHostBindings synthetic field was not rejected")

    old_replay_binding = allowed_host.replace(
        "RenderDiagnosticsView diagnostics;",
        "RenderDiagnosticsView diagnostics;\n        RunReplayScrubberState* replayScrubber = nullptr;",
    )
    if not any(
        error.message == "new RuntimeRenderHostBindings fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, old_replay_binding)
    ):
        failures.append("old RuntimeRenderHost replay binding synthetic field was not rejected")

    old_dxr_world_binding = allowed_host.replace(
        "PhysicsDebugVisualizer* physicsDebugVisualizer = nullptr;",
        "PhysicsDebugVisualizer* physicsDebugVisualizer = nullptr;\n"
        "        std::array<float, MAX_GAME_MODELS * 16>* dxrReflectionTransforms = nullptr;",
    )
    if not any(
        error.message == "new RenderWorldView fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, old_dxr_world_binding)
    ):
        failures.append("old RuntimeRenderHost DXR reflection binding synthetic field was not rejected")

    nested_world_binding = allowed_host.replace(
        "PhysicsDebugVisualizer* physicsDebugVisualizer = nullptr;",
        "PhysicsDebugVisualizer* physicsDebugVisualizer = nullptr;\n        RunMousePickupState* mousePickup = nullptr;",
    )
    if not any(
        error.message == "new RenderWorldView fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, nested_world_binding)
    ):
        failures.append("new RenderWorldView synthetic field was not rejected")

    nested_replay_binding = allowed_host.replace(
        "ReplayRuntime* replayRuntime = nullptr;",
        "ReplayRuntime* replayRuntime = nullptr;\n        RunReplayScrubberState* scrubber = nullptr;",
    )
    if not any(
        error.message == "new RenderReplayOverlayView fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, nested_replay_binding)
    ):
        failures.append("new RenderReplayOverlayView synthetic field was not rejected")

    nested_diagnostics_binding = allowed_host.replace(
        "RunTimerState* timers = nullptr;",
        "RunTimerState* timers = nullptr;\n        RunPerfLogState* perfLog = nullptr;",
    )
    if not any(
        error.message == "new RenderDiagnosticsView fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, nested_diagnostics_binding)
    ):
        failures.append("new RenderDiagnosticsView synthetic field was not rejected")

    new_callback = allowed_host.replace(
        "VoidFn refreshRuntimeViewModel = nullptr;",
        "VoidFn refreshRuntimeViewModel = nullptr;\n        VoidFn newRenderCallback = nullptr;",
    )
    if not any(
        error.message == "new RuntimeRenderHostCallbacks fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, new_callback)
    ):
        failures.append("new RuntimeRenderHostCallbacks synthetic field was not rejected")

    old_main_memory_callback_typedef = allowed_host.replace(
        "using VoidFn = void ( * )( void* user );",
        "using VoidFn = void ( * )( void* user );\n        using MainMemoryStatsFn = MainMemoryStats ( * )( void* user, double nowSeconds );",
    )
    if not any(
        error.message == "new RuntimeRenderHostCallbacks typedefs are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, old_main_memory_callback_typedef)
    ):
        failures.append("old RuntimeRenderHost main-memory callback typedef was not rejected")

    old_main_memory_callback_field = allowed_host.replace(
        "VoidFn refreshRuntimeViewModel = nullptr;",
        "VoidFn refreshRuntimeViewModel = nullptr;\n        MainMemoryStatsFn refreshMainMemoryStats = nullptr;",
    )
    if not any(
        error.message == "new RuntimeRenderHostCallbacks fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, old_main_memory_callback_field)
    ):
        failures.append("old RuntimeRenderHost main-memory callback field was not rejected")

    old_scrubber_callback_field = allowed_host.replace(
        "VoidFn refreshRuntimeViewModel = nullptr;",
        "VoidFn refreshRuntimeViewModel = nullptr;\n        VoidFn shouldRenderReplayScrubber = nullptr;",
    )
    if not any(
        error.message == "new RuntimeRenderHostCallbacks fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, old_scrubber_callback_field)
    ):
        failures.append("old RuntimeRenderHost scrubber callback field was not rejected")

    old_replay_scrubber_overlay_callback_field = allowed_host.replace(
        "VoidFn refreshRuntimeViewModel = nullptr;",
        "VoidFn refreshRuntimeViewModel = nullptr;\n        VoidFn renderReplayScrubberOverlay = nullptr;",
    )
    if not any(
        error.message == "new RuntimeRenderHostCallbacks fields are blocked"
        for error in check_runtime_render_host_guardrails_text(
            synthetic_path,
            old_replay_scrubber_overlay_callback_field,
        )
    ):
        failures.append("old RuntimeRenderHost replay scrubber overlay callback field was not rejected")

    old_prediction_ghost_callback_field = allowed_host.replace(
        "VoidFn refreshRuntimeViewModel = nullptr;",
        "VoidFn refreshRuntimeViewModel = nullptr;\n        VoidFn renderReplayPredictionGhosts = nullptr;",
    )
    if not any(
        error.message == "new RuntimeRenderHostCallbacks fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, old_prediction_ghost_callback_field)
    ):
        failures.append("old RuntimeRenderHost replay prediction ghost callback field was not rejected")

    mutable_callback = allowed_host.replace(
        "using VoidFn = void ( * )( void* user );",
        "using VoidFn = void ( * )( void* user );\n        using MutableSceneFn = RunSceneState& (*)( void* user );",
    )
    mutable_errors = check_runtime_render_host_guardrails_text(synthetic_path, mutable_callback)
    if not any(error.message == "new RuntimeRenderHostCallbacks typedefs are blocked" for error in mutable_errors):
        failures.append("new RuntimeRenderHostCallbacks synthetic typedef was not rejected")
    if not any(error.message == "mutable RuntimeRenderHostCallbacks returns are blocked" for error in mutable_errors):
        failures.append("mutable RuntimeRenderHostCallbacks synthetic return was not rejected")

    mutable_pointer_callback = allowed_host.replace(
        "using VoidFn = void ( * )( void* user );",
        "using VoidFn = void ( * )( void* user );\n        using MutableScenePtrFn = RunSceneState* (*)( void* user );",
    )
    pointer_errors = check_runtime_render_host_guardrails_text(synthetic_path, mutable_pointer_callback)
    if not any(error.message == "new RuntimeRenderHostCallbacks typedefs are blocked" for error in pointer_errors):
        failures.append("new pointer RuntimeRenderHostCallbacks synthetic typedef was not rejected")
    if not any(error.message == "mutable RuntimeRenderHostCallbacks returns are blocked" for error in pointer_errors):
        failures.append("mutable pointer RuntimeRenderHostCallbacks synthetic return was not rejected")

    old_replay_callback = allowed_host.replace(
        "using VoidFn = void ( * )( void* user );",
        "using VoidFn = void ( * )( void* user );\n        using ReplayPresentationSampleFn = const ReplayPresentationSample* (*)( void* user );",
    )
    if not any(
        error.message == "new RuntimeRenderHostCallbacks typedefs are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, old_replay_callback)
    ):
        failures.append("old RuntimeRenderHost replay callback typedef was not rejected")

    old_prediction_ghost_callback_typedef = allowed_host.replace(
        "using VoidFn = void ( * )( void* user );",
        "using VoidFn = void ( * )( void* user );\n"
        "        using ReplayPredictionGhostsFn = void ( * )( void* user, const RenderFrameContext& frame );",
    )
    if not any(
        error.message == "new RuntimeRenderHostCallbacks typedefs are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, old_prediction_ghost_callback_typedef)
    ):
        failures.append("old RuntimeRenderHost replay prediction ghost callback typedef was not rejected")

    pick_service_call = "RuntimePickService::TryPickModel( request, result );"
    if check_pick_helper_guardrails_text(Path("synthetic/RunInput.cpp"), pick_service_call):
        failures.append("direct RuntimePickService synthetic call was rejected")

    duplicated_pick_helper = "bool Run::TryPickEditorModel( const Ray& ray ) { return false; }"
    if not any(
        error.message == "duplicated runtime pick helper wrappers are blocked"
        for error in check_pick_helper_guardrails_text(Path("synthetic/RunEditorTools.cpp"), duplicated_pick_helper)
    ):
        failures.append("duplicated runtime pick helper synthetic wrapper was not rejected")

    suffixed_pick_helper = "bool Run::TryPickEditorModelFromMouse() { return false; }"
    if not any(
        error.message == "duplicated runtime pick helper wrappers are blocked"
        for error in check_pick_helper_guardrails_text(Path("synthetic/RunEditorTools.cpp"), suffixed_pick_helper)
    ):
        failures.append("suffixed runtime pick helper synthetic wrapper was not rejected")

    old_replay_cause_tree_source_helper = "bool Run::BuildReplayCauseTreeRows() { return false; }"
    if not any(
        error.message == "Run replay cause-tree row builders are blocked"
        for error in check_run_replay_cause_tree_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_cause_tree_source_helper,
        )
    ):
        failures.append("replay cause-tree source helper synthetic surface was not rejected")

    renamed_replay_cause_tree_source_helper = "bool Run::BuildCauseTreeRows() { return false; }"
    if not any(
        error.message == "Run replay cause-tree row builders are blocked"
        for error in check_run_replay_cause_tree_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            renamed_replay_cause_tree_source_helper,
        )
    ):
        failures.append("renamed replay cause-tree source helper synthetic surface was not rejected")

    old_replay_prediction_job_source_helper = "void Run::CancelReplayPredictionJob( bool clearSamples ) {}"
    if not any(
        error.message == "Run replay prediction job-state helpers are blocked"
        for error in check_run_replay_prediction_job_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_prediction_job_source_helper,
        )
    ):
        failures.append("replay prediction job-state source helper synthetic surface was not rejected")

    old_replay_prediction_capture_source_helper = "void Run::CaptureReplayPredictionFrame( ReplayFrameIndex frameIndex ) {}"
    if not any(
        error.message == "Run replay prediction capture helpers are blocked"
        for error in check_run_replay_prediction_capture_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_prediction_capture_source_helper,
        )
    ):
        failures.append("replay prediction capture source helper synthetic surface was not rejected")

    old_replay_prediction_lifecycle_source_helper = "bool Run::StepReplayPredictionJob() { return false; }"
    if not any(
        error.message == "Run replay prediction lifecycle helpers are blocked"
        for error in check_run_replay_prediction_lifecycle_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_prediction_lifecycle_source_helper,
        )
    ):
        failures.append("replay prediction lifecycle source helper synthetic surface was not rejected")

    old_replay_path_state_source_helper = "void Run::ClearReplayPathVisualizer() {}"
    if not any(
        error.message == "Run replay path-state helpers are blocked"
        for error in check_run_replay_path_state_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_path_state_source_helper,
        )
    ):
        failures.append("replay path-state source helper synthetic surface was not rejected")

    old_replay_cause_tree_lookup_source_helper = (
        "bool Run::TryResolveReplayCauseTreeBodyPosition( ReplayBodyId id ) { return false; }"
    )
    if not any(
        error.message == "Run replay cause-tree lookup helpers are blocked"
        for error in check_run_replay_cause_tree_lookup_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_cause_tree_lookup_source_helper,
        )
    ):
        failures.append("replay cause-tree body lookup source helper synthetic surface was not rejected")

    old_replay_cause_tree_focus_source_helper = "bool Run::FocusReplayCauseTreeBody( ReplayBodyId id ) { return true; }"
    if not any(
        error.message == "Run replay cause-tree focus wrappers are blocked"
        for error in check_run_replay_cause_tree_focus_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_cause_tree_focus_source_helper,
        )
    ):
        failures.append("replay cause-tree body focus source helper synthetic surface was not rejected")

    old_replay_cause_tree_camera_activation_source_helper = (
        "void Run::ActivateReplayCameraForCauseRow( const RunReplayCauseTreeRow& row, int rowIndex ) {}"
    )
    if not any(
        error.message == "Run replay cause-tree camera activation helper is blocked"
        for error in check_run_replay_cause_tree_camera_activation_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_cause_tree_camera_activation_source_helper,
        )
    ):
        failures.append("replay cause-tree camera activation source helper synthetic surface was not rejected")

    old_replay_render_state_source_helper = "void Run::RestoreReplayRenderStateForFrame() {}"
    if not any(
        error.message == "Run replay render-state helpers are blocked"
        for error in check_run_replay_render_state_source_guardrails_text(
            Path("synthetic/RunRender.cpp"),
            old_replay_render_state_source_helper,
        )
    ):
        failures.append("replay render-state source helper synthetic surface was not rejected")

    old_replay_launcher_visual_sample_source_helper = (
        "void Run::RestoreReplayLauncherVisualSample( const ReplayLauncherVisualSample& sample ) {}"
    )
    if not any(
        error.message == "Run replay launcher visual sample helpers are blocked"
        for error in check_run_replay_launcher_visual_sample_source_guardrails_text(
            Path("synthetic/Run.cpp"),
            old_replay_launcher_visual_sample_source_helper,
        )
    ):
        failures.append("replay launcher visual sample source helper synthetic surface was not rejected")

    old_replay_sample_comparison_source_helper = "void Run::CompareLatestReplaySamples() {}"
    if not any(
        error.message == "Run replay sample comparison helper is blocked"
        for error in check_run_replay_sample_comparison_source_guardrails_text(
            Path("synthetic/RunFrame.cpp"),
            old_replay_sample_comparison_source_helper,
        )
    ):
        failures.append("replay sample comparison source helper synthetic surface was not rejected")

    old_render_host_texture_source_helper = (
        "Textures::TextureCollection& Run::Textures() { throw; }\n"
        "uint32_t Run::TextureHandle( uint32_t textureHash ) { return textureHash; }\n"
        "void Run::SelectRenderTexture( uint32_t textureHash ) { (void)textureHash; }"
    )
    if not any(
        error.message == "Run render host texture wrappers are blocked"
        for error in check_run_render_host_texture_source_guardrails_text(
            Path("synthetic/Run.cpp"),
            old_render_host_texture_source_helper,
        )
    ):
        failures.append("render host texture source helpers synthetic surface was not rejected")

    old_replay_presentation_picker_source_helper = "bool Run::PromptLoadReplayPresentationArtifact( HWND hwnd ) { return false; }"
    if not any(
        error.message == "Run replay presentation artifact picker is blocked"
        for error in check_run_replay_presentation_picker_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_presentation_picker_source_helper,
        )
    ):
        failures.append("replay presentation picker source helper synthetic surface was not rejected")

    old_replay_scrubber_save_source_helper = "bool Run::SaveReplayBufferFromScrubber( RunReplayTrack track ) { return false; }"
    if not any(
        error.message == "Run replay scrubber save helper is blocked"
        for error in check_run_replay_scrubber_save_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_scrubber_save_source_helper,
        )
    ):
        failures.append("replay scrubber save source helper synthetic surface was not rejected")

    old_replay_restore_event_source_helper = (
        "bool Run::ApplyReplayEventForRestoreTarget( const ReplayEventSample& event, char* outReason, "
        "std::size_t reasonSize ) { return false; }"
    )
    if not any(
        error.message == "Run replay restore event helper is blocked"
        for error in check_run_replay_restore_event_source_guardrails_text(
            Path("synthetic/RunFrame.cpp"),
            old_replay_restore_event_source_helper,
        )
    ):
        failures.append("replay restore event source helper synthetic surface was not rejected")

    old_replay_inspection_camera_source_helper = "void Run::UpdateReplayInspectionCamera() {}"
    if not any(
        error.message == "Run replay inspection camera update wrapper is blocked"
        for error in check_run_replay_inspection_camera_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_inspection_camera_source_helper,
        )
    ):
        failures.append("replay inspection camera source helper synthetic surface was not rejected")

    old_replay_inspection_query_source_helper = (
        "bool Run::ReplayInspectionActive() const { return false; }\n"
        "bool Run::ReplayInspectionMouseLookActive() const { return false; }"
    )
    if not any(
        error.message == "Run replay inspection query wrappers are blocked"
        for error in check_run_replay_inspection_query_source_guardrails_text(
            Path("synthetic/RunInput.cpp"),
            old_replay_inspection_query_source_helper,
        )
    ):
        failures.append("replay inspection query source helpers synthetic surface was not rejected")

    old_replay_live_advance_source_helper = "void Run::SetReplayLiveAdvanceHeld( bool held ) { }"
    if not any(
        error.message == "Run replay live-advance wrapper is blocked"
        for error in check_run_replay_live_advance_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_live_advance_source_helper,
        )
    ):
        failures.append("replay live-advance source helper synthetic surface was not rejected")

    old_replay_scrubber_reset_source_helper = "void Run::ResetReplayScrubber() {}"
    if not any(
        error.message == "Run replay scrubber reset wrapper is blocked"
        for error in check_run_replay_scrubber_reset_source_guardrails_text(
            Path("synthetic/Run.cpp"),
            old_replay_scrubber_reset_source_helper,
        )
    ):
        failures.append("replay scrubber reset source helper synthetic surface was not rejected")

    old_replay_event_frame_cursor_source_helper = "ReplayFrameIndex Run::NextReplayEventFrameIndex() const { return 0; }"
    if not any(
        error.message == "Run replay event frame cursor wrapper is blocked"
        for error in check_run_replay_event_frame_cursor_source_guardrails_text(
            Path("synthetic/Run.cpp"),
            old_replay_event_frame_cursor_source_helper,
        )
    ):
        failures.append("replay event frame cursor source helper synthetic surface was not rejected")

    old_replay_event_record_source_helper = "void Run::RecordReplayEvent( ReplayEventKind kind ) { }"
    if not any(
        error.message == "Run replay event record wrapper is blocked"
        for error in check_run_replay_event_record_source_guardrails_text(
            Path("synthetic/Run.cpp"),
            old_replay_event_record_source_helper,
        )
    ):
        failures.append("replay event record source helper synthetic surface was not rejected")

    old_replay_generated_scene_config_source_helper = "void Run::RecordReplayGeneratedSceneConfigEvent() { }"
    if not any(
        error.message == "Run replay generated-scene config wrapper is blocked"
        for error in check_run_replay_generated_scene_config_source_guardrails_text(
            Path("synthetic/Run.cpp"),
            old_replay_generated_scene_config_source_helper,
        )
    ):
        failures.append("replay generated-scene config source helper synthetic surface was not rejected")

    old_replay_physics_capture_source_helper = (
        "void Run::CaptureReplayPhysicsStep() { }\n"
        "void Run::CaptureReplayPhysicsStepThunk( void* userData ) { }"
    )
    if not any(
        error.message == "Run replay physics capture wrappers are blocked"
        for error in check_run_replay_physics_capture_source_guardrails_text(
            Path("synthetic/RunFrame.cpp"),
            old_replay_physics_capture_source_helper,
        )
    ):
        failures.append("replay physics capture source helper synthetic surface was not rejected")

    old_replay_world_override_event_source_helper = "void Run::RecordReplayWorldOverrideEvent( float gravity ) { }"
    if not any(
        error.message == "Run replay world override event wrapper is blocked"
        for error in check_run_replay_world_override_event_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_replay_world_override_event_source_helper,
        )
    ):
        failures.append("replay world override event source helper synthetic surface was not rejected")

    old_replay_launcher_config_event_source_helper = "void Run::RecordReplayLauncherConfigEvent( uint32_t changedFlags ) { }"
    if not any(
        error.message == "Run replay launcher config event wrapper is blocked"
        for error in check_run_replay_launcher_config_event_source_guardrails_text(
            Path("synthetic/RunInput.cpp"),
            old_replay_launcher_config_event_source_helper,
        )
    ):
        failures.append("replay launcher config event source helper synthetic surface was not rejected")

    old_replay_launcher_fire_event_source_helper = "void Run::RecordReplayLauncherFireEvent( const Vector3& rayOrigin ) { }"
    if not any(
        error.message == "Run replay launcher fire event wrapper is blocked"
        for error in check_run_replay_launcher_fire_event_source_guardrails_text(
            Path("synthetic/RunInput.cpp"),
            old_replay_launcher_fire_event_source_helper,
        )
    ):
        failures.append("replay launcher fire event source helper synthetic surface was not rejected")

    old_replay_editor_place_event_source_helper = "void Run::RecordReplayEditorPlaceEvent( int objectType ) { }"
    if not any(
        error.message == "Run replay editor place event wrapper is blocked"
        for error in check_run_replay_editor_place_event_source_guardrails_text(
            Path("synthetic/RunEditorTools.cpp"),
            old_replay_editor_place_event_source_helper,
        )
    ):
        failures.append("replay editor place event source helper synthetic surface was not rejected")

    old_replay_editor_transform_event_source_helper = (
        "void Run::RecordReplayEditorTransformEvent( int modelIndex ) { }"
    )
    if not any(
        error.message == "Run replay editor transform event wrapper is blocked"
        for error in check_run_replay_editor_transform_event_source_guardrails_text(
            Path("synthetic/RunEditorTools.cpp"),
            old_replay_editor_transform_event_source_helper,
        )
    ):
        failures.append("replay editor transform event source helper synthetic surface was not rejected")

    old_replay_loaded_presentation_scrubber_source_helper = (
        "void Run::ArmLoadedReplayPresentationScrubber( float normalized ) { }"
    )
    if not any(
        error.message == "Run loaded-presentation scrubber arming wrapper is blocked"
        for error in check_run_replay_loaded_presentation_scrubber_source_guardrails_text(
            Path("synthetic/Run.cpp"),
            old_replay_loaded_presentation_scrubber_source_helper,
        )
    ):
        failures.append("replay loaded-presentation scrubber source helper synthetic surface was not rejected")

    old_replay_camera_focus_clear_source_helper = "void Run::ClearReplayCameraFocus( bool restoreCamera ) { }"
    if not any(
        error.message == "Run replay camera focus clear wrapper is blocked"
        for error in check_run_replay_camera_focus_clear_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_camera_focus_clear_source_helper,
        )
    ):
        failures.append("replay camera focus clear source helper synthetic surface was not rejected")

    old_replay_velocity_target_source_helper = "int Run::ResolveReplayVelocityEditModelIndex() const { return -1; }"
    if not any(
        error.message == "Run replay velocity target lookup helpers are blocked"
        for error in check_run_replay_velocity_target_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_velocity_target_source_helper,
        )
    ):
        failures.append("replay velocity target source helper synthetic surface was not rejected")

    old_replay_velocity_hit_source_helper = "int Run::HitReplayVelocityLinearAxis() const { return -1; }"
    if not any(
        error.message == "Run replay velocity hit helpers are blocked"
        for error in check_run_replay_velocity_hit_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_velocity_hit_source_helper,
        )
    ):
        failures.append("replay velocity hit source helper synthetic surface was not rejected")

    old_replay_velocity_toggle_source_helper = "void Run::SetReplayVelocityEditEnabled(bool enabled) {}"
    if not any(
        error.message == "Run replay velocity edit toggle helper is blocked"
        for error in check_run_replay_velocity_toggle_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_velocity_toggle_source_helper,
        )
    ):
        failures.append("replay velocity edit toggle source helper synthetic surface was not rejected")

    old_replay_velocity_apply_source_helper = "void Run::ApplyReplayVelocityEditDrag() {}"
    if not any(
        error.message == "Run replay velocity apply helper is blocked"
        for error in check_run_replay_velocity_apply_source_guardrails_text(
            Path("synthetic/RunReplayTools.cpp"),
            old_replay_velocity_apply_source_helper,
        )
    ):
        failures.append("replay velocity apply source helper synthetic surface was not rejected")

    old_replay_scrubber_overlay_source_helper = "void Run::RenderReplayScrubberOverlay() {}"
    if not any(
        error.message == "Run replay overlay render helpers are blocked"
        for error in check_run_replay_overlay_source_guardrails_text(
            Path("synthetic/RunUiTextPass.cpp"),
            old_replay_scrubber_overlay_source_helper,
        )
    ):
        failures.append("replay scrubber overlay source helper synthetic surface was not rejected")

    old_replay_cause_tree_overlay_source_helper = "void Run::RenderReplayCauseTreeOverlay() {}"
    if not any(
        error.message == "Run replay overlay render helpers are blocked"
        for error in check_run_replay_overlay_source_guardrails_text(
            Path("synthetic/RunUiTextPass.cpp"),
            old_replay_cause_tree_overlay_source_helper,
        )
    ):
        failures.append("replay cause-tree overlay source helper synthetic surface was not rejected")

    old_scene_reset_source_helper = "void Run::ClearSceneRuntimeUIOverrides() {}"
    if not any(
        error.message == "Run scene runtime reset helpers are blocked"
        for error in check_run_scene_reset_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_scene_reset_source_helper,
        )
    ):
        failures.append("scene runtime reset source helper synthetic surface was not rejected")

    old_scene_queue_source_helper = "const std::string* Run::CurrentSceneQueuePath() const { return nullptr; }"
    if not any(
        error.message == "Run scene queue wrappers are blocked"
        for error in check_run_scene_queue_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_scene_queue_source_helper,
        )
    ):
        failures.append("scene queue source wrapper synthetic surface was not rejected")

    old_scene_context_builder_source_helper = "SceneGeneratedModelContext Run::BuildSceneGeneratedModelContext() { return {}; }"
    if not any(
        error.message == "Run scene context builders are blocked"
        for error in check_run_scene_context_builder_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_scene_context_builder_source_helper,
        )
    ):
        failures.append("scene context builder source synthetic surface was not rejected")

    old_generated_camera_setup_source_helper = "void Run::SetUpCameras() {}"
    if not any(
        error.message == "Run generated camera setup wrapper is blocked"
        for error in check_run_generated_camera_setup_source_guardrails_text(
            Path("synthetic/RunRender.cpp"),
            old_generated_camera_setup_source_helper,
        )
    ):
        failures.append("generated camera setup source synthetic surface was not rejected")

    old_scene_terrain_world_source_helper = "void Run::UseDefaultTerrain() {}"
    if not any(
        error.message == "Run scene terrain/world setup wrappers are blocked"
        for error in check_run_scene_terrain_world_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_scene_terrain_world_source_helper,
        )
    ):
        failures.append("scene terrain/world source synthetic surface was not rejected")

    old_editable_scene_snapshot_source_helper = "bool Run::SaveCurrentEditableSceneSnapshot() { return false; }"
    if not any(
        error.message == "Run editable scene snapshot helper is blocked"
        for error in check_run_editable_scene_snapshot_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_editable_scene_snapshot_source_helper,
        )
    ):
        failures.append("editable scene snapshot source synthetic surface was not rejected")

    old_tornado_defaults_source_helper = "void Run::ApplyTornadoDefaultsForActiveScene() {}"
    if not any(
        error.message == "Run tornado defaults helper is blocked"
        for error in check_run_tornado_defaults_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_tornado_defaults_source_helper,
        )
    ):
        failures.append("tornado defaults source synthetic surface was not rejected")

    old_tornado_sync_source_helper = "void Run::SyncTornadoFieldToPhysics() {}"
    if not any(
        error.message == "Run tornado physics sync wrapper is blocked"
        for error in check_run_tornado_sync_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_tornado_sync_source_helper,
        )
    ):
        failures.append("tornado sync source helper synthetic surface was not rejected")

    old_diagnostics_source_helper = "void Run::LogPerfMemory( const char* checkpoint ) {}"
    if not any(
        error.message == "Run diagnostics perf-memory wrappers are blocked"
        for error in check_run_diagnostics_source_guardrails_text(
            Path("synthetic/RunCapture.cpp"),
            old_diagnostics_source_helper,
        )
    ):
        failures.append("diagnostics perf-memory source wrapper synthetic surface was not rejected")

    old_diagnostics_tick_source_helper = "void Run::TickPerfLog() {}"
    if not any(
        error.message == "Run diagnostics perf-log tick wrappers are blocked"
        for error in check_run_diagnostics_perf_tick_source_guardrails_text(
            Path("synthetic/RunFrame.cpp"),
            old_diagnostics_tick_source_helper,
        )
    ):
        failures.append("diagnostics perf-log tick source wrapper synthetic surface was not rejected")

    old_diagnostics_memory_dump_source_helper = "bool Run::WriteMainMemoryDump( const char* checkpoint ) { return true; }"
    if not any(
        error.message == "Run diagnostics memory-dump wrapper is blocked"
        for error in check_run_diagnostics_memory_dump_source_guardrails_text(
            Path("synthetic/RunCapture.cpp"),
            old_diagnostics_memory_dump_source_helper,
        )
    ):
        failures.append("diagnostics memory-dump source wrapper synthetic surface was not rejected")

    old_diagnostics_ui_stress_source_helper = (
        "unsigned int Run::NextUIStressRandom() { return 0; }\n"
        "int Run::NextUIStressInt( int maxExclusive ) { return maxExclusive; }\n"
        "float Run::NextUIStressFloat( float minValue, float maxValue ) { return minValue + maxValue; }"
    )
    if not any(
        error.message == "Run diagnostics UI stress RNG helpers are blocked"
        for error in check_run_diagnostics_ui_stress_rng_source_guardrails_text(
            Path("synthetic/RunStress.cpp"),
            old_diagnostics_ui_stress_source_helper,
        )
    ):
        failures.append("diagnostics UI stress RNG source helpers synthetic surface was not rejected")

    old_run_scene_perf_file_helper = """
    void Run::LoadScene( int index )
    {
        fopen_s( &m_diagnosticsRuntime.PerfLog().perfLogFile,
                 m_diagnosticsRuntime.PerfLog().perfLogPath,
                 "w" );
    }
    """
    if not any(
        error.message == "RunScene direct perf-log lifecycle access is blocked"
        for error in check_run_scene_perf_log_lifecycle_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_run_scene_perf_file_helper,
        )
    ):
        failures.append("RunScene direct perf-log file synthetic surface was not rejected")

    old_run_scene_perf_field_helper = "m_diagnosticsRuntime.PerfLog().isPerfLogFlushEnabled = true;"
    if not any(
        error.message == "RunScene direct perf-log lifecycle access is blocked"
        for error in check_run_scene_perf_log_lifecycle_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_run_scene_perf_field_helper,
        )
    ):
        failures.append("RunScene direct perf-log field synthetic surface was not rejected")

    unrelated_run_scene_file_helper = """
    void Run::LoadScene( int index )
    {
        FILE* file = nullptr;
        fopen_s( &file, "scene.txt", "w" );
    }
    """
    if any(
        error.message == "RunScene direct perf-log lifecycle access is blocked"
        for error in check_run_scene_perf_log_lifecycle_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            unrelated_run_scene_file_helper,
        )
    ):
        failures.append("RunScene unrelated file open synthetic surface was rejected")

    old_scene_control_source_helper = "bool Run::AdvanceScene() { return false; }"
    if not any(
        error.message == "Run scene-control wrappers are blocked"
        for error in check_run_scene_control_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_scene_control_source_helper,
        )
    ):
        failures.append("scene-control source wrapper synthetic surface was not rejected")

    old_scene_browser_refresh_source_helper = "void Run::RefreshSceneBrowserList() {}"
    if not any(
        error.message == "Run scene browser refresh wrapper is blocked"
        for error in check_run_scene_browser_refresh_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_scene_browser_refresh_source_helper,
        )
    ):
        failures.append("scene browser refresh source wrapper synthetic surface was not rejected")

    old_scene_browser_index_source_helper = "int Run::CurrentSceneBrowserIndex() const { return -1; }"
    if not any(
        error.message == "Run scene browser index wrapper is blocked"
        for error in check_run_scene_browser_index_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_scene_browser_index_source_helper,
        )
    ):
        failures.append("scene browser index source wrapper synthetic surface was not rejected")

    old_scene_defaults_source_helper = (
        "bool Run::SaveRenderDefaults() { return true; }\n"
        "bool Run::SaveSkyDefaults() { return true; }"
    )
    if not any(
        error.message == "Run scene default persistence wrappers are blocked"
        for error in check_run_scene_defaults_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_scene_defaults_source_helper,
        )
    ):
        failures.append("scene default persistence source wrappers synthetic surface was not rejected")

    old_scene_create_source_helper = "bool Run::CreateSceneFromUI( const char* requestedName ) { return requestedName; }"
    if not any(
        error.message == "Run scene create wrapper is blocked"
        for error in check_run_scene_create_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_scene_create_source_helper,
        )
    ):
        failures.append("scene create source wrapper synthetic surface was not rejected")

    old_scene_world_override_source_helper = (
        "void Run::ApplyUIWorldOverride( float gravity, float fluidHeight, float fluidDensity ) {}"
    )
    if not any(
        error.message == "Run scene world override wrapper is blocked"
        for error in check_run_scene_world_override_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_scene_world_override_source_helper,
        )
    ):
        failures.append("scene world override source wrapper synthetic surface was not rejected")

    old_scene_generated_control_source_helper = (
        "void Run::ApplyUIModelCountOverride( int count ) {}\n"
        "void Run::ApplyUISolverObjectCounts( int balls, int boxes ) {}"
    )
    if not any(
        error.message == "Run scene generated control wrappers are blocked"
        for error in check_run_scene_generated_control_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_scene_generated_control_source_helper,
        )
    ):
        failures.append("scene generated control source wrappers synthetic surface was not rejected")

    old_scene_style_source_helper = "void Run::ApplyLiveStyleScene( const TestScene& styleScene ) {}"
    if not any(
        error.message == "Run scene style wrappers are blocked"
        for error in check_run_scene_style_source_guardrails_text(
            Path("synthetic/RunScene.cpp"),
            old_scene_style_source_helper,
        )
    ):
        failures.append("scene style source wrapper synthetic surface was not rejected")

    old_scene_coordinator_callback_source_helper = """
    SceneRuntimeCoordinatorCallbacks Run::BuildSceneRuntimeCoordinatorCallbacks()
    {
        return {};
    }
    """
    if not any(
        error.message == "Run scene coordinator callback builders are blocked"
        for error in check_run_scene_coordinator_callback_source_guardrails_text(
            Path("synthetic/Run.cpp"),
            old_scene_coordinator_callback_source_helper,
        )
    ):
        failures.append("scene coordinator callback builder source synthetic surface was not rejected")

    old_scene_coordinator_callback_state = """
    struct SceneRuntimeCoordinatorCallbacks {};
    class SceneRuntimeCoordinator
    {
        SceneRuntimeCoordinatorCallbacks m_callbacks;
    };
    """
    coordinator_callback_errors = check_scene_runtime_coordinator_callback_guardrails_text(
        Path("synthetic/SceneRuntimeCoordinator.h"),
        old_scene_coordinator_callback_state,
    )
    if not any(error.message == "SceneRuntimeCoordinator callbacks are blocked" for error in coordinator_callback_errors):
        failures.append("SceneRuntimeCoordinator callback state synthetic surface was not rejected")

    old_ui_text_pass_replay_scrubber_overlay_definition = "void RuntimeRenderHost::RenderReplayScrubberOverlay() const {}"
    if not any(
        error.message == "replay overlay renderer definitions must stay out of RunUiTextPass.cpp"
        for error in check_run_ui_text_pass_replay_overlay_guardrails_text(
            Path("synthetic/RunUiTextPass.cpp"),
            old_ui_text_pass_replay_scrubber_overlay_definition,
        )
    ):
        failures.append("RunUiTextPass replay scrubber overlay definition synthetic surface was not rejected")

    old_ui_text_pass_replay_cause_tree_overlay_definition = "void RuntimeRenderHost::RenderReplayCauseTreeOverlay() const {}"
    if not any(
        error.message == "replay overlay renderer definitions must stay out of RunUiTextPass.cpp"
        for error in check_run_ui_text_pass_replay_overlay_guardrails_text(
            Path("synthetic/RunUiTextPass.cpp"),
            old_ui_text_pass_replay_cause_tree_overlay_definition,
        )
    ):
        failures.append("RunUiTextPass replay cause-tree overlay definition synthetic surface was not rejected")

    new_ui_text_pass_replay_scrubber_overlay_definition = "void ReplayOverlay::RenderReplayScrubberOverlay() {}"
    if not any(
        error.message == "replay overlay renderer definitions must stay out of RunUiTextPass.cpp"
        for error in check_run_ui_text_pass_replay_overlay_guardrails_text(
            Path("synthetic/RunUiTextPass.cpp"),
            new_ui_text_pass_replay_scrubber_overlay_definition,
        )
    ):
        failures.append("RunUiTextPass replay scrubber free-function renderer synthetic surface was not rejected")

    new_ui_text_pass_replay_cause_tree_overlay_definition = "void RenderReplayCauseTreeOverlay() {}"
    if not any(
        error.message == "replay overlay renderer definitions must stay out of RunUiTextPass.cpp"
        for error in check_run_ui_text_pass_replay_overlay_guardrails_text(
            Path("synthetic/RunUiTextPass.cpp"),
            new_ui_text_pass_replay_cause_tree_overlay_definition,
        )
    ):
        failures.append("RunUiTextPass replay cause-tree free-function renderer synthetic surface was not rejected")

    allowed_ui_text_pass_replay_overlay_host_call = "m_host.RenderReplayScrubberOverlay();"
    if check_run_ui_text_pass_replay_overlay_guardrails_text(
        Path("synthetic/RunUiTextPass.cpp"),
        allowed_ui_text_pass_replay_overlay_host_call,
    ):
        failures.append("RunUiTextPass replay overlay host call synthetic surface was rejected")

    allowed_physics_text = """
    namespace SkullbonezCore::GameObjects
    {
    class GameModelCollection;
    }
    """
    if check_physics_game_model_collection_guardrails_text(
        allowed_physics_path,
        allowed_physics_text,
        allowed_physics_path,
        synthetic_physics_allowlist,
    ):
        failures.append("allowed physics GameModelCollection synthetic dependency failed")

    commented_physics_text = """
    // GameModelCollection is mentioned in a migration note only.
    /*
       GameModelCollection appears in block comments too.
    */
    class PhysicsBodyStore;
    """
    if check_physics_game_model_collection_guardrails_text(
        Path("SkullbonezSource/Physics/NewPhysicsStore.h"),
        commented_physics_text,
        Path("SkullbonezSource/Physics/NewPhysicsStore.h"),
        synthetic_physics_allowlist,
    ):
        failures.append("comment-only physics GameModelCollection synthetic text was rejected")

    new_physics_dependency = """
    namespace SkullbonezCore::GameObjects
    {
    class GameModelCollection;
    }
    """
    if not any(
        error.message == "new physics GameModelCollection dependencies are blocked"
        for error in check_physics_game_model_collection_guardrails_text(
            Path("SkullbonezSource/Physics/NewPhysicsStore.h"),
            new_physics_dependency,
            Path("SkullbonezSource/Physics/NewPhysicsStore.h"),
            synthetic_physics_allowlist,
        )
    ):
        failures.append("new physics GameModelCollection synthetic dependency was not rejected")

    duplicate_physics_dependency = allowed_physics_text + allowed_physics_text
    if not any(
        error.message == "new physics GameModelCollection dependencies are blocked"
        for error in check_physics_game_model_collection_guardrails_text(
            allowed_physics_path,
            duplicate_physics_dependency,
            allowed_physics_path,
            synthetic_physics_allowlist,
        )
    ):
        failures.append("duplicate physics GameModelCollection synthetic dependency was not rejected")

    old_physics_engine_step = "void Step( GameObjects::GameModelCollection& collection, float deltaSeconds );"
    if not any(
        error.message == "new physics GameModelCollection dependencies are blocked"
        for error in check_physics_game_model_collection_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsEngine.h"),
            old_physics_engine_step,
            Path("SkullbonezSource/Physics/PhysicsEngine.h"),
            synthetic_physics_allowlist,
        )
    ):
        failures.append("old PhysicsEngine collection step synthetic surface was not rejected")

    old_physics_world_step = (
        "void RunPhysics( GameObjects::GameModelCollection& collection, "
        "PhysicsBodyStore& bodyStore, float fChangeInTime );"
    )
    if not any(
        error.message == "new physics GameModelCollection dependencies are blocked"
        for error in check_physics_game_model_collection_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.h"),
            old_physics_world_step,
            Path("SkullbonezSource/Physics/PhysicsWorld.h"),
            synthetic_physics_allowlist,
        )
    ):
        failures.append("old PhysicsWorld collection step synthetic surface was not rejected")

    deleted_model_view_text = """
    void GameModelCollection::MakePhysicsModelView();
    class PhysicsModelView;
    """
    if not any(
        error.message == "deleted PhysicsModelView boundary is blocked"
        for error in check_deleted_physics_model_view_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsScene.h"),
            deleted_model_view_text,
        )
    ):
        failures.append("deleted PhysicsModelView synthetic surface was not rejected")

    allowed_physics_models_path = Path("SkullbonezSource/Runtime/RunFrame.cpp")
    allowed_physics_models_line = (
        "std::vector<SkullbonezCore::GameObjects::GameModel>& physicsModels = "
        "m_cGameModelCollection.PhysicsModels();"
    )
    synthetic_physics_models_allowlist = Counter(
        { ( allowed_physics_models_path, normalize_boundary_line( allowed_physics_models_line ) ): 1 }
    )
    if check_physics_models_access_guardrails_text(
        allowed_physics_models_path,
        allowed_physics_models_line,
        allowed_physics_models_path,
        synthetic_physics_models_allowlist,
    ):
        failures.append("allowed PhysicsModels synthetic access failed")

    commented_physics_models_text = """
    // m_cGameModelCollection.PhysicsModels() is mentioned in a migration note only.
    /*
       collection.PhysicsModels() appears in block comments too.
    */
    void UseStoresInstead();
    """
    if check_physics_models_access_guardrails_text(
        Path("SkullbonezSource/Runtime/NewPhysicsCaller.cpp"),
        commented_physics_models_text,
        Path("SkullbonezSource/Runtime/NewPhysicsCaller.cpp"),
        synthetic_physics_models_allowlist,
    ):
        failures.append("comment-only PhysicsModels synthetic text was rejected")

    duplicate_physics_models_access = allowed_physics_models_line + "\n" + allowed_physics_models_line
    if not any(
        error.message == "direct PhysicsModels() compatibility access is blocked"
        for error in check_physics_models_access_guardrails_text(
            allowed_physics_models_path,
            duplicate_physics_models_access,
            allowed_physics_models_path,
            synthetic_physics_models_allowlist,
        )
    ):
        failures.append("duplicate PhysicsModels synthetic access was not rejected")

    new_physics_models_access = "auto& models = collection.PhysicsModels();"
    if not any(
        error.message == "direct PhysicsModels() compatibility access is blocked"
        for error in check_physics_models_access_guardrails_text(
            Path("SkullbonezSource/Runtime/NewPhysicsCaller.cpp"),
            new_physics_models_access,
            Path("SkullbonezSource/Runtime/NewPhysicsCaller.cpp"),
            synthetic_physics_models_allowlist,
        )
    ):
        failures.append("new PhysicsModels synthetic access was not rejected")

    return failures


def find_matching_close_brace(text: str, open_brace_offset: int) -> int:
    depth = 0
    for offset in range(open_brace_offset, len(text)):
        if text[offset] == "{":
            depth += 1
        elif text[offset] == "}":
            depth -= 1
            if depth == 0:
                return offset
    return len(text)


def find_run_function_spans(stripped: str) -> dict[str, tuple[int, int]]:
    spans: dict[str, tuple[int, int]] = {}
    pattern = re.compile(r"\bRun::([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?\{", re.S)
    for match in pattern.finditer(stripped):
        open_brace_offset = stripped.find("{", match.start(), match.end())
        if open_brace_offset < 0:
            continue
        close_brace_offset = find_matching_close_brace(stripped, open_brace_offset)
        spans[match.group(1)] = ( match.start(), close_brace_offset + 1 )
    return spans


def offset_in_allowed_function(
    relative_path: Path,
    function_spans: dict[str, tuple[int, int]],
    offset: int,
    allowed_functions: set[tuple[Path, str]],
) -> bool:
    for allowed_path, function_name in allowed_functions:
        if relative_path.as_posix() != allowed_path.as_posix():
            continue
        span = function_spans.get(function_name)
        if span is not None and span[0] <= offset < span[1]:
            return True
    return False


def check_interaction_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    camera_message, camera_pattern, camera_detail = CAMERA_MODE_WRITE_RULE
    owner_message, owner_pattern, owner_detail = WORLD_OWNER_WRITE_RULE

    for path in sorted((repo / RUNTIME_ROOT).rglob("*")):
        if path.suffix not in { ".cpp", ".h" }:
            continue
        relative_path = path.relative_to(repo)
        text = path.read_text(encoding="utf-8")
        stripped = strip_cpp_comments(text)
        function_spans = find_run_function_spans(stripped)

        for match in re.finditer(camera_pattern, stripped):
            if not offset_in_allowed_function(
                relative_path,
                function_spans,
                match.start(),
                ALLOWED_CAMERA_MODE_WRITE_FUNCTIONS,
            ):
                errors.append(
                    BoundaryError(path, line_for_offset(stripped, match.start()), camera_message, camera_detail)
                )

        for match in re.finditer(owner_pattern, stripped):
            if not offset_in_allowed_function(
                relative_path,
                function_spans,
                match.start(),
                ALLOWED_WORLD_OWNER_WRITE_FUNCTIONS,
            ):
                errors.append(
                    BoundaryError(path, line_for_offset(stripped, match.start()), owner_message, owner_detail)
                )

    return errors


def validate_runtime_boundaries(repo: Path) -> list[BoundaryError]:
    run_header = repo / RUN_HEADER
    errors = check_text_rules(run_header, run_header.read_text(encoding="utf-8"), RUN_HEADER_RULES)
    errors.extend(check_run_private_method_count_text(run_header, run_header.read_text(encoding="utf-8")))
    errors.extend(check_run_internal_scrubber_guardrails(repo))
    errors.extend(check_run_internal_replay_layout_guardrails(repo))
    errors.extend(check_run_internal_scene_runtime_guardrails(repo))
    errors.extend(check_run_internal_scene_style_guardrails(repo))
    errors.extend(check_run_storage(repo))
    errors.extend(check_runtime_render_host_guardrails(repo))
    errors.extend(check_pick_helper_guardrails(repo))
    errors.extend(check_run_replay_cause_tree_source_guardrails(repo))
    errors.extend(check_run_replay_prediction_job_source_guardrails(repo))
    errors.extend(check_run_replay_prediction_capture_source_guardrails(repo))
    errors.extend(check_run_replay_prediction_lifecycle_source_guardrails(repo))
    errors.extend(check_run_replay_path_state_source_guardrails(repo))
    errors.extend(check_run_replay_cause_tree_lookup_source_guardrails(repo))
    errors.extend(check_run_replay_cause_tree_focus_source_guardrails(repo))
    errors.extend(check_run_replay_cause_tree_camera_activation_source_guardrails(repo))
    errors.extend(check_run_replay_render_state_source_guardrails(repo))
    errors.extend(check_run_replay_launcher_visual_sample_source_guardrails(repo))
    errors.extend(check_run_replay_sample_comparison_source_guardrails(repo))
    errors.extend(check_run_render_host_texture_source_guardrails(repo))
    errors.extend(check_run_replay_presentation_picker_source_guardrails(repo))
    errors.extend(check_run_replay_scrubber_save_source_guardrails(repo))
    errors.extend(check_run_replay_restore_event_source_guardrails(repo))
    errors.extend(check_run_replay_inspection_camera_source_guardrails(repo))
    errors.extend(check_run_replay_inspection_query_source_guardrails(repo))
    errors.extend(check_run_replay_live_advance_source_guardrails(repo))
    errors.extend(check_run_replay_scrubber_reset_source_guardrails(repo))
    errors.extend(check_run_replay_event_frame_cursor_source_guardrails(repo))
    errors.extend(check_run_replay_event_record_source_guardrails(repo))
    errors.extend(check_run_replay_generated_scene_config_source_guardrails(repo))
    errors.extend(check_run_replay_physics_capture_source_guardrails(repo))
    errors.extend(check_run_replay_world_override_event_source_guardrails(repo))
    errors.extend(check_run_replay_launcher_config_event_source_guardrails(repo))
    errors.extend(check_run_replay_launcher_fire_event_source_guardrails(repo))
    errors.extend(check_run_replay_editor_place_event_source_guardrails(repo))
    errors.extend(check_run_replay_editor_transform_event_source_guardrails(repo))
    errors.extend(check_run_replay_loaded_presentation_scrubber_source_guardrails(repo))
    errors.extend(check_run_replay_camera_focus_clear_source_guardrails(repo))
    errors.extend(check_run_replay_velocity_target_source_guardrails(repo))
    errors.extend(check_run_replay_velocity_hit_source_guardrails(repo))
    errors.extend(check_run_replay_velocity_toggle_source_guardrails(repo))
    errors.extend(check_run_replay_velocity_apply_source_guardrails(repo))
    errors.extend(check_run_replay_overlay_source_guardrails(repo))
    errors.extend(check_run_scene_reset_source_guardrails(repo))
    errors.extend(check_run_scene_queue_source_guardrails(repo))
    errors.extend(check_run_scene_context_builder_source_guardrails(repo))
    errors.extend(check_run_generated_camera_setup_source_guardrails(repo))
    errors.extend(check_run_scene_terrain_world_source_guardrails(repo))
    errors.extend(check_run_editable_scene_snapshot_source_guardrails(repo))
    errors.extend(check_run_tornado_defaults_source_guardrails(repo))
    errors.extend(check_run_tornado_sync_source_guardrails(repo))
    errors.extend(check_run_diagnostics_source_guardrails(repo))
    errors.extend(check_run_diagnostics_perf_tick_source_guardrails(repo))
    errors.extend(check_run_diagnostics_memory_dump_source_guardrails(repo))
    errors.extend(check_run_diagnostics_ui_stress_rng_source_guardrails(repo))
    errors.extend(check_run_scene_perf_log_lifecycle_guardrails(repo))
    errors.extend(check_run_scene_control_source_guardrails(repo))
    errors.extend(check_run_scene_browser_refresh_source_guardrails(repo))
    errors.extend(check_run_scene_browser_index_source_guardrails(repo))
    errors.extend(check_run_scene_defaults_source_guardrails(repo))
    errors.extend(check_run_scene_create_source_guardrails(repo))
    errors.extend(check_run_scene_world_override_source_guardrails(repo))
    errors.extend(check_run_scene_generated_control_source_guardrails(repo))
    errors.extend(check_run_scene_style_source_guardrails(repo))
    errors.extend(check_run_scene_coordinator_callback_source_guardrails(repo))
    errors.extend(check_scene_runtime_coordinator_callback_guardrails(repo))
    errors.extend(check_run_ui_text_pass_replay_overlay_guardrails(repo))
    errors.extend(check_interaction_guardrails(repo))
    errors.extend(check_physics_game_model_collection_guardrails(repo))
    errors.extend(check_deleted_physics_model_view_guardrails(repo))
    errors.extend(check_physics_models_access_guardrails(repo))
    errors.extend(check_direct_gfx_raytracing_guardrails(repo))
    errors.extend(check_irender_backend_raytracing_declarations(repo))
    errors.extend(check_graph_owned_render_pass_scheduling(repo))
    return errors


def write_summary(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--max-errors", type=int, default=80)
    args = parser.parse_args()

    repo = args.repo.resolve()
    summary_path = args.json_out or repo / "TestOutput" / "validation" / "runtime_boundaries" / "summary.json"
    self_test_failures = run_self_tests()
    if self_test_failures:
        for failure in self_test_failures:
            print(f"ERROR: runtime boundary self-test failed: {failure}")
        return 98

    errors = validate_runtime_boundaries(repo)
    summary = {
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "status": "pass" if not errors else "fail",
        "errorCount": len(errors),
        "errors": [
            {
                "path": repo_relative(repo, error.path),
                "line": error.line,
                "message": error.message,
                "detail": error.detail,
            }
            for error in errors
        ],
    }
    write_summary(summary_path, summary)

    for error in errors[: args.max_errors]:
        print(
            f"ERROR: {repo_relative(repo, error.path)}:{error.line}: "
            f"{error.message} {error.detail}"
        )
    if len(errors) > args.max_errors:
        print(f"ERROR: suppressed {len(errors) - args.max_errors} additional runtime boundary issue(s).")

    print(f"Runtime boundary summary: {repo_relative(repo, summary_path)} ({len(errors)} errors)")
    if errors:
        print("FAIL: Runtime boundary validation failed.")
        return 1

    print("PASS: Runtime boundary validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
