#!/usr/bin/env python3
#
# File: tools/check_runtime_boundaries.py
# Purpose:
#   Check that Run.h stays a runtime composition root instead of regrowing
#   extracted subsystem ownership, prevent new source inheritance outside the
#   approved stable-boundary budget, and prevent new physics dependencies on the
#   legacy GameModelCollection world container, new game-object types on public
#   physics facades, or raytracing calls on the wide render backend facade. It
#   also blocks direct scheduling or manual-barrier regressions for passes that
#   already moved to render graph callback ownership, and new normal-path global
#   service access while explicit service contexts are built. Renderer globals
#   have an extra file-classification fence so count allowances do not silently
#   approve a new compatibility location.
#
# Mental model:
#   Runtime decomposition is easy to regress by adding one convenient field or
#   helper back to Run. Physics data ownership is similarly easy to regress by
#   threading GameModelCollection into one more API. Render ownership can regress
#   when DXR reflection calls creep back onto Gfx(), when a graph-owned pass is
#   called directly from the runtime frame loop again, or when migrated runtime
#   pass code starts issuing DX12 barriers outside graph declarations. This
#   check is intentionally small: it watches the boundaries named by the active
#   architecture plans.
#
# Glossary:
#   Composition root: Top-level owner that wires subsystems together.
#   Boundary guardrail: Static check that blocks architecture drift.
#   Inheritance guardrail: Static check that blocks source base classes unless
#     they are in the approved stable-boundary budget.
#   Allowlist: Explicit set of legacy references accepted during migration.
#   Migration artifact: Temporary adapter, data-transfer object, or
#     compatibility name that must disappear once its real owner or API replaces
#     it.
#
# Invariants:
#   - Run.h may own subsystem objects, but not their extracted transient state.
#   - Subsystems may borrow explicit service/context structs, but not store Run.
#   - Physics may only keep the current GameModelCollection compatibility
#     surface while stores and handles become authoritative.
#   - Public physics facades expose handles, descriptors, views, or the named
#     PhysicsModelAccess bridge instead of game-object storage types.
#   - IRenderBackend stays a temporary aggregate of named render capabilities;
#     raytracing calls go through IRenderRayTracing/GfxRayTracing instead of the
#     wide IRenderBackend/Gfx facade.
#   - Graph-owned render passes stay scheduled through render graph callback
#     helpers after migration, and runtime pass code must not issue DX12
#     ResourceBarrier calls or backend transition helpers directly.
#   - Unknown render graph resource states remain explicitly counted handoffs
#     until the graph owns concrete initial access for migrated resources.
#   - Existing global service calls are counted debt; adding new ones requires
#     migrating the caller or lowering another allowlist entry first. Direct
#     renderer service calls also need an approved debt-location classification.
#   - Source inheritance is deny-by-default; only rows in
#     APPROVED_INHERITANCE_DECLARATIONS are accepted.
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
RUN_SOURCE = Path("SkullbonezSource/Runtime/Run.cpp")
RUN_FRAME_SOURCE = Path("SkullbonezSource/Runtime/RunFrame.cpp")
RUN_INTERNAL_HEADER = Path("SkullbonezSource/Runtime/RunInternal.h")
RUNTIME_ROOT = Path("SkullbonezSource/Runtime")
PHYSICS_ROOT = Path("SkullbonezSource/Physics")
SKULL_SCOPE_SOURCE = Path("SkullbonezSource/Core/SkullScope.cpp")
PHYSICS_WORLD_SOURCE = PHYSICS_ROOT / "PhysicsWorld.cpp"
PHYSICS_SCENE_SOURCE = PHYSICS_ROOT / "PhysicsScene.cpp"
PHYSICS_DIAGNOSTICS_SINK_SOURCE = PHYSICS_ROOT / "PhysicsDiagnosticsSink.cpp"
RAGDOLL_SOURCE = PHYSICS_ROOT / "Ragdoll.cpp"
GAME_MODEL_COLLECTION_SOURCE = Path("SkullbonezSource/GameObjects/GameModelCollection.cpp")
GAME_MODEL_COLLECTION_HEADER = Path("SkullbonezSource/GameObjects/GameModelCollection.h")
GAME_MODEL_COLLECTION_PHYSICS_ADAPTER_SOURCE = Path("SkullbonezSource/GameObjects/GameModelCollectionPhysicsAdapter.cpp")
IRENDER_BACKEND_HEADER = Path("SkullbonezSource/Rendering/IRenderBackend.h")
RUN_RENDER_SOURCE = Path("SkullbonezSource/Runtime/RunRender.cpp")
RENDER_PIPELINE_SOURCE = Path("SkullbonezSource/Rendering/RenderPipeline.cpp")
RUN_INPUT_SOURCE = Path("SkullbonezSource/Runtime/RunInput.cpp")
RUN_PASSES_SOURCE = Path("SkullbonezSource/Runtime/RunPasses.cpp")
RUN_UI_TEXT_PASS_SOURCE = Path("SkullbonezSource/Runtime/RunUiTextPass.cpp")
RUN_SCENE_SOURCE = Path("SkullbonezSource/Runtime/Scene/RunScene.cpp")
SCENE_AUTHORED_SETUP_SOURCE = Path("SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp")
SCENE_GENERATED_SETUP_SOURCE = Path("SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp")
EDITOR_OBJECT_PLACEMENT_SOURCE = Path("SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.inl")
EDITOR_TOOLS_SOURCE = Path("SkullbonezSource/Runtime/Editor/RunEditorTools.cpp")
MOUSE_PICKUP_TOOLS_SOURCE = Path("SkullbonezSource/Runtime/Editor/RunMousePickupTools.inl")
RUNTIME_TOOLS_SOURCE = Path("SkullbonezSource/Runtime/Tools/RuntimeTools.cpp")
REPLAY_VELOCITY_EDIT_SOURCE = Path("SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.inl")
REPLAY_RECORDER_SOURCE = Path("SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp")
RUNTIME_RENDER_HOST_HEADER = Path("SkullbonezSource/Runtime/Render/RuntimeRenderHost.h")
RUNTIME_RENDER_PASS_CAPABILITY_SOURCES = (
    RUN_PASSES_SOURCE,
    RUN_UI_TEXT_PASS_SOURCE,
    Path("SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h"),
    Path("SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h"),
)
PHYSICS_HOT_PATH_INHERITANCE_SOURCES = (
    PHYSICS_ROOT / "PhysicsWorld.h",
    PHYSICS_ROOT / "PhysicsWorld.cpp",
    PHYSICS_ROOT / "PersistentContactSolver.h",
    PHYSICS_ROOT / "PersistentContactSolver.cpp",
    PHYSICS_ROOT / "PhysicsBodyStore.h",
    PHYSICS_ROOT / "PhysicsBodyStore.cpp",
    PHYSICS_ROOT / "ColliderStore.h",
    PHYSICS_ROOT / "ColliderStore.cpp",
    PHYSICS_ROOT / "SimulationSystem.h",
    PHYSICS_ROOT / "SimulationSystem.cpp",
    PHYSICS_ROOT / "SleepIslandSystem.h",
    PHYSICS_ROOT / "SleepIslandSystem.cpp",
    PHYSICS_ROOT / "Ragdoll.h",
    PHYSICS_ROOT / "Ragdoll.cpp",
)
FIELD_TAIL_PATTERN = r"(?=[^;{}]*\bm_[A-Za-z_]\w*)[^;{}]*;"
RUN_NAME_PATTERN = r"(?:(?:[A-Za-z_]\w*::)*Run)\b"
RUN_CV_PATTERN = rf"(?:const\s+{RUN_NAME_PATTERN}|{RUN_NAME_PATTERN}\s+const|{RUN_NAME_PATTERN})"
GAME_MODEL_COLLECTION_PATTERN = re.compile(r"\bGameModelCollection\b")
PHYSICS_DELETED_MODEL_VIEW_PATTERN = re.compile(r"\b(?:MakePhysicsModelView|PhysicsModelView)\b")
PHYSICS_MODELS_ACCESS_PATTERN = re.compile(r"\bPhysicsModels\s*\(")
PHYSICS_MODELS_COMPAT_ACCESS_PATTERN = re.compile(
    r"\b(?:MutablePhysicsModelsForCompatibility|PhysicsModelsForCompatibility)\s*\("
)
PERSISTENT_CONTACT_SOLVER_CONTEXT_PATTERN = re.compile(
    r"\bstruct\s+PersistentContactSolverContext\b(?P<body>.*?)\n\s*\};",
    re.S,
)
# Invariant: persistent contact solving emits compact side-effect arrays. The
# context must not regain model/event/world callback references.
PERSISTENT_SOLVER_CALLBACK_BOUNDARY_PATTERN = re.compile(
    r"\b(?:PhysicsModelAccess|PhysicsBodyEventSink|PhysicsBodyWritebackSink|PhysicsWorld)\s*&\s*[A-Za-z_]\w*\s*;"
)
PERSISTENT_SOLVER_CONTEXT_MODEL_STREAM_PATTERN = re.compile(
    r"\b(?:GameObjects\s*::\s*)?GameModelBodyStream\b"
)
PHYSICS_WORLD_SOLVER_MODEL_STREAM_FUNCTIONS = (
    "RunSolverPhysics",
    "ApplyTornadoField",
    "WakePointJointConnectedBodies",
)
PHYSICS_WORLD_SOLVER_MODEL_STREAM_PATTERN = re.compile(r"\b(?:GameModelBodyStream|GetBodyStream)\b")
PHYSICS_WORLD_CONTACT_HIGHLIGHT_TICK_PATTERN = re.compile(r"\bmodelAccess\s*\.\s*TickContactHighlights\s*\(")
RENDER_INSTANCE_MODEL_REFRESH_PATTERN = re.compile(
    r"\brenderInstanceStore\s*\.\s*Refresh\s*\(\s*m_gameModels\s*\)"
)
PHYSICS_DIAGNOSTICS_MODEL_RECORD_COMPAT_PATTERN = re.compile(
    r"\bmodelAccess\s*\.\s*TryGetPhysicsDiagnosticsModel\s*\(\s*[^,()]+\s*,\s*[^,()]+\s*\)"
)
PHYSICS_DIAGNOSTICS_MODEL_RECORD_SOURCES = (
    PHYSICS_DIAGNOSTICS_SINK_SOURCE,
    SKULL_SCOPE_SOURCE,
)
REPLAY_RECORDER_MODEL_STATE_CAPTURE_PATTERN = re.compile(
    r"\b(?:ShapeKindForModel\s*\(|[A-Za-z_]\w*\s*(?:->|\.)\s*"
    r"(?:GetReplayBodyId|GetCollisionShape|GetPosition|GetVelocity|GetAngularVelocity|GetOrientation|GetMass|"
    r"GetInvertedMass|GetRotationalInertia|GetInvertedRotationalInertia|IsFixed)\s*\()"
)
RUN_REPLAY_RESTORE_BODY_STORE_REFRESH_PATTERN = re.compile(
    r"\bm_cGameModelCollection\s*\.\s*GetPhysicsBodyStore\s*\("
)
GAME_MODEL_COLLECTION_REPLAY_RESTORE_FUNCTION_PATTERN = re.compile(
    r"\bbool\s+GameModelCollection::TryRestoreReplayBodyState\s*\("
)
GAME_MODEL_COLLECTION_REPLAY_PREDICTION_RESTORE_FUNCTION_PATTERN = re.compile(
    r"\bbool\s+GameModelCollection::TryRestoreReplayPredictionBodyState\s*\("
)
GAME_MODEL_COLLECTION_REPLAY_RESTORE_MODEL_REFRESH_PATTERN = re.compile(
    r"\b(?:CommitEditedModelPhysicsState|RefreshBodyFromModel|GetPhysicsBodyStore)\s*\("
)
GAME_MODEL_COLLECTION_REPLAY_RENDER_POSE_FUNCTION_PATTERN = re.compile(
    r"\bbool\s+GameModelCollection::TrySetReplayRenderPose\s*\("
)
GAME_MODEL_COLLECTION_REPLAY_RENDER_POSE_PHYSICS_COMMIT_PATTERN = re.compile(
    r"\b(?:CommitEditedModelPhysicsState|RefreshBodyFromModel|GetPhysicsBodyStore)\s*\("
)
PHYSICS_SCENE_RUN_PHYSICS_FUNCTION_PATTERN = re.compile(r"\bvoid\s+PhysicsScene::RunPhysics\s*\(")
PHYSICS_SCENE_STEP_BODY_RELOAD_PATTERN = re.compile(r"\bmodelAccess\s*\.\s*ReloadPhysicsBodies\s*\(")
PHYSICS_SCENE_TOPOLOGY_BODY_RELOAD_PATTERN = re.compile(
    r"if\s*\(\s*m_bodyStore\s*\.\s*Count\s*\(\s*\)\s*!=\s*"
    r"(?:modelCount|modelAccess\s*\.\s*ModelCount\s*\(\s*\))\s*\)\s*\{\s*"
    r"modelAccess\s*\.\s*ReloadPhysicsBodies\s*\(",
    re.S,
)
PHYSICS_SCENE_COMMAND_BODY_REFRESH_FUNCTIONS = (
    "WakeBody",
    "SeedBodyAsleep",
    "SetBodyVelocity",
    "SetPendingBodyImpulse",
)
PHYSICS_SCENE_COMMAND_BODY_REFRESH_PATTERN = re.compile(r"\bRefreshBodyStore\s*\(\s*modelAccess\s*\)")
PHYSICS_SCENE_COMMAND_TOPOLOGY_BODY_REFRESH_PATTERN = re.compile(
    r"if\s*\(\s*m_bodyStore\s*\.\s*Count\s*\(\s*\)\s*!=\s*"
    r"(?:modelCount|modelAccess\s*\.\s*ModelCount\s*\(\s*\))\s*\)\s*\{\s*"
    r"RefreshBodyStore\s*\(\s*modelAccess\s*\)",
    re.S,
)
GAME_MODEL_COLLECTION_ADAPTER_BODY_HANDLE_FUNCTION_PATTERN = re.compile(
    r"\bPhysicsBodyHandle\s+GameModelCollectionPhysicsAdapter::BodyHandleForModelIndex\s*\("
)
GAME_MODEL_COLLECTION_ADAPTER_BODY_REFRESH_PATTERN = re.compile(
    r"\bm_collection\s*\.\s*m_physicsEngine\s*\.\s*RefreshBodyStore\s*\(\s*modelAccess\s*\)"
)
GAME_MODEL_COLLECTION_ADAPTER_TOPOLOGY_BODY_REFRESH_PATTERN = re.compile(
    r"if\s*\(\s*m_collection\s*\.\s*m_physicsEngine\s*\.\s*BodyStore\s*\(\s*\)\s*\.\s*Count\s*\(\s*\)\s*!=\s*"
    r"m_collection\s*\.\s*GetModelCount\s*\(\s*\)\s*\)\s*\{\s*"
    r"m_collection\s*\.\s*m_physicsEngine\s*\.\s*RefreshBodyStore\s*\(\s*modelAccess\s*\)",
    re.S,
)
SCENE_SETUP_MODEL_INDEX_PHYSICS_COMMAND_PATTERN = re.compile(
    r"\bcontext\s*\.\s*models\s*\.\s*"
    r"(?:SetPendingBodyImpulse|SeedModelAsleep|WakeModel|ApplyBodyImpulse)\s*\("
)
SCENE_SETUP_PHYSICS_COMMAND_SOURCES = (
    SCENE_AUTHORED_SETUP_SOURCE,
    SCENE_GENERATED_SETUP_SOURCE,
)
EDITOR_MODEL_INDEX_PHYSICS_COMMAND_PATTERN = re.compile(
    r"\b(?:context\s*\.\s*models|collection)\s*\.\s*"
    r"(?:SetPendingBodyImpulse|SeedModelAsleep|WakeModel|ApplyBodyImpulse)\s*\("
)
EDITOR_PHYSICS_COMMAND_SOURCES = (
    EDITOR_OBJECT_PLACEMENT_SOURCE,
    EDITOR_TOOLS_SOURCE,
)
MOUSE_PICKUP_MODEL_INDEX_PHYSICS_COMMAND_PATTERN = re.compile(
    r"\bm_cGameModelCollection\s*\.\s*"
    r"(?:SetPendingBodyImpulse|SeedModelAsleep|WakeModel|ApplyBodyImpulse)\s*\("
)
LAUNCHER_MODEL_INDEX_PHYSICS_COMMAND_PATTERN = re.compile(
    r"\bcollection\s*\.\s*"
    r"(?:SetPendingBodyImpulse|SeedModelAsleep|WakeModel|ApplyBodyImpulse)\s*\("
)
REPLAY_VELOCITY_MODEL_STATE_PHYSICS_COMMAND_PATTERN = re.compile(
    r"\b(?:"
    r"model\s*\.\s*(?:SetLinearVelocity|SetAngularVelocity)|"
    r"(?:modelCollection|m_cGameModelCollection)\s*\.\s*(?:CommitEditedModelPhysicsState|WakeModel)"
    r")\s*\("
)
RUN_FRAME_REPLAY_EDITOR_TRANSFORM_WAKE_PATTERN = re.compile(
    r"\bm_cGameModelCollection\s*\.\s*WakeModel\s*\("
)
RAGDOLL_MODEL_INDEX_PHYSICS_COMMAND_PATTERN = re.compile(
    r"\bcollection\s*\.\s*(?:SeedModelAsleep|WakeModel|ApplyBodyImpulse|SetPendingBodyImpulse)\s*\("
)
DELETED_GAME_MODEL_COLLECTION_PHYSICS_WRAPPER_PATTERN = re.compile(
    r"\b(?:void\s+GameModelCollection\s*::\s*)?"
    r"(?:WakeModel|SeedModelAsleep|ApplyBodyImpulse|SetPendingBodyImpulse)\s*\("
)
HOT_PATH_INHERITANCE_PATTERN = re.compile(
    r"^\s*(?:class|struct)\s+[A-Za-z_]\w*[^{;\n]*:\s*(?:public|protected|private)\b",
    re.M,
)
PHYSICS_MODEL_ACCESS_INHERITANCE_PATTERN = re.compile(
    r"^\s*(?:class|struct)\s+[A-Za-z_]\w*[^{;]*:\s*[^;{]*"
    r"(?:public|protected|private)\s+(?:Physics::)?(?:PhysicsModelAccess|PhysicsBodyEventSink)\b",
    re.M,
)
INHERITANCE_DECLARATION_PATTERN = re.compile(
    r"^\s*(?:class|struct)\s+"
    r"(?P<name>(?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)"
    r"(?:\s+(?:final|[A-Z_][A-Z0-9_]*))*"
    r"\s*:(?!:)\s*(?P<bases>[^{};]+?)\s*\{",
    re.M | re.S,
)
SOURCE_BEARING_SUFFIXES = { ".cpp", ".h", ".hpp", ".inl" }
# Intentional runtime-polymorphism budget. Adding a row means the owning plan
# has accepted a stable boundary, real runtime dispatch need, call frequency,
# and validation/perf evidence. Everything else should be composition or values.
APPROVED_INHERITANCE_DECLARATIONS: dict[tuple[Path, str], str] = {
    (
        Path("SkullbonezSource/Rendering/IRenderBackend.h"),
        "IRenderBackend",
    ): "public IRenderDeviceLifecycle, public IRenderResourceFactory, public IRenderCommandContext, public IRenderDiagnostics, public IRenderCaptureBackend",
    (
        Path("SkullbonezSource/Rendering/DX12/FramebufferDX12.h"),
        "FramebufferDX12",
    ): "public IFramebuffer",
    (
        Path("SkullbonezSource/Rendering/DX12/MeshDX12.h"),
        "MeshDX12",
    ): "public IMesh",
    (
        Path("SkullbonezSource/Rendering/DX12/RenderBackendDX12.h"),
        "RenderBackendDX12",
    ): "public IRenderBackend, public IRenderRayTracing",
    (
        Path("SkullbonezSource/Rendering/DX12/ShaderDX12.h"),
        "ShaderDX12",
    ): "public IShader",
}
DELETED_MIGRATION_ARTIFACT_PATTERNS: tuple[tuple[str, re.Pattern[str], str], ...] = (
    (
        "GameModelRuntimePhysicsTuning",
        re.compile(r"\bGameModelRuntimePhysicsTuning\b"),
        "Keep runtime physics tuning on authored assets or explicit body descriptors, not a migration DTO.",
    ),
    (
        "legacyModelIndex",
        re.compile(r"\blegacyModelIndex\b"),
        "Use stable physics/entity handles or a quarantined migration map instead of reviving legacy model indices.",
    ),
    (
        "RuntimeConfigSnapshot",
        re.compile(r"\bRuntimeConfigSnapshot\b"),
        "Pass WorldEnvironmentSettings and other owner-specific settings instead of a catch-all runtime snapshot.",
    ),
    (
        "IRenderSceneView",
        re.compile(r"\bIRenderSceneView\b"),
        "Render passes should consume concrete render/model data paths, not a one-implementation migration interface.",
    ),
    (
        "PhysicsModelAccess raw model range facade",
        re.compile(
            r"\b(?:PhysicsModelMutableRange|PhysicsModelConstRange|BorrowMutableModels)\b"
            r"|\b(?:MutableModelData|ModelData)\s*\("
            r"|\b(?:modelAccess|physicsModelAccess)\s*\.\s*Models\s*\("
        ),
        "Use PhysicsModelAccess command/query methods and store-backed views instead of raw GameModel ranges.",
    ),
    (
        "PhysicsBodyWritebackSink",
        re.compile(r"\bPhysicsBodyWritebackSink\b"),
        "Queue solver body writeback as plain side-effect data and apply it from PhysicsWorld after the solve.",
    ),
    (
        "PhysicsBodyEventSink",
        re.compile(r"\bPhysicsBodyEventSink\b"),
        "Apply solver-triggered model-owner events through PhysicsModelAccess commands after hot-path work completes.",
    ),
    (
        "PersistentContactSolver body mirror writeback queue",
        re.compile(r"\b(?:bodyMirrorWritebacks|QueueBodyMirrorWriteback)\b"),
        "Solver mutation belongs in PhysicsBodyStore; keep model mirroring as one named step-boundary sync.",
    ),
    (
        "AssetSystem::CreateShader(const char*)",
        re.compile(
            r"\bAssetSystem\s*::\s*CreateShader\s*\(\s*const\s+char\s*\*\s*[A-Za-z_]\w*\s*\)"
            r"|\bCreateShader\s*\(\s*const\s+char\s*\*\s*[A-Za-z_]\w*\s*\)\s*const\s*;",
            re.S,
        ),
        "Create shaders through AssetSystem::CreateShader(renderResources, name) so render resource authority is explicit.",
    ),
    (
        "PhysicsStandaloneWorld body mirror arrays",
        re.compile(
            r"\bstd\s*::\s*vector\s*<\s*PhysicsBodyView\s*>\s*m_bodies\b"
            r"|\bm_(?:generations|alive|freeIndices)\b"
        ),
        "Standalone bodies must live in PhysicsBodyStore; do not recreate a parallel view/liveness/generation mirror.",
    ),
)
PUBLIC_PHYSICS_FACADE_HEADERS = (
    Path("SkullbonezSource/Physics/PhysicsApi.h"),
    Path("SkullbonezSource/Physics/PhysicsEngine.h"),
)
# Invariant: PhysicsApi/PhysicsEngine are the front door for new physics callers.
# Keep them on handles, descriptors, immutable views, or the deliberately named
# PhysicsModelAccess compatibility bridge until the old collection path is gone.
PUBLIC_PHYSICS_FACADE_GAME_OBJECT_PATTERN = re.compile(
    r"\b(?:GameObjects\s*::\s*)?(?:GameModelCollection|GameModel)\b|"
    r"\bstd\s*::\s*vector\s*<\s*(?:GameObjects\s*::\s*)?GameModel\b"
)
DIRECT_GFX_RAYTRACING_PATTERN = re.compile(
    r"\bGfx\s*\(\s*\)\s*\.\s*(?:InitDXR|DispatchReflectionRays|BuildTLAS|GetReflectionUAVTexture|"
    r"ShutdownDXR|GetInstancedMeshStaticVBVA|GetInstancedMeshStaticStride)\s*\("
)
IRENDER_BACKEND_RAYTRACING_DECLARATION_PATTERN = re.compile(
    r"\b(?:virtual\s+)?(?:void|uint32_t|uint64_t|int)\s+"
    r"(?:InitDXR|DispatchReflectionRays|BuildTLAS|GetReflectionUAVTexture|ShutdownDXR|"
    r"GetInstancedMeshStaticVBVA|GetInstancedMeshStaticStride)\s*\("
)
IRENDER_BACKEND_CLASS_PATTERN = re.compile(r"\bclass\s+IRenderBackend\b[^{]*\{", re.S)
IRENDER_BACKEND_DIRECT_METHOD_PATTERN = re.compile(
    r"(?:^|[;\n])\s*(?:virtual\s+)?"
    r"(?:[A-Za-z_:][A-Za-z0-9_:<>,]*[\s*&]+)+"
    r"([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:override\s*)?(?:=\s*(?:0|default))?\s*;",
    re.S,
)
RUNTIME_RENDER_PASS_WIDE_BACKEND_PATTERN = re.compile(
    r"\bIRenderBackend\b|#\s*include\s+[<\"][^>\"]*IRenderBackend\.h[>\"]"
)
GRAPH_OWNED_RENDER_PASS_DIRECT_CALL_PATTERN = re.compile(
    r"\bm_(?:(?:shadowPass|skyPass|reflectionPass|objectPass|terrainPass|waterPass|tornadoVisualPass|"
    r"debugOverlayPass|volumetricPass|tonemapPass|uiTextPass)\s*\.\s*Render|sceneTargetPass\s*\.\s*Begin)\s*\("
    r"|\bhost\s*\.\s*RenderReplayPredictionGhosts\s*\("
)
GRAPH_OWNED_RENDER_PASS_MANUAL_BARRIER_PATTERN = re.compile(
    r"\b(?:ResourceBarrier\s*\(|D3D12_RESOURCE_BARRIER\b|ExecuteGraph(?:Transition|UavBarrier)\s*\()"
)
GRAPH_OWNED_RENDER_PASS_MANUAL_BARRIER_SOURCES = (
    RUN_RENDER_SOURCE,
    RUN_PASSES_SOURCE,
    RUN_UI_TEXT_PASS_SOURCE,
)
RENDER_GRAPH_ADD_EXTERNAL_RESOURCE_CALL_PATTERN = re.compile(r"\bAddExternalResource\s*\(")
RENDER_GRAPH_UNKNOWN_ACCESS_VALUE_PATTERN = re.compile(
    r"\b(?:(?:[A-Za-z_]\w*)::)*RenderGraphResourceAccess::Unknown\b"
)
RENDER_GRAPH_UNKNOWN_ACCESS_SOURCES = (
    RUN_RENDER_SOURCE,
    RENDER_PIPELINE_SOURCE,
)
RENDER_GRAPH_UNKNOWN_ACCESS_ALLOWLIST: Counter[tuple[Path, str]] = Counter(
    {
        # Handoff: CinematicSceneDepth still starts in a DX12 framebuffer-owned
        # state until graph transient/import ownership replaces this legacy edge.
        ( RUN_RENDER_SOURCE, "CinematicSceneDepth" ): 1,
        ( RENDER_PIPELINE_SOURCE, "CinematicSceneDepth" ): 1,
    }
)
GLOBAL_SERVICE_ACCESS_PATTERNS: tuple[tuple[str, re.Pattern[str], str, str], ...] = (
    (
        "Cfg()",
        re.compile(r"\bCfg\s*\(\s*\)"),
        "global config access is count-guarded",
        "Pass a config snapshot or explicit config reference instead of adding another Cfg() call.",
    ),
    (
        "Gfx()",
        re.compile(r"\bGfx\s*\(\s*\)"),
        "global renderer service access is count-guarded",
        "Pass an explicit renderer/render context for new code instead of adding another Gfx() call.",
    ),
    (
        "GfxRayTracing()",
        re.compile(r"\bGfxRayTracing\s*\(\s*\)"),
        "global raytracing service access is count-guarded",
        "Borrow IRenderRayTracing through an explicit render context before adding another global DXR access.",
    ),
    (
        "IsGfxReady()",
        re.compile(r"\bIsGfxReady\s*\(\s*\)"),
        "global renderer readiness access is count-guarded",
        "Carry renderer readiness through an explicit backend view instead of adding another global readiness probe.",
    ),
    (
        "IsGfxRayTracingReady()",
        re.compile(r"\bIsGfxRayTracingReady\s*\(\s*\)"),
        "global raytracing readiness access is count-guarded",
        "Carry DXR readiness through an explicit backend view instead of adding another global readiness probe.",
    ),
    (
        "ActiveAssetSystem()",
        re.compile(r"\bActiveAssetSystem\s*\("),
        "global asset-system access is count-guarded",
        "Pass an explicit AssetSystem/asset context instead of adding another active-asset bridge call.",
    ),
    (
        "CreateShaderFromActiveAssets()",
        re.compile(r"\bCreateShaderFromActiveAssets\s*\("),
        "global shader factory access is count-guarded",
        "Create shaders through an explicit asset/render context instead of the active-asset bridge.",
    ),
    (
        "TextureCollection::Instance()",
        re.compile(r"\bTextureCollection\s*::\s*Instance\s*\("),
        "global texture collection access is count-guarded",
        "Borrow the runtime-owned texture service instead of adding another TextureCollection singleton call.",
    ),
    (
        "CameraCollection::Instance()",
        re.compile(r"\bCameraCollection\s*::\s*Instance\s*\("),
        "global camera collection access is count-guarded",
        "Borrow the scene/world camera service instead of adding another CameraCollection singleton call.",
    ),
    (
        "Window::Instance()",
        re.compile(r"\bWindow\s*::\s*Instance\s*\("),
        "global window service access is count-guarded",
        "Use a bound window service or the callback bridge instead of adding another Window singleton call.",
    ),
    (
        "SkyBox::Instance()",
        re.compile(r"\bSkyBox\s*::\s*Instance\s*\("),
        "global skybox access is count-guarded",
        "Keep skybox lifetime world-owned instead of adding another SkyBox singleton call.",
    ),
    (
        "WorkerPool::Instance()",
        re.compile(r"\bWorkerPool\s*::\s*Instance\s*\("),
        "global worker pool access is count-guarded",
        "Borrow worker services from runtime context before adding another WorkerPool singleton call.",
    ),
    (
        "Profiler::Instance()",
        re.compile(r"\bProfiler\s*::\s*Instance\s*\("),
        "global profiler access is count-guarded",
        "Route diagnostics/profiling through an explicit diagnostics context before adding another profiler singleton call.",
    ),
)
GLOBAL_RENDERER_SERVICE_LABELS = { "Gfx()", "GfxRayTracing()", "IsGfxReady()", "IsGfxRayTracingReady()" }
# Location classifications are a second fence over the counted Gfx() ratchet:
# they make each remaining direct renderer-service file an explicitly reviewed
# compatibility location instead of letting a raw count entry approve a new file.
GLOBAL_RENDERER_SERVICE_ACCESS_CLASSIFICATIONS: dict[Path, str] = {
    Path("SkullbonezSource/Core/Profiler.cpp"): "diagnostics/profiler bridge",
    Path("SkullbonezSource/Physics/Debug/BroadphaseVisualizer.cpp"): "physics debug visualizer compatibility",
    Path("SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp"): "physics debug visualizer compatibility",
    Path("SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp"): "physics debug visualizer compatibility",
    Path("SkullbonezSource/Physics/TornadoField.cpp"): "physics debug rendering compatibility",
    Path("SkullbonezSource/Rendering/IRenderBackend.cpp"): "backend accessor definition",
    Path("SkullbonezSource/Rendering/IRenderBackend.h"): "backend accessor declaration and tracing RAII",
    Path("SkullbonezSource/Runtime/Editor/LauncherLaser.cpp"): "editor transient geometry compatibility",
    Path("SkullbonezSource/Runtime/Editor/RunEditorTracer.inl"): "editor debug tracing compatibility",
    Path("SkullbonezSource/Runtime/Run.cpp"): "runtime composition root",
    Path("SkullbonezSource/Runtime/RunFrame.cpp"): "runtime frame lifecycle",
    Path("SkullbonezSource/Runtime/RunInput.cpp"): "runtime input cinematic bridge",
    Path("SkullbonezSource/Runtime/RunPasses.cpp"): "runtime render pass lifecycle",
    Path("SkullbonezSource/Runtime/RunRender.cpp"): "runtime render service composition",
    Path("SkullbonezSource/Runtime/RunStress.cpp"): "runtime stress harness bridge",
    Path("SkullbonezSource/Runtime/RunUiTextPass.cpp"): "UI text pass compatibility",
    Path("SkullbonezSource/Runtime/Window.cpp"): "window resize bridge",
    Path("SkullbonezSource/UI/UITabProfiler.cpp"): "UI diagnostics compatibility",
}
GENERIC_INSTANCE_ACCESS_PATTERN = re.compile(r"\b(?P<class_name>[A-Za-z_]\w*)\s*::\s*Instance\s*\(")
NAMED_GLOBAL_SERVICE_INSTANCE_CLASSES = {
    "TextureCollection",
    "CameraCollection",
    "Window",
    "SkyBox",
    "WorkerPool",
    "Profiler",
}
PROCESS_GLOBAL_POINTER_PATTERN = re.compile(r"\bpInstance\b")
MUTABLE_PROCESS_GLOBAL_PATTERN = re.compile(r"\bg_[A-Za-z_]\w*\b")
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
        ( "SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp", "GameModelCollection& models," ),
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
        ( "SkullbonezSource/Physics/Debug/CollisionVisualizer.h", "GameObjects::GameModelCollection& models," ),
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
        # PhysicsModelAccess is the named owner facade. It may hold the owner;
        # physics code must not add unrelated GameModelCollection dependencies.
        ( "SkullbonezSource/Physics/PhysicsModelAccess.h", "class GameModelCollection;" ),
        (
            "SkullbonezSource/Physics/PhysicsModelAccess.h",
            "explicit PhysicsModelAccess( GameObjects::GameModelCollection& collection );",
        ),
        ( "SkullbonezSource/Physics/PhysicsModelAccess.h", "GameObjects::GameModelCollection& m_collection;" ),
    )
)

# The old neutral PhysicsModels() name is fully blocked. Remaining vector
# borrowers must use the explicit *ForCompatibility() accessors until stable
# body/entity handles replace them.
PHYSICS_MODELS_ACCESS_ALLOWLIST: Counter[tuple[Path, str]] = Counter()

# Deleted named compatibility accessors stay blocked at zero hits. Remaining
# raw model borrowers are tracked by PhysicsModelAccess and owner-specific
# store migration rows instead of by reviving these vector accessor names.
PHYSICS_MODELS_COMPAT_ACCESS_ALLOWLIST: Counter[tuple[Path, str]] = Counter()

# Counted allowlist for the current global-service compatibility surface. This
# is not approval for more singleton use; it is a ratchet. New Gfx(), active
# asset bridge, shader-factory, and service Instance() calls must either migrate
# to explicit context wiring or deliberately lower another entry.
GLOBAL_SERVICE_ACCESS_ALLOWLIST: Counter[tuple[Path, str]] = Counter(
    {
        ( Path(path), label ): count
        for path, label, count in (
            ( "SkullbonezSource/Core/Common.h", "Cfg()", 2 ),
            ( "SkullbonezSource/Core/Common.h", "EngineConfig::Instance()", 1 ),
            ( "SkullbonezSource/Core/Config.cpp", "EngineConfig::Instance()", 1 ),
            ( "SkullbonezSource/Core/LockOrderValidator.cpp", "LockOrderValidator::Instance()", 5 ),
            ( "SkullbonezSource/Core/LockOrderValidator.cpp", "g_*", 12 ),
            ( "SkullbonezSource/Core/PlatformProfiler.cpp", "g_*", 12 ),
            ( "SkullbonezSource/Core/Profiler.cpp", "Gfx()", 9 ),
            ( "SkullbonezSource/Core/Profiler.cpp", "IsGfxReady()", 6 ),
            ( "SkullbonezSource/Core/Profiler.cpp", "Profiler::Instance()", 2 ),
            ( "SkullbonezSource/Core/Profiler.h", "Profiler::Instance()", 11 ),
            ( "SkullbonezSource/Core/WorkerPool.cpp", "WorkerPool::Instance()", 2 ),
            ( "SkullbonezSource/Core/WorkerPool.cpp", "g_*", 8 ),
            ( "SkullbonezSource/Physics/Debug/BroadphaseVisualizer.cpp", "Gfx()", 2 ),
            ( "SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp", "Gfx()", 14 ),
            ( "SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp", "IsGfxReady()", 1 ),
            ( "SkullbonezSource/Physics/Debug/PhysicsDebugVisualizer.cpp", "Gfx()", 2 ),
            ( "SkullbonezSource/Physics/TornadoField.cpp", "Gfx()", 2 ),
            ( "SkullbonezSource/Rendering/IRenderBackend.cpp", "Gfx()", 2 ),
            ( "SkullbonezSource/Rendering/IRenderBackend.cpp", "GfxRayTracing()", 1 ),
            ( "SkullbonezSource/Rendering/IRenderBackend.cpp", "IsGfxReady()", 1 ),
            ( "SkullbonezSource/Rendering/IRenderBackend.cpp", "IsGfxRayTracingReady()", 1 ),
            ( "SkullbonezSource/Rendering/IRenderBackend.h", "Gfx()", 3 ),
            ( "SkullbonezSource/Rendering/IRenderBackend.h", "GfxRayTracing()", 1 ),
            ( "SkullbonezSource/Rendering/IRenderBackend.h", "IsGfxReady()", 2 ),
            ( "SkullbonezSource/Rendering/IRenderBackend.h", "IsGfxRayTracingReady()", 1 ),
            ( "SkullbonezSource/Runtime/Editor/LauncherLaser.cpp", "Gfx()", 18 ),
            ( "SkullbonezSource/Runtime/Editor/LauncherLaser.cpp", "IsGfxReady()", 3 ),
            ( "SkullbonezSource/Runtime/Editor/LauncherTools.cpp", "Cfg()", 3 ),
            ( "SkullbonezSource/Runtime/Editor/RunEditorTracer.inl", "Gfx()", 1 ),
            ( "SkullbonezSource/Runtime/Editor/RunEditorTracer.inl", "IsGfxReady()", 1 ),
            ( "SkullbonezSource/Runtime/Init.cpp", "Cfg()", 15 ),
            ( "SkullbonezSource/Runtime/Init.cpp", "Window::Instance()", 1 ),
            ( "SkullbonezSource/Runtime/Init.cpp", "WorkerPool::Instance()", 1 ),
            ( "SkullbonezSource/Runtime/Init.cpp", "g_*", 6 ),
            ( "SkullbonezSource/Runtime/Input.cpp", "g_*", 43 ),
            ( "SkullbonezSource/Runtime/Run.cpp", "Gfx()", 8 ),
            ( "SkullbonezSource/Runtime/Run.cpp", "IsGfxReady()", 6 ),
            ( "SkullbonezSource/Runtime/Run.cpp", "Profiler::Instance()", 1 ),
            ( "SkullbonezSource/Runtime/RunFrame.cpp", "Gfx()", 1 ),
            ( "SkullbonezSource/Runtime/RunFrame.cpp", "Profiler::Instance()", 3 ),
            ( "SkullbonezSource/Runtime/RunInteractionAutomation.cpp", "Cfg()", 2 ),
            ( "SkullbonezSource/Runtime/RunInput.cpp", "IsGfxReady()", 1 ),
            ( "SkullbonezSource/Runtime/RunPasses.cpp", "IsGfxReady()", 1 ),
            ( "SkullbonezSource/Runtime/RunRender.cpp", "Cfg()", 3 ),
            ( "SkullbonezSource/Runtime/RunRender.cpp", "Gfx()", 1 ),
            ( "SkullbonezSource/Runtime/RunRender.cpp", "GfxRayTracing()", 1 ),
            ( "SkullbonezSource/Runtime/RunRender.cpp", "IsGfxReady()", 1 ),
            ( "SkullbonezSource/Runtime/RunRender.cpp", "IsGfxRayTracingReady()", 1 ),
            ( "SkullbonezSource/Runtime/RunStress.cpp", "Cfg()", 1 ),
            ( "SkullbonezSource/Runtime/RunStress.cpp", "Gfx()", 2 ),
            ( "SkullbonezSource/Runtime/RunStress.cpp", "IsGfxReady()", 2 ),
            ( "SkullbonezSource/Runtime/RunUiTextPass.cpp", "Profiler::Instance()", 1 ),
            ( "SkullbonezSource/Runtime/RuntimeDiagnostics.cpp", "Profiler::Instance()", 2 ),
            ( "SkullbonezSource/Runtime/Window.cpp", "Cfg()", 6 ),
            ( "SkullbonezSource/Runtime/Window.cpp", "Gfx()", 1 ),
            ( "SkullbonezSource/Runtime/Window.cpp", "IsGfxReady()", 1 ),
            ( "SkullbonezSource/Runtime/Window.cpp", "Window::Instance()", 1 ),
            ( "SkullbonezSource/Runtime/Window.cpp", "pInstance", 4 ),
            ( "SkullbonezSource/Runtime/Window.h", "pInstance", 1 ),
            ( "SkullbonezSource/UI/UITabProfiler.cpp", "Gfx()", 1 ),
            ( "SkullbonezSource/UI/UITabProfiler.cpp", "IsGfxReady()", 1 ),
            ( "SkullbonezSource/UI/UITabProfiler.cpp", "Profiler::Instance()", 6 ),
        )
    }
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
    "backend",
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
    "RuntimeRenderBackendView": {
        "renderBackend",
        "rayTracingBackend",
    },
    "RenderBackendView": {
        "active",
    },
    "RenderRuntimeView": {
        "systems",
        "config",
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


def strip_cpp_comments_and_string_literals(text: str) -> str:
    stripped = strip_cpp_comments(text)

    def blank_literal(match: re.Match[str]) -> str:
        return re.sub(r"[^\n]", " ", match.group(0))

    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', blank_literal, stripped, flags=re.S)


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


def extract_class_body(stripped: str, class_name: str) -> tuple[int, int, int, str] | None:
    match = re.search(rf"\bclass\s+{re.escape(class_name)}\b[^\{{]*\{{", stripped, flags=re.S)
    if not match:
        return None
    open_brace_offset = stripped.find("{", match.start(), match.end())
    if open_brace_offset < 0:
        return None
    close_brace_offset = find_matching_close_brace(stripped, open_brace_offset)
    return match.start(), open_brace_offset + 1, close_brace_offset, stripped[open_brace_offset + 1 : close_brace_offset]


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


def check_public_physics_facade_game_object_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PUBLIC_PHYSICS_FACADE_GAME_OBJECT_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "public physics facade game-object dependency is blocked",
                "Keep PhysicsApi/PhysicsEngine signatures on handles, descriptors, views, or PhysicsModelAccess compatibility only.",
            )
        )
    return errors


def check_public_physics_facade_game_object_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in PUBLIC_PHYSICS_FACADE_HEADERS:
        path = repo / relative_path
        errors.extend(check_public_physics_facade_game_object_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_deleted_migration_artifact_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for label, pattern, detail in DELETED_MIGRATION_ARTIFACT_PATTERNS:
        for match in pattern.finditer(stripped):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    f"deleted migration artifact is blocked: {label}",
                    detail,
                )
            )
    return errors


def check_deleted_migration_artifact_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / Path("SkullbonezSource")).rglob("*")):
        if path.suffix not in { ".cpp", ".h", ".hpp", ".inl" }:
            continue
        errors.extend(check_deleted_migration_artifact_guardrails_text(path, path.read_text(encoding="utf-8")))
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


def check_persistent_solver_context_model_access_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for context_match in PERSISTENT_CONTACT_SOLVER_CONTEXT_PATTERN.finditer(stripped):
        context_body = context_match.group("body")
        for member_match in PERSISTENT_SOLVER_CALLBACK_BOUNDARY_PATTERN.finditer(context_body):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, context_match.start("body") + member_match.start()),
                    "persistent contact solver callback boundary is blocked",
                    "Emit PersistentContactSolverSideEffects arrays and apply owner-side consequences after Solve().",
                )
            )
        for stream_match in PERSISTENT_SOLVER_CONTEXT_MODEL_STREAM_PATTERN.finditer(context_body):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, context_match.start("body") + stream_match.start()),
                    "persistent contact solver model stream boundary is blocked",
                    "Persistent contact solving should read PhysicsBodyStore and ColliderStore records, not GameModelBodyStream.",
                )
            )
    return errors


def check_persistent_solver_context_model_access_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_ROOT / "PhysicsWorld.h"
    return check_persistent_solver_context_model_access_guardrails_text(path, path.read_text(encoding="utf-8"))


# Invariant: RunSolverPhysics is the hot step. Per-body model mirror writes here
# recreate cache churn and make PhysicsBodyStore less authoritative.
# Invariant: the same hot-step helpers must not refresh GameModelBodyStream; the
# solver should read PhysicsBodyStore/ColliderStore records already in memory.
def check_physics_world_solver_body_writeback_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    solver_pattern = re.compile(r"\bvoid\s+PhysicsWorld::RunSolverPhysics\s*\([^{}]*\)\s*\{", re.S)
    writeback_pattern = re.compile(r"\bmodelAccess\s*\.\s*WriteBackPhysicsBody\s*\(")
    for solver_match in solver_pattern.finditer(stripped):
        open_brace = stripped.find("{", solver_match.start(), solver_match.end())
        if open_brace < 0:
            continue
        close_brace = find_matching_close_brace(stripped, open_brace)
        for writeback_match in writeback_pattern.finditer(stripped, open_brace, close_brace):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, writeback_match.start()),
                    "physics solver hot path per-body model writeback is blocked",
                    "Write solver results into PhysicsBodyStore and mirror models once at the PhysicsWorld step boundary.",
                )
            )
    return errors


def check_physics_world_solver_body_writeback_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_WORLD_SOURCE
    return check_physics_world_solver_body_writeback_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_physics_world_solver_model_stream_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for function_name in PHYSICS_WORLD_SOLVER_MODEL_STREAM_FUNCTIONS:
        function_pattern = re.compile(
            rf"\bvoid\s+PhysicsWorld::{re.escape(function_name)}\s*\([^{{}}]*\)\s*\{{",
            re.S,
        )
        for function_match in function_pattern.finditer(stripped):
            open_brace = stripped.find("{", function_match.start(), function_match.end())
            if open_brace < 0:
                continue
            close_brace = find_matching_close_brace(stripped, open_brace)
            for stream_match in PHYSICS_WORLD_SOLVER_MODEL_STREAM_PATTERN.finditer(stripped, open_brace, close_brace):
                errors.append(
                    BoundaryError(
                        path,
                        line_for_offset(stripped, stream_match.start()),
                        "physics solver hot path model body stream is blocked",
                        (
                            f"PhysicsWorld::{function_name} should read PhysicsBodyStore/ColliderStore records "
                            "instead of refreshing GameModelBodyStream."
                        ),
                    )
                )
    return errors


def check_physics_world_solver_model_stream_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_WORLD_SOURCE
    return check_physics_world_solver_model_stream_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_physics_world_contact_highlight_tick_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name != "PhysicsWorld.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_WORLD_CONTACT_HIGHLIGHT_TICK_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "physics world model contact-highlight tick is blocked",
                (
                    "Contact-highlight timers are model-owned presentation state; tick them at the PhysicsScene "
                    "compatibility edge instead of inside PhysicsWorld::RunPhysics."
                ),
            )
        )
    return errors


def check_physics_world_contact_highlight_tick_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_WORLD_SOURCE
    return check_physics_world_contact_highlight_tick_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_render_instance_store_authority_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in RENDER_INSTANCE_MODEL_REFRESH_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "render instance model-transform refresh is blocked",
                "Refresh render instances from PhysicsBodyStore and ColliderStore so draw poses do not depend on the post-solve GameModel mirror.",
            )
        )
    return errors


def check_render_instance_store_authority_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / GAME_MODEL_COLLECTION_SOURCE
    return check_render_instance_store_authority_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_physics_diagnostics_store_authority_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_DIAGNOSTICS_MODEL_RECORD_COMPAT_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "physics diagnostics model-state mirror read is blocked",
                (
                    "Pass PhysicsBodyStore and ColliderStore to TryGetPhysicsDiagnosticsModel; "
                    "use TryGetPhysicsDiagnosticsModelName only for name-only logs."
                ),
            )
        )
    return errors


def check_physics_diagnostics_store_authority_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in PHYSICS_DIAGNOSTICS_MODEL_RECORD_SOURCES:
        path = repo / relative_path
        errors.extend(check_physics_diagnostics_store_authority_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_replay_recorder_store_authority_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in REPLAY_RECORDER_MODEL_STATE_CAPTURE_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "replay recorder model-state capture is blocked",
                (
                    "Replay capture should read PhysicsBodyStore and ColliderStore for body physics state; "
                    "GameModel is name metadata only."
                ),
            )
        )
    return errors


def check_replay_recorder_store_authority_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / REPLAY_RECORDER_SOURCE
    return check_replay_recorder_store_authority_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_run_replay_restore_store_authority_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in RUN_REPLAY_RESTORE_BODY_STORE_REFRESH_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "replay restore body-store reload is blocked",
                (
                    "Replay restore should write sampled body state into PhysicsBodyStore directly; "
                    "do not reload the store from GameModel after restore."
                ),
            )
        )
    return errors


def check_game_model_collection_replay_restore_store_authority_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for function_pattern, function_name in (
        (GAME_MODEL_COLLECTION_REPLAY_RESTORE_FUNCTION_PATTERN, "TryRestoreReplayBodyState"),
        (GAME_MODEL_COLLECTION_REPLAY_PREDICTION_RESTORE_FUNCTION_PATTERN, "TryRestoreReplayPredictionBodyState"),
    ):
        bounds = _function_body_bounds(stripped, function_pattern)
        if not bounds:
            continue

        open_brace, close_brace = bounds
        for match in GAME_MODEL_COLLECTION_REPLAY_RESTORE_MODEL_REFRESH_PATTERN.finditer(
            stripped,
            open_brace,
            close_brace,
        ):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "replay restore model-to-store refresh is blocked",
                    (
                        f"{function_name} should call the store-owned replay restore command; "
                        "do not refresh PhysicsBodyStore from GameModel for restored body state."
                    ),
                )
            )
    return errors


def check_game_model_collection_replay_render_pose_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    bounds = _function_body_bounds(stripped, GAME_MODEL_COLLECTION_REPLAY_RENDER_POSE_FUNCTION_PATTERN)
    if not bounds:
        return errors

    open_brace, close_brace = bounds
    for match in GAME_MODEL_COLLECTION_REPLAY_RENDER_POSE_PHYSICS_COMMIT_PATTERN.finditer(
        stripped,
        open_brace,
        close_brace,
    ):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "replay render-pose physics commit is blocked",
                (
                    "TrySetReplayRenderPose is a presentation-only override; do not recapture GameModel "
                    "state into PhysicsBodyStore from this path."
                ),
            )
        )
    return errors


def check_replay_restore_store_authority_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    run_path = repo / RUN_SOURCE
    errors.extend(check_run_replay_restore_store_authority_guardrails_text(run_path, run_path.read_text(encoding="utf-8")))
    collection_path = repo / GAME_MODEL_COLLECTION_SOURCE
    errors.extend(
        check_game_model_collection_replay_restore_store_authority_guardrails_text(
            collection_path,
            collection_path.read_text(encoding="utf-8"),
        )
    )
    errors.extend(
        check_game_model_collection_replay_render_pose_guardrails_text(
            collection_path,
            collection_path.read_text(encoding="utf-8"),
        )
    )
    return errors


def check_physics_scene_step_body_reload_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    function_match = PHYSICS_SCENE_RUN_PHYSICS_FUNCTION_PATTERN.search(stripped)
    if not function_match:
        return errors

    open_brace = stripped.find("{", function_match.end())
    if open_brace < 0:
        return errors

    close_brace = find_matching_close_brace(stripped, open_brace)
    allowed_spans = [
        (open_brace + match.start(), open_brace + match.end())
        for match in PHYSICS_SCENE_TOPOLOGY_BODY_RELOAD_PATTERN.finditer(stripped[open_brace:close_brace])
    ]
    for match in PHYSICS_SCENE_STEP_BODY_RELOAD_PATTERN.finditer(stripped, open_brace, close_brace):
        if any(start <= match.start() < end for start, end in allowed_spans):
            continue
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "per-step model-to-body-store reload is blocked",
                (
                    "PhysicsScene::RunPhysics should keep PhysicsBodyStore authoritative during steady-state "
                    "steps; only topology/count mismatch may reload bodies from GameModel."
                ),
            )
        )
    return errors


def check_physics_scene_step_body_reload_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_SCENE_SOURCE
    return check_physics_scene_step_body_reload_guardrails_text(path, path.read_text(encoding="utf-8"))


def _function_body_bounds(stripped: str, function_pattern: re.Pattern[str]) -> tuple[int, int] | None:
    function_match = function_pattern.search(stripped)
    if not function_match:
        return None

    open_brace = stripped.find("{", function_match.end())
    if open_brace < 0:
        return None

    return open_brace, find_matching_close_brace(stripped, open_brace)


def _allowed_match_spans(function_body: str, body_offset: int, allowed_pattern: re.Pattern[str]) -> list[tuple[int, int]]:
    return [(body_offset + match.start(), body_offset + match.end()) for match in allowed_pattern.finditer(function_body)]


def check_physics_scene_command_body_refresh_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for function_name in PHYSICS_SCENE_COMMAND_BODY_REFRESH_FUNCTIONS:
        bounds = _function_body_bounds(
            stripped,
            re.compile(rf"\b(?:void|bool)\s+PhysicsScene::{function_name}\s*\("),
        )
        if not bounds:
            continue

        open_brace, close_brace = bounds
        function_body = stripped[open_brace:close_brace]
        allowed_spans = _allowed_match_spans(
            function_body,
            open_brace,
            PHYSICS_SCENE_COMMAND_TOPOLOGY_BODY_REFRESH_PATTERN,
        )
        for match in PHYSICS_SCENE_COMMAND_BODY_REFRESH_PATTERN.finditer(stripped, open_brace, close_brace):
            if any(start <= match.start() < end for start, end in allowed_spans):
                continue
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "command-side model-to-body-store refresh is blocked",
                    (
                        f"PhysicsScene::{function_name} should keep PhysicsBodyStore authoritative during "
                        "steady-state commands; only topology/count mismatch may import bodies from GameModel."
                    ),
                )
            )
    return errors


def check_game_model_collection_adapter_body_refresh_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    bounds = _function_body_bounds(stripped, GAME_MODEL_COLLECTION_ADAPTER_BODY_HANDLE_FUNCTION_PATTERN)
    if not bounds:
        return errors

    open_brace, close_brace = bounds
    function_body = stripped[open_brace:close_brace]
    allowed_spans = _allowed_match_spans(
        function_body,
        open_brace,
        GAME_MODEL_COLLECTION_ADAPTER_TOPOLOGY_BODY_REFRESH_PATTERN,
    )
    for match in GAME_MODEL_COLLECTION_ADAPTER_BODY_REFRESH_PATTERN.finditer(stripped, open_brace, close_brace):
        if any(start <= match.start() < end for start, end in allowed_spans):
            continue
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "adapter body-handle model-to-store refresh is blocked",
                (
                    "BodyHandleForModelIndex should refresh body records only on model/body count mismatch; "
                    "steady-state handle resolution must not reload PhysicsBodyStore from GameModel."
                ),
            )
        )
    return errors


def check_command_side_body_refresh_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    scene_path = repo / PHYSICS_SCENE_SOURCE
    errors.extend(check_physics_scene_command_body_refresh_guardrails_text(scene_path, scene_path.read_text(encoding="utf-8")))
    adapter_path = repo / GAME_MODEL_COLLECTION_PHYSICS_ADAPTER_SOURCE
    errors.extend(
        check_game_model_collection_adapter_body_refresh_guardrails_text(
            adapter_path,
            adapter_path.read_text(encoding="utf-8"),
        )
    )
    return errors


def check_scene_setup_model_index_physics_command_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in SCENE_SETUP_MODEL_INDEX_PHYSICS_COMMAND_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "scene setup model-index physics command is blocked",
                (
                    "Scene setup should resolve PhysicsBodyHandle once at the construction boundary and call "
                    "PhysicsEngine handle commands instead of GameModelCollection model-index wrappers."
                ),
            )
        )
    return errors


def check_scene_setup_model_index_physics_command_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in SCENE_SETUP_PHYSICS_COMMAND_SOURCES:
        path = repo / relative_path
        errors.extend(check_scene_setup_model_index_physics_command_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_editor_model_index_physics_command_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in EDITOR_MODEL_INDEX_PHYSICS_COMMAND_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "editor model-index physics command is blocked",
                (
                    "Editor commands may keep model indices for selection, but physics mutation should resolve "
                    "PhysicsBodyHandle at the editor command boundary and call PhysicsEngine handle commands."
                ),
            )
        )
    return errors


def check_editor_model_index_physics_command_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in EDITOR_PHYSICS_COMMAND_SOURCES:
        path = repo / relative_path
        errors.extend(check_editor_model_index_physics_command_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_mouse_pickup_model_index_physics_command_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in MOUSE_PICKUP_MODEL_INDEX_PHYSICS_COMMAND_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "mouse pickup model-index physics command is blocked",
                (
                    "Mouse pickup may store a model index for interaction identity, but physics impulses should "
                    "resolve PhysicsBodyHandle at the tool boundary and call PhysicsEngine handle commands."
                ),
            )
        )
    return errors


def check_mouse_pickup_model_index_physics_command_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / MOUSE_PICKUP_TOOLS_SOURCE
    return check_mouse_pickup_model_index_physics_command_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_launcher_model_index_physics_command_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in LAUNCHER_MODEL_INDEX_PHYSICS_COMMAND_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "launcher model-index physics command is blocked",
                (
                    "Launcher tools may identify hits and spawned projectiles by model index, but physics mutation "
                    "should resolve PhysicsBodyHandle at the launcher boundary and call PhysicsEngine handle commands."
                ),
            )
        )
    return errors


def check_launcher_model_index_physics_command_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / RUNTIME_TOOLS_SOURCE
    return check_launcher_model_index_physics_command_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_replay_velocity_model_state_physics_command_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in REPLAY_VELOCITY_MODEL_STATE_PHYSICS_COMMAND_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "replay velocity model-state physics command is blocked",
                (
                    "Replay velocity edit must resolve PhysicsBodyHandle at the replay boundary and call "
                    "PhysicsEngine handle commands instead of mutating GameModel velocity or collection "
                    "model-index wrappers."
                ),
            )
        )
    return errors


def check_replay_velocity_model_state_physics_command_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / REPLAY_VELOCITY_EDIT_SOURCE
    return check_replay_velocity_model_state_physics_command_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_run_frame_replay_editor_transform_wake_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in RUN_FRAME_REPLAY_EDITOR_TRANSFORM_WAKE_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "RunFrame replay editor transform wake wrapper is blocked",
                (
                    "Replay editor-transform restore may keep model index as saved event identity, but wake commands "
                    "must resolve PhysicsBodyHandle and call PhysicsEngine directly."
                ),
            )
        )
    return errors


def check_run_frame_replay_editor_transform_wake_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / RUN_FRAME_SOURCE
    return check_run_frame_replay_editor_transform_wake_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_ragdoll_model_index_physics_command_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in RAGDOLL_MODEL_INDEX_PHYSICS_COMMAND_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "ragdoll model-index physics command is blocked",
                (
                    "Ragdoll construction already resolves PhysicsBodyHandle values for joints; sleep/impulse "
                    "commands should enter PhysicsEngine by handle instead of GameModelCollection wrappers."
                ),
            )
        )
    return errors


def check_ragdoll_model_index_physics_command_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / RAGDOLL_SOURCE
    return check_ragdoll_model_index_physics_command_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_deleted_game_model_collection_physics_wrapper_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in DELETED_GAME_MODEL_COLLECTION_PHYSICS_WRAPPER_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "deleted GameModelCollection model-index physics wrapper is blocked",
                (
                    "GameModelCollection no longer exposes model-index physics command wrappers; callers should "
                    "resolve PhysicsBodyHandle at their boundary or use GameModelCollectionPhysicsAdapter while "
                    "legacy model identity is still being migrated."
                ),
            )
        )
    return errors


def check_deleted_game_model_collection_physics_wrapper_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in (GAME_MODEL_COLLECTION_HEADER, GAME_MODEL_COLLECTION_SOURCE):
        path = repo / relative_path
        errors.extend(check_deleted_game_model_collection_physics_wrapper_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_physics_hot_path_inheritance_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in HOT_PATH_INHERITANCE_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "physics hot-path inheritance is blocked",
                "Use compact store arrays, value structs, and explicit post-pass side-effect buffers unless a plan approves a stable runtime-polymorphic boundary.",
            )
        )
    return errors


def check_physics_hot_path_inheritance_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in PHYSICS_HOT_PATH_INHERITANCE_SOURCES:
        path = repo / relative_path
        errors.extend(check_physics_hot_path_inheritance_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_physics_model_access_inheritance_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_MODEL_ACCESS_INHERITANCE_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "PhysicsModelAccess inheritance is blocked",
                "Use a local PhysicsModelAccess facade value instead of deriving model owners from physics interfaces.",
            )
        )
    return errors


def check_physics_model_access_inheritance_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / Path("SkullbonezSource")).rglob("*")):
        if path.suffix not in SOURCE_BEARING_SUFFIXES:
            continue
        errors.extend(check_physics_model_access_inheritance_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def normalize_inheritance_bases(bases: str) -> str:
    return re.sub(r"\s+", " ", bases).strip()


def check_approved_inheritance_guardrails_text(
    path: Path,
    text: str,
    relative_path: Path | None = None,
) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    allowed_path = relative_path or path
    errors: list[BoundaryError] = []
    for match in INHERITANCE_DECLARATION_PATTERN.finditer(stripped):
        name = match.group("name").split("::")[-1]
        bases = normalize_inheritance_bases(match.group("bases"))
        if APPROVED_INHERITANCE_DECLARATIONS.get((allowed_path, name)) == bases:
            continue
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "unapproved inheritance is blocked",
                "Use composition/value data, or add an explicit approved stable-boundary row with owner, reason, call frequency, and validation evidence.",
            )
        )
    return errors


def check_approved_inheritance_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / Path("SkullbonezSource")).rglob("*")):
        if path.suffix not in SOURCE_BEARING_SUFFIXES:
            continue
        relative_path = path.relative_to(repo)
        errors.extend(
            check_approved_inheritance_guardrails_text(
                path,
                path.read_text(encoding="utf-8"),
                relative_path,
            )
        )
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


def check_named_physics_models_compat_access_guardrails_text(
    path: Path,
    text: str,
    relative_path: Path | None = None,
    allowlist: Counter[tuple[Path, str]] | None = None,
) -> list[BoundaryError]:
    key_path = relative_path or path
    allowed = PHYSICS_MODELS_COMPAT_ACCESS_ALLOWLIST if allowlist is None else allowlist
    stripped = strip_cpp_comments(text)
    seen: Counter[tuple[Path, str]] = Counter()
    errors: list[BoundaryError] = []

    for line_no, line in enumerate(stripped.splitlines(), start=1):
        if not PHYSICS_MODELS_COMPAT_ACCESS_PATTERN.search(line):
            continue

        normalized = normalize_boundary_line(line)
        key = ( key_path, normalized )
        seen[key] += 1
        if seen[key] > allowed.get(key, 0):
            errors.append(
                BoundaryError(
                    path,
                    line_no,
                    "named physics model vector compatibility access is count-guarded",
                    "Move the caller to PhysicsModelAccess, stores, or stable handles before adding another vector borrower.",
                )
            )

    return errors


def check_named_physics_models_compat_access_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / Path("SkullbonezSource")).rglob("*")):
        if path.suffix not in { ".cpp", ".h", ".hpp", ".inl" }:
            continue
        errors.extend(
            check_named_physics_models_compat_access_guardrails_text(
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


def check_irender_backend_aggregate_contract_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    class_body = extract_class_body(stripped, "IRenderBackend")
    if class_body is None:
        return []

    class_start, body_start, _body_end, body = class_body
    class_header = stripped[class_start:body_start]
    errors: list[BoundaryError] = []

    if re.search(r"\bIRenderRayTracing\b", class_header):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, class_start),
                "IRenderBackend must not inherit raytracing",
                "Keep DXR on IRenderRayTracing/GfxRayTracing so the aggregate facade does not expose raytracing commands.",
            )
        )

    for match in IRENDER_BACKEND_DIRECT_METHOD_PATTERN.finditer(body):
        method_name = match.group(1)
        if method_name == "IRenderBackend":
            continue
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, body_start + match.start(1)),
                "IRenderBackend direct methods are blocked",
                "Put new render behavior on a named capability interface instead of regrowing the temporary aggregate facade.",
            )
        )
    return errors


def check_irender_backend_aggregate_contract(repo: Path) -> list[BoundaryError]:
    path = repo / IRENDER_BACKEND_HEADER
    return check_irender_backend_aggregate_contract_text(path, path.read_text(encoding="utf-8"))


def check_runtime_render_pass_wide_backend_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in RUNTIME_RENDER_PASS_WIDE_BACKEND_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "runtime render pass wide backend access is blocked",
                "Pass IRenderCommandContext, IRenderResourceFactory, IRenderDiagnostics, or IRenderRayTracing instead of IRenderBackend.",
            )
        )
    return errors


def check_runtime_render_pass_wide_backend_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in RUNTIME_RENDER_PASS_CAPABILITY_SOURCES:
        source_path = repo / path
        if source_path.exists():
            errors.extend(check_runtime_render_pass_wide_backend_guardrails_text(path, source_path.read_text(encoding="utf-8")))
    return errors


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


def check_graph_owned_render_pass_manual_barriers_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in GRAPH_OWNED_RENDER_PASS_MANUAL_BARRIER_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "graph-owned render pass manual barriers are blocked",
                "Declare pass resource access through RenderGraph; runtime pass code must not issue DX12 barriers or backend transition helpers directly.",
            )
        )
    return errors


def check_graph_owned_render_pass_manual_barriers(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in GRAPH_OWNED_RENDER_PASS_MANUAL_BARRIER_SOURCES:
        path = repo / relative_path
        errors.extend(check_graph_owned_render_pass_manual_barriers_text(path, path.read_text(encoding="utf-8")))
    return errors


def find_matching_close_paren(text: str, open_paren_offset: int) -> int:
    depth = 0
    quote: str | None = None
    escaped = False
    for offset in range(open_paren_offset, len(text)):
        char = text[offset]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in ('"', "'"):
            quote = char
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return offset
    return len(text)


def split_top_level_arguments(argument_text: str) -> list[str]:
    arguments: list[str] = []
    start = 0
    paren_depth = 0
    bracket_depth = 0
    brace_depth = 0
    quote: str | None = None
    escaped = False
    for offset, char in enumerate(argument_text):
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in ('"', "'"):
            quote = char
        elif char == "(":
            paren_depth += 1
        elif char == ")":
            paren_depth = max(0, paren_depth - 1)
        elif char == "[":
            bracket_depth += 1
        elif char == "]":
            bracket_depth = max(0, bracket_depth - 1)
        elif char == "{":
            brace_depth += 1
        elif char == "}":
            brace_depth = max(0, brace_depth - 1)
        elif char == "," and paren_depth == 0 and bracket_depth == 0 and brace_depth == 0:
            arguments.append(argument_text[start:offset].strip())
            start = offset + 1
    tail = argument_text[start:].strip()
    if tail:
        arguments.append(tail)
    return arguments


def check_render_graph_unknown_access_text(
    path: Path,
    text: str,
    relative_path: Path | None = None,
    allowlist: Counter[tuple[Path, str]] | None = None,
) -> list[BoundaryError]:
    key_path = relative_path or path
    allowed = RENDER_GRAPH_UNKNOWN_ACCESS_ALLOWLIST if allowlist is None else allowlist
    stripped = strip_cpp_comments(text)
    seen: Counter[tuple[Path, str]] = Counter()
    errors: list[BoundaryError] = []

    for match in RENDER_GRAPH_ADD_EXTERNAL_RESOURCE_CALL_PATTERN.finditer(stripped):
        open_paren_offset = stripped.find("(", match.start(), match.end())
        close_paren_offset = find_matching_close_paren(stripped, open_paren_offset)
        arguments = split_top_level_arguments(stripped[open_paren_offset + 1 : close_paren_offset])
        if len(arguments) < 2 or not RENDER_GRAPH_UNKNOWN_ACCESS_VALUE_PATTERN.search(arguments[1]):
            continue
        resource_expr = arguments[0].strip()
        resource_name = (
            resource_expr[1:-1]
            if resource_expr.startswith('"') and resource_expr.endswith('"')
            else "<dynamic-resource-name>"
        )
        key = ( key_path, resource_name )
        seen[key] += 1
        if seen[key] > allowed[key]:
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "render graph Unknown resource access is count-guarded",
                    "Give migrated graph resources a concrete access state, or add an explicit handoff allowlist entry with the owning plan.",
                )
            )
    return errors


def check_render_graph_unknown_access(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in RENDER_GRAPH_UNKNOWN_ACCESS_SOURCES:
        path = repo / relative_path
        errors.extend(
            check_render_graph_unknown_access_text(
                path,
                path.read_text(encoding="utf-8"),
                relative_path=relative_path,
            )
        )
    return errors


def check_global_service_access_guardrails_text(
    path: Path,
    text: str,
    relative_path: Path | None = None,
    allowlist: Counter[tuple[Path, str]] | None = None,
) -> list[BoundaryError]:
    key_path = relative_path or path
    allowed = GLOBAL_SERVICE_ACCESS_ALLOWLIST if allowlist is None else allowlist
    stripped = strip_cpp_comments_and_string_literals(text)
    matches: list[tuple[int, str, str, str]] = []
    seen: Counter[tuple[Path, str]] = Counter()
    errors: list[BoundaryError] = []

    for label, pattern, message, detail in GLOBAL_SERVICE_ACCESS_PATTERNS:
        for match in pattern.finditer(stripped):
            matches.append(( match.start(), label, message, detail ))

    for match in GENERIC_INSTANCE_ACCESS_PATTERN.finditer(stripped):
        class_name = match.group("class_name")
        if class_name in NAMED_GLOBAL_SERVICE_INSTANCE_CLASSES:
            continue
        matches.append(
            (
                match.start(),
                f"{class_name}::Instance()",
                "generic singleton access is count-guarded",
                "Class::Instance() access must be classified as bootstrap/bridge debt or replaced by an explicit service context.",
            )
        )

    for match in PROCESS_GLOBAL_POINTER_PATTERN.finditer(stripped):
        matches.append(
            (
                match.start(),
                "pInstance",
                "process singleton pointer access is count-guarded",
                "Do not add pInstance singleton storage without explicit lifecycle/bridge classification.",
            )
        )

    for match in MUTABLE_PROCESS_GLOBAL_PATTERN.finditer(stripped):
        matches.append(
            (
                match.start(),
                "g_*",
                "mutable process global access is count-guarded",
                "Do not add mutable g_ process globals outside a named callback bridge or bootstrap compatibility path.",
            )
        )

    for offset, label, message, detail in sorted(matches):
        key = ( key_path, label )
        seen[key] += 1
        if label in GLOBAL_RENDERER_SERVICE_LABELS and key_path not in GLOBAL_RENDERER_SERVICE_ACCESS_CLASSIFICATIONS:
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, offset),
                    "global renderer service access is outside approved compatibility files",
                    "Borrow a renderer capability/context, or first classify this file as explicit renderer-service debt in the Carmack global/backend plans.",
                )
            )
            continue
        if seen[key] > allowed.get(key, 0):
            errors.append(BoundaryError(path, line_for_offset(stripped, offset), message, detail))

    return errors


def check_global_service_access_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / Path("SkullbonezSource")).rglob("*")):
        if path.suffix not in { ".cpp", ".h", ".hpp", ".inl" }:
            continue
        errors.extend(
            check_global_service_access_guardrails_text(
                path,
                path.read_text(encoding="utf-8"),
                path.relative_to(repo),
            )
        )
    return errors


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
        RenderBackendView backend;
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

    allowed_irender_backend_aggregate = """
    class IRenderBackend : public IRenderDeviceLifecycle,
                           public IRenderResourceFactory,
                           public IRenderCommandContext,
                           public IRenderDiagnostics,
                           public IRenderCaptureBackend
    {
      public:
        ~IRenderBackend() override = default;
    };
    """
    if check_irender_backend_aggregate_contract_text(
        Path("synthetic/IRenderBackend.h"),
        allowed_irender_backend_aggregate,
    ):
        failures.append("allowed IRenderBackend aggregate synthetic surface failed")

    old_irender_backend_raytracing_inheritance = """
    class IRenderBackend : public IRenderDeviceLifecycle,
                           public IRenderRayTracing
    {
      public:
        ~IRenderBackend() override = default;
    };
    """
    if not any(
        error.message == "IRenderBackend must not inherit raytracing"
        for error in check_irender_backend_aggregate_contract_text(
            Path("synthetic/IRenderBackend.h"),
            old_irender_backend_raytracing_inheritance,
        )
    ):
        failures.append("IRenderBackend raytracing inheritance synthetic surface was not rejected")

    old_irender_backend_direct_method = """
    class IRenderBackend : public IRenderDeviceLifecycle
    {
      public:
        ~IRenderBackend() override = default;
        virtual void Clear( bool color, bool depth ) = 0;
        RenderCapabilities GetCapabilities() const;
    };
    """
    if not any(
        error.message == "IRenderBackend direct methods are blocked"
        for error in check_irender_backend_aggregate_contract_text(
            Path("synthetic/IRenderBackend.h"),
            old_irender_backend_direct_method,
        )
    ):
        failures.append("IRenderBackend direct method synthetic surface was not rejected")

    allowed_runtime_pass_narrow_capabilities = """
    #include "../Rendering/IRenderCommandContext.h"
    #include "../Rendering/IRenderResourceFactory.h"
    void Draw( Rendering::IRenderCommandContext& commands, Rendering::IRenderResourceFactory& resources )
    {
        commands.Clear( true, true );
        resources.CreateDynamicVB( attribs, 2, 6 );
    }
    """
    if check_runtime_render_pass_wide_backend_guardrails_text(
        Path("synthetic/RunPasses.cpp"),
        allowed_runtime_pass_narrow_capabilities,
    ):
        failures.append("allowed runtime render pass narrow capability synthetic surface failed")

    old_runtime_pass_wide_backend_include = """
    #include "../Rendering/IRenderBackend.h"
    void Draw( IRenderBackend& backend )
    {
        backend.Clear( true, true );
    }
    """
    if not any(
        error.message == "runtime render pass wide backend access is blocked"
        for error in check_runtime_render_pass_wide_backend_guardrails_text(
            Path("synthetic/RunPasses.cpp"),
            old_runtime_pass_wide_backend_include,
        )
    ):
        failures.append("runtime render pass wide backend include synthetic surface was not rejected")

    allowed_graph_owned_pass_scheduling = """
    void RuntimeRenderer::RenderFrame()
    {
        ExecuteShadowThroughRenderGraph( frame, activeShadowConfig );
        ExecuteSceneTargetBeginThroughRenderGraph( frame );
        ExecuteSkyboxThroughRenderGraph( frame );
        ExecuteReflectionThroughRenderGraph( frame, activeCinematic, objectShadowFrame );
        ExecuteObjectThroughRenderGraph( frame, ObjectPassMode::Opaque, useCinematicTarget );
        ExecuteTerrainThroughRenderGraph( frame, useCinematicTarget );
        ExecuteWaterThroughRenderGraph( frame, reflection, useCinematicTarget );
        ExecuteTornadoVisualThroughRenderGraph( frame, useCinematicTarget );
        ExecuteReplayGhostsThroughRenderGraph( frame, useCinematicTarget );
        ExecuteDebugOverlayThroughRenderGraph( frame, useCinematicTarget );
        ExecuteCinematicPostThroughRenderGraph( frame );
        ExecuteUiTextThroughRenderGraph( secondsPerFrame );
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
        m_shadowPass.Render( frame );
        m_sceneTargetPass.Begin( frame, m_skyPass );
        m_skyPass.Render( frame, frame.baseView, SkyPassMode::CubemapOnly );
        m_reflectionPass.Render( frame, m_skyPass );
        m_objectPass.Render( frame );
        m_terrainPass.Render( frame );
        m_waterPass.Render( frame );
        m_tornadoVisualPass.Render( frame );
        host.RenderReplayPredictionGhosts( frame, activeCinematic, objectShadowFrame );
        m_debugOverlayPass.Render( frame );
        m_volumetricPass.Render( frame );
        m_tonemapPass.Render( frame, false, true );
        m_uiTextPass.Render( secondsPerFrame );
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

    allowed_graph_owned_pass_graph_access = """
    void RuntimeRenderer::RenderFrame()
    {
        graph.AddExternalResource( "SwapchainBackbuffer", Rendering::RenderGraphResourceAccess::RenderTarget );
        graph.AddPass( "UiTextPass", Rendering::RenderGraphQueueType::Graphics );
    }
    """
    if check_graph_owned_render_pass_manual_barriers_text(
        Path("synthetic/RunRender.cpp"),
        allowed_graph_owned_pass_graph_access,
    ):
        failures.append("allowed graph-owned pass graph-access synthetic surface failed")

    old_manual_graph_owned_pass_barrier = """
    void RuntimeRenderer::RenderFrame()
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        commandList->ResourceBarrier( 1, &barrier );
    }
    """
    if not any(
        error.message == "graph-owned render pass manual barriers are blocked"
        for error in check_graph_owned_render_pass_manual_barriers_text(
            Path("synthetic/RunRender.cpp"),
            old_manual_graph_owned_pass_barrier,
        )
    ):
        failures.append("manual graph-owned pass barrier synthetic surface was not rejected")

    old_resource_barrier_call_only = "void Render() { commandList->ResourceBarrier( 1, &barrier ); }"
    if not any(
        error.message == "graph-owned render pass manual barriers are blocked"
        for error in check_graph_owned_render_pass_manual_barriers_text(
            Path("synthetic/RunRender.cpp"),
            old_resource_barrier_call_only,
        )
    ):
        failures.append("ResourceBarrier-only synthetic surface was not rejected")

    commented_manual_barrier = """
    void RuntimeRenderer::RenderFrame()
    {
        // commandList->ResourceBarrier( 1, &barrier );
        const char* label = "D3D12_RESOURCE_BARRIER and ExecuteGraphTransition are documentation text";
    }
    """
    if check_graph_owned_render_pass_manual_barriers_text(
        Path("synthetic/RunRender.cpp"),
        commented_manual_barrier,
    ):
        failures.append("comment/string manual barrier synthetic surface was falsely rejected")

    old_graph_transition_helper_in_runtime_pass = """
    void SceneTargetPass::Begin()
    {
        backend.ExecuteGraphTransition( "SceneTarget", resource, before, after );
    }
    """
    if not any(
        error.message == "graph-owned render pass manual barriers are blocked"
        for error in check_graph_owned_render_pass_manual_barriers_text(
            Path("synthetic/RunPasses.cpp"),
            old_graph_transition_helper_in_runtime_pass,
        )
    ):
        failures.append("runtime pass backend transition helper synthetic surface was not rejected")

    old_graph_uav_helper_in_ui_text_pass = """
    void UiTextPass::Render()
    {
        backend.ExecuteGraphUavBarrier( "UiTextWriteOrder", "SwapchainBackbuffer", resource );
    }
    """
    if not any(
        error.message == "graph-owned render pass manual barriers are blocked"
        for error in check_graph_owned_render_pass_manual_barriers_text(
            Path("synthetic/RunUiTextPass.cpp"),
            old_graph_uav_helper_in_ui_text_pass,
        )
    ):
        failures.append("UiTextPass graph UAV helper synthetic surface was not rejected")

    allowed_unknown_graph_access_path = Path("SkullbonezSource/Runtime/RunRender.cpp")
    allowed_unknown_graph_access = """
    void BuildMigratedGraph()
    {
        graph.AddExternalResource( "CinematicSceneDepth", Rendering::RenderGraphResourceAccess::Unknown );
    }
    """
    synthetic_unknown_access_allowlist: Counter[tuple[Path, str]] = Counter(
        { ( allowed_unknown_graph_access_path, "CinematicSceneDepth" ): 1 }
    )
    if check_render_graph_unknown_access_text(
        allowed_unknown_graph_access_path,
        allowed_unknown_graph_access,
        allowlist=synthetic_unknown_access_allowlist,
    ):
        failures.append("count-allowed render graph Unknown access synthetic surface was rejected")

    new_unknown_graph_access = """
    void BuildMigratedGraph()
    {
        graph.AddExternalResource( "NewSceneDepth", Rendering::RenderGraphResourceAccess::Unknown );
    }
    """
    if not any(
        error.message == "render graph Unknown resource access is count-guarded"
        for error in check_render_graph_unknown_access_text(
            Path("synthetic/RunRender.cpp"),
            new_unknown_graph_access,
        )
    ):
        failures.append("new render graph Unknown access synthetic surface was not rejected")

    new_unknown_graph_access_with_native = """
    void BuildMigratedGraph()
    {
        graph.AddExternalResource(
            "NewSceneDepth",
            Rendering::RenderGraphResourceAccess::Unknown,
            nativeDepth );
    }
    """
    if not any(
        error.message == "render graph Unknown resource access is count-guarded"
        for error in check_render_graph_unknown_access_text(
            Path("synthetic/RunRender.cpp"),
            new_unknown_graph_access_with_native,
        )
    ):
        failures.append("three-argument render graph Unknown access synthetic surface was not rejected")

    dynamic_unknown_graph_access = """
    void BuildMigratedGraph()
    {
        graph.AddExternalResource(
            MakeResourceName( sceneName, passName ),
            SkullbonezCore::Rendering::RenderGraphResourceAccess::Unknown );
    }
    """
    if not any(
        error.message == "render graph Unknown resource access is count-guarded"
        for error in check_render_graph_unknown_access_text(
            Path("synthetic/RunRender.cpp"),
            dynamic_unknown_graph_access,
        )
    ):
        failures.append("dynamic-name render graph Unknown access synthetic surface was not rejected")

    allowed_global_service_path = Path("SkullbonezSource/Runtime/Run.cpp")
    allowed_global_service_access = "void BootstrapRenderer() { Gfx().Present(); }"
    synthetic_global_service_allowlist: Counter[tuple[Path, str]] = Counter(
        { ( allowed_global_service_path, "Gfx()" ): 1 }
    )
    if check_global_service_access_guardrails_text(
        allowed_global_service_path,
        allowed_global_service_access,
        allowlist=synthetic_global_service_allowlist,
    ):
        failures.append("count-allowed global service synthetic surface was rejected")

    unclassified_global_renderer_path = Path("SkullbonezSource/Runtime/NewRenderPath.cpp")
    synthetic_unclassified_renderer_allowlist: Counter[tuple[Path, str]] = Counter(
        { ( unclassified_global_renderer_path, "Gfx()" ): 1 }
    )
    if not any(
        error.message == "global renderer service access is outside approved compatibility files"
        for error in check_global_service_access_guardrails_text(
            unclassified_global_renderer_path,
            "void NewRenderPath() { Gfx().Present(); }",
            allowlist=synthetic_unclassified_renderer_allowlist,
        )
    ):
        failures.append("unclassified count-allowed Gfx synthetic surface was not rejected")

    unclassified_global_dxr_path = Path("SkullbonezSource/Runtime/NewDxrPath.cpp")
    synthetic_unclassified_dxr_allowlist: Counter[tuple[Path, str]] = Counter(
        { ( unclassified_global_dxr_path, "GfxRayTracing()" ): 1 }
    )
    if not any(
        error.message == "global renderer service access is outside approved compatibility files"
        for error in check_global_service_access_guardrails_text(
            unclassified_global_dxr_path,
            "void NewDxrPath() { GfxRayTracing().GetReflectionUAVTexture(); }",
            allowlist=synthetic_unclassified_dxr_allowlist,
        )
    ):
        failures.append("unclassified count-allowed GfxRayTracing synthetic surface was not rejected")

    grown_global_service_access = """
    void BootstrapRenderer() { Gfx().Present(); }
    void NewRenderHelper() { Gfx().Clear( true, true ); }
    """
    if not any(
        error.message == "global renderer service access is count-guarded"
        for error in check_global_service_access_guardrails_text(
            allowed_global_service_path,
            grown_global_service_access,
            allowlist=synthetic_global_service_allowlist,
        )
    ):
        failures.append("grown global renderer service synthetic surface was not rejected")

    new_window_singleton_access = "void DeepInputPath() { Window::Instance()->ShowCursor( true ); }"
    if not any(
        error.message == "global window service access is count-guarded"
        for error in check_global_service_access_guardrails_text(
            Path("SkullbonezSource/Runtime/InputNew.cpp"),
            new_window_singleton_access,
            relative_path=Path("SkullbonezSource/Runtime/InputNew.cpp"),
        )
    ):
        failures.append("new window singleton synthetic surface was not rejected")

    new_cfg_access = "void DeepConfigPath() { int threads = Cfg().workerThreads; }"
    if not any(
        error.message == "global config access is count-guarded"
        for error in check_global_service_access_guardrails_text(
            Path("SkullbonezSource/Runtime/NewConfigPath.cpp"),
            new_cfg_access,
            relative_path=Path("SkullbonezSource/Runtime/NewConfigPath.cpp"),
        )
    ):
        failures.append("new Cfg synthetic surface was not rejected")

    new_generic_instance_access = "void DeepConfigPath() { EngineConfig::Instance().workerThreads = 0; }"
    if not any(
        error.message == "generic singleton access is count-guarded"
        for error in check_global_service_access_guardrails_text(
            Path("SkullbonezSource/Runtime/NewConfigPath.cpp"),
            new_generic_instance_access,
            relative_path=Path("SkullbonezSource/Runtime/NewConfigPath.cpp"),
        )
    ):
        failures.append("new generic Instance synthetic surface was not rejected")

    new_process_singleton_pointer = "class Service { static Service* pInstance; };"
    if not any(
        error.message == "process singleton pointer access is count-guarded"
        for error in check_global_service_access_guardrails_text(
            Path("SkullbonezSource/Runtime/NewService.h"),
            new_process_singleton_pointer,
            relative_path=Path("SkullbonezSource/Runtime/NewService.h"),
        )
    ):
        failures.append("new pInstance synthetic surface was not rejected")

    new_mutable_process_global = "int g_newInputBridge = 0;"
    if not any(
        error.message == "mutable process global access is count-guarded"
        for error in check_global_service_access_guardrails_text(
            Path("SkullbonezSource/Runtime/NewInputBridge.cpp"),
            new_mutable_process_global,
            relative_path=Path("SkullbonezSource/Runtime/NewInputBridge.cpp"),
        )
    ):
        failures.append("new g_ global synthetic surface was not rejected")

    diagnostic_text_only_global = r'''
    void LogMigrationHint()
    {
        printf( "Do not add Gfx(), Cfg(), Window::Instance(), pInstance, or g_newService here." );
        // Gfx().Clear( true, true );
        /* Cfg().workerThreads = 0; */
    }
    '''
    if check_global_service_access_guardrails_text(
        Path("SkullbonezSource/Runtime/DiagnosticStrings.cpp"),
        diagnostic_text_only_global,
        relative_path=Path("SkullbonezSource/Runtime/DiagnosticStrings.cpp"),
    ):
        failures.append("diagnostic string/comment global service synthetic surface was rejected")

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

    allowed_public_physics_facade = """
    struct PhysicsBodyCreateDesc;
    struct PhysicsBodyCollectionView;
    class PhysicsEngine
    {
      public:
        PhysicsBodyHandle CreateBody( const PhysicsBodyCreateDesc& desc );
        PhysicsBodyCollectionView Bodies() const;
        void Step( PhysicsModelAccess& modelAccess, float deltaSeconds );
    };
    """
    if check_public_physics_facade_game_object_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsEngine.h"),
        allowed_public_physics_facade,
    ):
        failures.append("allowed public physics facade synthetic surface was rejected")

    old_public_physics_collection_api = """
    class PhysicsEngine
    {
      public:
        void Step( GameObjects::GameModelCollection& collection, float deltaSeconds );
    };
    """
    if not any(
        error.message == "public physics facade game-object dependency is blocked"
        for error in check_public_physics_facade_game_object_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsEngine.h"),
            old_public_physics_collection_api,
        )
    ):
        failures.append("public physics GameModelCollection facade synthetic surface was not rejected")

    old_public_physics_collection_pointer_api = """
    class PhysicsEngine
    {
      public:
        void AttachWorld( GameModelCollection* collection );
    };
    """
    if not any(
        error.message == "public physics facade game-object dependency is blocked"
        for error in check_public_physics_facade_game_object_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsEngine.h"),
            old_public_physics_collection_pointer_api,
        )
    ):
        failures.append("public physics GameModelCollection pointer synthetic surface was not rejected")

    old_public_physics_model_ref_api = """
    namespace GameObjects
    {
        class GameModel;
    }
    class PhysicsEngine
    {
      public:
        void RefreshBody( GameObjects::GameModel& model );
    };
    """
    if not any(
        error.message == "public physics facade game-object dependency is blocked"
        for error in check_public_physics_facade_game_object_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsEngine.h"),
            old_public_physics_model_ref_api,
        )
    ):
        failures.append("public physics raw GameModel reference synthetic surface was not rejected")

    old_public_physics_vector_api = """
    class PhysicsEngine
    {
      public:
        void RefreshBodies( std::vector<GameObjects::GameModel>& models );
    };
    """
    if not any(
        error.message == "public physics facade game-object dependency is blocked"
        for error in check_public_physics_facade_game_object_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsEngine.h"),
            old_public_physics_vector_api,
        )
    ):
        failures.append("public physics raw GameModel vector synthetic surface was not rejected")

    public_facade_comment_only_text = """
    // GameModelCollection and std::vector<GameModel>& are migration notes only.
    /*
       A PhysicsEngine facade must not accept GameModel here.
    */
    class PhysicsEngine
    {
      public:
        PhysicsBodyCollectionView Bodies() const;
    };
    """
    if check_public_physics_facade_game_object_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsEngine.h"),
        public_facade_comment_only_text,
    ):
        failures.append("public physics facade comment-only synthetic surface was rejected")

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

    deleted_migration_artifact_text = """
    struct GameModelRuntimePhysicsTuning {};
    int legacyModelIndex = 0;
    RuntimeConfigSnapshot snapshot;
    class GameModelCollection : public Rendering::IRenderSceneView {};
    PhysicsModelMutableRange mutableRange;
    PhysicsModelConstRange constRange;
    auto* mutableModels = modelAccess.MutableModelData();
    auto* constModels = modelAccess.ModelData();
    auto rawRange = modelAccess.Models();
    auto borrowedRange = BorrowMutableModels( modelAccess );
    auto mirror = sideEffects.bodyMirrorWritebacks;
    QueueBodyMirrorWriteback( index );
    PhysicsBodyWritebackSink* writebackSink = nullptr;
    PhysicsBodyEventSink* eventSink = nullptr;
    std::unique_ptr<Rendering::IShader> AssetSystem::CreateShader( const char* logicalNameOrBaseName ) const;
    """
    if not any(
        error.message.startswith("deleted migration artifact is blocked:")
        for error in check_deleted_migration_artifact_guardrails_text(
            Path("SkullbonezSource/Runtime/SyntheticDeletedArtifacts.cpp"),
            deleted_migration_artifact_text,
        )
    ):
        failures.append("deleted migration artifact synthetic surface was not rejected")

    standalone_body_mirror_text = """
    class PhysicsStandaloneWorld
    {
        std::vector<PhysicsBodyView> m_bodies;
        std::vector<uint32_t> m_generations;
        std::vector<uint8_t> m_alive;
        std::vector<uint32_t> m_freeIndices;
    };
    """
    if not any(
        error.message == "deleted migration artifact is blocked: PhysicsStandaloneWorld body mirror arrays"
        for error in check_deleted_migration_artifact_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsApi.h"),
            standalone_body_mirror_text,
        )
    ):
        failures.append("standalone body mirror synthetic surface was not rejected")

    deleted_render_scene_view_text = """
    class GameModelCollection : public Rendering::IRenderSceneView {};
    """
    if not any(
        error.message == "deleted migration artifact is blocked: IRenderSceneView"
        for error in check_deleted_migration_artifact_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.h"),
            deleted_render_scene_view_text,
        )
    ):
        failures.append("deleted IRenderSceneView synthetic surface was not rejected")

    commented_deleted_migration_artifact_text = """
    // GameModelRuntimePhysicsTuning, legacyModelIndex, RuntimeConfigSnapshot, and IRenderSceneView are migration notes only.
    // PhysicsModelMutableRange, MutableModelData(), and modelAccess.Models() are notes only.
    // PhysicsBodyWritebackSink, QueueBodyMirrorWriteback, bodyMirrorWritebacks, and PhysicsBodyEventSink are deleted migration notes only.
    // PhysicsStandaloneWorld used to store std::vector<PhysicsBodyView> m_bodies plus m_alive/m_generations/m_freeIndices.
    /*
       AssetSystem::CreateShader( const char* name ) is mentioned in the plan but must not be code.
       BorrowMutableModels(modelAccess) appears in the audit notes, not compiled source.
    */
    void UseExplicitStoresAndFactories();
    """
    if check_deleted_migration_artifact_guardrails_text(
        Path("SkullbonezSource/Runtime/SyntheticDeletedArtifacts.cpp"),
        commented_deleted_migration_artifact_text,
    ):
        failures.append("comment-only deleted migration artifact synthetic text was rejected")

    allowed_persistent_solver_context = """
    struct PersistentContactSolverContext
    {
        PersistentContactSolverSideEffects& sideEffects;
        int bodyStoreCount = 0;
        int pipelineRecordCapacity = 0;
    };
    """
    if check_persistent_solver_context_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.h"),
        allowed_persistent_solver_context,
    ):
        failures.append("allowed persistent solver sink context synthetic surface was rejected")

    old_persistent_solver_context = """
    struct PersistentContactSolverContext
    {
        PhysicsModelAccess& modelAccess;
        PhysicsBodyEventSink& bodyEvents;
        PhysicsWorld& world;
    };
    """
    if not any(
        error.message == "persistent contact solver callback boundary is blocked"
        for error in check_persistent_solver_context_model_access_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.h"),
            old_persistent_solver_context,
        )
    ):
        failures.append("old persistent solver broad model access synthetic surface was not rejected")

    old_persistent_solver_body_stream_context = """
    struct PersistentContactSolverContext
    {
        const GameObjects::GameModelBodyStream& bodyStream;
        PersistentContactSolverSideEffects& sideEffects;
    };
    """
    if not any(
        error.message == "persistent contact solver model stream boundary is blocked"
        for error in check_persistent_solver_context_model_access_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.h"),
            old_persistent_solver_body_stream_context,
        )
    ):
        failures.append("old persistent solver body stream synthetic surface was not rejected")

    commented_persistent_solver_context = """
    struct PersistentContactSolverContext
    {
        // PhysicsModelAccess& modelAccess; and PhysicsBodyEventSink& bodyEvents are deleted migration notes only.
        // const GameObjects::GameModelBodyStream& bodyStream is a deleted migration note only.
        PersistentContactSolverSideEffects& sideEffects;
    };
    """
    if check_persistent_solver_context_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.h"),
        commented_persistent_solver_context,
    ):
        failures.append("comment-only persistent solver broad model access synthetic text was rejected")

    old_solver_writeback_text = """
    void PhysicsWorld::RunSolverPhysics( PhysicsModelAccess& modelAccess )
    {
        modelAccess.WriteBackPhysicsBody( bodyStore, x );
    }
    """
    if not any(
        error.message == "physics solver hot path per-body model writeback is blocked"
        for error in check_physics_world_solver_body_writeback_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_solver_writeback_text,
        )
    ):
        failures.append("old solver per-body model writeback synthetic surface was not rejected")

    allowed_step_boundary_writeback_text = """
    void PhysicsWorld::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        modelAccess.WriteBackPhysicsBodies( bodyStore );
    }
    void PhysicsWorld::ApplyTornadoField( PhysicsModelAccess& modelAccess )
    {
        modelAccess.WriteBackPhysicsBody( bodyStore, fixedIndex );
    }
    """
    if check_physics_world_solver_body_writeback_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        allowed_step_boundary_writeback_text,
    ):
        failures.append("allowed non-solver physics writeback synthetic surface was rejected")

    commented_solver_writeback_text = """
    void PhysicsWorld::RunSolverPhysics( PhysicsModelAccess& modelAccess )
    {
        // modelAccess.WriteBackPhysicsBody(bodyStore, x) is a deleted hot-path note.
        KeepBodyStoreAuthoritative();
    }
    """
    if check_physics_world_solver_body_writeback_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_solver_writeback_text,
    ):
        failures.append("comment-only solver writeback synthetic text was rejected")

    old_solver_body_stream_text = """
    void PhysicsWorld::RunSolverPhysics( PhysicsModelAccess& modelAccess )
    {
        const GameModelBodyStream bodyStream = modelAccess.GetBodyStream();
    }
    void PhysicsWorld::ApplyTornadoField( PhysicsModelAccess& modelAccess )
    {
        auto stream = modelAccess.GetBodyStream();
    }
    """
    if not any(
        error.message == "physics solver hot path model body stream is blocked"
        for error in check_physics_world_solver_model_stream_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_solver_body_stream_text,
        )
    ):
        failures.append("old solver model body stream synthetic surface was not rejected")

    allowed_explicit_wake_body_stream_text = """
    void PhysicsWorld::WakeModel( PhysicsModelAccess& modelAccess, int index )
    {
        const GameModelBodyStream bodyStream = modelAccess.GetBodyStream();
        WakeModel( modelAccess, bodyStream, nullptr, nullptr, nullptr, index );
    }
    """
    if check_physics_world_solver_model_stream_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        allowed_explicit_wake_body_stream_text,
    ):
        failures.append("explicit non-solver wake body stream synthetic surface was rejected")

    commented_solver_body_stream_text = """
    void PhysicsWorld::RunSolverPhysics( PhysicsModelAccess& modelAccess )
    {
        // const GameModelBodyStream bodyStream = modelAccess.GetBodyStream(); is a deleted hot-path note.
        UseStoreRecords();
    }
    """
    if check_physics_world_solver_model_stream_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_solver_body_stream_text,
    ):
        failures.append("comment-only solver body stream synthetic text was rejected")

    old_world_contact_highlight_tick = """
    void PhysicsWorld::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        modelAccess.TickContactHighlights( modelCount, dt );
    }
    """
    if not any(
        error.message == "physics world model contact-highlight tick is blocked"
        for error in check_physics_world_contact_highlight_tick_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_world_contact_highlight_tick,
        )
    ):
        failures.append("old PhysicsWorld contact-highlight tick synthetic surface was not rejected")

    allowed_scene_contact_highlight_tick = """
    void PhysicsScene::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        modelAccess.TickContactHighlights( modelCount, dt );
        m_world.RunPhysics( modelAccess, bodyStore, colliderStore, dt, config, forces, workerPool );
    }
    """
    if check_physics_world_contact_highlight_tick_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_scene_contact_highlight_tick,
    ):
        failures.append("PhysicsScene contact-highlight tick synthetic surface was rejected")

    commented_world_contact_highlight_tick = """
    void PhysicsWorld::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        // modelAccess.TickContactHighlights(modelCount, dt) used to live here.
        StepBodyStores();
    }
    """
    if check_physics_world_contact_highlight_tick_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_world_contact_highlight_tick,
    ):
        failures.append("comment-only PhysicsWorld contact-highlight tick synthetic text was rejected")

    old_render_instance_model_refresh = """
    void GameModelCollection::RefreshRenderInstances( RenderInstanceStore& renderInstanceStore )
    {
        renderInstanceStore.Refresh( m_gameModels );
    }
    """
    if not any(
        error.message == "render instance model-transform refresh is blocked"
        for error in check_render_instance_store_authority_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
            old_render_instance_model_refresh,
        )
    ):
        failures.append("old render instance model refresh synthetic surface was not rejected")

    allowed_render_instance_store_refresh = """
    void GameModelCollection::RefreshRenderInstances( RenderInstanceStore& renderInstanceStore,
                                                      const PhysicsBodyStore& bodyStore,
                                                      const ColliderStore& colliderStore )
    {
        renderInstanceStore.Refresh( m_gameModels, bodyStore, colliderStore );
    }
    """
    if check_render_instance_store_authority_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        allowed_render_instance_store_refresh,
    ):
        failures.append("store-owned render instance refresh synthetic surface was rejected")

    commented_render_instance_model_refresh = """
    void GameModelCollection::RefreshRenderInstances( RenderInstanceStore& renderInstanceStore )
    {
        // renderInstanceStore.Refresh( m_gameModels ) is deleted render-transform debt.
        renderInstanceStore.Refresh( m_gameModels, bodyStore, colliderStore );
    }
    """
    if check_render_instance_store_authority_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        commented_render_instance_model_refresh,
    ):
        failures.append("comment-only render instance model refresh synthetic text was rejected")

    old_diagnostics_model_record_read = """
    void PhysicsDiagnosticsSink::EmitRegressionLog( PhysicsWorld& world, PhysicsModelAccess& modelAccess )
    {
        PhysicsDiagnosticsModelRecord model;
        modelAccess.TryGetPhysicsDiagnosticsModel( i, model );
    }
    """
    if not any(
        error.message == "physics diagnostics model-state mirror read is blocked"
        for error in check_physics_diagnostics_store_authority_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp"),
            old_diagnostics_model_record_read,
        )
    ):
        failures.append("old diagnostics GameModel-sourced record synthetic surface was not rejected")

    store_owned_diagnostics_model_record_read = """
    void PhysicsDiagnosticsSink::EmitRegressionLog( PhysicsWorld& world, PhysicsModelAccess& modelAccess )
    {
        PhysicsDiagnosticsModelRecord model;
        modelAccess.TryGetPhysicsDiagnosticsModel( i, bodyStore, colliderStore, model );
    }
    """
    if check_physics_diagnostics_store_authority_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp"),
        store_owned_diagnostics_model_record_read,
    ):
        failures.append("store-owned diagnostics record synthetic surface was rejected")

    diagnostics_name_only_read = """
    void PhysicsDiagnosticsSink::EmitCollisionTime( PhysicsModelAccess& modelAccess )
    {
        const char* name = "";
        modelAccess.TryGetPhysicsDiagnosticsModelName( bodyA, name );
    }
    """
    if check_physics_diagnostics_store_authority_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp"),
        diagnostics_name_only_read,
    ):
        failures.append("diagnostics name-only synthetic surface was rejected")

    commented_diagnostics_model_record_read = """
    void SkullScope::EmitFrame( Physics::PhysicsModelAccess& modelAccess )
    {
        // modelAccess.TryGetPhysicsDiagnosticsModel( i, model ) is old mirror debt.
        UseBodyAndColliderStores();
    }
    """
    if check_physics_diagnostics_store_authority_guardrails_text(
        Path("SkullbonezSource/Core/SkullScope.cpp"),
        commented_diagnostics_model_record_read,
    ):
        failures.append("comment-only diagnostics model record synthetic text was rejected")

    old_replay_recorder_model_state_read = """
    void ReplayRecorder::CaptureFrame( const ReplayCaptureInput& input )
    {
        const GameModel* model = models.TryGetModel( i );
        body.position = model->GetPosition();
        body.mass = model->GetMass();
    }
    """
    if not any(
        error.message == "replay recorder model-state capture is blocked"
        for error in check_replay_recorder_store_authority_guardrails_text(
            Path("SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp"),
            old_replay_recorder_model_state_read,
        )
    ):
        failures.append("old replay recorder GameModel-sourced body state synthetic surface was not rejected")

    store_owned_replay_recorder_body_read = """
    bool BuildReplayPresentationBodySample( int bodyIndex,
                                            const PhysicsBodyStore& bodyStore,
                                            const ColliderStore& colliderStore )
    {
        const PhysicsBodyRecord& bodyRecord = bodyStore.Records()[bodyIndex];
        const ColliderRecord& colliderRecord = colliderStore.Records()[bodyIndex];
        body.position = bodyRecord.position;
        body.mass = bodyRecord.mass;
        body.shapeKind = ShapeKindForCollider( colliderRecord );
    }
    """
    if check_replay_recorder_store_authority_guardrails_text(
        Path("SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp"),
        store_owned_replay_recorder_body_read,
    ):
        failures.append("store-owned replay recorder body read synthetic surface was rejected")

    replay_name_only_model_read = """
    void ReplayRecorder::CaptureFrame( const ReplayCaptureInput& input )
    {
        const GameModel* model = models.TryGetModel( i );
        const char* modelName = model->GetName();
    }
    """
    if check_replay_recorder_store_authority_guardrails_text(
        Path("SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp"),
        replay_name_only_model_read,
    ):
        failures.append("replay name-only GameModel read synthetic surface was rejected")

    commented_replay_recorder_model_state_read = """
    void ReplaySolverRecorder::CaptureFrame( const ReplayCaptureInput& input )
    {
        // model->GetPosition() and model->GetMass() are deleted replay mirror debt.
        UseBodyAndColliderStores();
    }
    """
    if check_replay_recorder_store_authority_guardrails_text(
        Path("SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp"),
        commented_replay_recorder_model_state_read,
    ):
        failures.append("comment-only replay recorder model-state synthetic text was rejected")

    old_run_replay_restore_body_store_reload = """
    bool Run::ApplyReplaySolverSampleState( const ReplaySolverFrameSample& sample )
    {
        m_cGameModelCollection.TryRestoreReplayBodyState( body.modelIndex, body.id.value, body.fixed, position, orientation, linearVelocity, angularVelocity );
        (void)m_cGameModelCollection.GetPhysicsBodyStore();
    }
    """
    if not any(
        error.message == "replay restore body-store reload is blocked"
        for error in check_run_replay_restore_store_authority_guardrails_text(
            Path("SkullbonezSource/Runtime/Run.cpp"),
            old_run_replay_restore_body_store_reload,
        )
    ):
        failures.append("old replay restore full body-store reload synthetic surface was not rejected")

    store_owned_run_replay_restore = """
    bool Run::ApplyReplaySolverSampleState( const ReplaySolverFrameSample& sample )
    {
        m_cGameModelCollection.TryRestoreReplayBodyState( body.modelIndex,
                                                          body.id.value,
                                                          body.fixed,
                                                          body.position,
                                                          orientation,
                                                          body.linearVelocity,
                                                          body.angularVelocity,
                                                          body.mass,
                                                          body.inverseMass,
                                                          body.rotationalInertia,
                                                          body.inverseRotationalInertia );
    }
    """
    if check_run_replay_restore_store_authority_guardrails_text(
        Path("SkullbonezSource/Runtime/Run.cpp"),
        store_owned_run_replay_restore,
    ):
        failures.append("store-owned replay restore synthetic surface was rejected")

    old_collection_replay_restore_model_refresh = """
    bool GameModelCollection::TryRestoreReplayBodyState( int index,
                                                         uint32_t replayBodyId,
                                                         bool fixed,
                                                         const Vector3& position,
                                                         const Quaternion& orientation,
                                                         const Vector3& linearVelocity,
                                                         const Vector3& angularVelocity )
    {
        CommitEditedModelPhysicsState( index, false );
        return true;
    }
    """
    if not any(
        error.message == "replay restore model-to-store refresh is blocked"
        for error in check_game_model_collection_replay_restore_store_authority_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
            old_collection_replay_restore_model_refresh,
        )
    ):
        failures.append("old replay restore model-refresh synthetic surface was not rejected")

    commented_collection_replay_restore_model_refresh = """
    bool GameModelCollection::TryRestoreReplayBodyState( int index,
                                                         uint32_t replayBodyId,
                                                         bool fixed,
                                                         const Vector3& position,
                                                         const Quaternion& orientation,
                                                         const Vector3& linearVelocity,
                                                         const Vector3& angularVelocity )
    {
        // CommitEditedModelPhysicsState( index, false ) is deleted restore debt.
        return m_physicsEngine.RestoreReplayBodyState( index, replayBodyId, fixed, position, orientation, linearVelocity, angularVelocity, mass, inverseMass, rotationalInertia, inverseRotationalInertia );
    }
    """
    if check_game_model_collection_replay_restore_store_authority_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        commented_collection_replay_restore_model_refresh,
    ):
        failures.append("comment-only replay restore model-refresh synthetic text was rejected")

    old_collection_replay_prediction_restore_model_refresh = """
    bool GameModelCollection::TryRestoreReplayPredictionBodyState( int index,
                                                                   uint32_t replayBodyId,
                                                                   bool fixed,
                                                                   const Vector3& position,
                                                                   const Quaternion& orientation,
                                                                   const Vector3& linearVelocity,
                                                                   const Vector3& angularVelocity )
    {
        CommitEditedModelPhysicsState( index, false );
        return true;
    }
    """
    if not any(
        error.message == "replay restore model-to-store refresh is blocked"
        for error in check_game_model_collection_replay_restore_store_authority_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
            old_collection_replay_prediction_restore_model_refresh,
        )
    ):
        failures.append("old replay prediction restore model-refresh synthetic surface was not rejected")

    allowed_collection_replay_prediction_restore = """
    bool GameModelCollection::TryRestoreReplayPredictionBodyState( int index,
                                                                   uint32_t replayBodyId,
                                                                   bool fixed,
                                                                   const Vector3& position,
                                                                   const Quaternion& orientation,
                                                                   const Vector3& linearVelocity,
                                                                   const Vector3& angularVelocity )
    {
        return m_physicsEngine.RestoreReplayBodyState( index, replayBodyId, fixed, position, orientation, linearVelocity, angularVelocity, mass, inverseMass, rotationalInertia, inverseRotationalInertia );
    }
    """
    if check_game_model_collection_replay_restore_store_authority_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        allowed_collection_replay_prediction_restore,
    ):
        failures.append("store-owned replay prediction restore synthetic surface was rejected")

    old_replay_render_pose_physics_commit = """
    bool GameModelCollection::TrySetReplayRenderPose( int index,
                                                      uint32_t replayBodyId,
                                                      const Vector3& position,
                                                      const Quaternion& orientation )
    {
        model.SetPosition( position );
        model.SetOrientation( orientation );
        CommitEditedModelPhysicsState( index, false );
        return true;
    }
    """
    if not any(
        error.message == "replay render-pose physics commit is blocked"
        for error in check_game_model_collection_replay_render_pose_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
            old_replay_render_pose_physics_commit,
        )
    ):
        failures.append("old replay render-pose physics commit synthetic surface was not rejected")

    allowed_replay_render_pose_override = """
    bool GameModelCollection::TrySetReplayRenderPose( int index,
                                                      uint32_t replayBodyId,
                                                      const Vector3& position,
                                                      const Quaternion& orientation )
    {
        model.SetPosition( position );
        model.SetOrientation( orientation );
        InvalidateSoA();
        return true;
    }
    """
    if check_game_model_collection_replay_render_pose_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        allowed_replay_render_pose_override,
    ):
        failures.append("presentation-only replay render-pose synthetic surface was rejected")

    commented_replay_render_pose_physics_commit = """
    bool GameModelCollection::TrySetReplayRenderPose( int index,
                                                      uint32_t replayBodyId,
                                                      const Vector3& position,
                                                      const Quaternion& orientation )
    {
        // CommitEditedModelPhysicsState( index, false ) used to run here.
        InvalidateSoA();
        return true;
    }
    """
    if check_game_model_collection_replay_render_pose_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        commented_replay_render_pose_physics_commit,
    ):
        failures.append("comment-only replay render-pose physics commit synthetic text was rejected")

    old_physics_scene_step_body_reload = """
    void PhysicsScene::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        modelAccess.ReloadPhysicsBodies( m_bodyStore, m_world.GetSleepStates() );
        m_world.RunPhysics( modelAccess, m_bodyStore, m_colliderStore, 0.016f, config, forces, workerPool );
    }
    """
    if not any(
        error.message == "per-step model-to-body-store reload is blocked"
        for error in check_physics_scene_step_body_reload_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
            old_physics_scene_step_body_reload,
        )
    ):
        failures.append("old unconditional step-start body reload synthetic surface was not rejected")

    topology_guarded_physics_scene_step_body_reload = """
    void PhysicsScene::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        const int modelCount = modelAccess.ModelCount();
        if ( m_bodyStore.Count() != modelCount )
        {
            modelAccess.ReloadPhysicsBodies( m_bodyStore, m_world.GetSleepStates() );
        }
        m_world.RunPhysics( modelAccess, m_bodyStore, m_colliderStore, 0.016f, config, forces, workerPool );
    }
    """
    if check_physics_scene_step_body_reload_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        topology_guarded_physics_scene_step_body_reload,
    ):
        failures.append("topology-guarded step-start body reload synthetic surface was rejected")

    commented_physics_scene_step_body_reload = """
    void PhysicsScene::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        // modelAccess.ReloadPhysicsBodies(...) used to run every tick.
        RunStoreOwnedStep();
    }
    """
    if check_physics_scene_step_body_reload_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        commented_physics_scene_step_body_reload,
    ):
        failures.append("comment-only step-start body reload synthetic text was rejected")

    old_physics_scene_command_body_refresh = """
    void PhysicsScene::SetPendingBodyImpulse( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        RefreshBodyStore( modelAccess );
        ApplyCommand();
    }
    """
    if not any(
        error.message == "command-side model-to-body-store refresh is blocked"
        for error in check_physics_scene_command_body_refresh_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
            old_physics_scene_command_body_refresh,
        )
    ):
        failures.append("old unconditional command-side body refresh synthetic surface was not rejected")

    topology_guarded_physics_scene_command_body_refresh = """
    void PhysicsScene::WakeBody( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        const int modelCount = modelAccess.ModelCount();
        if ( m_bodyStore.Count() != modelCount )
        {
            RefreshBodyStore( modelAccess );
        }
        ApplyCommand();
    }
    """
    if check_physics_scene_command_body_refresh_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        topology_guarded_physics_scene_command_body_refresh,
    ):
        failures.append("topology-guarded command-side body refresh synthetic surface was rejected")

    old_adapter_body_handle_refresh = """
    PhysicsBodyHandle GameModelCollectionPhysicsAdapter::BodyHandleForModelIndex( int modelIndex ) const
    {
        PhysicsModelAccess modelAccess( m_collection );
        m_collection.m_physicsEngine.RefreshBodyStore( modelAccess );
        return m_collection.m_physicsEngine.BodyStore().HandleForModelIndex( modelIndex );
    }
    """
    if not any(
        error.message == "adapter body-handle model-to-store refresh is blocked"
        for error in check_game_model_collection_adapter_body_refresh_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollectionPhysicsAdapter.cpp"),
            old_adapter_body_handle_refresh,
        )
    ):
        failures.append("old unconditional adapter body refresh synthetic surface was not rejected")

    topology_guarded_adapter_body_handle_refresh = """
    PhysicsBodyHandle GameModelCollectionPhysicsAdapter::BodyHandleForModelIndex( int modelIndex ) const
    {
        PhysicsModelAccess modelAccess( m_collection );
        if ( m_collection.m_physicsEngine.BodyStore().Count() != m_collection.GetModelCount() )
        {
            m_collection.m_physicsEngine.RefreshBodyStore( modelAccess );
        }
        return m_collection.m_physicsEngine.BodyStore().HandleForModelIndex( modelIndex );
    }
    """
    if check_game_model_collection_adapter_body_refresh_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollectionPhysicsAdapter.cpp"),
        topology_guarded_adapter_body_handle_refresh,
    ):
        failures.append("topology-guarded adapter body refresh synthetic surface was rejected")

    commented_command_body_refresh = """
    void PhysicsScene::WakeBody( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        // RefreshBodyStore( modelAccess ) used to run every command.
        ApplyCommand();
    }
    """
    if check_physics_scene_command_body_refresh_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        commented_command_body_refresh,
    ):
        failures.append("comment-only command-side body refresh synthetic text was rejected")

    old_scene_setup_model_index_command = """
    void SceneAuthoredSetup::SetUpGameModels( SceneAuthoredModelContext context )
    {
        const int modelIndex = context.models.GetModelCount();
        context.models.AddGameModel( std::move( model ) );
        context.models.SetPendingBodyImpulse( modelIndex, force, forcePos );
        context.models.SeedModelAsleep( modelIndex );
    }
    """
    if not any(
        error.message == "scene setup model-index physics command is blocked"
        for error in check_scene_setup_model_index_physics_command_guardrails_text(
            Path("SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp"),
            old_scene_setup_model_index_command,
        )
    ):
        failures.append("old scene setup model-index command synthetic surface was not rejected")

    allowed_scene_setup_handle_command = """
    void SceneAuthoredSetup::SetUpGameModels( SceneAuthoredModelContext context )
    {
        const int modelIndex = context.models.GetModelCount();
        context.models.AddGameModel( std::move( model ) );
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForModelIndex( modelIndex );
        context.physics.SetPendingBodyImpulse( body, force, forcePos );
        context.physics.SeedBodyAsleep( body );
    }
    """
    if check_scene_setup_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp"),
        allowed_scene_setup_handle_command,
    ):
        failures.append("handle-keyed scene setup command synthetic surface was rejected")

    commented_scene_setup_model_index_command = """
    void SceneAuthoredSetup::SetUpGameModels( SceneAuthoredModelContext context )
    {
        // context.models.SetPendingBodyImpulse(modelIndex, force, forcePos) used to be the migration path.
        context.physics.SetPendingBodyImpulse( body, force, forcePos );
    }
    """
    if check_scene_setup_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp"),
        commented_scene_setup_model_index_command,
    ):
        failures.append("comment-only scene setup model-index command synthetic text was rejected")

    old_editor_model_index_command = """
    bool PlaceEditorObjectAtTerrainPoint( EditorObjectPlacementContext context )
    {
        const int modelIndex = context.models.GetModelCount();
        context.models.AddGameModel( std::move( model ) );
        context.models.SeedModelAsleep( modelIndex );
        context.models.WakeModel( modelIndex );
        return true;
    }
    """
    if not any(
        error.message == "editor model-index physics command is blocked"
        for error in check_editor_model_index_physics_command_guardrails_text(
            Path("SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.inl"),
            old_editor_model_index_command,
        )
    ):
        failures.append("old editor placement model-index command synthetic surface was not rejected")

    old_editor_reset_model_index_command = """
    void ResetEditorModelMotionAndWake( GameModelCollection& collection, PhysicsEngine&, int index )
    {
        collection.CommitEditedModelPhysicsState( index, false );
        collection.WakeModel( index );
    }
    """
    if not any(
        error.message == "editor model-index physics command is blocked"
        for error in check_editor_model_index_physics_command_guardrails_text(
            Path("SkullbonezSource/Runtime/Editor/RunEditorTools.cpp"),
            old_editor_reset_model_index_command,
        )
    ):
        failures.append("old editor reset model-index command synthetic surface was not rejected")

    allowed_editor_handle_command = """
    void WakeEditorPhysicsBody( GameModelCollection& collection, PhysicsEngine& physics, int modelIndex )
    {
        GameModelCollectionPhysicsAdapter physicsBodies( collection );
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForModelIndex( modelIndex );
        PhysicsModelAccess modelAccess( collection );
        physics.WakeBody( modelAccess, body );
    }
    """
    if check_editor_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Editor/RunEditorTools.cpp"),
        allowed_editor_handle_command,
    ):
        failures.append("handle-keyed editor command synthetic surface was rejected")

    commented_editor_model_index_command = """
    void DocumentOldEditorCommand()
    {
        // context.models.WakeModel(modelIndex) was the old editor command.
        WakeEditorPhysicsBody( context.models, context.models.GetPhysicsEngine(), modelIndex );
    }
    """
    if check_editor_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.inl"),
        commented_editor_model_index_command,
    ):
        failures.append("comment-only editor model-index command synthetic text was rejected")

    old_mouse_pickup_model_index_command = """
    void Run::ApplyMousePickupPhysicsStep()
    {
        m_cGameModelCollection.ApplyBodyImpulse( modelIndex, impulse, ZERO_VECTOR );
    }
    """
    if not any(
        error.message == "mouse pickup model-index physics command is blocked"
        for error in check_mouse_pickup_model_index_physics_command_guardrails_text(
            Path("SkullbonezSource/Runtime/Editor/RunMousePickupTools.inl"),
            old_mouse_pickup_model_index_command,
        )
    ):
        failures.append("old mouse pickup model-index command synthetic surface was not rejected")

    allowed_mouse_pickup_handle_command = """
    void Run::ApplyMousePickupPhysicsStep()
    {
        ApplyRuntimeToolPhysicsImpulse( m_cGameModelCollection,
                                        m_cGameModelCollection.GetPhysicsEngine(),
                                        modelIndex,
                                        impulse,
                                        ZERO_VECTOR );
    }
    """
    if check_mouse_pickup_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Editor/RunMousePickupTools.inl"),
        allowed_mouse_pickup_handle_command,
    ):
        failures.append("handle-keyed mouse pickup command synthetic surface was rejected")

    commented_mouse_pickup_model_index_command = """
    void DocumentOldMousePickupCommand()
    {
        // m_cGameModelCollection.ApplyBodyImpulse(modelIndex, impulse, ZERO_VECTOR) used to run here.
        ApplyRuntimeToolPhysicsImpulse( m_cGameModelCollection, physics, modelIndex, impulse, ZERO_VECTOR );
    }
    """
    if check_mouse_pickup_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Editor/RunMousePickupTools.inl"),
        commented_mouse_pickup_model_index_command,
    ):
        failures.append("comment-only mouse pickup model-index command synthetic text was rejected")

    old_launcher_model_index_command = """
    void RuntimeTools::FireLauncherLaser( GameModelCollection& collection )
    {
        collection.ApplyBodyImpulse( modelHitIndex, impulse, localPoint );
        collection.WakeModel( projectileIndex );
    }
    """
    if not any(
        error.message == "launcher model-index physics command is blocked"
        for error in check_launcher_model_index_physics_command_guardrails_text(
            Path("SkullbonezSource/Runtime/Tools/RuntimeTools.cpp"),
            old_launcher_model_index_command,
        )
    ):
        failures.append("old launcher model-index command synthetic surface was not rejected")

    allowed_launcher_handle_command = """
    void RuntimeTools::FireLauncherLaser( GameModelCollection& collection )
    {
        ApplyLauncherPhysicsImpulse( collection, modelHitIndex, impulse, localPoint );
        WakeLauncherPhysicsBody( collection, projectileIndex );
    }
    """
    if check_launcher_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Tools/RuntimeTools.cpp"),
        allowed_launcher_handle_command,
    ):
        failures.append("handle-keyed launcher command synthetic surface was rejected")

    commented_launcher_model_index_command = """
    void DocumentOldLauncherCommand()
    {
        // collection.ApplyBodyImpulse(modelHitIndex, impulse, localPoint) used to run here.
        ApplyLauncherPhysicsImpulse( collection, modelHitIndex, impulse, localPoint );
    }
    """
    if check_launcher_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Tools/RuntimeTools.cpp"),
        commented_launcher_model_index_command,
    ):
        failures.append("comment-only launcher model-index command synthetic text was rejected")

    old_replay_velocity_model_state_command = """
    void ApplyReplayVelocityEditToModel( GameModelCollection& modelCollection )
    {
        GameModel& model = modelCollection.GetModelAtIndex( modelIndex );
        model.SetLinearVelocity( clampedLinear );
        model.SetAngularVelocity( clampedAngular );
        modelCollection.CommitEditedModelPhysicsState( modelIndex, false );
        modelCollection.WakeModel( modelIndex );
    }
    """
    if not any(
        error.message == "replay velocity model-state physics command is blocked"
        for error in check_replay_velocity_model_state_physics_command_guardrails_text(
            Path("SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.inl"),
            old_replay_velocity_model_state_command,
        )
    ):
        failures.append("old replay velocity model-state command synthetic surface was not rejected")

    allowed_replay_velocity_handle_command = """
    void ApplyReplayVelocityEditToModel( GameModelCollection& modelCollection )
    {
        GameModelCollectionPhysicsAdapter physicsBodies( modelCollection );
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForModelIndex( modelIndex );
        PhysicsModelAccess modelAccess( modelCollection );
        modelCollection.GetPhysicsEngine().SetBodyVelocity( modelAccess, body, linearVelocity, angularVelocity, true );
    }
    """
    if check_replay_velocity_model_state_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.inl"),
        allowed_replay_velocity_handle_command,
    ):
        failures.append("handle-keyed replay velocity command synthetic surface was rejected")

    commented_replay_velocity_model_state_command = """
    void DocumentOldReplayVelocityCommand()
    {
        // model.SetLinearVelocity(linearVelocity) used to run here.
        // modelCollection.CommitEditedModelPhysicsState(modelIndex, false) used to run here.
        modelCollection.GetPhysicsEngine().SetBodyVelocity( modelAccess, body, linearVelocity, angularVelocity, true );
    }
    """
    if check_replay_velocity_model_state_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.inl"),
        commented_replay_velocity_model_state_command,
    ):
        failures.append("comment-only replay velocity model-state command synthetic text was rejected")

    old_run_frame_replay_editor_transform_wake = """
    bool ApplyReplayEditorTransformEvent()
    {
        if ( !model.IsFixed() )
        {
            m_cGameModelCollection.WakeModel( event.value0 );
        }
        return true;
    }
    """
    if not any(
        error.message == "RunFrame replay editor transform wake wrapper is blocked"
        for error in check_run_frame_replay_editor_transform_wake_guardrails_text(
            Path("SkullbonezSource/Runtime/RunFrame.cpp"),
            old_run_frame_replay_editor_transform_wake,
        )
    ):
        failures.append("old RunFrame replay editor transform wake synthetic surface was not rejected")

    allowed_run_frame_replay_editor_transform_wake = """
    bool ApplyReplayEditorTransformEvent()
    {
        PhysicsEngine& physics = m_cGameModelCollection.GetPhysicsEngine();
        PhysicsModelAccess modelAccess( m_cGameModelCollection );
        const PhysicsBodyHandle body = physics.BodyStore().HandleForModelIndex( event.value0 );
        if ( body.IsValid() )
        {
            physics.WakeBody( modelAccess, body );
        }
        return true;
    }
    """
    if check_run_frame_replay_editor_transform_wake_guardrails_text(
        Path("SkullbonezSource/Runtime/RunFrame.cpp"),
        allowed_run_frame_replay_editor_transform_wake,
    ):
        failures.append("handle-keyed RunFrame replay editor transform wake synthetic surface was rejected")

    commented_run_frame_replay_editor_transform_wake = """
    void DocumentOldReplayEditorTransformWake()
    {
        // m_cGameModelCollection.WakeModel(event.value0) used to run here.
        physics.WakeBody( modelAccess, body );
    }
    """
    if check_run_frame_replay_editor_transform_wake_guardrails_text(
        Path("SkullbonezSource/Runtime/RunFrame.cpp"),
        commented_run_frame_replay_editor_transform_wake,
    ):
        failures.append("comment-only RunFrame replay editor transform wake synthetic text was rejected")

    old_ragdoll_model_index_command = """
    void Ragdoll::AddSimpleHumanoid( GameModelCollection& collection )
    {
        for ( int i = 0; i < PART_COUNT; ++i )
        {
            collection.SeedModelAsleep( firstBody + i );
        }
    }
    """
    if not any(
        error.message == "ragdoll model-index physics command is blocked"
        for error in check_ragdoll_model_index_physics_command_guardrails_text(
            Path("SkullbonezSource/Physics/Ragdoll.cpp"),
            old_ragdoll_model_index_command,
        )
    ):
        failures.append("old ragdoll model-index command synthetic surface was not rejected")

    allowed_ragdoll_handle_command = """
    void Ragdoll::AddSimpleHumanoid( GameModelCollection& collection, PhysicsEngine& physics )
    {
        const PhysicsBodyStore& bodyStore = collection.GetPhysicsBodyStore();
        PhysicsModelAccess modelAccess( collection );
        physics.SeedBodyAsleep( modelAccess, bodyStore.HandleForModelIndex( firstBody + i ) );
    }
    """
    if check_ragdoll_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Physics/Ragdoll.cpp"),
        allowed_ragdoll_handle_command,
    ):
        failures.append("handle-keyed ragdoll command synthetic surface was rejected")

    commented_ragdoll_model_index_command = """
    void DocumentOldRagdollCommand()
    {
        // collection.SeedModelAsleep(firstBody + i) used to run here.
        physics.SeedBodyAsleep( modelAccess, body );
    }
    """
    if check_ragdoll_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Physics/Ragdoll.cpp"),
        commented_ragdoll_model_index_command,
    ):
        failures.append("comment-only ragdoll model-index command synthetic text was rejected")

    old_game_model_collection_physics_wrapper_header = """
    class GameModelCollection
    {
        void WakeModel( int index );
        void SeedModelAsleep( int index );
    };
    """
    if not any(
        error.message == "deleted GameModelCollection model-index physics wrapper is blocked"
        for error in check_deleted_game_model_collection_physics_wrapper_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.h"),
            old_game_model_collection_physics_wrapper_header,
        )
    ):
        failures.append("old GameModelCollection physics wrapper header synthetic surface was not rejected")

    old_game_model_collection_physics_wrapper_definition = """
    void GameModelCollection::ApplyBodyImpulse( int index, const Vector3& impulse, const Vector3& localPoint )
    {
        GameModelCollectionPhysicsAdapter( *this ).ApplyBodyImpulseForModelIndex( index, impulse, localPoint );
    }
    """
    if not any(
        error.message == "deleted GameModelCollection model-index physics wrapper is blocked"
        for error in check_deleted_game_model_collection_physics_wrapper_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
            old_game_model_collection_physics_wrapper_definition,
        )
    ):
        failures.append("old GameModelCollection physics wrapper definition synthetic surface was not rejected")

    allowed_game_model_collection_adapter_use = """
    void GameModelCollection::ReleaseAttachedFixedTreeParts( int sourceIndex )
    {
        GameModelCollectionPhysicsAdapter( *this ).WakeBodyForModelIndex( sourceIndex );
    }
    """
    if check_deleted_game_model_collection_physics_wrapper_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        allowed_game_model_collection_adapter_use,
    ):
        failures.append("GameModelCollection adapter use synthetic surface was rejected")

    commented_game_model_collection_physics_wrapper = """
    void DocumentDeletedWrappers()
    {
        // void GameModelCollection::WakeModel( int index ) used to live here.
        GameModelCollectionPhysicsAdapter( *this ).WakeBodyForModelIndex( index );
    }
    """
    if check_deleted_game_model_collection_physics_wrapper_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        commented_game_model_collection_physics_wrapper,
    ):
        failures.append("comment-only GameModelCollection physics wrapper synthetic text was rejected")

    allowed_physics_hot_path_values = """
    struct SolverBodyState
    {
        float invMass = 0.0f;
    };
    class PersistentContactSolver
    {
      public:
        void Solve();
    };
    """
    if check_physics_hot_path_inheritance_guardrails_text(
        Path("SkullbonezSource/Physics/PersistentContactSolver.h"),
        allowed_physics_hot_path_values,
    ):
        failures.append("allowed physics hot-path value types synthetic surface was rejected")

    old_physics_hot_path_inheritance = """
    class SolverSideEffectSink : public PhysicsBodyEventSink
    {
    };
    """
    if not any(
        error.message == "physics hot-path inheritance is blocked"
        for error in check_physics_hot_path_inheritance_guardrails_text(
            Path("SkullbonezSource/Physics/PersistentContactSolver.h"),
            old_physics_hot_path_inheritance,
        )
    ):
        failures.append("physics hot-path inheritance synthetic surface was not rejected")

    commented_physics_hot_path_inheritance = """
    // class SolverSideEffectSink : public PhysicsBodyEventSink is a deleted migration note only.
    struct PersistentContactSolverSideEffects
    {
        int count = 0;
    };
    """
    if check_physics_hot_path_inheritance_guardrails_text(
        Path("SkullbonezSource/Physics/PersistentContactSolver.h"),
        commented_physics_hot_path_inheritance,
    ):
        failures.append("comment-only physics hot-path inheritance synthetic text was rejected")

    allowed_physics_model_access_facade = """
    class PhysicsModelAccess
    {
      public:
        explicit PhysicsModelAccess( GameObjects::GameModelCollection& collection );
    };
    """
    if check_physics_model_access_inheritance_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsModelAccess.h"),
        allowed_physics_model_access_facade,
    ):
        failures.append("allowed PhysicsModelAccess facade synthetic surface was rejected")

    old_physics_model_access_inheritance = """
    class GameModelCollection : public Rendering::IRenderSceneView,
                                public Physics::PhysicsModelAccess,
                                public Physics::PhysicsBodyEventSink
    {
    };
    """
    if not any(
        error.message == "PhysicsModelAccess inheritance is blocked"
        for error in check_physics_model_access_inheritance_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.h"),
            old_physics_model_access_inheritance,
        )
    ):
        failures.append("old PhysicsModelAccess inheritance synthetic surface was not rejected")

    commented_physics_model_access_inheritance = """
    // class GameModelCollection : public Physics::PhysicsModelAccess is a deleted migration note only.
    class GameModelCollection;
    """
    if check_physics_model_access_inheritance_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.h"),
        commented_physics_model_access_inheritance,
    ):
        failures.append("comment-only PhysicsModelAccess inheritance synthetic text was rejected")

    allowed_renderer_inheritance = """
    class IRenderBackend : public IRenderDeviceLifecycle,
                           public IRenderResourceFactory,
                           public IRenderCommandContext,
                           public IRenderDiagnostics,
                           public IRenderCaptureBackend
    {
    };
    """
    if check_approved_inheritance_guardrails_text(
        Path("SkullbonezSource/Rendering/IRenderBackend.h"),
        allowed_renderer_inheritance,
        Path("SkullbonezSource/Rendering/IRenderBackend.h"),
    ):
        failures.append("approved renderer inheritance synthetic surface was rejected")

    unapproved_runtime_inheritance = """
    struct RuntimeCaptureSink : public IScreenshotSink
    {
    };
    """
    if not any(
        error.message == "unapproved inheritance is blocked"
        for error in check_approved_inheritance_guardrails_text(
            Path("SkullbonezSource/Runtime/CaptureSystem.h"),
            unapproved_runtime_inheritance,
            Path("SkullbonezSource/Runtime/CaptureSystem.h"),
        )
    ):
        failures.append("unapproved runtime inheritance synthetic surface was not rejected")

    qualified_pimpl_definition = """
    struct ContactAudioService::Impl
    {
        int sampleCount = 0;
    };
    """
    if check_approved_inheritance_guardrails_text(
        Path("SkullbonezSource/Runtime/Audio/ContactAudioService.cpp"),
        qualified_pimpl_definition,
        Path("SkullbonezSource/Runtime/Audio/ContactAudioService.cpp"),
    ):
        failures.append("qualified PIMPL definition synthetic text was rejected as inheritance")

    commented_general_inheritance = """
    // class RuntimeCaptureSink : public IScreenshotSink is deleted migration debt.
    struct RuntimeCaptureSink
    {
        void* context = nullptr;
    };
    """
    if check_approved_inheritance_guardrails_text(
        Path("SkullbonezSource/Runtime/CaptureSystem.h"),
        commented_general_inheritance,
        Path("SkullbonezSource/Runtime/CaptureSystem.h"),
    ):
        failures.append("comment-only general inheritance synthetic text was rejected")

    compatibility_physics_models_text = (
        "std::vector<SkullbonezCore::GameObjects::GameModel>& physicsModels = "
        "m_cGameModelCollection.MutablePhysicsModelsForCompatibility();"
    )
    empty_physics_models_allowlist: Counter[tuple[Path, str]] = Counter()
    if check_physics_models_access_guardrails_text(
        Path("SkullbonezSource/Runtime/RunFrame.cpp"),
        compatibility_physics_models_text,
        Path("SkullbonezSource/Runtime/RunFrame.cpp"),
        empty_physics_models_allowlist,
    ):
        failures.append("named PhysicsModels compatibility adapter was rejected")

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
        empty_physics_models_allowlist,
    ):
        failures.append("comment-only PhysicsModels synthetic text was rejected")

    duplicate_physics_models_access = "auto& models = collection.PhysicsModels();\n" * 2
    if not any(
        error.message == "direct PhysicsModels() compatibility access is blocked"
        for error in check_physics_models_access_guardrails_text(
            Path("SkullbonezSource/Runtime/NewPhysicsCaller.cpp"),
            duplicate_physics_models_access,
            Path("SkullbonezSource/Runtime/NewPhysicsCaller.cpp"),
            empty_physics_models_allowlist,
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
            empty_physics_models_allowlist,
        )
    ):
        failures.append("new PhysicsModels synthetic access was not rejected")

    allowed_named_physics_models_path = Path("SkullbonezSource/Runtime/RunFrame.cpp")
    allowed_named_physics_models_line = "m_cGameModelCollection.MutablePhysicsModelsForCompatibility();"
    synthetic_named_physics_models_allowlist = Counter(
        { ( allowed_named_physics_models_path, normalize_boundary_line( allowed_named_physics_models_line ) ): 1 }
    )
    if check_named_physics_models_compat_access_guardrails_text(
        allowed_named_physics_models_path,
        allowed_named_physics_models_line,
        allowed_named_physics_models_path,
        synthetic_named_physics_models_allowlist,
    ):
        failures.append("allowed named PhysicsModels compatibility access failed")

    if not any(
        error.message == "named physics model vector compatibility access is count-guarded"
        for error in check_named_physics_models_compat_access_guardrails_text(
            allowed_named_physics_models_path,
            allowed_named_physics_models_line,
            allowed_named_physics_models_path,
            empty_physics_models_allowlist,
        )
    ):
        failures.append("deleted named PhysicsModels compatibility access was not rejected without an allowlist")

    commented_named_physics_models_text = """
    // m_cGameModelCollection.MutablePhysicsModelsForCompatibility() is mentioned in a note only.
    /*
       collection.PhysicsModelsForCompatibility() appears in block comments too.
    */
    void UseStoresInstead();
    """
    if check_named_physics_models_compat_access_guardrails_text(
        Path("SkullbonezSource/Runtime/NewPhysicsCaller.cpp"),
        commented_named_physics_models_text,
        Path("SkullbonezSource/Runtime/NewPhysicsCaller.cpp"),
        synthetic_named_physics_models_allowlist,
    ):
        failures.append("comment-only named PhysicsModels synthetic text was rejected")

    duplicate_named_physics_models_access = allowed_named_physics_models_line + "\n" + allowed_named_physics_models_line
    if not any(
        error.message == "named physics model vector compatibility access is count-guarded"
        for error in check_named_physics_models_compat_access_guardrails_text(
            allowed_named_physics_models_path,
            duplicate_named_physics_models_access,
            allowed_named_physics_models_path,
            synthetic_named_physics_models_allowlist,
        )
    ):
        failures.append("duplicate named PhysicsModels synthetic access was not rejected")

    new_named_physics_models_access = "auto& models = collection.MutablePhysicsModelsForCompatibility();"
    if not any(
        error.message == "named physics model vector compatibility access is count-guarded"
        for error in check_named_physics_models_compat_access_guardrails_text(
            Path("SkullbonezSource/Runtime/NewPhysicsCaller.cpp"),
            new_named_physics_models_access,
            Path("SkullbonezSource/Runtime/NewPhysicsCaller.cpp"),
            synthetic_named_physics_models_allowlist,
        )
    ):
        failures.append("new named PhysicsModels synthetic access was not rejected")

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
    errors.extend(check_public_physics_facade_game_object_guardrails(repo))
    errors.extend(check_deleted_migration_artifact_guardrails(repo))
    errors.extend(check_deleted_physics_model_view_guardrails(repo))
    errors.extend(check_persistent_solver_context_model_access_guardrails(repo))
    errors.extend(check_physics_world_solver_body_writeback_guardrails(repo))
    errors.extend(check_physics_world_solver_model_stream_guardrails(repo))
    errors.extend(check_physics_world_contact_highlight_tick_guardrails(repo))
    errors.extend(check_render_instance_store_authority_guardrails(repo))
    errors.extend(check_physics_diagnostics_store_authority_guardrails(repo))
    errors.extend(check_replay_recorder_store_authority_guardrails(repo))
    errors.extend(check_replay_restore_store_authority_guardrails(repo))
    errors.extend(check_physics_scene_step_body_reload_guardrails(repo))
    errors.extend(check_command_side_body_refresh_guardrails(repo))
    errors.extend(check_scene_setup_model_index_physics_command_guardrails(repo))
    errors.extend(check_editor_model_index_physics_command_guardrails(repo))
    errors.extend(check_mouse_pickup_model_index_physics_command_guardrails(repo))
    errors.extend(check_launcher_model_index_physics_command_guardrails(repo))
    errors.extend(check_replay_velocity_model_state_physics_command_guardrails(repo))
    errors.extend(check_run_frame_replay_editor_transform_wake_guardrails(repo))
    errors.extend(check_ragdoll_model_index_physics_command_guardrails(repo))
    errors.extend(check_deleted_game_model_collection_physics_wrapper_guardrails(repo))
    errors.extend(check_physics_hot_path_inheritance_guardrails(repo))
    errors.extend(check_physics_model_access_inheritance_guardrails(repo))
    errors.extend(check_approved_inheritance_guardrails(repo))
    errors.extend(check_physics_models_access_guardrails(repo))
    errors.extend(check_named_physics_models_compat_access_guardrails(repo))
    errors.extend(check_direct_gfx_raytracing_guardrails(repo))
    errors.extend(check_irender_backend_raytracing_declarations(repo))
    errors.extend(check_irender_backend_aggregate_contract(repo))
    errors.extend(check_runtime_render_pass_wide_backend_guardrails(repo))
    errors.extend(check_graph_owned_render_pass_scheduling(repo))
    errors.extend(check_graph_owned_render_pass_manual_barriers(repo))
    errors.extend(check_render_graph_unknown_access(repo))
    errors.extend(check_global_service_access_guardrails(repo))
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
