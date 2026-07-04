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
#   have an extra file-classification fence, object rendering has a
#   render-instance authority fence, and runtime picking has a store-authority
#   fence, so count allowances do not silently approve a new compatibility
#   location.
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
#   Topology repair: Count-gated compatibility import that rebuilds store rows
#     only after object counts change.
#   Render instance store: Physics-backed, model-order snapshot consumed by
#     render passes instead of rebuilding GameModel pose streams.
#   Store-authority fence: Static rule that keeps a migrated reader on physics
#     body/collider records instead of reopening a GameModel mirror path.
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
#   - Object rendering and DXR matrix upload read prepared render instances
#     rather than recomputing GameModel model matrices or body streams.
#   - Runtime picking reads PhysicsBodyStore/ColliderStore records instead of
#     requiring the GameModel compatibility mirror to be refreshed before input.
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
PHYSICS_ENGINE_SOURCE = PHYSICS_ROOT / "PhysicsEngine.cpp"
PHYSICS_ENGINE_HEADER = PHYSICS_ROOT / "PhysicsEngine.h"
PHYSICS_WORLD_SOURCE = PHYSICS_ROOT / "PhysicsWorld.cpp"
PHYSICS_WORLD_HEADER = PHYSICS_ROOT / "PhysicsWorld.h"
PHYSICS_SCENE_SOURCE = PHYSICS_ROOT / "PhysicsScene.cpp"
PHYSICS_SCENE_HEADER = PHYSICS_ROOT / "PhysicsScene.h"
PHYSICS_DIAGNOSTICS_SINK_SOURCE = PHYSICS_ROOT / "PhysicsDiagnosticsSink.cpp"
PHYSICS_DIAGNOSTICS_SINK_HEADER = PHYSICS_ROOT / "PhysicsDiagnosticsSink.h"
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
REPLAY_PREDICTION_HELPERS_SOURCE = Path("SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl")
REPLAY_RECORDER_SOURCE = Path("SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp")
RUNTIME_RENDER_HOST_HEADER = Path("SkullbonezSource/Runtime/Render/RuntimeRenderHost.h")
RUNTIME_RENDER_PASS_CAPABILITY_SOURCES = (
    RUN_PASSES_SOURCE,
    RUN_UI_TEXT_PASS_SOURCE,
    Path("SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h"),
    Path("SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h"),
)
PHYSICS_HOT_PATH_INHERITANCE_SOURCES = (
    PHYSICS_WORLD_HEADER,
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
# Invariant: PhysicsApi.cpp is the standalone proof surface. It should never
# import game-object ownership to make a smoke or helper pass.
STANDALONE_PHYSICS_IMPLEMENTATION_SOURCES = (
    PHYSICS_ROOT / "PhysicsApi.cpp",
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
PHYSICS_WORLD_FIXED_CONTACT_NOTIFY_PATTERN = re.compile(r"\bmodelAccess\s*\.\s*NotifyFixedContact\s*\(")
PHYSICS_WORLD_PERSISTENT_CONTACT_TREE_RELEASE_PATTERN = re.compile(
    r"\bmodelAccess\s*\.\s*ReleaseAttachedFixedTreeParts\s*\("
)
PHYSICS_WORLD_TORNADO_RELEASE_MODEL_ACCESS_PATTERN = re.compile(
    r"\bmodelAccess\s*\.\s*(?:WriteBackPhysicsBody|ReloadPhysicsBodies|InvalidatePhysicsStreams)\s*\("
    r"|\bmodelAccess\s*\.\s*ReleaseAttachedFixedTreeParts\s*\((?!\s*bodyStore\s*,)"
)
PHYSICS_WORLD_STEP_MODEL_ACCESS_SIGNATURE_PATTERN = re.compile(
    r"\b(?:PhysicsWorld::)?(?:RunPhysics|RunSolverPhysics|ApplyTornadoField)\s*\("
    r"(?P<args>[^;{}]*?\bPhysicsModelAccess\s*&[^;{}]*?)\)"
)
PHYSICS_ENGINE_STEP_MODEL_ACCESS_SIGNATURE_PATTERN = re.compile(
    r"\b(?:PhysicsEngine::)?Step\s*\(\s*PhysicsModelAccess\s*&"
)
PHYSICS_SCENE_STEP_MODEL_ACCESS_SIGNATURE_PATTERN = re.compile(
    r"\b(?:PhysicsScene::)?RunPhysics\s*\(\s*PhysicsModelAccess\s*&"
)
SIMULATION_SYSTEM_OWNER_BORROW_PATTERN = re.compile(
    r"\b(?:SimulationPhysicsStep|PhysicsModelAccess|PhysicsEngine|PhysicsWorldForces|WorkerPool)\b"
)
PHYSICS_MODEL_ACCESS_DELETED_STEP_FACADE_PATTERN = re.compile(
    r"\b(?:GetPhysicsBodyStream|GetBodyStream|InvalidatePhysicsStreams|WriteBackPhysicsBodies|WriteBackPhysicsBody|"
    r"NotifyFixedContact|TickContactHighlights|TryGetPhysicsDiagnosticsModelName|FillPhysicsDiagnosticsNames|"
    r"Count|size)\s*\("
)
PHYSICS_MODEL_ACCESS_DELETED_STEP_FACADE_DEFINITION_PATTERN = re.compile(
    r"\bPhysicsModelAccess::"
    r"(?:GetPhysicsBodyStream|GetBodyStream|InvalidatePhysicsStreams|WriteBackPhysicsBodies|WriteBackPhysicsBody|"
    r"NotifyFixedContact|TickContactHighlights|TryGetPhysicsDiagnosticsModelName|FillPhysicsDiagnosticsNames|"
    r"Count|size)\s*\("
)
PHYSICS_WORLD_STORE_SEED_MODEL_ACCESS_PATTERN = re.compile(
    r"\b(?:GameModelBodyStream|GetBodyStream)\b|\bmodelAccess\s*\.\s*InvalidatePhysicsStreams\s*\("
)
PHYSICS_WORLD_STORE_WAKE_MODEL_ACCESS_PATTERN = re.compile(
    r"\b(?:GameModelBodyStream|GetBodyStream)\b|\bmodelAccess\s*\.\s*InvalidatePhysicsStreams\s*\("
)
PHYSICS_WORLD_STORE_WAKE_MODEL_ACCESS_SIGNATURE_PATTERN = re.compile(r"\bPhysicsModelAccess\s*&")
# Why: the model-stream wake/seed overloads were deleted after store-owned
# commands became the real path. Reintroducing the old signature would quietly
# bring back a GameModel cache rebuild inside PhysicsWorld.
PHYSICS_WORLD_DELETED_MODEL_STREAM_WAKE_SEED_PATTERN = re.compile(
    r"\b(?:GameModelBodyStream|GetBodyStream)\b|\b(?:WakeModel|SeedModelAsleep)\s*\(\s*PhysicsModelAccess\s*&"
)
PHYSICS_WORLD_RUN_PHYSICS_WRITEBACK_PATTERN = re.compile(r"\bmodelAccess\s*\.\s*WriteBackPhysicsBodies\s*\(")
PHYSICS_WORLD_RUN_PHYSICS_INVALIDATION_PATTERN = re.compile(r"\bmodelAccess\s*\.\s*InvalidatePhysicsStreams\s*\(")
PHYSICS_WORLD_RUN_PHYSICS_DIAGNOSTICS_PATTERN = re.compile(
    r"\bm_diagnostics\s*\.\s*(?:EmitRegressionLog|IncrementCollisionTimeFrameIfEnabled|EmitFrame)\s*\("
)
# Why: step diagnostics may carry cold presentation names, but PhysicsWorld
# should consume them as a value view instead of borrowing PhysicsModelAccess.
PHYSICS_WORLD_STEP_DIAGNOSTICS_MODEL_ACCESS_PATTERN = re.compile(
    r"\bvoid\s+PhysicsWorld::Emit(?:StepDiagnostics|PhysicsDiagnosticsFrame)\s*\(\s*PhysicsModelAccess\s*&"
    r"|\bmodelAccess\s*\.\s*FillPhysicsDiagnosticsNames\s*\("
)
RENDER_INSTANCE_MODEL_REFRESH_PATTERN = re.compile(
    r"\brenderInstanceStore\s*\.\s*Refresh\s*\(\s*m_gameModels\s*\)"
)
RENDER_INSTANCE_STORE_MODEL_ONLY_REFRESH_SIGNATURE_PATTERN = re.compile(
    r"\bvoid\s+RenderInstanceStore::Refresh\s*\(\s*"
    r"(?:std::vector\s*<\s*GameModel\s*>\s*&\s*models|GameModel\s*\*\s*models\s*,\s*int\s+modelCount)"
    r"\s*\)"
    r"|\bvoid\s+Refresh\s*\(\s*"
    r"(?:std::vector\s*<\s*GameObjects::GameModel\s*>\s*&\s*models|"
    r"GameObjects::GameModel\s*\*\s*models\s*,\s*int\s+modelCount)"
    r"\s*\)\s*;"
)
RENDER_INSTANCE_STORE_MODEL_REFRESH_FALLBACK_PATTERN = re.compile(
    r"\bRefresh\s*\(\s*models\s*,\s*modelCount\s*\)"
)
GAME_MODEL_RENDERER_MODEL_POSE_STREAM_PATTERN = re.compile(
    r"\b(?:GameModelRenderStream|GameModelBodyStream)\b"
    r"|\bcollection\s*\.\s*(?:GetRenderStream|GetBodyStream)\s*\("
    r"|\bmodels\s*\[[^\]]+\]\s*\.\s*(?:GetPosition|GetOrientation|GetRenderMaterial)\s*\("
    r"|\bmodels\s*\[[^\]]+\]\s*\.\s*(?:IsSphere|IsBox|IsConvexHull)\s*\("
)
DXR_MODEL_MATRIX_RENDER_INSTANCE_PATTERN = re.compile(
    r"\bm_gameModels\s*\[[^\]]+\]\s*\.\s*GetModelMatrix\s*\("
)
# Why: the old GameModel SoA streams were deleted once render/physics consumers
# moved to PhysicsBodyStore, ColliderStore, and RenderInstanceStore snapshots.
# Reintroducing them would recreate GameModel-derived hot-path copies.
DELETED_GAME_MODEL_STREAM_PROJECT_PATTERN = re.compile(
    r"SkullbonezSource\\GameObjects\\GameModel(?:Streams|SoACache)\.(?:cpp|h)"
)
# Why: read-only presentation helpers should not rebuild body stores from the
# GameModel mirror. Count drift is topology repair; same-count state belongs to
# PhysicsBodyStore until a compatibility writeback explicitly projects it.
GAME_MODEL_COLLECTION_BODY_STORE_REFRESH_PATTERN = re.compile(
    r"\bm_physicsEngine\s*\.\s*RefreshBodyStore\s*\(\s*modelAccess\s*\)"
)
GAME_MODEL_COLLECTION_BODY_STORE_COUNT_GATE_PATTERN = re.compile(
    r"\bm_physicsEngine\s*\.\s*BodyStore\s*\(\s*\)\s*\.\s*Count\s*\(\s*\)\s*!=\s*ModelCount\s*\(\s*\)"
)
GAME_MODEL_COLLECTION_RUN_PHYSICS_FUNCTION_PATTERN = re.compile(r"\bvoid\s+GameModelCollection::RunPhysics\s*\(")
GAME_MODEL_COLLECTION_RUN_PHYSICS_MODEL_ACCESS_DECL_PATTERN = re.compile(
    r"\bPhysicsModelAccess\s+modelAccess\s*\(\s*\*\s*this\s*\)\s*;"
)
GAME_MODEL_COLLECTION_BODY_READ_MODEL_FIELD_PATTERN = re.compile(
    r"\bm_gameModels\s*\[[^\]]+\]\s*\.\s*GetPosition\s*\("
    r"|\bmodel\s*\.\s*(?:IsFixed|GetVelocity|GetAngularVelocity|GetRotationalInertia|GetMass)\s*\("
)
# Why: runtime picking is a shared input/tool policy. If it reads GameModel body
# fields again, every caller has to keep the compatibility mirror fresh before
# simple mouse picks, which is exactly the cache-hostile edge this slice deletes.
RUNTIME_PICK_SERVICE_GAME_MODEL_INCLUDE_PATTERN = re.compile(
    r'#\s*include\s+"(?:\.\./)?GameObjects/GameModel\.h"'
)
RUNTIME_PICK_SERVICE_MODEL_STATE_PATTERN = re.compile(
    r"\bGameObjects::GameModel\b"
    r"|\bstd::vector\s*<\s*GameObjects::GameModel\s*>"
    r"|\brequest\s*\.\s*models\b"
    r"|\bmodel\s*\.\s*(?:IsFixed|GetPosition|GetOrientation|GetCollisionShape)\s*\("
)
GAME_MODEL_COLLECTION_COLLIDER_STORE_REFRESH_PATTERN = re.compile(
    r"\bm_physicsEngine\s*\.\s*RefreshColliderStore\s*\(\s*modelAccess\s*\)"
)
# Why: scene saves are live simulation snapshots. If the writer reads body facts
# from GameModel, bulk post-step writeback remains required just to make saving
# correct. Cold metadata such as names and render materials may still come from
# GameModel until scene/entity metadata owns them.
SCENE_SNAPSHOT_GAME_MODEL_PHYSICS_READ_PATTERN = re.compile(
    r"\b(?:m_gameModels|models)\s*\[[^\]]+\]\s*\.\s*"
    r"(?:GetPosition|GetVelocity|GetAngularVelocity|GetRotationalInertia|GetCollisionShape|GetMass|"
    r"GetCoefficientRestitution|GetOrientation|IsFixed|ReleasesFromFixedOnContact|"
    r"GetContactReleaseImpulseThreshold)\s*\("
    r"|\bmodel\s*\.\s*"
    r"(?:GetPosition|GetVelocity|GetAngularVelocity|GetRotationalInertia|GetCollisionShape|GetMass|"
    r"GetCoefficientRestitution|GetOrientation|IsFixed|ReleasesFromFixedOnContact|"
    r"GetContactReleaseImpulseThreshold)\s*\("
    r"|\bOrientationJson\s*\(\s*(?:m_gameModels|models)\s*\["
)
# Why: model-side force integration was dead after PhysicsBodyStore became the
# active force/impulse owner. Reintroducing these names restores cache-hostile
# GameModel force copies without making the solver more standalone.
GAME_MODEL_DELETED_FORCE_BRIDGE_PATTERN = re.compile(
    r"\b(?:ApplyForces|ApplyWorldForces|SetWorldForce|SetImpulseForce|ClearImpulseForce)\s*\("
)
WORLD_ENVIRONMENT_DELETED_MODEL_FORCE_BRIDGE_PATTERN = re.compile(
    r"\bAddWorldForces\s*\("
    r"|\bGameObjects\s*::\s*GameModel\b"
    r"|\bclass\s+GameModel\s*;"
    r'|#\s*include\s+"(?:\.\./)?GameObjects/GameModel\.h"'
)
RIGID_BODY_DELETED_FORCE_BRIDGE_PATTERN = re.compile(
    r"\b(?:ApplyWorldForce|ApplyLinearForce|ApplyAngularForce|ApplyForces|ApplyImpulseForce|"
    r"SetWorldForce|SetImpulseForce|ClearImpulseForce|ZeroForce)\s*\("
    r"|\bm_(?:isForceApplied|appliedForce|forceApplicationPoint|worldForce|worldTorque|"
    r"linearAcceleration|angularAcceleration|torque)\b"
)
PHYSICS_BODY_STORE_PENDING_IMPULSE_MODEL_MIRROR_PATTERN = re.compile(
    r"\bmodel\s*\.\s*(?:SetImpulseForce|ClearImpulseForce)\s*\("
)
PHYSICS_DIAGNOSTICS_MODEL_RECORD_COMPAT_PATTERN = re.compile(
    r"\bmodelAccess\s*\.\s*TryGetPhysicsDiagnosticsModel\s*\(\s*[^,()]+\s*,\s*[^,()]+\s*\)"
)
# Why: diagnostics emitters now receive a store-owned frame input plus a cold
# name view. Letting them call back for full model records would recreate the
# deleted diagnostics bridge inside the sink/query path.
PHYSICS_DIAGNOSTICS_MODEL_RECORD_ACCESS_PATTERN = re.compile(
    r"\bmodelAccess\s*\.\s*TryGetPhysicsDiagnosticsModel\s*\("
)
# Why: collision-time CSV rows now receive the same cold name view as the
# regression/SkullScope emitters. Reopening PhysicsModelAccess here would put a
# model-owner lookup back inside the narrowphase diagnostics path.
PHYSICS_COLLISION_TIME_NAME_MODEL_ACCESS_PATTERN = re.compile(
    r"\b(?:PhysicsDiagnosticsSink::)?EmitCollisionTime\s*\(\s*PhysicsModelAccess\s*&"
    r"|\bPhysicsWorld::EmitPhysicsCollisionTime\s*\(\s*PhysicsModelAccess\s*&"
    r"|\bmodelAccess\s*\.\s*TryGetPhysicsDiagnosticsModelName\s*\("
)
PHYSICS_DIAGNOSTICS_MODEL_RECORD_SOURCES = (
    PHYSICS_DIAGNOSTICS_SINK_SOURCE,
    SKULL_SCOPE_SOURCE,
)
PHYSICS_COLLISION_TIME_NAME_MODEL_ACCESS_SOURCES = (
    PHYSICS_DIAGNOSTICS_SINK_SOURCE,
    PHYSICS_DIAGNOSTICS_SINK_HEADER,
    PHYSICS_WORLD_SOURCE,
    PHYSICS_WORLD_HEADER,
)
REPLAY_RECORDER_MODEL_STATE_CAPTURE_PATTERN = re.compile(
    r"\b(?:ShapeKindForModel\s*\(|[A-Za-z_]\w*\s*(?:->|\.)\s*"
    r"(?:GetReplayBodyId|GetCollisionShape|GetPosition|GetVelocity|GetAngularVelocity|GetOrientation|GetMass|"
    r"GetInvertedMass|GetRotationalInertia|GetInvertedRotationalInertia|IsFixed)\s*\()"
)
REPLAY_PREDICTION_CAPTURE_BODY_FUNCTION_PATTERN = re.compile(r"\bbool\s+CaptureReplayPredictionBodyState\s*\(")
REPLAY_PREDICTION_MODEL_STATE_CAPTURE_PATTERN = re.compile(
    r"\bmodel\s*(?:->|\.)\s*"
    r"(?:GetReplayBodyId|GetPosition|GetVelocity|GetAngularVelocity|GetOrientation|GetMass|"
    r"GetInvertedMass|GetRotationalInertia|GetInvertedRotationalInertia|IsFixed)\s*\("
    r"|\bmodelCollection\s*\.\s*GetPhysicsBodyStore\s*\("
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
PHYSICS_SCENE_PENDING_IMPULSE_FUNCTION_PATTERN = re.compile(
    r"\bvoid\s+PhysicsScene::SetPendingBodyImpulse\s*\(\s*PhysicsModelAccess\s*&"
)
PHYSICS_SCENE_PENDING_IMPULSE_MODEL_MIRROR_PATTERN = re.compile(
    r"\bmodelAccess\s*\.\s*(?:WriteBackPhysicsBody|InvalidatePhysicsStreams)\s*\("
)
PHYSICS_SCENE_SET_BODY_VELOCITY_FUNCTION_PATTERN = re.compile(r"\bbool\s+PhysicsScene::SetBodyVelocity\s*\(")
PHYSICS_SCENE_SET_BODY_VELOCITY_MODEL_MIRROR_PATTERN = re.compile(
    r"\bmodelAccess\s*\.\s*(?:WriteBackPhysicsBody|InvalidatePhysicsStreams)\s*\("
)
PHYSICS_VELOCITY_MODEL_ACCESS_SOURCES = (
    PHYSICS_ENGINE_SOURCE,
    PHYSICS_ENGINE_HEADER,
    PHYSICS_SCENE_SOURCE,
    PHYSICS_SCENE_HEADER,
    REPLAY_VELOCITY_EDIT_SOURCE,
)
PHYSICS_VELOCITY_MODEL_ACCESS_OVERLOAD_PATTERN = re.compile(
    r"\bSetBodyVelocity\s*\(\s*PhysicsModelAccess\s*&"
)
PHYSICS_VELOCITY_MODEL_ACCESS_CALL_PATTERN = re.compile(r"\bSetBodyVelocity\s*\(\s*modelAccess\s*,")
PHYSICS_SCENE_WAKE_BODY_FUNCTION_PATTERN = re.compile(
    r"\bvoid\s+PhysicsScene::WakeBody\s*\("
)
PHYSICS_SCENE_WAKE_BODY_MODEL_MIRROR_PATTERN = re.compile(
    r"\bmodelAccess\s*\.\s*(?:WriteBackPhysicsBody|InvalidatePhysicsStreams)\s*\("
)
PHYSICS_WAKE_APPLY_MODEL_ACCESS_SOURCES = (
    PHYSICS_ENGINE_SOURCE,
    PHYSICS_ENGINE_HEADER,
    PHYSICS_SCENE_SOURCE,
    PHYSICS_SCENE_HEADER,
    GAME_MODEL_COLLECTION_PHYSICS_ADAPTER_SOURCE,
    RUNTIME_TOOLS_SOURCE,
    EDITOR_TOOLS_SOURCE,
    RUN_FRAME_SOURCE,
)
PHYSICS_WAKE_APPLY_MODEL_ACCESS_OVERLOAD_PATTERN = re.compile(
    r"\b(?:WakeBody|ApplyBodyImpulse)\s*\(\s*PhysicsModelAccess\s*&"
)
PHYSICS_WAKE_APPLY_MODEL_ACCESS_CALL_PATTERN = re.compile(
    r"\b(?:(?:[A-Za-z_][A-Za-z0-9_\\.]*)\s*\.\s*)?(?:WakeBody|ApplyBodyImpulse)\s*\(\s*modelAccess\s*,"
)
PHYSICS_SEED_BODY_ASLEEP_MODEL_ACCESS_SOURCES = (
    PHYSICS_ENGINE_SOURCE,
    PHYSICS_ENGINE_HEADER,
    PHYSICS_SCENE_SOURCE,
    PHYSICS_SCENE_HEADER,
    RAGDOLL_SOURCE,
    EDITOR_TOOLS_SOURCE,
    GAME_MODEL_COLLECTION_PHYSICS_ADAPTER_SOURCE,
)
PHYSICS_SEED_BODY_ASLEEP_MODEL_ACCESS_OVERLOAD_PATTERN = re.compile(
    r"\bSeedBodyAsleep\s*\(\s*PhysicsModelAccess\s*&"
)
PHYSICS_SEED_BODY_ASLEEP_MODEL_ACCESS_CALL_PATTERN = re.compile(
    r"\b[A-Za-z_][A-Za-z0-9_\\.]*\s*\.\s*SeedBodyAsleep\s*\(\s*modelAccess\s*,"
)
PHYSICS_PENDING_IMPULSE_MODEL_ACCESS_SOURCES = (
    PHYSICS_ENGINE_SOURCE,
    PHYSICS_ENGINE_HEADER,
    PHYSICS_SCENE_SOURCE,
    PHYSICS_SCENE_HEADER,
    GAME_MODEL_COLLECTION_PHYSICS_ADAPTER_SOURCE,
)
PHYSICS_PENDING_IMPULSE_MODEL_ACCESS_OVERLOAD_PATTERN = re.compile(
    r"\bSetPendingBodyImpulse\s*\(\s*PhysicsModelAccess\s*&"
)
PHYSICS_PENDING_IMPULSE_MODEL_ACCESS_CALL_PATTERN = re.compile(
    r"\b[A-Za-z_][A-Za-z0-9_\\.]*\s*\.\s*SetPendingBodyImpulse\s*\(\s*modelAccess\s*,"
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
SCENE_SETUP_ADAPTER_BODY_LOOKUP_PATTERN = re.compile(
    r"\b(?:GameModelCollectionPhysicsAdapter\b|[A-Za-z_]\w*\s*\.\s*BodyHandleForModelIndex\s*\()"
)
SCENE_SETUP_PHYSICS_COMMAND_SOURCES = (
    SCENE_AUTHORED_SETUP_SOURCE,
    SCENE_GENERATED_SETUP_SOURCE,
)
EDITOR_MODEL_INDEX_PHYSICS_COMMAND_PATTERN = re.compile(
    r"\b(?:context\s*\.\s*models|collection)\s*\.\s*"
    r"(?:SetPendingBodyImpulse|SeedModelAsleep|WakeModel|ApplyBodyImpulse)\s*\("
)
EDITOR_ADAPTER_COMMAND_WRAPPER_PATTERN = re.compile(
    r"\b[A-Za-z_]\w*\s*\.\s*"
    r"(?:ApplyBodyImpulseForModelIndex|WakeBodyForModelIndex|SeedBodyAsleepForModelIndex|"
    r"SetPendingBodyImpulseForModelIndex)\s*\("
)
EDITOR_PHYSICS_COMMAND_SOURCES = (
    EDITOR_OBJECT_PLACEMENT_SOURCE,
    EDITOR_TOOLS_SOURCE,
)
MOUSE_PICKUP_MODEL_INDEX_PHYSICS_COMMAND_PATTERN = re.compile(
    r"\bm_cGameModelCollection\s*\.\s*"
    r"(?:SetPendingBodyImpulse|SeedModelAsleep|WakeModel|ApplyBodyImpulse|TrySetModelAngularVelocity)\s*\("
)
MOUSE_PICKUP_GAME_MODEL_BODY_READ_PATTERN = re.compile(
    r"\b[A-Za-z_]\w*\s*(?:->|\.)\s*(?:GetPosition|GetVelocity|GetAngularVelocity|IsFixed)\s*\("
)
LAUNCHER_MODEL_INDEX_PHYSICS_COMMAND_PATTERN = re.compile(
    r"\bcollection\s*\.\s*"
    r"(?:SetPendingBodyImpulse|SeedModelAsleep|WakeModel|ApplyBodyImpulse)\s*\("
)
LAUNCHER_ADAPTER_COMMAND_WRAPPER_PATTERN = re.compile(
    r"\b[A-Za-z_]\w*\s*\.\s*(?:ApplyBodyImpulseForModelIndex|WakeBodyForModelIndex)\s*\("
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
RUN_FRAME_REPLAY_EDITOR_TRANSFORM_ADAPTER_WAKE_PATTERN = re.compile(
    r"\bGameModelCollectionPhysicsAdapter\s*\(\s*m_cGameModelCollection\s*\)\s*"
    r"\.\s*WakeBodyForModelIndex\s*\(",
    re.S,
)
RAGDOLL_MODEL_INDEX_PHYSICS_COMMAND_PATTERN = re.compile(
    r"\bcollection\s*\.\s*(?:SeedModelAsleep|WakeModel|ApplyBodyImpulse|SetPendingBodyImpulse)\s*\("
)
DELETED_GAME_MODEL_COLLECTION_PHYSICS_WRAPPER_PATTERN = re.compile(
    r"\b(?:void\s+GameModelCollection\s*::\s*)?"
    r"(?:WakeModel|SeedModelAsleep|ApplyBodyImpulse|SetPendingBodyImpulse)\s*\("
)
DELETED_GAME_MODEL_COLLECTION_PHYSICS_ADAPTER_COMMAND_WRAPPER_PATTERN = re.compile(
    r"\b(?:void\s+)?(?:GameModelCollectionPhysicsAdapter\s*::\s*)?"
    r"(?:WakeBodyForModelIndex|SeedBodyAsleepForModelIndex|ApplyBodyImpulseForModelIndex|"
    r"SetPendingBodyImpulseForModelIndex)\s*\("
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
        "GameModel SoA stream cache",
        re.compile(r"\b(?:GameModelSoACache|GameModelStreamProvider|GameModelBodyStream|GameModelRenderStream)\b"),
        "Use PhysicsBodyStore, ColliderStore, or RenderInstanceStore records instead of reintroducing GameModel-derived SoA streams.",
    ),
    (
        "GameModel stream API",
        re.compile(r"\b(?:GetPhysicsBodyStream|GetBodyStream|GetRenderStream|PrepareRenderStreams|InvalidatePhysicsStreams)\s*\("),
        "Prepare or read authoritative stores directly; the deleted GameModel stream/cache API must not return.",
    ),
    (
        "GameModel SoA memory stat",
        re.compile(r"\bsoaCacheBytes\b"),
        "The GameModel SoA cache was deleted, so memory reporting should not carry a live stat for it.",
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
# Why: a descriptor field named around model indices quietly reintroduces the
# old vector-order authority even when the surrounding type avoids GameModel.
PUBLIC_PHYSICS_DESC_PATTERN = re.compile(r"\bstruct\s+[A-Za-z_]\w*Desc\b(?P<body>.*?)\n\s*\};", re.S)
PUBLIC_PHYSICS_DESC_MODEL_INDEX_FIELD_PATTERN = re.compile(
    r"\b(?:int|uint32_t|uint64_t|std\s*::\s*size_t|size_t)\s+\w*modelIndex\w*\b",
    re.I,
)
# Why: standalone implementation files can regress through local helpers even
# when the public facade stays clean.
STANDALONE_PHYSICS_GAME_OBJECT_PATTERN = re.compile(
    r"\b(?:GameObjects\s*::\s*)?GameModel\b|"
    r"\bstd\s*::\s*vector\s*<\s*(?:GameObjects\s*::\s*)?GameModel\b|"
    r"\b(?:modelAccess|physicsModelAccess)\s*\.\s*Models\s*\("
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


def check_public_physics_descriptor_model_index_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for desc_match in PUBLIC_PHYSICS_DESC_PATTERN.finditer(stripped):
        body = desc_match.group("body")
        body_start = desc_match.start("body")
        for field_match in PUBLIC_PHYSICS_DESC_MODEL_INDEX_FIELD_PATTERN.finditer(body):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, body_start + field_match.start()),
                    "public physics descriptor model-index field is blocked",
                    "Public descriptors should name PhysicsBodyHandle, PhysicsColliderHandle, scene object ids, or replay ids instead of model indices.",
                )
            )
    return errors


def check_standalone_physics_implementation_game_object_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name not in {source.name for source in STANDALONE_PHYSICS_IMPLEMENTATION_SOURCES}:
        return []

    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in STANDALONE_PHYSICS_GAME_OBJECT_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "standalone physics implementation game-object dependency is blocked",
                "PhysicsStandaloneWorld should stay on PhysicsBodyStore, ColliderStore, handles, descriptors, and value views.",
            )
        )
    return errors


def check_public_physics_facade_game_object_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in PUBLIC_PHYSICS_FACADE_HEADERS:
        path = repo / relative_path
        errors.extend(check_public_physics_facade_game_object_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_public_physics_descriptor_model_index_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in PUBLIC_PHYSICS_FACADE_HEADERS:
        path = repo / relative_path
        errors.extend(check_public_physics_descriptor_model_index_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_standalone_physics_implementation_game_object_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in STANDALONE_PHYSICS_IMPLEMENTATION_SOURCES:
        path = repo / relative_path
        errors.extend(check_standalone_physics_implementation_game_object_guardrails_text(path, path.read_text(encoding="utf-8")))
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


def check_physics_world_fixed_contact_notify_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name != "PhysicsWorld.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_WORLD_FIXED_CONTACT_NOTIFY_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "physics world fixed-contact presentation notify is blocked",
                (
                    "Fixed-contact highlights are GameModel presentation feedback; emit them from PhysicsScene "
                    "after PhysicsWorld exposes the compact body-index queue."
                ),
            )
        )
    return errors


def check_physics_world_fixed_contact_notify_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_WORLD_SOURCE
    return check_physics_world_fixed_contact_notify_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_physics_world_persistent_contact_tree_release_guardrails_text(
    path: Path, text: str
) -> list[BoundaryError]:
    if path.name != "PhysicsWorld.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    function_pattern = re.compile(r"\bvoid\s+PhysicsWorld::ApplyPersistentContactSideEffects\s*\([^{}]*\)\s*\{", re.S)
    for function_match in function_pattern.finditer(stripped):
        open_brace = stripped.find("{", function_match.start(), function_match.end())
        if open_brace < 0:
            continue
        close_brace = find_matching_close_brace(stripped, open_brace)
        for match in PHYSICS_WORLD_PERSISTENT_CONTACT_TREE_RELEASE_PATTERN.finditer(
            stripped, open_brace, close_brace
        ):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "physics world persistent-contact tree release is blocked",
                    (
                        "Persistent-contact fixed-tree release events must be applied at the PhysicsScene "
                        "store-owned edge, not through modelAccess inside PhysicsWorld side effects."
                    ),
                )
            )
    return errors


def check_physics_world_persistent_contact_tree_release_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_WORLD_SOURCE
    return check_physics_world_persistent_contact_tree_release_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_physics_world_tornado_release_model_access_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name != "PhysicsWorld.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    function_pattern = re.compile(r"\bvoid\s+PhysicsWorld::ApplyTornadoField\s*\([^{}]*\)\s*\{", re.S)
    for function_match in function_pattern.finditer(stripped):
        open_brace = stripped.find("{", function_match.start(), function_match.end())
        if open_brace < 0:
            continue
        close_brace = find_matching_close_brace(stripped, open_brace)
        for match in PHYSICS_WORLD_TORNADO_RELEASE_MODEL_ACCESS_PATTERN.finditer(stripped, open_brace, close_brace):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "physics world tornado release model access is blocked",
                    (
                        "Tornado fixed-tree release must mutate PhysicsBodyStore records and reuse a wake list "
                        "instead of writing through GameModel and reloading the body store."
                    ),
                )
            )
    return errors


def check_physics_world_tornado_release_model_access_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_WORLD_SOURCE
    return check_physics_world_tornado_release_model_access_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_physics_world_step_model_access_signature_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name not in { "PhysicsWorld.cpp", "PhysicsWorld.h" }:
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_WORLD_STEP_MODEL_ACCESS_SIGNATURE_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "physics world step model-access signature is blocked",
                (
                    "PhysicsWorld step helpers must take store-owned body/collider inputs directly; "
                    "PhysicsModelAccess belongs at the PhysicsScene compatibility edge."
                ),
            )
        )
    return errors


def check_physics_world_step_model_access_signature_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in (PHYSICS_WORLD_SOURCE, Path("SkullbonezSource/Physics/PhysicsWorld.h")):
        path = repo / relative_path
        errors.extend(check_physics_world_step_model_access_signature_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_physics_engine_step_model_access_signature_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name not in { "PhysicsEngine.cpp", "PhysicsEngine.h" }:
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_ENGINE_STEP_MODEL_ACCESS_SIGNATURE_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "physics engine step model-access signature is blocked",
                (
                    "PhysicsEngine::Step must step owned stores directly; model import/export belongs at "
                    "the GameModelCollection compatibility edge."
                ),
            )
        )
    return errors


def check_physics_engine_step_model_access_signature_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in ( PHYSICS_ENGINE_SOURCE, PHYSICS_ENGINE_HEADER ):
        path = repo / relative_path
        errors.extend(
            check_physics_engine_step_model_access_signature_guardrails_text(
                path,
                path.read_text(encoding="utf-8"),
            )
        )
    return errors


def check_physics_scene_step_model_access_signature_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name not in { "PhysicsScene.cpp", "PhysicsScene.h" }:
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_SCENE_STEP_MODEL_ACCESS_SIGNATURE_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "physics scene step model-access signature is blocked",
                (
                    "PhysicsScene::RunPhysics must consume PhysicsBodyStore and ColliderStore ownership only; "
                    "GameModel presentation sync belongs outside the scene step."
                ),
            )
        )
    return errors


def check_physics_scene_step_model_access_signature_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in ( PHYSICS_SCENE_SOURCE, PHYSICS_SCENE_HEADER ):
        path = repo / relative_path
        errors.extend(
            check_physics_scene_step_model_access_signature_guardrails_text(
                path,
                path.read_text(encoding="utf-8"),
            )
        )
    return errors


def check_simulation_system_owner_borrow_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name not in { "SimulationSystem.cpp", "SimulationSystem.h" }:
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in SIMULATION_SYSTEM_OWNER_BORROW_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "simulation scheduler physics-owner borrow is blocked",
                (
                    "SimulationSystem is a tick-count scheduler only; runtime owners must execute returned "
                    "physics steps through their real model/store boundary."
                ),
            )
        )
    return errors


def check_simulation_system_owner_borrow_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in ( PHYSICS_ROOT / "SimulationSystem.cpp", PHYSICS_ROOT / "SimulationSystem.h" ):
        path = repo / relative_path
        errors.extend(check_simulation_system_owner_borrow_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_physics_model_access_deleted_step_facade_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name not in { "PhysicsModelAccess.h", "GameModelCollection.cpp" }:
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    pattern = (
        PHYSICS_MODEL_ACCESS_DELETED_STEP_FACADE_DEFINITION_PATTERN
        if path.name == "GameModelCollection.cpp"
        else PHYSICS_MODEL_ACCESS_DELETED_STEP_FACADE_PATTERN
    )
    errors: list[BoundaryError] = []
    for match in pattern.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "deleted PhysicsModelAccess step facade surface is blocked",
                (
                    "PhysicsModelAccess is now restricted to model-owned store refresh; step writeback, "
                    "presentation events, diagnostics names, and body streams must stay with their real owners."
                ),
            )
        )
    return errors


def check_physics_model_access_deleted_step_facade_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in ( PHYSICS_ROOT / "PhysicsModelAccess.h", GAME_MODEL_COLLECTION_SOURCE ):
        path = repo / relative_path
        errors.extend(
            check_physics_model_access_deleted_step_facade_guardrails_text(
                path,
                path.read_text(encoding="utf-8"),
            )
        )
    return errors


def check_physics_world_store_seed_model_access_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name != "PhysicsWorld.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    function_pattern = re.compile(
        r"\bvoid\s+PhysicsWorld::SeedModelAsleep\s*\([^{}]*(?:const\s+PhysicsBodyStore&|std::vector<PhysicsBodyRecord>)[^{}]*\)\s*\{",
        re.S,
    )
    for function_match in function_pattern.finditer(stripped):
        open_brace = stripped.find("{", function_match.start(), function_match.end())
        if open_brace < 0:
            continue
        close_brace = find_matching_close_brace(stripped, open_brace)
        for match in PHYSICS_WORLD_STORE_SEED_MODEL_ACCESS_PATTERN.finditer(stripped, open_brace, close_brace):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "physics world store seed model access is blocked",
                    (
                        "Store-owned sleep seeding must read PhysicsBodyStore/body records directly and leave "
                        "GameModel stream invalidation to the PhysicsScene compatibility edge."
                    ),
                )
            )
    return errors


def check_physics_world_store_seed_model_access_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_WORLD_SOURCE
    return check_physics_world_store_seed_model_access_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_physics_world_store_wake_model_access_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name != "PhysicsWorld.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    function_pattern = re.compile(
        r"\bvoid\s+PhysicsWorld::"
        r"(?:WakeModel|WakeSleepVisualIsland|WakePointJointIsland|WakeRestingContactIsland|WakePointJointConnectedBodies)"
        r"\s*\([^{}]*(?:PhysicsBodyStore&|std::vector<PhysicsBodyRecord>|int\s+bodyCount)[^{}]*\)\s*\{",
        re.S,
    )
    for function_match in function_pattern.finditer(stripped):
        open_brace = stripped.find("{", function_match.start(), function_match.end())
        if open_brace < 0:
            continue
        close_brace = find_matching_close_brace(stripped, open_brace)
        for match in PHYSICS_WORLD_STORE_WAKE_MODEL_ACCESS_SIGNATURE_PATTERN.finditer(
            stripped, function_match.start(), function_match.end()
        ):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "physics world store wake model access is blocked",
                    (
                        "Store-owned wake propagation must operate on PhysicsBodyStore/body records and leave "
                        "GameModel stream invalidation to PhysicsScene."
                    ),
                )
            )
        for match in PHYSICS_WORLD_STORE_WAKE_MODEL_ACCESS_PATTERN.finditer(stripped, open_brace, close_brace):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "physics world store wake model access is blocked",
                    (
                        "Store-owned wake propagation must operate on PhysicsBodyStore/body records and leave "
                        "GameModel stream invalidation to PhysicsScene."
                    ),
                )
            )
    return errors


def check_physics_world_store_wake_model_access_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_WORLD_SOURCE
    return check_physics_world_store_wake_model_access_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_physics_world_deleted_model_stream_wake_seed_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name not in {"PhysicsWorld.cpp", "PhysicsWorld.h"}:
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_WORLD_DELETED_MODEL_STREAM_WAKE_SEED_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "physics world legacy model-stream wake/seed path is deleted",
                (
                    "PhysicsWorld wake/seed commands must use PhysicsBodyStore/body records; "
                    "model-stream compatibility belongs at the PhysicsScene edge."
                ),
            )
        )
    return errors


def check_physics_world_deleted_model_stream_wake_seed_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in (PHYSICS_WORLD_SOURCE, PHYSICS_WORLD_HEADER):
        path = repo / relative_path
        errors.extend(
            check_physics_world_deleted_model_stream_wake_seed_guardrails_text(
                path,
                path.read_text(encoding="utf-8"),
            )
        )
    return errors


def check_physics_world_run_invalidation_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name != "PhysicsWorld.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    function_pattern = re.compile(r"\bvoid\s+PhysicsWorld::RunPhysics\s*\([^{}]*\)\s*\{", re.S)
    for function_match in function_pattern.finditer(stripped):
        open_brace = stripped.find("{", function_match.start(), function_match.end())
        if open_brace < 0:
            continue
        close_brace = find_matching_close_brace(stripped, open_brace)
        for match in PHYSICS_WORLD_RUN_PHYSICS_INVALIDATION_PATTERN.finditer(stripped, open_brace, close_brace):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "physics world model stream invalidation is blocked",
                    (
                        "Invalidate GameModel SoA streams at the PhysicsScene compatibility edge after "
                        "PhysicsWorld::RunPhysics returns."
                    ),
                )
            )
    return errors


def check_physics_world_run_invalidation_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_WORLD_SOURCE
    return check_physics_world_run_invalidation_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_physics_world_run_writeback_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name != "PhysicsWorld.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    function_pattern = re.compile(r"\bvoid\s+PhysicsWorld::RunPhysics\s*\([^{}]*\)\s*\{", re.S)
    for function_match in function_pattern.finditer(stripped):
        open_brace = stripped.find("{", function_match.start(), function_match.end())
        if open_brace < 0:
            continue
        close_brace = find_matching_close_brace(stripped, open_brace)
        for match in PHYSICS_WORLD_RUN_PHYSICS_WRITEBACK_PATTERN.finditer(stripped, open_brace, close_brace):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "physics world bulk model writeback is blocked",
                    (
                        "Mirror solved PhysicsBodyStore state back to GameModel at the PhysicsScene compatibility "
                        "edge instead of inside PhysicsWorld::RunPhysics."
                    ),
                )
            )
    return errors


def check_physics_world_run_writeback_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_WORLD_SOURCE
    return check_physics_world_run_writeback_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_physics_world_run_diagnostics_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name != "PhysicsWorld.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    function_pattern = re.compile(r"\bvoid\s+PhysicsWorld::RunPhysics\s*\([^{}]*\)\s*\{", re.S)
    for function_match in function_pattern.finditer(stripped):
        open_brace = stripped.find("{", function_match.start(), function_match.end())
        if open_brace < 0:
            continue
        close_brace = find_matching_close_brace(stripped, open_brace)
        for match in PHYSICS_WORLD_RUN_PHYSICS_DIAGNOSTICS_PATTERN.finditer(stripped, open_brace, close_brace):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "physics world run diagnostics emission is blocked",
                    (
                        "Emit step diagnostics from the PhysicsScene-owned step edge after scene-side store "
                        "effects have been applied."
                    ),
                )
            )
    return errors


def check_physics_world_run_diagnostics_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_WORLD_SOURCE
    return check_physics_world_run_diagnostics_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_physics_world_step_diagnostics_model_access_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name not in { "PhysicsWorld.cpp", "PhysicsWorld.h" }:
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_WORLD_STEP_DIAGNOSTICS_MODEL_ACCESS_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "physics world step diagnostics model access is blocked",
                (
                    "PhysicsScene owns the cold diagnostics name overlay; PhysicsWorld step diagnostics "
                    "must consume store-owned views plus names, not borrow PhysicsModelAccess."
                ),
            )
        )
    return errors


def check_physics_world_step_diagnostics_model_access_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in ( PHYSICS_WORLD_SOURCE, PHYSICS_WORLD_HEADER ):
        path = repo / relative_path
        errors.extend(check_physics_world_step_diagnostics_model_access_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_render_instance_store_authority_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    if path.name == "GameModelCollection.cpp":
        for match in RENDER_INSTANCE_MODEL_REFRESH_PATTERN.finditer(stripped):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "render instance model-transform refresh is blocked",
                    "Refresh render instances from PhysicsBodyStore and ColliderStore so draw poses do not depend on the post-solve GameModel mirror.",
                )
            )
    if path.name in { "RenderInstanceStore.cpp", "RenderInstanceStore.h" }:
        for match in RENDER_INSTANCE_STORE_MODEL_ONLY_REFRESH_SIGNATURE_PATTERN.finditer(stripped):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "RenderInstanceStore model-only refresh overload is blocked",
                    (
                        "RenderInstanceStore refresh must be backed by PhysicsBodyStore and ColliderStore; "
                        "do not restore GameModel-owned transform refresh overloads."
                    ),
                )
            )
    if path.name == "RenderInstanceStore.cpp":
        for match in RENDER_INSTANCE_STORE_MODEL_REFRESH_FALLBACK_PATTERN.finditer(stripped):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "RenderInstanceStore GameModel fallback refresh is blocked",
                    (
                        "Topology mismatches should fail closed instead of rebuilding render transforms "
                        "from GameModel and hiding store-authority regressions."
                    ),
                )
            )
    return errors


def check_render_instance_store_authority_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in (
        GAME_MODEL_COLLECTION_SOURCE,
        Path("SkullbonezSource/Rendering/RenderInstanceStore.cpp"),
        Path("SkullbonezSource/Rendering/RenderInstanceStore.h"),
    ):
        path = repo / relative_path
        errors.extend(check_render_instance_store_authority_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_game_model_renderer_render_instance_authority_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name != "GameModelRenderer.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in GAME_MODEL_RENDERER_MODEL_POSE_STREAM_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "GameModelRenderer model-pose stream read is blocked",
                (
                    "Object rendering should read RenderInstanceStore records for transforms, fixed flags, "
                    "material highlights, shape kind, and bounds instead of rebuilding GameModel SoA pose streams."
                ),
            )
        )
    return errors


def check_game_model_renderer_render_instance_authority_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / Path("SkullbonezSource/Rendering/GameModelRenderer.cpp")
    return check_game_model_renderer_render_instance_authority_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_dxr_render_instance_matrix_authority_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name != "GameModelCollection.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    function_pattern = re.compile(
        r"\bint\s+GameModelCollection::CopyDxrModelMatrices\s*\([^{}]*\)\s*\{",
        re.S,
    )
    for function_match in function_pattern.finditer(stripped):
        open_brace = stripped.find("{", function_match.start(), function_match.end())
        if open_brace < 0:
            continue
        close_brace = find_matching_close_brace(stripped, open_brace)
        for match in DXR_MODEL_MATRIX_RENDER_INSTANCE_PATTERN.finditer(stripped, open_brace, close_brace):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "DXR model-matrix upload must use render instances",
                    (
                        "CopyDxrModelMatrices should copy RenderInstanceStore matrices from the prepared "
                        "physics-backed snapshot instead of recomputing GameModel model matrices."
                    ),
                )
            )
    return errors


def check_dxr_render_instance_matrix_authority_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / GAME_MODEL_COLLECTION_SOURCE
    return check_dxr_render_instance_matrix_authority_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_deleted_game_model_stream_project_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for match in DELETED_GAME_MODEL_STREAM_PROJECT_PATTERN.finditer(text):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(text, match.start()),
                "deleted GameModel stream/cache file is blocked",
                (
                    "GameModelStreams and GameModelSoACache were deleted after render/physics consumers moved "
                    "to store-backed snapshots; do not add them back to the project."
                ),
            )
        )
    return errors


def check_deleted_game_model_stream_project_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in ( Path("SKULLBONEZ_CORE.vcxproj"), Path("SKULLBONEZ_CORE.vcxproj.filters") ):
        path = repo / relative_path
        errors.extend(check_deleted_game_model_stream_project_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_game_model_collection_body_store_read_authority_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name != "GameModelCollection.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []

    refresh_function_pattern = re.compile(
        r"\bconst\s+SkullbonezCore::Physics::PhysicsBodyStore&\s+"
        r"GameModelCollection::GetPhysicsBodyStore\s*\(\s*\)\s*\{",
        re.S,
    )
    for function_match in refresh_function_pattern.finditer(stripped):
        open_brace = stripped.find("{", function_match.start(), function_match.end())
        if open_brace < 0:
            continue
        close_brace = find_matching_close_brace(stripped, open_brace)
        function_body = stripped[open_brace:close_brace]
        if GAME_MODEL_COLLECTION_BODY_STORE_REFRESH_PATTERN.search(function_body) and not (
            GAME_MODEL_COLLECTION_BODY_STORE_COUNT_GATE_PATTERN.search(function_body)
        ):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, function_match.start()),
                    "body-store read accessor must not unconditionally refresh from GameModel",
                    (
                        "GetPhysicsBodyStore may repair topology drift, but same-count reads must preserve "
                        "PhysicsBodyStore authority instead of reloading the GameModel compatibility mirror."
                    ),
                )
            )

    collider_function_pattern = re.compile(
        r"\bconst\s+SkullbonezCore::Physics::ColliderStore&\s+"
        r"GameModelCollection::GetColliderStore\s*\(\s*\)\s*\{",
        re.S,
    )
    for function_match in collider_function_pattern.finditer(stripped):
        open_brace = stripped.find("{", function_match.start(), function_match.end())
        if open_brace < 0:
            continue
        close_brace = find_matching_close_brace(stripped, open_brace)
        for match in GAME_MODEL_COLLECTION_COLLIDER_STORE_REFRESH_PATTERN.finditer(stripped, open_brace, close_brace):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "collider-store read accessor must preserve body-store authority",
                    (
                        "GetColliderStore may refresh collider shape/material snapshots, but it must not call "
                        "RefreshColliderStore because that path reloads same-count body rows from GameModel."
                    ),
                )
            )

    for function_name in ( "GetModelPosition", "GetSceneKineticEnergy" ):
        function_pattern = re.compile(rf"\b(?:Vector3|double)\s+GameModelCollection::{function_name}\s*\([^{{}}]*\)\s*\{{", re.S)
        for function_match in function_pattern.finditer(stripped):
            open_brace = stripped.find("{", function_match.start(), function_match.end())
            if open_brace < 0:
                continue
            close_brace = find_matching_close_brace(stripped, open_brace)
            for match in GAME_MODEL_COLLECTION_BODY_READ_MODEL_FIELD_PATTERN.finditer(stripped, open_brace, close_brace):
                errors.append(
                    BoundaryError(
                        path,
                        line_for_offset(stripped, match.start()),
                        "GameModelCollection body read should use PhysicsBodyStore",
                        (
                            "Read camera-follow pose and scene energy from PhysicsBodyStore records so the "
                            "post-step GameModel mirror is not required for runtime presentation statistics."
                        ),
                    )
                )
    return errors


def check_game_model_collection_body_store_read_authority_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / GAME_MODEL_COLLECTION_SOURCE
    return check_game_model_collection_body_store_read_authority_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_game_model_collection_run_physics_model_access_guardrails_text(
    path: Path,
    text: str,
) -> list[BoundaryError]:
    if path.name != "GameModelCollection.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    function_match = GAME_MODEL_COLLECTION_RUN_PHYSICS_FUNCTION_PATTERN.search(stripped)
    if not function_match:
        return errors

    open_brace = stripped.find("{", function_match.end())
    if open_brace < 0:
        return errors
    close_brace = find_matching_close_brace(stripped, open_brace)

    for match in GAME_MODEL_COLLECTION_RUN_PHYSICS_MODEL_ACCESS_DECL_PATTERN.finditer(
        stripped,
        open_brace,
        close_brace,
    ):
        preceding_function_text = stripped[open_brace:match.start()]
        brace_depth = preceding_function_text.count("{") - preceding_function_text.count("}")
        if brace_depth <= 1:
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "steady physics step model-access facade is blocked",
                    (
                        "GameModelCollection::RunPhysics should create PhysicsModelAccess only inside topology "
                        "repair branches, so steady frames step PhysicsBodyStore and ColliderStore without "
                        "borrowing the GameModel owner."
                    ),
                )
            )
    return errors


def check_game_model_collection_run_physics_model_access_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / GAME_MODEL_COLLECTION_SOURCE
    return check_game_model_collection_run_physics_model_access_guardrails_text(
        path,
        path.read_text(encoding="utf-8"),
    )


def check_runtime_pick_service_store_authority_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name not in { "RuntimePickService.cpp", "RuntimePickService.h" }:
        return []
    code_without_comments = strip_cpp_comments(text)
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []

    for match in RUNTIME_PICK_SERVICE_GAME_MODEL_INCLUDE_PATTERN.finditer(code_without_comments):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(code_without_comments, match.start()),
                "RuntimePickService must use physics stores for body state",
                (
                    "The central pick service should scan PhysicsBodyStore and ColliderStore records, not "
                    "include GameModel and require callers to refresh the compatibility mirror before picking."
                ),
            )
        )

    for match in RUNTIME_PICK_SERVICE_MODEL_STATE_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "RuntimePickService must use physics stores for body state",
                (
                    "The central pick service should scan PhysicsBodyStore and ColliderStore records, not "
                    "GameModel position/orientation/fixed/shape fields."
                ),
            )
        )

    return errors


def check_runtime_pick_service_store_authority_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in (
        Path("SkullbonezSource/Runtime/RuntimePickService.cpp"),
        Path("SkullbonezSource/Runtime/RuntimePickService.h"),
    ):
        path = repo / relative_path
        errors.extend(check_runtime_pick_service_store_authority_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_scene_snapshot_store_authority_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name != "SceneSnapshotWriter.cpp":
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in SCENE_SNAPSHOT_GAME_MODEL_PHYSICS_READ_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "scene snapshot physics state must use stores",
                (
                    "SceneSnapshotWriter should serialize live body pose, velocity, fixed/sleep state, mass, "
                    "inertia, restitution, and shapes from PhysicsBodyStore/ColliderStore instead of the "
                    "post-step GameModel compatibility mirror."
                ),
            )
        )
    return errors


def check_scene_snapshot_store_authority_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / Path("SkullbonezSource/Scene/SceneSnapshotWriter.cpp")
    return check_scene_snapshot_store_authority_guardrails_text(path, path.read_text(encoding="utf-8"))


def check_deleted_model_force_bridge_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    code_without_comments = strip_cpp_comments(text)
    errors: list[BoundaryError] = []

    if path.name in { "GameModel.cpp", "GameModel.h" }:
        for match in GAME_MODEL_DELETED_FORCE_BRIDGE_PATTERN.finditer(stripped):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "deleted GameModel force bridge is blocked",
                    (
                        "GameModel must not expose model-side force integration; PhysicsBodyStore owns "
                        "world-force and pending-impulse integration."
                    ),
                )
            )
        return errors

    if path.name in { "WorldEnvironment.cpp", "WorldEnvironment.h" }:
        for match in WORLD_ENVIRONMENT_DELETED_MODEL_FORCE_BRIDGE_PATTERN.finditer(code_without_comments):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(code_without_comments, match.start()),
                    "deleted WorldEnvironment model force bridge is blocked",
                    (
                        "WorldEnvironment should expose scalar PhysicsWorldForces only; model-specific water/drag "
                        "integration belongs in PhysicsBodyStore records."
                    ),
                )
            )
        return errors

    if path.name in { "RigidBody.cpp", "RigidBody.h" }:
        for match in RIGID_BODY_DELETED_FORCE_BRIDGE_PATTERN.finditer(stripped):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "deleted RigidBody force bridge is blocked",
                    (
                        "RigidBody is legacy GameModel storage now; force accumulators and integration wrappers "
                        "belong in PhysicsBodyStore."
                    ),
                )
            )
        return errors

    if path.name == "PhysicsBodyStore.cpp":
        for match in PHYSICS_BODY_STORE_PENDING_IMPULSE_MODEL_MIRROR_PATTERN.finditer(stripped):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "pending impulses must not mirror into GameModel",
                    (
                        "PhysicsBodyStore owns pending impulses; bulk compatibility writeback should not copy "
                        "them into GameModel."
                    ),
                )
            )
    return errors


def check_deleted_model_force_bridge_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in (
        Path("SkullbonezSource/GameObjects/GameModel.cpp"),
        Path("SkullbonezSource/GameObjects/GameModel.h"),
        Path("SkullbonezSource/World/WorldEnvironment.cpp"),
        Path("SkullbonezSource/World/WorldEnvironment.h"),
        Path("SkullbonezSource/Physics/RigidBody.cpp"),
        Path("SkullbonezSource/Physics/RigidBody.h"),
        Path("SkullbonezSource/Physics/PhysicsBodyStore.cpp"),
    ):
        path = repo / relative_path
        errors.extend(check_deleted_model_force_bridge_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_physics_diagnostics_store_authority_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    if path.name in {PHYSICS_DIAGNOSTICS_SINK_SOURCE.name, SKULL_SCOPE_SOURCE.name}:
        for match in PHYSICS_DIAGNOSTICS_MODEL_RECORD_ACCESS_PATTERN.finditer(stripped):
            errors.append(
                BoundaryError(
                    path,
                    line_for_offset(stripped, match.start()),
                    "physics diagnostics model-record access is deleted",
                    (
                        "Physics diagnostics emitters should consume PhysicsDiagnosticsFrameInput, "
                        "PhysicsBodyStore, ColliderStore, and the name view instead of requesting "
                        "model-access diagnostics records."
                    ),
                )
            )
        return errors
    for match in PHYSICS_DIAGNOSTICS_MODEL_RECORD_COMPAT_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "physics diagnostics model-state mirror read is blocked",
                (
                    "Pass PhysicsBodyStore and ColliderStore to TryGetPhysicsDiagnosticsModel; "
                    "use PhysicsDiagnosticsNameView for name-only logs."
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


def check_physics_collision_time_name_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    if path.name not in {source.name for source in PHYSICS_COLLISION_TIME_NAME_MODEL_ACCESS_SOURCES}:
        return []
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_COLLISION_TIME_NAME_MODEL_ACCESS_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "physics collision-time diagnostics model-name access is deleted",
                (
                    "Collision-time diagnostics must consume PhysicsDiagnosticsNameView-style name tables "
                    "prepared by PhysicsScene, not borrow PhysicsModelAccess from PhysicsWorld or the sink."
                ),
            )
        )
    return errors


def check_physics_collision_time_name_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in PHYSICS_COLLISION_TIME_NAME_MODEL_ACCESS_SOURCES:
        path = repo / relative_path
        errors.extend(check_physics_collision_time_name_guardrails_text(path, path.read_text(encoding="utf-8")))
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


def check_replay_prediction_body_capture_store_authority_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    bounds = _function_body_bounds(stripped, REPLAY_PREDICTION_CAPTURE_BODY_FUNCTION_PATTERN)
    if not bounds:
        return errors

    open_brace, close_brace = bounds
    for match in REPLAY_PREDICTION_MODEL_STATE_CAPTURE_PATTERN.finditer(stripped, open_brace, close_brace):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "replay prediction model-state capture is blocked",
                (
                    "Replay prediction backup must read physics state from PhysicsBodyStore records. GameModel may "
                    "supply presentation-only timers, but not pose, velocity, mass, inertia, fixed state, replay id, "
                    "or a model-refreshing body-store accessor."
                ),
            )
        )
    return errors


def check_replay_prediction_body_capture_store_authority_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / REPLAY_PREDICTION_HELPERS_SOURCE
    return check_replay_prediction_body_capture_store_authority_guardrails_text(
        path,
        path.read_text(encoding="utf-8"),
    )


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


def check_physics_scene_pending_impulse_model_mirror_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    bounds = _function_body_bounds(stripped, PHYSICS_SCENE_PENDING_IMPULSE_FUNCTION_PATTERN)
    if not bounds:
        return errors

    open_brace, close_brace = bounds
    for match in PHYSICS_SCENE_PENDING_IMPULSE_MODEL_MIRROR_PATTERN.finditer(stripped, open_brace, close_brace):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "pending impulse model mirror is blocked",
                (
                    "PhysicsScene::SetPendingBodyImpulse should mutate PhysicsBodyStore only; ApplyBodyImpulse "
                    "owns any wake or presentation compatibility mirror."
                ),
            )
        )
    return errors


def check_physics_scene_pending_impulse_model_mirror_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_SCENE_SOURCE
    return check_physics_scene_pending_impulse_model_mirror_guardrails_text(
        path,
        path.read_text(encoding="utf-8"),
    )


def check_physics_scene_velocity_model_mirror_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    bounds = _function_body_bounds(stripped, PHYSICS_SCENE_SET_BODY_VELOCITY_FUNCTION_PATTERN)
    if not bounds:
        return errors

    open_brace, close_brace = bounds
    for match in PHYSICS_SCENE_SET_BODY_VELOCITY_MODEL_MIRROR_PATTERN.finditer(stripped, open_brace, close_brace):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "velocity edit model mirror is blocked",
                (
                    "PhysicsScene::SetBodyVelocity should leave replay/editor velocity edits in "
                    "PhysicsBodyStore; prediction and the next step own the remaining presentation projection."
                ),
            )
        )
    return errors


def check_physics_scene_velocity_model_mirror_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_SCENE_SOURCE
    return check_physics_scene_velocity_model_mirror_guardrails_text(
        path,
        path.read_text(encoding="utf-8"),
    )


def check_physics_velocity_model_access_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_VELOCITY_MODEL_ACCESS_OVERLOAD_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "velocity model-access overload is blocked",
                (
                    "SetBodyVelocity should be a store-owned PhysicsBodyHandle command; "
                    "legacy model-index callers must refresh topology before resolving the handle."
                ),
            )
        )
    for match in PHYSICS_VELOCITY_MODEL_ACCESS_CALL_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "velocity model-access call is blocked",
                (
                    "Call SetBodyVelocity(body, linearVelocity, angularVelocity, wakeIfMoving) after resolving a "
                    "PhysicsBodyHandle; do not borrow PhysicsModelAccess for velocity edits."
                ),
            )
        )
    return errors


def check_physics_velocity_model_access_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in PHYSICS_VELOCITY_MODEL_ACCESS_SOURCES:
        path = repo / relative_path
        errors.extend(check_physics_velocity_model_access_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_physics_scene_wake_body_model_mirror_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    bounds = _function_body_bounds(stripped, PHYSICS_SCENE_WAKE_BODY_FUNCTION_PATTERN)
    if not bounds:
        return errors

    open_brace, close_brace = bounds
    for match in PHYSICS_SCENE_WAKE_BODY_MODEL_MIRROR_PATTERN.finditer(stripped, open_brace, close_brace):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "wake command model mirror is blocked",
                (
                    "PhysicsScene::WakeBody should update PhysicsWorld and PhysicsBodyStore sleep state only; "
                    "the normal step boundary owns any later GameModel projection."
                ),
            )
        )
    return errors


def check_physics_scene_wake_body_model_mirror_guardrails(repo: Path) -> list[BoundaryError]:
    path = repo / PHYSICS_SCENE_SOURCE
    return check_physics_scene_wake_body_model_mirror_guardrails_text(
        path,
        path.read_text(encoding="utf-8"),
    )


def check_physics_wake_apply_model_access_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_WAKE_APPLY_MODEL_ACCESS_OVERLOAD_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "wake/apply model-access overload is blocked",
                (
                    "WakeBody and ApplyBodyImpulse should be store-owned PhysicsBodyHandle commands; "
                    "legacy model-index callers must refresh topology in GameModelCollectionPhysicsAdapter."
                ),
            )
        )
    for match in PHYSICS_WAKE_APPLY_MODEL_ACCESS_CALL_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "wake/apply model-access call is blocked",
                (
                    "Call WakeBody(body) or ApplyBodyImpulse(body, impulse, point) after resolving a "
                    "PhysicsBodyHandle; do not borrow PhysicsModelAccess for wake/apply commands."
                ),
            )
        )
    return errors


def check_physics_wake_apply_model_access_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in PHYSICS_WAKE_APPLY_MODEL_ACCESS_SOURCES:
        path = repo / relative_path
        errors.extend(check_physics_wake_apply_model_access_guardrails_text(path, path.read_text(encoding="utf-8")))
    return errors


def check_physics_seed_body_asleep_model_access_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_SEED_BODY_ASLEEP_MODEL_ACCESS_OVERLOAD_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "sleep seed model-access overload is blocked",
                (
                    "SeedBodyAsleep should be a store-owned PhysicsBodyHandle command; "
                    "do not reintroduce the PhysicsModelAccess overload or synchronous GameModel mirror."
                ),
            )
        )
    for match in PHYSICS_SEED_BODY_ASLEEP_MODEL_ACCESS_CALL_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "sleep seed model-access call is blocked",
                (
                    "Call SeedBodyAsleep(body) so sleep seeding stays in PhysicsBodyStore/PhysicsWorld "
                    "without per-command GameModel writeback or stream invalidation."
                ),
            )
        )
    return errors


def check_physics_seed_body_asleep_model_access_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in PHYSICS_SEED_BODY_ASLEEP_MODEL_ACCESS_SOURCES:
        path = repo / relative_path
        errors.extend(
            check_physics_seed_body_asleep_model_access_guardrails_text(path, path.read_text(encoding="utf-8"))
        )
    return errors


def check_physics_pending_impulse_model_access_guardrails_text(path: Path, text: str) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in PHYSICS_PENDING_IMPULSE_MODEL_ACCESS_OVERLOAD_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "pending impulse model-access overload is blocked",
                (
                    "SetPendingBodyImpulse should be a store-owned PhysicsBodyHandle command; "
                    "do not reintroduce the PhysicsModelAccess overload or model-refresh path."
                ),
            )
        )
    for match in PHYSICS_PENDING_IMPULSE_MODEL_ACCESS_CALL_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "pending impulse model-access call is blocked",
                (
                    "Call SetPendingBodyImpulse(body, impulse, point) so pending solver input stays in "
                    "PhysicsBodyStore without borrowing the model owner."
                ),
            )
        )
    return errors


def check_physics_pending_impulse_model_access_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for relative_path in PHYSICS_PENDING_IMPULSE_MODEL_ACCESS_SOURCES:
        path = repo / relative_path
        errors.extend(
            check_physics_pending_impulse_model_access_guardrails_text(path, path.read_text(encoding="utf-8"))
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
    for match in SCENE_SETUP_ADAPTER_BODY_LOOKUP_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "scene setup adapter body lookup is blocked",
                (
                    "Scene construction receives PhysicsBodyHandle from AddGameModel's append-time body registration; "
                    "authored/generated setup must not re-resolve a just-created model through "
                    "GameModelCollectionPhysicsAdapter."
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
    for match in EDITOR_ADAPTER_COMMAND_WRAPPER_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "editor adapter command wrapper is blocked",
                (
                    "Editor commands should resolve PhysicsBodyHandle at the editor boundary, then call "
                    "PhysicsEngine directly instead of hiding mutation behind adapter model-index command wrappers."
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
    for match in MOUSE_PICKUP_GAME_MODEL_BODY_READ_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "mouse pickup GameModel body read is blocked",
                (
                    "Mouse pickup should read body position, velocity, and fixed state through "
                    "PhysicsBodyStore using the picked PhysicsBodyHandle; keep modelIndex only for "
                    "interaction identity and stale-slot validation."
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
    for match in LAUNCHER_ADAPTER_COMMAND_WRAPPER_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "launcher adapter command wrapper is blocked",
                (
                    "Launcher tools should resolve a wake-ready PhysicsBodyHandle at the tool boundary, then call "
                    "PhysicsEngine directly instead of hiding mutation behind adapter model-index command wrappers."
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
    for match in RUN_FRAME_REPLAY_EDITOR_TRANSFORM_ADAPTER_WAKE_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "RunFrame replay editor transform adapter wake wrapper is blocked",
                (
                    "Replay editor-transform restore should resolve a wake-ready PhysicsBodyHandle at the replay "
                    "boundary, then call PhysicsEngine directly instead of hiding mutation behind the adapter "
                    "model-index wake command wrapper."
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
                    "resolve PhysicsBodyHandle at their boundary, using GameModelCollectionPhysicsAdapter only as "
                    "a legacy identity-to-handle resolver while model identity is still being migrated."
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


def check_deleted_game_model_collection_physics_adapter_command_guardrails_text(
    path: Path,
    text: str,
) -> list[BoundaryError]:
    stripped = strip_cpp_comments_and_string_literals(text)
    errors: list[BoundaryError] = []
    for match in DELETED_GAME_MODEL_COLLECTION_PHYSICS_ADAPTER_COMMAND_WRAPPER_PATTERN.finditer(stripped):
        errors.append(
            BoundaryError(
                path,
                line_for_offset(stripped, match.start()),
                "deleted GameModelCollectionPhysicsAdapter command wrapper is blocked",
                (
                    "The adapter should only resolve legacy model identity into PhysicsBodyHandle values. "
                    "Callers must invoke PhysicsEngine handle commands directly instead of restoring "
                    "model-index command wrappers on the adapter."
                ),
            )
        )
    return errors


def check_deleted_game_model_collection_physics_adapter_command_guardrails(repo: Path) -> list[BoundaryError]:
    errors: list[BoundaryError] = []
    for path in sorted((repo / Path("SkullbonezSource")).rglob("*")):
        if path.suffix not in SOURCE_BEARING_SUFFIXES:
            continue
        errors.extend(
            check_deleted_game_model_collection_physics_adapter_command_guardrails_text(
                path,
                path.read_text(encoding="utf-8"),
            )
        )
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

    old_public_descriptor_model_index = """
    struct PhysicsBodyCreateDesc
    {
        int modelIndex = -1;
        PhysicsBodyMotionKind motionKind = PhysicsBodyMotionKind::Dynamic;
    };
    """
    if not any(
        error.message == "public physics descriptor model-index field is blocked"
        for error in check_public_physics_descriptor_model_index_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsApi.h"),
            old_public_descriptor_model_index,
        )
    ):
        failures.append("public physics descriptor model-index synthetic field was not rejected")

    public_descriptor_handle_only = """
    struct PhysicsBodyCreateDesc
    {
        PhysicsBodyHandle parentBody;
        PhysicsSceneObjectId sceneObject;
    };
    """
    if check_public_physics_descriptor_model_index_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsApi.h"),
        public_descriptor_handle_only,
    ):
        failures.append("public physics descriptor handle-only synthetic surface was rejected")

    old_standalone_model_access_models = """
    void PhysicsStandaloneWorld::StepFromLegacyModelAccess( PhysicsModelAccess& modelAccess )
    {
        auto models = modelAccess.Models();
        (void)models;
    }
    """
    if not any(
        error.message == "standalone physics implementation game-object dependency is blocked"
        for error in check_standalone_physics_implementation_game_object_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsApi.cpp"),
            old_standalone_model_access_models,
        )
    ):
        failures.append("standalone modelAccess.Models synthetic surface was not rejected")

    old_standalone_raw_game_model = """
    void PhysicsStandaloneWorld::ImportForTests( std::vector<GameObjects::GameModel>& models )
    {
        GameObjects::GameModel& model = models.front();
        (void)model;
    }
    """
    if not any(
        error.message == "standalone physics implementation game-object dependency is blocked"
        for error in check_standalone_physics_implementation_game_object_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsApi.cpp"),
            old_standalone_raw_game_model,
        )
    ):
        failures.append("standalone raw GameModel synthetic surface was not rejected")

    standalone_store_only = """
    void PhysicsStandaloneWorld::StepStores( const PhysicsStandaloneStepDesc& desc )
    {
        m_bodyStore.StepStandalone( desc.deltaSeconds );
        m_colliderStore.RefreshBroadphase();
    }
    """
    if check_standalone_physics_implementation_game_object_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsApi.cpp"),
        standalone_store_only,
    ):
        failures.append("standalone store-only synthetic surface was rejected")

    explicit_runtime_adapter_use = """
    void WakeEditorPhysicsBody( GameModelCollection& collection, PhysicsEngine& physics, int modelIndex )
    {
        GameModelCollectionPhysicsAdapter physicsBodies( collection );
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForModelIndex( modelIndex );
        physics.WakeBody( body );
    }
    """
    if check_standalone_physics_implementation_game_object_guardrails_text(
        Path("SkullbonezSource/Runtime/Editor/RunEditorTools.cpp"),
        explicit_runtime_adapter_use,
    ):
        failures.append("explicit runtime adapter synthetic surface was rejected")

    diagnostics_view_only = """
    struct PhysicsDiagnosticsFrameInput
    {
        const PhysicsDiagnosticsView& world;
        const PhysicsBodyStore& bodyStore;
    };
    """
    if check_standalone_physics_implementation_game_object_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsDiagnosticsSink.h"),
        diagnostics_view_only,
    ):
        failures.append("diagnostics-only view synthetic surface was rejected")

    test_fixture_game_model_text = """
    void BuildLegacyFixture( GameObjects::GameModel& model )
    {
        (void)model;
    }
    """
    if check_standalone_physics_implementation_game_object_guardrails_text(
        Path("SkullbonezSource/Tests/PhysicsFixture.cpp"),
        test_fixture_game_model_text,
    ):
        failures.append("test fixture GameModel synthetic surface was rejected")

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
    GameModelSoACache soaCache;
    GameModelStreamProvider::PrepareRenderStreams( soaCache, models );
    GameModelBodyStream bodyStream = collection.GetBodyStream();
    GameModelRenderStream renderStream = collection.GetRenderStream();
    collection.GetPhysicsBodyStream();
    collection.InvalidatePhysicsStreams();
    stats.soaCacheBytes = 0;
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

    deleted_game_model_stream_cache_text = """
    class GameModelCollection
    {
        GameModelSoACache m_soaCache;
        GameModelBodyStream GetBodyStream();
        GameModelRenderStream GetRenderStream();
        GameModelBodyStream GetPhysicsBodyStream();
        void InvalidatePhysicsStreams();
        void PrepareRenderStreams();
    };
    void UseStreamProvider( GameModelSoACache& cache )
    {
        GameModelStreamProvider::GetBodyStream( cache, models );
    }
    """
    if not any(
        error.message.startswith("deleted migration artifact is blocked: GameModel")
        for error in check_deleted_migration_artifact_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.h"),
            deleted_game_model_stream_cache_text,
        )
    ):
        failures.append("deleted GameModel stream/cache synthetic surface was not rejected")

    deleted_game_model_stream_project_text = """
    <ClCompile Include="SkullbonezSource\\GameObjects\\GameModelStreams.cpp" />
    <ClInclude Include="SkullbonezSource\\GameObjects\\GameModelSoACache.h" />
    """
    if not any(
        error.message == "deleted GameModel stream/cache file is blocked"
        for error in check_deleted_game_model_stream_project_guardrails_text(
            Path("SKULLBONEZ_CORE.vcxproj"),
            deleted_game_model_stream_project_text,
        )
    ):
        failures.append("deleted GameModel stream/cache project entry synthetic surface was not rejected")

    allowed_render_instance_prepare_text = """
    class GameModelCollection
    {
        void PrepareRenderInstances();
        const RenderInstanceStore& RenderInstances() const;
    };
    void RuntimeRenderer::RenderFrame()
    {
        collection.PrepareRenderInstances();
        const RenderInstanceStore& instances = collection.RenderInstances();
    }
    """
    if check_deleted_migration_artifact_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.h"),
        allowed_render_instance_prepare_text,
    ):
        failures.append("render-instance preparation synthetic surface was rejected")

    commented_deleted_migration_artifact_text = """
    // GameModelRuntimePhysicsTuning, legacyModelIndex, RuntimeConfigSnapshot, and IRenderSceneView are migration notes only.
    // GameModelSoACache, GameModelStreamProvider, GetBodyStream(), GetRenderStream(), PrepareRenderStreams(),
    // InvalidatePhysicsStreams(), and soaCacheBytes were deleted.
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

    old_world_run_invalidation = """
    void PhysicsWorld::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        StepBodyStores();
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if not any(
        error.message == "physics world model stream invalidation is blocked"
        for error in check_physics_world_run_invalidation_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_world_run_invalidation,
        )
    ):
        failures.append("old PhysicsWorld RunPhysics stream invalidation synthetic surface was not rejected")

    allowed_world_wake_invalidation = """
    void PhysicsWorld::WakeModel( PhysicsModelAccess& modelAccess )
    {
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if check_physics_world_run_invalidation_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        allowed_world_wake_invalidation,
    ):
        failures.append("non-RunPhysics PhysicsWorld stream invalidation synthetic surface was rejected")

    allowed_scene_run_invalidation = """
    void PhysicsScene::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        m_world.RunPhysics( modelAccess, bodyStore, colliderStore, dt, config, forces, workerPool );
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if check_physics_world_run_invalidation_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_scene_run_invalidation,
    ):
        failures.append("PhysicsScene stream invalidation synthetic surface was rejected")

    commented_world_run_invalidation = """
    void PhysicsWorld::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        // modelAccess.InvalidatePhysicsStreams() used to live here.
        StepBodyStores();
    }
    """
    if check_physics_world_run_invalidation_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_world_run_invalidation,
    ):
        failures.append("comment-only PhysicsWorld RunPhysics stream invalidation synthetic text was rejected")

    old_world_run_writeback = """
    void PhysicsWorld::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        StepBodyStores();
        modelAccess.WriteBackPhysicsBodies( bodyStore );
    }
    """
    if not any(
        error.message == "physics world bulk model writeback is blocked"
        for error in check_physics_world_run_writeback_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_world_run_writeback,
        )
    ):
        failures.append("old PhysicsWorld RunPhysics bulk writeback synthetic surface was not rejected")

    allowed_scene_run_writeback = """
    void PhysicsScene::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        m_world.RunPhysics( modelAccess, bodyStore, colliderStore, dt, config, forces, workerPool );
        modelAccess.WriteBackPhysicsBodies( bodyStore );
    }
    """
    if check_physics_world_run_writeback_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_scene_run_writeback,
    ):
        failures.append("PhysicsScene bulk writeback synthetic surface was rejected")

    commented_world_run_writeback = """
    void PhysicsWorld::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        // modelAccess.WriteBackPhysicsBodies(bodyStore) used to live here.
        StepBodyStores();
    }
    """
    if check_physics_world_run_writeback_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_world_run_writeback,
    ):
        failures.append("comment-only PhysicsWorld RunPhysics bulk writeback synthetic text was rejected")

    old_world_fixed_contact_notify = """
    void PhysicsWorld::ApplyPersistentContactSideEffects( PhysicsModelAccess& modelAccess )
    {
        modelAccess.NotifyFixedContact( index, 0.5f );
    }
    """
    if not any(
        error.message == "physics world fixed-contact presentation notify is blocked"
        for error in check_physics_world_fixed_contact_notify_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_world_fixed_contact_notify,
        )
    ):
        failures.append("old PhysicsWorld fixed-contact notify synthetic surface was not rejected")

    allowed_scene_fixed_contact_notify = """
    void PhysicsScene::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        for ( int index : m_world.GetFixedContactHighlightBodies() )
        {
            modelAccess.NotifyFixedContact( index, 0.5f );
        }
    }
    """
    if check_physics_world_fixed_contact_notify_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_scene_fixed_contact_notify,
    ):
        failures.append("PhysicsScene fixed-contact notify synthetic surface was rejected")

    commented_world_fixed_contact_notify = """
    void PhysicsWorld::ApplyPersistentContactSideEffects( PhysicsModelAccess& modelAccess )
    {
        // modelAccess.NotifyFixedContact(index, 0.5f) used to live here.
        ApplyStoreSideEffects();
    }
    """
    if check_physics_world_fixed_contact_notify_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_world_fixed_contact_notify,
    ):
        failures.append("comment-only PhysicsWorld fixed-contact notify synthetic text was rejected")

    old_world_persistent_contact_tree_release = """
    void PhysicsWorld::ApplyPersistentContactSideEffects( PhysicsModelAccess& modelAccess )
    {
        modelAccess.ReleaseAttachedFixedTreeParts( event );
    }
    """
    if not any(
        error.message == "physics world persistent-contact tree release is blocked"
        for error in check_physics_world_persistent_contact_tree_release_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_world_persistent_contact_tree_release,
        )
    ):
        failures.append("old PhysicsWorld persistent-contact tree release synthetic surface was not rejected")

    allowed_tornado_tree_release = """
    void PhysicsWorld::ApplyTornadoField( PhysicsModelAccess& modelAccess )
    {
        modelAccess.ReleaseAttachedFixedTreeParts( event );
    }
    """
    if check_physics_world_persistent_contact_tree_release_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        allowed_tornado_tree_release,
    ):
        failures.append("non-persistent-contact PhysicsWorld tree release synthetic surface was rejected")

    allowed_scene_tree_release = """
    void PhysicsScene::ApplyFixedTreeReleaseEvents( PhysicsModelAccess& modelAccess )
    {
        modelAccess.ReleaseAttachedFixedTreeParts( bodyStore, event, wakeBodies );
    }
    """
    if check_physics_world_persistent_contact_tree_release_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_scene_tree_release,
    ):
        failures.append("PhysicsScene fixed-tree release synthetic surface was rejected")

    commented_world_persistent_contact_tree_release = """
    void PhysicsWorld::ApplyPersistentContactSideEffects( PhysicsModelAccess& modelAccess )
    {
        // modelAccess.ReleaseAttachedFixedTreeParts(event) used to live here.
        ApplyStoreSideEffects();
    }
    """
    if check_physics_world_persistent_contact_tree_release_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_world_persistent_contact_tree_release,
    ):
        failures.append("comment-only PhysicsWorld persistent-contact tree release synthetic text was rejected")

    old_tornado_release_model_access = """
    void PhysicsWorld::ApplyTornadoField( PhysicsModelAccess& modelAccess )
    {
        modelAccess.WriteBackPhysicsBody( bodyStore, fixedIndex );
        modelAccess.ReleaseAttachedFixedTreeParts( event );
        modelAccess.ReloadPhysicsBodies( bodyStore, sleepState );
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if not any(
        error.message == "physics world tornado release model access is blocked"
        for error in check_physics_world_tornado_release_model_access_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_tornado_release_model_access,
        )
    ):
        failures.append("old PhysicsWorld tornado release model-access synthetic surface was not rejected")

    allowed_tornado_store_release = """
    void PhysicsWorld::ApplyTornadoField( PhysicsBodyStore& bodyStore )
    {
        PhysicsBodyStore::ReleaseFixedRecord( record, linearVelocity, angularVelocity );
        bodyStore.ReleaseAttachedFixedTreeParts( event, wakeBodies );
    }
    """
    if check_physics_world_tornado_release_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        allowed_tornado_store_release,
    ):
        failures.append("store-owned PhysicsWorld tornado release synthetic surface was rejected")

    allowed_scene_tornado_model_access = """
    void PhysicsScene::ApplyTornadoReleaseEvents( PhysicsModelAccess& modelAccess )
    {
        modelAccess.WriteBackPhysicsBody( bodyStore, index );
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if check_physics_world_tornado_release_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_scene_tornado_model_access,
    ):
        failures.append("PhysicsScene tornado model-access synthetic surface was rejected")

    commented_tornado_release_model_access = """
    void PhysicsWorld::ApplyTornadoField( PhysicsModelAccess& modelAccess )
    {
        // modelAccess.ReloadPhysicsBodies(bodyStore, sleepState) used to live here.
        ApplyStoreSideEffects();
    }
    """
    if check_physics_world_tornado_release_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_tornado_release_model_access,
    ):
        failures.append("comment-only PhysicsWorld tornado release synthetic text was rejected")

    old_world_step_model_access_signature = """
    void PhysicsWorld::RunPhysics( PhysicsModelAccess& modelAccess, PhysicsBodyStore& bodyStore )
    {
        StepBodyStores();
    }
    """
    if not any(
        error.message == "physics world step model-access signature is blocked"
        for error in check_physics_world_step_model_access_signature_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_world_step_model_access_signature,
        )
    ):
        failures.append("old PhysicsWorld step model-access signature synthetic surface was not rejected")

    old_world_tornado_model_access_signature = """
    void PhysicsWorld::ApplyTornadoField( PhysicsModelAccess& modelAccess )
    {
        StepBodyStores();
    }
    """
    if not any(
        error.message == "physics world step model-access signature is blocked"
        for error in check_physics_world_step_model_access_signature_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_world_tornado_model_access_signature,
        )
    ):
        failures.append("old PhysicsWorld tornado model-access signature synthetic surface was not rejected")

    allowed_world_step_store_signature = """
    void PhysicsWorld::RunPhysics( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore )
    {
        bodyStore.ReleaseAttachedFixedTreeParts( event, wakeBodies );
    }
    """
    if check_physics_world_step_model_access_signature_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        allowed_world_step_store_signature,
    ):
        failures.append("store-owned PhysicsWorld step signature synthetic surface was rejected")

    commented_world_step_model_access_signature = """
    void PhysicsWorld::RunPhysics( PhysicsBodyStore& bodyStore )
    {
        // RunPhysics(PhysicsModelAccess& modelAccess, ...) used to live here.
        StepBodyStores();
    }
    """
    if check_physics_world_step_model_access_signature_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_world_step_model_access_signature,
    ):
        failures.append("comment-only PhysicsWorld model-access signature synthetic text was rejected")

    old_engine_step_model_access_signature = """
    void PhysicsEngine::Step( PhysicsModelAccess& modelAccess, float deltaSeconds )
    {
        m_scene.RunPhysics( modelAccess, deltaSeconds, config, forces, workerPool );
    }
    """
    if not any(
        error.message == "physics engine step model-access signature is blocked"
        for error in check_physics_engine_step_model_access_signature_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsEngine.cpp"),
            old_engine_step_model_access_signature,
        )
    ):
        failures.append("old PhysicsEngine step model-access signature synthetic surface was not rejected")

    old_engine_step_model_access_declaration = """
    class PhysicsEngine
    {
        void Step( PhysicsModelAccess& modelAccess, float deltaSeconds );
    };
    """
    if not any(
        error.message == "physics engine step model-access signature is blocked"
        for error in check_physics_engine_step_model_access_signature_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsEngine.h"),
            old_engine_step_model_access_declaration,
        )
    ):
        failures.append("old PhysicsEngine step header model-access synthetic surface was not rejected")

    allowed_engine_step_store_signature = """
    void PhysicsEngine::Step( float deltaSeconds,
                              const Basics::EngineConfig& config,
                              const PhysicsWorldForces& worldForces,
                              Threading::WorkerPool& workerPool,
                              const char* const* diagnosticNames,
                              int diagnosticNameCount )
    {
        m_scene.RunPhysics( deltaSeconds, config, worldForces, workerPool, diagnosticNames, diagnosticNameCount );
    }
    """
    if check_physics_engine_step_model_access_signature_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsEngine.cpp"),
        allowed_engine_step_store_signature,
    ):
        failures.append("model-free PhysicsEngine step synthetic surface was rejected")

    commented_engine_step_model_access_signature = """
    void PhysicsEngine::Step( float deltaSeconds )
    {
        // Step( PhysicsModelAccess& modelAccess, float deltaSeconds ) was deleted.
        m_scene.RunPhysics( deltaSeconds, config, forces, workerPool, nullptr, 0 );
    }
    """
    if check_physics_engine_step_model_access_signature_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsEngine.cpp"),
        commented_engine_step_model_access_signature,
    ):
        failures.append("comment-only PhysicsEngine model-access signature synthetic text was rejected")

    old_scene_step_model_access_signature = """
    void PhysicsScene::RunPhysics( PhysicsModelAccess& modelAccess, float deltaSeconds )
    {
        m_world.RunPhysics( m_bodyStore, m_colliderStore, deltaSeconds, config, forces, workerPool, nullptr, 0 );
    }
    """
    if not any(
        error.message == "physics scene step model-access signature is blocked"
        for error in check_physics_scene_step_model_access_signature_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
            old_scene_step_model_access_signature,
        )
    ):
        failures.append("old PhysicsScene step model-access signature synthetic surface was not rejected")

    old_scene_step_model_access_declaration = """
    class PhysicsScene
    {
        void RunPhysics( PhysicsModelAccess& modelAccess, float deltaSeconds );
    };
    """
    if not any(
        error.message == "physics scene step model-access signature is blocked"
        for error in check_physics_scene_step_model_access_signature_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsScene.h"),
            old_scene_step_model_access_declaration,
        )
    ):
        failures.append("old PhysicsScene step header model-access synthetic surface was not rejected")

    allowed_scene_step_store_signature = """
    void PhysicsScene::RunPhysics( float deltaSeconds,
                                   const Basics::EngineConfig& config,
                                   const PhysicsWorldForces& worldForces,
                                   Threading::WorkerPool& workerPool,
                                   const char* const* diagnosticNames,
                                   int diagnosticNameCount )
    {
        m_world.RunPhysics( m_bodyStore, m_colliderStore, deltaSeconds, config, worldForces, workerPool, diagnosticNames, diagnosticNameCount );
    }
    """
    if check_physics_scene_step_model_access_signature_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_scene_step_store_signature,
    ):
        failures.append("model-free PhysicsScene step synthetic surface was rejected")

    commented_scene_step_model_access_signature = """
    void PhysicsScene::RunPhysics( float deltaSeconds )
    {
        // RunPhysics( PhysicsModelAccess& modelAccess, float deltaSeconds ) was deleted.
        RunStoreOwnedStep();
    }
    """
    if check_physics_scene_step_model_access_signature_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        commented_scene_step_model_access_signature,
    ):
        failures.append("comment-only PhysicsScene model-access signature synthetic text was rejected")

    old_simulation_step_owner_borrow = """
    struct SimulationPhysicsStep
    {
        PhysicsEngine* engine = nullptr;
        PhysicsModelAccess* modelAccess = nullptr;
        WorkerPool* workerPool = nullptr;
        PhysicsWorldForces* worldForces = nullptr;
    };
    """
    if not any(
        error.message == "simulation scheduler physics-owner borrow is blocked"
        for error in check_simulation_system_owner_borrow_guardrails_text(
            Path("SkullbonezSource/Physics/SimulationSystem.h"),
            old_simulation_step_owner_borrow,
        )
    ):
        failures.append("old SimulationSystem owner-borrow synthetic surface was not rejected")

    old_simulation_system_direct_step = """
    #include "PhysicsModelAccess.h"
    #include "PhysicsEngine.h"
    void SimulationPhysicsStep::Run( float deltaSeconds ) const
    {
        engine->Step( *modelAccess, deltaSeconds, *config, *worldForces, *workerPool );
    }
    """
    if not any(
        error.message == "simulation scheduler physics-owner borrow is blocked"
        for error in check_simulation_system_owner_borrow_guardrails_text(
            Path("SkullbonezSource/Physics/SimulationSystem.cpp"),
            old_simulation_system_direct_step,
        )
    ):
        failures.append("old SimulationSystem direct physics step synthetic surface was not rejected")

    allowed_simulation_tick_count = """
    SimulationTickResult SimulationSystem::Tick( const SimulationTickInput& input )
    {
        SimulationTickResult result;
        const bool canStepPhysics = input.canStepPhysics;
        result.committedPhysicsTicks = canStepPhysics ? 1 : 0;
        return result;
    }
    """
    if check_simulation_system_owner_borrow_guardrails_text(
        Path("SkullbonezSource/Physics/SimulationSystem.cpp"),
        allowed_simulation_tick_count,
    ):
        failures.append("owner-free SimulationSystem tick-count synthetic surface was rejected")

    commented_simulation_owner_borrow = """
    SimulationTickResult SimulationSystem::Tick( const SimulationTickInput& input )
    {
        // SimulationPhysicsStep used to borrow PhysicsModelAccess here.
        return {};
    }
    """
    if check_simulation_system_owner_borrow_guardrails_text(
        Path("SkullbonezSource/Physics/SimulationSystem.cpp"),
        commented_simulation_owner_borrow,
    ):
        failures.append("comment-only SimulationSystem owner-borrow synthetic text was rejected")

    old_physics_model_access_step_facade_header = """
    class PhysicsModelAccess
    {
        GameModelBodyStream GetBodyStream();
        void InvalidatePhysicsStreams();
        void WriteBackPhysicsBodies( const PhysicsBodyStore& bodyStore );
        void WriteBackPhysicsBody( const PhysicsBodyStore& bodyStore, int modelIndex );
        void NotifyFixedContact( int modelIndex, float seconds );
        void TickContactHighlights( int modelCount, float deltaSeconds );
        void FillPhysicsDiagnosticsNames( int bodyCount, std::vector<const char*>& names ) const;
    };
    """
    if not any(
        error.message == "deleted PhysicsModelAccess step facade surface is blocked"
        for error in check_physics_model_access_deleted_step_facade_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsModelAccess.h"),
            old_physics_model_access_step_facade_header,
        )
    ):
        failures.append("old PhysicsModelAccess step facade header synthetic surface was not rejected")

    old_physics_model_access_step_facade_definitions = """
    void PhysicsModelAccess::InvalidatePhysicsStreams()
    {
        m_collection.InvalidatePhysicsStreams();
    }
    void PhysicsModelAccess::WriteBackPhysicsBody( const PhysicsBodyStore& bodyStore, int index )
    {
        m_collection.WriteBackPhysicsBody( bodyStore, index );
    }
    void PhysicsModelAccess::NotifyFixedContact( int index, float seconds )
    {
        m_collection.NotifyFixedContact( index, seconds );
    }
    """
    if not any(
        error.message == "deleted PhysicsModelAccess step facade surface is blocked"
        for error in check_physics_model_access_deleted_step_facade_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
            old_physics_model_access_step_facade_definitions,
        )
    ):
        failures.append("old PhysicsModelAccess step facade definitions synthetic surface was not rejected")

    allowed_physics_model_access_refresh_facade = """
    class PhysicsModelAccess
    {
        int ModelCount() const;
        void ReloadPhysicsBodies( PhysicsBodyStore& bodyStore, const std::vector<uint8_t>& sleepStates );
        void RefreshPhysicsBodyFromModel( PhysicsBodyStore& bodyStore, int modelIndex );
        void RefreshPhysicsColliders( ColliderStore& colliderStore, const PhysicsBodyStore& bodyStore );
        void RefreshRenderInstances( Rendering::RenderInstanceStore& renderInstanceStore,
                                     const PhysicsBodyStore& bodyStore,
                                     const ColliderStore& colliderStore );
    };
    """
    if check_physics_model_access_deleted_step_facade_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsModelAccess.h"),
        allowed_physics_model_access_refresh_facade,
    ):
        failures.append("refresh-only PhysicsModelAccess synthetic surface was rejected")

    commented_physics_model_access_step_facade = """
    class PhysicsModelAccess
    {
        int ModelCount() const;
        // WriteBackPhysicsBodies and FillPhysicsDiagnosticsNames were deleted from this facade.
    };
    """
    if check_physics_model_access_deleted_step_facade_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsModelAccess.h"),
        commented_physics_model_access_step_facade,
    ):
        failures.append("comment-only PhysicsModelAccess step facade synthetic text was rejected")

    old_store_seed_model_access = """
    void PhysicsWorld::SeedModelAsleep( PhysicsModelAccess& modelAccess, const PhysicsBodyStore& bodyStore, int index )
    {
        const GameModelBodyStream bodyStream = modelAccess.GetBodyStream();
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if not any(
        error.message == "physics world store seed model access is blocked"
        for error in check_physics_world_store_seed_model_access_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_store_seed_model_access,
        )
    ):
        failures.append("old PhysicsWorld store seed model-access synthetic surface was not rejected")

    allowed_store_seed_body_records = """
    void PhysicsWorld::SeedModelAsleep( int bodyCount, const std::vector<PhysicsBodyRecord>& bodyRecords, int index )
    {
        const int modelCount = bodyCount;
        if ( bodyRecords[index].isFixed )
        {
            return;
        }
    }
    """
    if check_physics_world_store_seed_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        allowed_store_seed_body_records,
    ):
        failures.append("store-owned PhysicsWorld seed synthetic surface was rejected")

    old_legacy_seed_body_stream = """
    void PhysicsWorld::SeedModelAsleep( PhysicsModelAccess& modelAccess, const GameModelBodyStream& bodyStream, int index )
    {
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if not any(
        error.message == "physics world legacy model-stream wake/seed path is deleted"
        for error in check_physics_world_deleted_model_stream_wake_seed_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_legacy_seed_body_stream,
        )
    ):
        failures.append("old PhysicsWorld seed model-stream synthetic surface was not rejected")

    allowed_scene_seed_invalidation = """
    void PhysicsScene::SeedBodyAsleep( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        modelAccess.WriteBackPhysicsBody( bodyStore, index );
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if check_physics_world_store_seed_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_scene_seed_invalidation,
    ):
        failures.append("PhysicsScene seed invalidation synthetic surface was rejected")

    commented_store_seed_model_access = """
    void PhysicsWorld::SeedModelAsleep( const PhysicsBodyStore& bodyStore, int index )
    {
        // modelAccess.GetBodyStream() used to live here.
        SeedBodyRecord();
    }
    """
    if check_physics_world_store_seed_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_store_seed_model_access,
    ):
        failures.append("comment-only PhysicsWorld store seed synthetic text was rejected")

    old_store_wake_model_access = """
    void PhysicsWorld::WakeModel( PhysicsModelAccess& modelAccess, PhysicsBodyStore& bodyStore, int index )
    {
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if not any(
        error.message == "physics world store wake model access is blocked"
        for error in check_physics_world_store_wake_model_access_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_store_wake_model_access,
        )
    ):
        failures.append("old PhysicsWorld store wake model-access synthetic surface was not rejected")

    old_store_wake_connected_invalidation = """
    void PhysicsWorld::WakePointJointConnectedBodies( PhysicsModelAccess& modelAccess, PhysicsBodyStore& bodyStore )
    {
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if not any(
        error.message == "physics world store wake model access is blocked"
        for error in check_physics_world_store_wake_model_access_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_store_wake_connected_invalidation,
        )
    ):
        failures.append("old PhysicsWorld store point-joint wake invalidation synthetic surface was not rejected")

    allowed_store_wake_body_records = """
    void PhysicsWorld::WakeModel( int bodyCount, const std::vector<PhysicsBodyRecord>& bodyRecords, int index )
    {
        WakeSleepVisualIsland( bodyCount, bodyRecords, nullptr, index, 0.0f, false );
    }
    """
    if check_physics_world_store_wake_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        allowed_store_wake_body_records,
    ):
        failures.append("store-owned PhysicsWorld wake synthetic surface was rejected")

    old_legacy_wake_body_stream = """
    void PhysicsWorld::WakeModel( PhysicsModelAccess& modelAccess, const GameModelBodyStream& bodyStream, int index )
    {
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if not any(
        error.message == "physics world legacy model-stream wake/seed path is deleted"
        for error in check_physics_world_deleted_model_stream_wake_seed_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_legacy_wake_body_stream,
        )
    ):
        failures.append("old PhysicsWorld wake model-stream synthetic surface was not rejected")

    allowed_scene_wake_invalidation = """
    void PhysicsScene::WakeBody( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        modelAccess.WriteBackPhysicsBody( bodyStore, index );
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if check_physics_world_store_wake_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_scene_wake_invalidation,
    ):
        failures.append("PhysicsScene wake invalidation synthetic surface was rejected")

    commented_store_wake_model_access = """
    void PhysicsWorld::WakeModel( PhysicsBodyStore& bodyStore, int index )
    {
        // modelAccess.InvalidatePhysicsStreams() used to live here.
        WakeBodyRecord();
    }
    """
    if check_physics_world_store_wake_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_store_wake_model_access,
    ):
        failures.append("comment-only PhysicsWorld store wake synthetic text was rejected")

    old_public_wake_seed_model_access = """
    class PhysicsWorld
    {
        void WakeModel( PhysicsModelAccess& modelAccess, int index );
        void SeedModelAsleep( PhysicsModelAccess& modelAccess, int index );
    };
    """
    if not any(
        error.message == "physics world legacy model-stream wake/seed path is deleted"
        for error in check_physics_world_deleted_model_stream_wake_seed_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.h"),
            old_public_wake_seed_model_access,
        )
    ):
        failures.append("old PhysicsWorld public model-access wake/seed synthetic surface was not rejected")

    allowed_store_wake_seed_header = """
    class PhysicsWorld
    {
        void WakeModel( PhysicsBodyStore& bodyStore, int index );
        void SeedModelAsleep( const PhysicsBodyStore& bodyStore, int index );
    };
    """
    if check_physics_world_deleted_model_stream_wake_seed_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.h"),
        allowed_store_wake_seed_header,
    ):
        failures.append("store-owned PhysicsWorld wake/seed header synthetic surface was rejected")

    commented_deleted_world_stream = """
    void PhysicsWorld::WakeModel( PhysicsBodyStore& bodyStore, int index )
    {
        // WakeModel( PhysicsModelAccess& modelAccess, int index ) used to live here.
        // const GameModelBodyStream bodyStream = modelAccess.GetBodyStream();
        WakeBodyRecord();
    }
    """
    if check_physics_world_deleted_model_stream_wake_seed_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_deleted_world_stream,
    ):
        failures.append("comment-only PhysicsWorld deleted model-stream synthetic text was rejected")

    old_world_run_diagnostics = """
    void PhysicsWorld::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        m_diagnostics.EmitRegressionLog( *this, modelAccess, bodyStore, colliderStore );
        m_diagnostics.EmitFrame( modelAccess, bodyStore, colliderStore, dt );
    }
    """
    if not any(
        error.message == "physics world run diagnostics emission is blocked"
        for error in check_physics_world_run_diagnostics_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_world_run_diagnostics,
        )
    ):
        failures.append("old PhysicsWorld RunPhysics diagnostics synthetic surface was not rejected")

    allowed_world_step_diagnostics = """
    void PhysicsWorld::EmitStepDiagnostics( const PhysicsBodyStore& bodyStore )
    {
        const PhysicsDiagnosticsNameView names{ diagnosticNames, diagnosticNameCount };
        const PhysicsDiagnosticsFrameInput frame{ GetDiagnosticsView(), bodyStore, colliderStore, names, dt };
        m_diagnostics.EmitRegressionLog( frame );
        m_diagnostics.EmitFrame( frame );
    }
    """
    if check_physics_world_run_diagnostics_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        allowed_world_step_diagnostics,
    ):
        failures.append("PhysicsWorld EmitStepDiagnostics synthetic surface was rejected")

    commented_world_run_diagnostics = """
    void PhysicsWorld::RunPhysics( PhysicsModelAccess& modelAccess )
    {
        // m_diagnostics.EmitFrame(modelAccess, bodyStore, colliderStore, dt) used to live here.
        StepBodyStores();
    }
    """
    if check_physics_world_run_diagnostics_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_world_run_diagnostics,
    ):
        failures.append("comment-only PhysicsWorld RunPhysics diagnostics synthetic text was rejected")

    old_world_step_diagnostics_model_access = """
    void PhysicsWorld::EmitStepDiagnostics( PhysicsModelAccess& modelAccess,
                                            const PhysicsBodyStore& bodyStore,
                                            const ColliderStore& colliderStore,
                                            float dt )
    {
        modelAccess.FillPhysicsDiagnosticsNames( bodyStore.Count(), m_physicsDiagnosticsModelNames );
        m_diagnostics.EmitFrame( frame );
    }
    void PhysicsWorld::EmitPhysicsDiagnosticsFrame( PhysicsModelAccess& modelAccess,
                                                    const PhysicsBodyStore& bodyStore )
    {
        modelAccess.FillPhysicsDiagnosticsNames( bodyStore.Count(), m_physicsDiagnosticsModelNames );
    }
    """
    if not any(
        error.message == "physics world step diagnostics model access is blocked"
        for error in check_physics_world_step_diagnostics_model_access_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
            old_world_step_diagnostics_model_access,
        )
    ):
        failures.append("old PhysicsWorld step diagnostics model-access synthetic surface was not rejected")

    allowed_world_step_diagnostics_names = """
    bool PhysicsWorld::ShouldEmitStepDiagnostics() const
    {
        return !m_diagnosticsSuppressed && m_diagnostics.IsFrameLogEnabled();
    }
    void PhysicsWorld::EmitStepDiagnostics( const PhysicsBodyStore& bodyStore,
                                            const ColliderStore& colliderStore,
                                            float dt,
                                            const char* const* diagnosticNames,
                                            int diagnosticNameCount )
    {
        const PhysicsDiagnosticsNameView names{ diagnosticNames, diagnosticNameCount };
        const PhysicsDiagnosticsFrameInput frame{ GetDiagnosticsView(), bodyStore, colliderStore, names, dt };
        m_diagnostics.EmitFrame( frame );
    }
    """
    if check_physics_world_step_diagnostics_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        allowed_world_step_diagnostics_names,
    ):
        failures.append("model-free PhysicsWorld step diagnostics synthetic surface was rejected")

    commented_world_step_diagnostics_model_access = """
    void PhysicsWorld::EmitStepDiagnostics( const PhysicsBodyStore& bodyStore )
    {
        // EmitStepDiagnostics(PhysicsModelAccess& modelAccess, ...) used to format names here.
        // modelAccess.FillPhysicsDiagnosticsNames(bodyStore.Count(), names) used to run here.
        const PhysicsDiagnosticsNameView names{ diagnosticNames, diagnosticNameCount };
        m_diagnostics.EmitFrame( frame );
    }
    """
    if check_physics_world_step_diagnostics_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsWorld.cpp"),
        commented_world_step_diagnostics_model_access,
    ):
        failures.append("comment-only PhysicsWorld step diagnostics model-access synthetic text was rejected")

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

    old_render_instance_store_model_only_refresh = """
    void RenderInstanceStore::Refresh( std::vector<GameModel>& models )
    {
        Refresh( models.empty() ? nullptr : models.data(), static_cast<int>( models.size() ) );
    }
    void RenderInstanceStore::Refresh( GameModel* models, int modelCount )
    {
        record.modelMatrix = model.GetModelMatrix();
    }
    """
    if not any(
        error.message == "RenderInstanceStore model-only refresh overload is blocked"
        for error in check_render_instance_store_authority_guardrails_text(
            Path("SkullbonezSource/Rendering/RenderInstanceStore.cpp"),
            old_render_instance_store_model_only_refresh,
        )
    ):
        failures.append("old RenderInstanceStore model-only refresh synthetic surface was not rejected")

    old_render_instance_store_model_only_declarations = """
    class RenderInstanceStore
    {
        void Refresh( std::vector<GameObjects::GameModel>& models );
        void Refresh( GameObjects::GameModel* models, int modelCount );
    };
    """
    if not any(
        error.message == "RenderInstanceStore model-only refresh overload is blocked"
        for error in check_render_instance_store_authority_guardrails_text(
            Path("SkullbonezSource/Rendering/RenderInstanceStore.h"),
            old_render_instance_store_model_only_declarations,
        )
    ):
        failures.append("old RenderInstanceStore model-only refresh declarations were not rejected")

    old_render_instance_store_refresh_fallback = """
    void RenderInstanceStore::Refresh( GameModel* models,
                                       int modelCount,
                                       const PhysicsBodyStore& bodyStore,
                                       const ColliderStore& colliderStore )
    {
        if ( bodyStore.Count() != modelCount || colliderStore.Count() != modelCount )
        {
            Refresh( models, modelCount );
            return;
        }
    }
    """
    if not any(
        error.message == "RenderInstanceStore GameModel fallback refresh is blocked"
        for error in check_render_instance_store_authority_guardrails_text(
            Path("SkullbonezSource/Rendering/RenderInstanceStore.cpp"),
            old_render_instance_store_refresh_fallback,
        )
    ):
        failures.append("old RenderInstanceStore fallback refresh synthetic surface was not rejected")

    allowed_render_instance_store_fail_closed = """
    void RenderInstanceStore::Refresh( GameModel* models,
                                       int modelCount,
                                       const PhysicsBodyStore& bodyStore,
                                       const ColliderStore& colliderStore )
    {
        if ( bodyStore.Count() != modelCount || colliderStore.Count() != modelCount )
        {
            assert( bodyStore.Count() == modelCount );
            assert( colliderStore.Count() == modelCount );
            Clear();
            return;
        }
        record.modelMatrix = BuildPhysicsModelMatrix( body, collider );
    }
    void RenderInstanceStore::OverridePoseFromModel( int modelIndex, GameModel& model )
    {
        record.modelMatrix = model.GetModelMatrix();
    }
    """
    if check_render_instance_store_authority_guardrails_text(
        Path("SkullbonezSource/Rendering/RenderInstanceStore.cpp"),
        allowed_render_instance_store_fail_closed,
    ):
        failures.append("store-backed RenderInstanceStore fail-closed synthetic surface was rejected")

    commented_render_instance_store_refresh_fallback = """
    void RenderInstanceStore::Refresh( GameModel* models,
                                       int modelCount,
                                       const PhysicsBodyStore& bodyStore,
                                       const ColliderStore& colliderStore )
    {
        // Refresh( models, modelCount ) was the deleted GameModel fallback.
        Clear();
    }
    """
    if check_render_instance_store_authority_guardrails_text(
        Path("SkullbonezSource/Rendering/RenderInstanceStore.cpp"),
        commented_render_instance_store_refresh_fallback,
    ):
        failures.append("comment-only RenderInstanceStore fallback refresh synthetic text was rejected")

    old_game_model_renderer_stream_reads = """
    void GameModelRenderer::RenderModels( GameModelCollection& collection )
    {
        const GameModelRenderStream renderStream = collection.GetRenderStream();
        const GameModelBodyStream bodyStream = collection.GetBodyStream();
        if ( models[x].IsSphere() || models[x].IsBox() || models[x].IsConvexHull() ) {}
        const Matrix4 bodyModel = Matrix4::Translate( models[x].GetPosition() ) *
                                  Matrix4::FromQuaternion( models[x].GetOrientation() );
        const bool isPineVisual = IsPineVisualMaterial( models[x].GetRenderMaterial() );
    }
    """
    if not any(
        error.message == "GameModelRenderer model-pose stream read is blocked"
        for error in check_game_model_renderer_render_instance_authority_guardrails_text(
            Path("SkullbonezSource/Rendering/GameModelRenderer.cpp"),
            old_game_model_renderer_stream_reads,
        )
    ):
        failures.append("old GameModelRenderer model-pose stream synthetic surface was not rejected")

    allowed_game_model_renderer_render_instances = """
    void GameModelRenderer::RenderModels( GameModelCollection& collection )
    {
        const RenderInstanceStore& renderStore = collection.RenderInstances();
        const std::vector<RenderInstanceRecord>& instances = renderStore.Records();
        if ( instances[x].shapeKind == RenderInstanceShapeKind::Sphere ||
             instances[x].shapeKind == RenderInstanceShapeKind::ConvexHull )
        {
            const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &models[x].GetCollisionShape() );
            RenderHelper::DrawSphereBatchModel( instances[x].modelMatrix, instances[x].material );
        }
    }
    """
    if check_game_model_renderer_render_instance_authority_guardrails_text(
        Path("SkullbonezSource/Rendering/GameModelRenderer.cpp"),
        allowed_game_model_renderer_render_instances,
    ):
        failures.append("RenderInstanceStore-backed GameModelRenderer synthetic surface was rejected")

    commented_game_model_renderer_stream_reads = """
    void GameModelRenderer::RenderModels( GameModelCollection& collection )
    {
        // collection.GetRenderStream() and models[x].GetPosition() used to drive object rendering.
        const RenderInstanceStore& renderStore = collection.RenderInstances();
    }
    """
    if check_game_model_renderer_render_instance_authority_guardrails_text(
        Path("SkullbonezSource/Rendering/GameModelRenderer.cpp"),
        commented_game_model_renderer_stream_reads,
    ):
        failures.append("comment-only GameModelRenderer stream-read synthetic text was rejected")

    old_dxr_model_matrix_copy = """
    int GameModelCollection::CopyDxrModelMatrices( float* outMatrixFloats, int maxModelCount )
    {
        const Matrix4 modelMatrix = m_gameModels[static_cast<std::size_t>( i )].GetModelMatrix();
        memcpy( outMatrixFloats, modelMatrix.Data(), 16u * sizeof( float ) );
        return 1;
    }
    """
    if not any(
        error.message == "DXR model-matrix upload must use render instances"
        for error in check_dxr_render_instance_matrix_authority_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
            old_dxr_model_matrix_copy,
        )
    ):
        failures.append("old DXR GameModel matrix copy synthetic surface was not rejected")

    allowed_dxr_render_instance_matrix_copy = """
    int GameModelCollection::CopyDxrModelMatrices( float* outMatrixFloats, int maxModelCount )
    {
        const std::vector<Rendering::RenderInstanceRecord>& instances = m_physicsEngine.RenderInstances().Records();
        const Matrix4& modelMatrix = instances[static_cast<std::size_t>( i )].modelMatrix;
        memcpy( outMatrixFloats, modelMatrix.Data(), 16u * sizeof( float ) );
        return 1;
    }
    """
    if check_dxr_render_instance_matrix_authority_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        allowed_dxr_render_instance_matrix_copy,
    ):
        failures.append("RenderInstanceStore-backed DXR matrix copy synthetic surface was rejected")

    commented_dxr_model_matrix_copy = """
    int GameModelCollection::CopyDxrModelMatrices( float* outMatrixFloats, int maxModelCount )
    {
        // m_gameModels[i].GetModelMatrix() used to feed DXR.
        return 0;
    }
    """
    if check_dxr_render_instance_matrix_authority_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        commented_dxr_model_matrix_copy,
    ):
        failures.append("comment-only DXR model matrix copy synthetic text was rejected")

    old_unconditional_body_store_refresh = """
    const SkullbonezCore::Physics::PhysicsBodyStore& GameModelCollection::GetPhysicsBodyStore()
    {
        PhysicsModelAccess modelAccess( *this );
        m_physicsEngine.RefreshBodyStore( modelAccess );
        return m_physicsEngine.BodyStore();
    }
    """
    if not any(
        error.message == "body-store read accessor must not unconditionally refresh from GameModel"
        for error in check_game_model_collection_body_store_read_authority_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
            old_unconditional_body_store_refresh,
        )
    ):
        failures.append("old unconditional body-store refresh synthetic surface was not rejected")

    allowed_count_gated_body_store_refresh = """
    const SkullbonezCore::Physics::PhysicsBodyStore& GameModelCollection::GetPhysicsBodyStore()
    {
        if ( m_physicsEngine.BodyStore().Count() != ModelCount() )
        {
            PhysicsModelAccess modelAccess( *this );
            m_physicsEngine.RefreshBodyStore( modelAccess );
        }
        return m_physicsEngine.BodyStore();
    }
    """
    if check_game_model_collection_body_store_read_authority_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        allowed_count_gated_body_store_refresh,
    ):
        failures.append("count-gated body-store refresh synthetic surface was rejected")

    old_unconditional_collider_store_refresh = """
    const SkullbonezCore::Physics::ColliderStore& GameModelCollection::GetColliderStore()
    {
        PhysicsModelAccess modelAccess( *this );
        m_physicsEngine.RefreshColliderStore( modelAccess );
        return m_physicsEngine.Colliders();
    }
    """
    if not any(
        error.message == "collider-store read accessor must preserve body-store authority"
        for error in check_game_model_collection_body_store_read_authority_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
            old_unconditional_collider_store_refresh,
        )
    ):
        failures.append("old full collider-store refresh synthetic surface was not rejected")

    allowed_collider_snapshot_refresh = """
    const SkullbonezCore::Physics::ColliderStore& GameModelCollection::GetColliderStore()
    {
        GetPhysicsBodyStore();
        PhysicsModelAccess modelAccess( *this );
        m_physicsEngine.RefreshColliderSnapshot( modelAccess );
        return m_physicsEngine.Colliders();
    }
    """
    if check_game_model_collection_body_store_read_authority_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        allowed_collider_snapshot_refresh,
    ):
        failures.append("collider snapshot refresh synthetic surface was rejected")

    commented_collider_store_refresh = """
    const SkullbonezCore::Physics::ColliderStore& GameModelCollection::GetColliderStore()
    {
        // m_physicsEngine.RefreshColliderStore( modelAccess ) used to reload body rows here.
        GetPhysicsBodyStore();
        return m_physicsEngine.Colliders();
    }
    """
    if check_game_model_collection_body_store_read_authority_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        commented_collider_store_refresh,
    ):
        failures.append("comment-only collider-store refresh synthetic text was rejected")

    old_run_physics_steady_model_access = """
    void GameModelCollection::RunPhysics( float dt )
    {
        PhysicsModelAccess modelAccess( *this );
        const int modelCount = ModelCount();
        if ( m_physicsEngine.BodyStore().Count() != modelCount )
        {
            m_physicsEngine.RefreshBodyStore( modelAccess );
        }
        m_physicsEngine.Step( dt, config, worldForces, workerPool, nullptr, 0 );
    }
    """
    if not any(
        error.message == "steady physics step model-access facade is blocked"
        for error in check_game_model_collection_run_physics_model_access_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
            old_run_physics_steady_model_access,
        )
    ):
        failures.append("old steady RunPhysics model-access facade synthetic surface was not rejected")

    allowed_run_physics_topology_model_access = """
    void GameModelCollection::RunPhysics( float dt )
    {
        const int modelCount = ModelCount();
        const bool bodyTopologyChanged = m_physicsEngine.BodyStore().Count() != modelCount;
        const bool colliderTopologyChanged = m_physicsEngine.Colliders().Count() != modelCount;
        if ( bodyTopologyChanged || colliderTopologyChanged )
        {
            PhysicsModelAccess modelAccess( *this );
            if ( bodyTopologyChanged )
            {
                m_physicsEngine.RefreshBodyStore( modelAccess );
            }
            if ( colliderTopologyChanged )
            {
                m_physicsEngine.RefreshColliderSnapshot( modelAccess );
            }
        }
        m_physicsEngine.Step( dt, config, worldForces, workerPool, nullptr, 0 );
    }
    """
    if check_game_model_collection_run_physics_model_access_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        allowed_run_physics_topology_model_access,
    ):
        failures.append("topology-gated RunPhysics model-access synthetic surface was rejected")

    commented_run_physics_model_access = """
    void GameModelCollection::RunPhysics( float dt )
    {
        // PhysicsModelAccess modelAccess( *this ) used to be created before count checks.
        m_physicsEngine.Step( dt, config, worldForces, workerPool, nullptr, 0 );
    }
    """
    if check_game_model_collection_run_physics_model_access_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        commented_run_physics_model_access,
    ):
        failures.append("comment-only RunPhysics model-access synthetic text was rejected")

    old_collection_body_model_reads = """
    Vector3 GameModelCollection::GetModelPosition( int index )
    {
        return m_gameModels[index].GetPosition();
    }

    double GameModelCollection::GetSceneKineticEnergy()
    {
        double totalEnergy = 0.0;
        for ( GameModel& model : m_gameModels )
        {
            if ( model.IsFixed() ) continue;
            const Vector3& vel = model.GetVelocity();
            const Vector3& omega = model.GetAngularVelocity();
            const Vector3& inertia = model.GetRotationalInertia();
            totalEnergy += 0.5 * static_cast<double>( model.GetMass() );
        }
        return totalEnergy;
    }
    """
    if not any(
        error.message == "GameModelCollection body read should use PhysicsBodyStore"
        for error in check_game_model_collection_body_store_read_authority_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
            old_collection_body_model_reads,
        )
    ):
        failures.append("old collection body model-read synthetic surface was not rejected")

    allowed_collection_body_store_reads = """
    Vector3 GameModelCollection::GetModelPosition( int index )
    {
        const PhysicsBodyStore& bodyStore = GetPhysicsBodyStore();
        const PhysicsBodyRecord* record = bodyStore.RecordForModelIndex( index );
        return record->position;
    }

    double GameModelCollection::GetSceneKineticEnergy()
    {
        double totalEnergy = 0.0;
        const PhysicsBodyStore& bodyStore = GetPhysicsBodyStore();
        for ( const PhysicsBodyRecord& body : bodyStore.Records() )
        {
            totalEnergy += static_cast<double>( body.mass );
        }
        return totalEnergy;
    }
    """
    if check_game_model_collection_body_store_read_authority_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        allowed_collection_body_store_reads,
    ):
        failures.append("PhysicsBodyStore-backed collection body-read synthetic surface was rejected")

    commented_collection_body_model_reads = """
    double GameModelCollection::GetSceneKineticEnergy()
    {
        // model.GetVelocity() and m_gameModels[index].GetPosition() used to read the compatibility mirror.
        const PhysicsBodyStore& bodyStore = GetPhysicsBodyStore();
        return static_cast<double>( bodyStore.Count() );
    }
    """
    if check_game_model_collection_body_store_read_authority_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        commented_collection_body_model_reads,
    ):
        failures.append("comment-only collection body model-read synthetic text was rejected")

    old_runtime_pick_service_model_reads = """
    #include "../GameObjects/GameModel.h"
    bool RuntimePickService::TryPickModel( const RuntimePickRequest& request, RuntimePickResult& outResult )
    {
        const std::vector<GameObjects::GameModel>& models = *request.models;
        const GameObjects::GameModel& model = models[0];
        if ( model.IsFixed() )
        {
            return false;
        }
        RuntimePickShapeTransform transform;
        transform.position = model.GetPosition();
        transform.orientation = model.GetOrientation();
        return TryIntersectRuntimePickShape( model.GetCollisionShape(), transform, request.rayOrigin, request.rayDirection, outResult.rayT );
    }
    """
    if not any(
        error.message == "RuntimePickService must use physics stores for body state"
        for error in check_runtime_pick_service_store_authority_guardrails_text(
            Path("SkullbonezSource/Runtime/RuntimePickService.cpp"),
            old_runtime_pick_service_model_reads,
        )
    ):
        failures.append("old RuntimePickService GameModel-backed synthetic surface was not rejected")

    allowed_runtime_pick_service_store_reads = """
    bool RuntimePickService::TryPickModel( const RuntimePickRequest& request, RuntimePickResult& outResult )
    {
        const std::vector<Physics::PhysicsBodyRecord>& bodies = request.bodyStore->Records();
        const std::vector<Physics::ColliderRecord>& colliders = request.colliderStore->Records();
        const Physics::PhysicsBodyRecord& body = bodies[0];
        const Physics::ColliderRecord& collider = colliders[0];
        RuntimePickShapeTransform transform;
        transform.position = body.position;
        transform.orientation = body.orientation;
        return TryIntersectRuntimePickShape( collider.shape, transform, request.rayOrigin, request.rayDirection, outResult.rayT );
    }
    """
    if check_runtime_pick_service_store_authority_guardrails_text(
        Path("SkullbonezSource/Runtime/RuntimePickService.cpp"),
        allowed_runtime_pick_service_store_reads,
    ):
        failures.append("Physics-store RuntimePickService synthetic surface was rejected")

    commented_runtime_pick_service_model_reads = """
    bool RuntimePickService::TryPickModel( const RuntimePickRequest& request, RuntimePickResult& outResult )
    {
        // request.models, GameObjects::GameModel, and model.GetCollisionShape() used to live here.
        const std::vector<Physics::PhysicsBodyRecord>& bodies = request.bodyStore->Records();
        return !bodies.empty();
    }
    """
    if check_runtime_pick_service_store_authority_guardrails_text(
        Path("SkullbonezSource/Runtime/RuntimePickService.cpp"),
        commented_runtime_pick_service_model_reads,
    ):
        failures.append("comment-only RuntimePickService model-read synthetic text was rejected")

    old_scene_snapshot_model_physics_reads = """
    bool SceneSnapshotWriter::Save( GameModelCollection& collection, const char* path )
    {
        const std::vector<GameModel>& m_gameModels = collection.Models();
        const Vector3& pos = m_gameModels[i].GetPosition();
        const Vector3& vel = m_gameModels[i].GetVelocity();
        const Vector3& avel = m_gameModels[i].GetAngularVelocity();
        const Vector3& ri = m_gameModels[i].GetRotationalInertia();
        const auto& shape = m_gameModels[i].GetCollisionShape();
        float mass = m_gameModels[i].GetMass();
        float rest = m_gameModels[i].GetCoefficientRestitution();
        bool fixed = m_gameModels[i].IsFixed();
        auto orientation = OrientationJson( m_gameModels[i] );
        return fixed || mass > 0.0f || rest > 0.0f || !shape.valueless_by_exception();
    }
    """
    if not any(
        error.message == "scene snapshot physics state must use stores"
        for error in check_scene_snapshot_store_authority_guardrails_text(
            Path("SkullbonezSource/Scene/SceneSnapshotWriter.cpp"),
            old_scene_snapshot_model_physics_reads,
        )
    ):
        failures.append("old SceneSnapshotWriter GameModel-backed physics reads were not rejected")

    allowed_scene_snapshot_store_reads = """
    bool SceneSnapshotWriter::Save( GameModelCollection& collection, const char* path )
    {
        const PhysicsBodyStore& bodyStore = collection.GetPhysicsBodyStore();
        const ColliderStore& colliderStore = collection.GetColliderStore();
        const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( i );
        const ColliderRecord* collider = colliderStore.RecordForHandle( colliderStore.HandleForModelIndex( i ) );
        const Vector3& pos = body->position;
        const Vector3& vel = body->linearVelocity;
        const Vector3& avel = body->angularVelocity;
        const Vector3& ri = body->rotationalInertia;
        const auto& shape = collider->shape;
        float mass = body->mass;
        float rest = collider->restitution;
        bool fixed = body->isFixed;
        auto orientation = OrientationJson( body->orientation );
        return fixed || mass > 0.0f || rest > 0.0f || !shape.valueless_by_exception();
    }
    """
    if check_scene_snapshot_store_authority_guardrails_text(
        Path("SkullbonezSource/Scene/SceneSnapshotWriter.cpp"),
        allowed_scene_snapshot_store_reads,
    ):
        failures.append("Physics-store SceneSnapshotWriter synthetic surface was rejected")

    commented_scene_snapshot_model_physics_reads = """
    bool SceneSnapshotWriter::Save( GameModelCollection& collection, const char* path )
    {
        // m_gameModels[i].GetPosition(), model.GetMass(), and OrientationJson( m_gameModels[i] ) used to live here.
        const PhysicsBodyStore& bodyStore = collection.GetPhysicsBodyStore();
        const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( i );
        return body != nullptr;
    }
    """
    if check_scene_snapshot_store_authority_guardrails_text(
        Path("SkullbonezSource/Scene/SceneSnapshotWriter.cpp"),
        commented_scene_snapshot_model_physics_reads,
    ):
        failures.append("comment-only SceneSnapshotWriter model physics-read synthetic text was rejected")

    old_game_model_force_bridge = """
    class GameModel
    {
        void ApplyForces( float changeInTime );
        void ApplyWorldForces( float changeInTime );
        void SetWorldForce( const Vector3& force, const Vector3& torque );
        void SetImpulseForce( const Vector3& impulse, const Vector3& point );
        void ClearImpulseForce();
    };
    """
    if not any(
        error.message == "deleted GameModel force bridge is blocked"
        for error in check_deleted_model_force_bridge_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModel.h"),
            old_game_model_force_bridge,
        )
    ):
        failures.append("old GameModel force bridge synthetic surface was not rejected")

    old_world_environment_model_force_bridge = """
    #include "../GameObjects/GameModel.h"
    void WorldEnvironment::AddWorldForces( GameObjects::GameModel& target, float changeInTime )
    {
        target.SetWorldForce( force, torque );
    }
    """
    if not any(
        error.message == "deleted WorldEnvironment model force bridge is blocked"
        for error in check_deleted_model_force_bridge_guardrails_text(
            Path("SkullbonezSource/World/WorldEnvironment.cpp"),
            old_world_environment_model_force_bridge,
        )
    ):
        failures.append("old WorldEnvironment model force bridge synthetic surface was not rejected")

    old_rigid_body_force_bridge = """
    class RigidBody
    {
        bool m_isForceApplied;
        Vector3 m_appliedForce;
        Vector3 m_worldForce;
        void ApplyWorldForce();
        void ApplyImpulseForce();
        void SetImpulseForce( const Vector3& impulse, const Vector3& point );
    };
    """
    if not any(
        error.message == "deleted RigidBody force bridge is blocked"
        for error in check_deleted_model_force_bridge_guardrails_text(
            Path("SkullbonezSource/Physics/RigidBody.h"),
            old_rigid_body_force_bridge,
        )
    ):
        failures.append("old RigidBody force bridge synthetic surface was not rejected")

    old_pending_impulse_model_mirror = """
    void WriteRecordToCompatibilityModel( const PhysicsBodyRecord& record, GameModel& model )
    {
        if ( record.hasPendingImpulse )
        {
            model.SetImpulseForce( record.pendingImpulse, record.pendingImpulseApplicationPoint );
        }
        else
        {
            model.ClearImpulseForce();
        }
    }
    """
    if not any(
        error.message == "pending impulses must not mirror into GameModel"
        for error in check_deleted_model_force_bridge_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsBodyStore.cpp"),
            old_pending_impulse_model_mirror,
        )
    ):
        failures.append("old pending-impulse model mirror synthetic surface was not rejected")

    allowed_store_force_owner = """
    bool PhysicsBodyStore::ApplyForces( const PhysicsWorldForces& worldForces,
                                        const ColliderStore& colliderStore,
                                        int modelIndex,
                                        float deltaSeconds )
    {
        ApplyWorldForces( *record, *collider, worldForces, deltaSeconds );
        ConsumePendingBodyImpulse( *record );
        return true;
    }
    """
    if check_deleted_model_force_bridge_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsBodyStore.cpp"),
        allowed_store_force_owner,
    ):
        failures.append("store-owned force integration synthetic surface was rejected")

    commented_model_force_bridge = """
    class RigidBody
    {
        // ApplyWorldForce(), SetImpulseForce(), m_worldForce, and m_appliedForce were deleted.
        void SetLinearVelocity( const Vector3& velocity );
    };
    """
    if check_deleted_model_force_bridge_guardrails_text(
        Path("SkullbonezSource/Physics/RigidBody.h"),
        commented_model_force_bridge,
    ):
        failures.append("comment-only deleted model force bridge synthetic text was rejected")

    old_diagnostics_model_record_read = """
    void PhysicsDiagnosticsSink::EmitRegressionLog( PhysicsWorld& world, PhysicsModelAccess& modelAccess )
    {
        PhysicsDiagnosticsModelRecord model;
        modelAccess.TryGetPhysicsDiagnosticsModel( i, model );
    }
    """
    if not any(
        error.message == "physics diagnostics model-record access is deleted"
        for error in check_physics_diagnostics_store_authority_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp"),
            old_diagnostics_model_record_read,
        )
    ):
        failures.append("old diagnostics GameModel-sourced record synthetic surface was not rejected")

    deleted_sink_diagnostics_model_record_read = """
    void PhysicsDiagnosticsSink::EmitRegressionLog( PhysicsWorld& world, PhysicsModelAccess& modelAccess )
    {
        PhysicsDiagnosticsModelRecord model;
        modelAccess.TryGetPhysicsDiagnosticsModel( i, bodyStore, colliderStore, model );
    }
    """
    if not any(
        error.message == "physics diagnostics model-record access is deleted"
        for error in check_physics_diagnostics_store_authority_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp"),
            deleted_sink_diagnostics_model_record_read,
        )
    ):
        failures.append("deleted diagnostics sink record synthetic surface was not rejected")

    deleted_skullscope_diagnostics_model_record_read = """
    void SkullScope::EmitFrame( Physics::PhysicsModelAccess& modelAccess )
    {
        PhysicsDiagnosticsModelRecord model;
        modelAccess.TryGetPhysicsDiagnosticsModel( i, bodyStore, colliderStore, model );
    }
    """
    if not any(
        error.message == "physics diagnostics model-record access is deleted"
        for error in check_physics_diagnostics_store_authority_guardrails_text(
            Path("SkullbonezSource/Core/SkullScope.cpp"),
            deleted_skullscope_diagnostics_model_record_read,
        )
    ):
        failures.append("deleted SkullScope diagnostics record synthetic surface was not rejected")

    allowed_skullscope_frame_input = """
    void SkullScope::EmitFrame( const Physics::PhysicsDiagnosticsFrameInput& frame )
    {
        PhysicsDiagnosticsModelRecord model;
        TryBuildPhysicsDiagnosticsModelRecord( i, frame.bodyStore, frame.colliderStore, frame.names, model );
    }
    """
    if check_physics_diagnostics_store_authority_guardrails_text(
        Path("SkullbonezSource/Core/SkullScope.cpp"),
        allowed_skullscope_frame_input,
    ):
        failures.append("SkullScope frame-input synthetic surface was rejected")

    deleted_collision_time_model_name_read = """
    void PhysicsDiagnosticsSink::EmitCollisionTime( PhysicsModelAccess& modelAccess )
    {
        const char* name = "";
        modelAccess.TryGetPhysicsDiagnosticsModelName( bodyA, name );
    }
    void PhysicsWorld::EmitPhysicsCollisionTime( PhysicsModelAccess& modelAccess )
    {
        m_diagnostics.EmitCollisionTime( modelAccess, type, bodyA, bodyB, collisionTime, availableTime );
    }
    """
    if not any(
        error.message == "physics collision-time diagnostics model-name access is deleted"
        for error in check_physics_collision_time_name_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp"),
            deleted_collision_time_model_name_read,
        )
    ):
        failures.append("deleted collision-time diagnostics model-name synthetic surface was not rejected")

    allowed_collision_time_name_view = """
    void PhysicsDiagnosticsSink::EmitCollisionTime( const char* const* diagnosticNames,
                                                    int diagnosticNameCount )
    {
        const PhysicsDiagnosticsNameView names{ diagnosticNames, diagnosticNameCount };
        const char* name = names.NameFor( bodyA );
        Log().Writef( path, "%s", name );
    }
    """
    if check_physics_collision_time_name_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp"),
        allowed_collision_time_name_view,
    ):
        failures.append("collision-time diagnostics name-view synthetic surface was rejected")

    commented_collision_time_model_name_read = """
    void PhysicsDiagnosticsSink::EmitCollisionTime( const char* const* diagnosticNames )
    {
        // modelAccess.TryGetPhysicsDiagnosticsModelName(bodyA, name) is deleted.
        const PhysicsDiagnosticsNameView names{ diagnosticNames, diagnosticNameCount };
    }
    """
    if check_physics_collision_time_name_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp"),
        commented_collision_time_model_name_read,
    ):
        failures.append("comment-only collision-time diagnostics model-name synthetic text was rejected")

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

    old_prediction_model_state_capture = """
    bool CaptureReplayPredictionBodyState( GameModelCollection& modelCollection )
    {
        const GameModel* model = modelCollection.TryGetModel( i );
        backup.id.value = model->GetReplayBodyId();
        backup.position = model->GetPosition();
        backup.linearVelocity = model->GetVelocity();
        backup.mass = model->GetMass();
        backup.fixed = model->IsFixed();
        return true;
    }
    """
    if not any(
        error.message == "replay prediction model-state capture is blocked"
        for error in check_replay_prediction_body_capture_store_authority_guardrails_text(
            Path("SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl"),
            old_prediction_model_state_capture,
        )
    ):
        failures.append("old replay prediction model-state capture synthetic surface was not rejected")

    old_prediction_refreshing_body_store_capture = """
    bool CaptureReplayPredictionBodyState( GameModelCollection& modelCollection )
    {
        const PhysicsBodyStore& bodyStore = modelCollection.GetPhysicsBodyStore();
        Use( bodyStore );
        return true;
    }
    """
    if not any(
        error.message == "replay prediction model-state capture is blocked"
        for error in check_replay_prediction_body_capture_store_authority_guardrails_text(
            Path("SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl"),
            old_prediction_refreshing_body_store_capture,
        )
    ):
        failures.append("old replay prediction refreshing body-store synthetic surface was not rejected")

    store_owned_prediction_capture = """
    bool CaptureReplayPredictionBodyState( GameModelCollection& modelCollection )
    {
        const std::vector<PhysicsBodyRecord>& bodyRecords = modelCollection.GetPhysicsEngine().BodyStore().Records();
        const GameModel* model = modelCollection.TryGetModel( i );
        const PhysicsBodyRecord& body = bodyRecords[i];
        backup.id.value = body.replayBodyId;
        backup.position = body.position;
        backup.linearVelocity = body.linearVelocity;
        backup.mass = body.mass;
        backup.fixed = body.isFixed;
        backup.fixedContactHighlightSeconds = model->GetFixedContactHighlightSeconds();
        return true;
    }
    """
    if check_replay_prediction_body_capture_store_authority_guardrails_text(
        Path("SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl"),
        store_owned_prediction_capture,
    ):
        failures.append("store-owned replay prediction capture synthetic surface was rejected")

    commented_prediction_model_state_capture = """
    bool CaptureReplayPredictionBodyState( GameModelCollection& modelCollection )
    {
        // model->GetVelocity() and modelCollection.GetPhysicsBodyStore() used to live here.
        const PhysicsBodyRecord& body = bodyRecords[i];
        backup.linearVelocity = body.linearVelocity;
        return true;
    }
    """
    if check_replay_prediction_body_capture_store_authority_guardrails_text(
        Path("SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl"),
        commented_prediction_model_state_capture,
    ):
        failures.append("comment-only replay prediction model-state synthetic text was rejected")

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

    old_pending_impulse_model_mirror = """
    void PhysicsScene::SetPendingBodyImpulse( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        m_bodyStore.SetPendingBodyImpulse( body, impulse, localPoint );
        modelAccess.WriteBackPhysicsBody( m_bodyStore, bodyIndex );
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if not any(
        error.message == "pending impulse model mirror is blocked"
        for error in check_physics_scene_pending_impulse_model_mirror_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
            old_pending_impulse_model_mirror,
        )
    ):
        failures.append("old pending-impulse model mirror synthetic surface was not rejected")

    allowed_pending_impulse_store_only = """
    void PhysicsScene::SetPendingBodyImpulse( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        const int modelCount = modelAccess.ModelCount();
        if ( m_bodyStore.Count() != modelCount )
        {
            RefreshBodyStore( modelAccess );
        }
        m_bodyStore.SetPendingBodyImpulse( body, impulse, localPoint );
    }
    """
    if check_physics_scene_pending_impulse_model_mirror_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_pending_impulse_store_only,
    ):
        failures.append("store-only pending impulse synthetic surface was rejected")

    allowed_apply_body_impulse_mirror = """
    void PhysicsScene::ApplyBodyImpulse( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        SetPendingBodyImpulse( modelAccess, body, impulse, localPoint );
        WakeBody( modelAccess, body );
    }
    void PhysicsScene::WakeBody( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        modelAccess.WriteBackPhysicsBody( m_bodyStore, index );
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if check_physics_scene_pending_impulse_model_mirror_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_apply_body_impulse_mirror,
    ):
        failures.append("non-pending impulse compatibility mirror synthetic surface was rejected")

    commented_pending_impulse_model_mirror = """
    void PhysicsScene::SetPendingBodyImpulse( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        // modelAccess.WriteBackPhysicsBody(m_bodyStore, bodyIndex) used to mirror pending impulses.
        // modelAccess.InvalidatePhysicsStreams() used to rebuild model streams here.
        m_bodyStore.SetPendingBodyImpulse( body, impulse, localPoint );
    }
    """
    if check_physics_scene_pending_impulse_model_mirror_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        commented_pending_impulse_model_mirror,
    ):
        failures.append("comment-only pending impulse model mirror synthetic text was rejected")

    old_velocity_model_mirror = """
    bool PhysicsScene::SetBodyVelocity( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        m_bodyStore.SetBodyVelocity( body, linearVelocity, angularVelocity );
        modelAccess.WriteBackPhysicsBody( m_bodyStore, bodyIndex );
        modelAccess.InvalidatePhysicsStreams();
        return true;
    }
    """
    if not any(
        error.message == "velocity edit model mirror is blocked"
        for error in check_physics_scene_velocity_model_mirror_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
            old_velocity_model_mirror,
        )
    ):
        failures.append("old SetBodyVelocity model mirror synthetic surface was not rejected")

    allowed_velocity_store_only = """
    bool PhysicsScene::SetBodyVelocity( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        const int modelCount = modelAccess.ModelCount();
        if ( m_bodyStore.Count() != modelCount )
        {
            RefreshBodyStore( modelAccess );
        }
        const int index = m_bodyStore.ModelIndexForHandle( body );
        if ( index < 0 || !m_bodyStore.SetBodyVelocity( body, linearVelocity, angularVelocity ) )
        {
            return false;
        }
        if ( wakeIfMoving )
        {
            m_world.WakeModel( m_bodyStore, index );
            m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
        }
        return true;
    }
    """
    if check_physics_scene_velocity_model_mirror_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_velocity_store_only,
    ):
        failures.append("store-only velocity edit synthetic surface was rejected")

    allowed_wake_body_mirror_after_velocity = """
    bool PhysicsScene::SetBodyVelocity( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        m_bodyStore.SetBodyVelocity( body, linearVelocity, angularVelocity );
        return true;
    }
    void PhysicsScene::WakeBody( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        modelAccess.WriteBackPhysicsBody( m_bodyStore, index );
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if check_physics_scene_velocity_model_mirror_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_wake_body_mirror_after_velocity,
    ):
        failures.append("non-velocity wake compatibility mirror synthetic surface was rejected")

    commented_velocity_model_mirror = """
    bool PhysicsScene::SetBodyVelocity( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        // modelAccess.WriteBackPhysicsBody(m_bodyStore, bodyIndex) used to mirror velocity edits.
        // modelAccess.InvalidatePhysicsStreams() used to rebuild model streams here.
        m_bodyStore.SetBodyVelocity( body, linearVelocity, angularVelocity );
        return true;
    }
    """
    if check_physics_scene_velocity_model_mirror_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        commented_velocity_model_mirror,
    ):
        failures.append("comment-only velocity edit model mirror synthetic text was rejected")

    old_velocity_model_access_overloads = """
    bool PhysicsEngine::SetBodyVelocity( PhysicsModelAccess& modelAccess,
                                         PhysicsBodyHandle body,
                                         const Vector3& linearVelocity,
                                         const Vector3& angularVelocity,
                                         bool wakeIfMoving )
    {
        return m_scene.SetBodyVelocity( modelAccess, body, linearVelocity, angularVelocity, wakeIfMoving );
    }
    bool PhysicsScene::SetBodyVelocity( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        return m_bodyStore.SetBodyVelocity( body, linearVelocity, angularVelocity );
    }
    """
    if not any(
        error.message == "velocity model-access overload is blocked"
        for error in check_physics_velocity_model_access_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsEngine.cpp"),
            old_velocity_model_access_overloads,
        )
    ):
        failures.append("old SetBodyVelocity model-access overload synthetic surface was not rejected")

    old_velocity_model_access_call = """
    void ApplyReplayVelocityEditToModel( GameModelCollection& modelCollection )
    {
        PhysicsModelAccess modelAccess( modelCollection );
        modelCollection.GetPhysicsEngine().SetBodyVelocity( modelAccess, body, linearVelocity, angularVelocity, true );
    }
    """
    if not any(
        error.message == "velocity model-access call is blocked"
        for error in check_physics_velocity_model_access_guardrails_text(
            Path("SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.inl"),
            old_velocity_model_access_call,
        )
    ):
        failures.append("old SetBodyVelocity model-access call synthetic surface was not rejected")

    allowed_velocity_handle_command = """
    PhysicsBodyHandle GameModelCollectionPhysicsAdapter::BodyHandleForVelocityCommand( int modelIndex,
                                                                                       bool wakeIfMoving ) const
    {
        return wakeIfMoving ? BodyHandleForWakeCommand( modelIndex ) : BodyHandleForModelIndex( modelIndex );
    }
    void ApplyReplayVelocityEditToModel( GameModelCollection& modelCollection )
    {
        GameModelCollectionPhysicsAdapter physicsBodies( modelCollection );
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForVelocityCommand( modelIndex, true );
        modelCollection.GetPhysicsEngine().SetBodyVelocity( body, linearVelocity, angularVelocity, true );
    }
    bool PhysicsScene::SetBodyVelocity( PhysicsBodyHandle body )
    {
        return m_bodyStore.SetBodyVelocity( body, linearVelocity, angularVelocity );
    }
    """
    if check_physics_velocity_model_access_guardrails_text(
        Path("SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.inl"),
        allowed_velocity_handle_command,
    ):
        failures.append("body-only velocity command synthetic surface was rejected")

    commented_velocity_model_access = """
    void DocumentOldVelocityCommand()
    {
        // modelCollection.GetPhysicsEngine().SetBodyVelocity(modelAccess, body, linearVelocity, angularVelocity, true) used to run here.
        // bool PhysicsScene::SetBodyVelocity(PhysicsModelAccess& modelAccess, PhysicsBodyHandle body) was deleted.
        modelCollection.GetPhysicsEngine().SetBodyVelocity( body, linearVelocity, angularVelocity, true );
    }
    """
    if check_physics_velocity_model_access_guardrails_text(
        Path("SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.inl"),
        commented_velocity_model_access,
    ):
        failures.append("comment-only velocity model-access synthetic text was rejected")

    old_wake_body_model_mirror = """
    void PhysicsScene::WakeBody( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        m_world.WakeModel( m_bodyStore, index );
        m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
        modelAccess.WriteBackPhysicsBody( m_bodyStore, index );
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if not any(
        error.message == "wake command model mirror is blocked"
        for error in check_physics_scene_wake_body_model_mirror_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
            old_wake_body_model_mirror,
        )
    ):
        failures.append("old WakeBody model mirror synthetic surface was not rejected")

    allowed_wake_body_store_only = """
    void PhysicsScene::WakeBody( PhysicsBodyHandle body )
    {
        const int index = m_bodyStore.ModelIndexForHandle( body );
        if ( index < 0 )
        {
            return;
        }
        m_world.WakeModel( m_bodyStore, index );
        m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    }
    void PhysicsScene::ApplyBodyImpulse( PhysicsBodyHandle body )
    {
        SetPendingBodyImpulse( body, impulse, localPoint );
        WakeBody( body );
    }
    """
    if check_physics_scene_wake_body_model_mirror_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_wake_body_store_only,
    ):
        failures.append("store-only WakeBody synthetic surface was rejected")

    commented_wake_body_model_mirror = """
    void PhysicsScene::WakeBody( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        // modelAccess.WriteBackPhysicsBody(m_bodyStore, index) used to mirror wake state.
        // modelAccess.InvalidatePhysicsStreams() used to rebuild model streams here.
        m_world.WakeModel( m_bodyStore, index );
        m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
    }
    """
    if check_physics_scene_wake_body_model_mirror_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        commented_wake_body_model_mirror,
    ):
        failures.append("comment-only WakeBody model mirror synthetic text was rejected")

    old_wake_apply_model_access_overloads = """
    void PhysicsScene::WakeBody( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        m_world.WakeModel( m_bodyStore, index );
    }
    void PhysicsScene::ApplyBodyImpulse( PhysicsModelAccess& modelAccess,
                                         PhysicsBodyHandle body,
                                         const Vector3& impulse,
                                         const Vector3& localPoint )
    {
        SetPendingBodyImpulse( body, impulse, localPoint );
        WakeBody( modelAccess, body );
    }
    """
    if not any(
        error.message == "wake/apply model-access overload is blocked"
        for error in check_physics_wake_apply_model_access_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
            old_wake_apply_model_access_overloads,
        )
    ):
        failures.append("old wake/apply model-access overload synthetic surface was not rejected")

    old_wake_apply_model_access_calls = """
    void ApplyLauncherPhysicsImpulse( PhysicsEngine& physics, PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        physics.ApplyBodyImpulse( modelAccess, body, impulse, localPoint );
        physics.WakeBody( modelAccess, body );
    }
    """
    if not any(
        error.message == "wake/apply model-access call is blocked"
        for error in check_physics_wake_apply_model_access_guardrails_text(
            Path("SkullbonezSource/Runtime/Tools/RuntimeTools.cpp"),
            old_wake_apply_model_access_calls,
        )
    ):
        failures.append("old wake/apply model-access call synthetic surface was not rejected")

    allowed_wake_apply_body_commands = """
    PhysicsBodyHandle GameModelCollectionPhysicsAdapter::BodyHandleForWakeCommand( int modelIndex ) const
    {
        const int modelCount = m_collection.GetModelCount();
        PhysicsModelAccess modelAccess( m_collection );
        if ( m_collection.m_physicsEngine.Colliders().Count() != modelCount )
        {
            m_collection.m_physicsEngine.RefreshColliderStore( modelAccess );
        }
        return m_collection.m_physicsEngine.BodyStore().HandleForModelIndex( modelIndex );
    }
    void GameModelCollectionPhysicsAdapter::ApplyBodyImpulseForModelIndex( int modelIndex ) const
    {
        const PhysicsBodyHandle body = BodyHandleForWakeCommand( modelIndex );
        m_collection.m_physicsEngine.ApplyBodyImpulse( body, impulse, localPoint );
        m_collection.m_physicsEngine.WakeBody( body );
    }
    void PhysicsScene::ApplyBodyImpulse( PhysicsBodyHandle body )
    {
        SetPendingBodyImpulse( body, impulse, localPoint );
        WakeBody( body );
    }
    """
    if check_physics_wake_apply_model_access_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollectionPhysicsAdapter.cpp"),
        allowed_wake_apply_body_commands,
    ):
        failures.append("body-only wake/apply synthetic surface was rejected")

    commented_wake_apply_model_access = """
    void PhysicsScene::WakeBody( PhysicsBodyHandle body )
    {
        // physics.WakeBody(modelAccess, body) used to borrow the model owner here.
        // void PhysicsScene::ApplyBodyImpulse(PhysicsModelAccess& modelAccess, PhysicsBodyHandle body) was deleted.
        m_world.WakeModel( m_bodyStore, index );
    }
    """
    if check_physics_wake_apply_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        commented_wake_apply_model_access,
    ):
        failures.append("comment-only wake/apply model-access synthetic text was rejected")

    old_seed_body_asleep_model_access_overload = """
    void PhysicsScene::SeedBodyAsleep( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        modelAccess.WriteBackPhysicsBody( m_bodyStore, index );
        modelAccess.InvalidatePhysicsStreams();
    }
    """
    if not any(
        error.message == "sleep seed model-access overload is blocked"
        for error in check_physics_seed_body_asleep_model_access_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
            old_seed_body_asleep_model_access_overload,
        )
    ):
        failures.append("old sleep seed model-access overload synthetic surface was not rejected")

    old_seed_body_asleep_model_access_call = """
    void SeedEditorPhysicsBodyAsleep( PhysicsEngine& physics, PhysicsModelAccess& modelAccess, PhysicsBodyHandle body )
    {
        physics.SeedBodyAsleep( modelAccess, body );
    }
    """
    if not any(
        error.message == "sleep seed model-access call is blocked"
        for error in check_physics_seed_body_asleep_model_access_guardrails_text(
            Path("SkullbonezSource/Runtime/Editor/RunEditorTools.cpp"),
            old_seed_body_asleep_model_access_call,
        )
    ):
        failures.append("old sleep seed model-access call synthetic surface was not rejected")

    allowed_seed_body_asleep_store_command = """
    void PhysicsScene::SeedBodyAsleep( PhysicsBodyHandle body )
    {
        const int index = m_bodyStore.ModelIndexForHandle( body );
        if ( m_bodyStore.SeedBodyAsleep( body ) )
        {
            m_world.SeedModelAsleep( m_bodyStore, index );
            m_bodyStore.CopySleepStatesFrom( m_world.GetSleepStates() );
        }
    }
    void SeedEditorPhysicsBodyAsleep( PhysicsEngine& physics, PhysicsBodyHandle body )
    {
        physics.SeedBodyAsleep( body );
    }
    """
    if check_physics_seed_body_asleep_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_seed_body_asleep_store_command,
    ):
        failures.append("store-only sleep seed synthetic surface was rejected")

    commented_seed_body_asleep_model_access = """
    void PhysicsScene::SeedBodyAsleep( PhysicsBodyHandle body )
    {
        // physics.SeedBodyAsleep(modelAccess, body) used to mirror GameModel state here.
        // void PhysicsScene::SeedBodyAsleep(PhysicsModelAccess& modelAccess, PhysicsBodyHandle body) was deleted.
        m_bodyStore.SeedBodyAsleep( body );
    }
    """
    if check_physics_seed_body_asleep_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        commented_seed_body_asleep_model_access,
    ):
        failures.append("comment-only sleep seed model-access synthetic text was rejected")

    old_pending_impulse_model_access_overload = """
    void PhysicsScene::SetPendingBodyImpulse( PhysicsModelAccess& modelAccess,
                                              PhysicsBodyHandle body,
                                              const Vector3& impulse,
                                              const Vector3& localPoint )
    {
        RefreshBodyStore( modelAccess );
        m_bodyStore.SetPendingBodyImpulse( body, impulse, localPoint );
    }
    """
    if not any(
        error.message == "pending impulse model-access overload is blocked"
        for error in check_physics_pending_impulse_model_access_guardrails_text(
            Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
            old_pending_impulse_model_access_overload,
        )
    ):
        failures.append("old pending impulse model-access overload synthetic surface was not rejected")

    old_pending_impulse_model_access_call = """
    void GameModelCollectionPhysicsAdapter::SetPendingBodyImpulseForModelIndex( int modelIndex ) const
    {
        PhysicsModelAccess modelAccess( m_collection );
        m_collection.m_physicsEngine.SetPendingBodyImpulse( modelAccess, body, impulse, localPoint );
    }
    """
    if not any(
        error.message == "pending impulse model-access call is blocked"
        for error in check_physics_pending_impulse_model_access_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollectionPhysicsAdapter.cpp"),
            old_pending_impulse_model_access_call,
        )
    ):
        failures.append("old pending impulse model-access call synthetic surface was not rejected")

    allowed_pending_impulse_store_command = """
    void PhysicsScene::SetPendingBodyImpulse( PhysicsBodyHandle body, const Vector3& impulse, const Vector3& localPoint )
    {
        m_bodyStore.SetPendingBodyImpulse( body, impulse, localPoint );
    }
    void PhysicsScene::ApplyBodyImpulse( PhysicsModelAccess& modelAccess,
                                         PhysicsBodyHandle body,
                                         const Vector3& impulse,
                                         const Vector3& localPoint )
    {
        SetPendingBodyImpulse( body, impulse, localPoint );
        WakeBody( modelAccess, body );
    }
    """
    if check_physics_pending_impulse_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        allowed_pending_impulse_store_command,
    ):
        failures.append("store-only pending impulse synthetic surface was rejected")

    commented_pending_impulse_model_access = """
    void PhysicsScene::SetPendingBodyImpulse( PhysicsBodyHandle body, const Vector3& impulse, const Vector3& localPoint )
    {
        // physics.SetPendingBodyImpulse(modelAccess, body, impulse, localPoint) used to refresh model state here.
        // void PhysicsScene::SetPendingBodyImpulse(PhysicsModelAccess& modelAccess, PhysicsBodyHandle body) was deleted.
        m_bodyStore.SetPendingBodyImpulse( body, impulse, localPoint );
    }
    """
    if check_physics_pending_impulse_model_access_guardrails_text(
        Path("SkullbonezSource/Physics/PhysicsScene.cpp"),
        commented_pending_impulse_model_access,
    ):
        failures.append("comment-only pending impulse model-access synthetic text was rejected")

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

    old_scene_setup_adapter_body_lookup = """
    void SceneGeneratedSetup::SetUpGameModels( SceneGeneratedModelContext context )
    {
        GameModelCollectionPhysicsAdapter physicsBodies( context.models );
        const int modelIndex = context.models.GetModelCount();
        context.models.AddGameModel( std::move( model ) );
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForModelIndex( modelIndex );
        context.physics.SetPendingBodyImpulse( body, force, forcePos );
    }
    """
    if not any(
        error.message == "scene setup adapter body lookup is blocked"
        for error in check_scene_setup_model_index_physics_command_guardrails_text(
            Path("SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp"),
            old_scene_setup_adapter_body_lookup,
        )
    ):
        failures.append("old scene setup adapter body lookup synthetic surface was not rejected")

    allowed_scene_setup_handle_command = """
    void SceneAuthoredSetup::SetUpGameModels( SceneAuthoredModelContext context )
    {
        const PhysicsBodyHandle body = context.models.AddGameModel( std::move( model ) );
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
    void WakeEditorPhysicsBody( GameModelCollection& collection, int modelIndex )
    {
        GameModelCollectionPhysicsAdapter physicsBodies( collection );
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForVelocityCommand( modelIndex, true );
        collection.GetPhysicsEngine().WakeBody( body );
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

    old_editor_adapter_command = """
    void WakeEditorPhysicsBody( GameModelCollection& collection, int modelIndex )
    {
        GameModelCollectionPhysicsAdapter physicsBodies( collection );
        physicsBodies.WakeBodyForModelIndex( modelIndex );
    }
    """
    if not any(
        error.message == "editor adapter command wrapper is blocked"
        for error in check_editor_model_index_physics_command_guardrails_text(
            Path("SkullbonezSource/Runtime/Editor/RunEditorTools.cpp"),
            old_editor_adapter_command,
        )
    ):
        failures.append("old editor adapter command synthetic surface was not rejected")

    commented_editor_adapter_command = """
    void DocumentOldEditorAdapterCommand()
    {
        // physicsBodies.WakeBodyForModelIndex(modelIndex) used to run here.
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForVelocityCommand( modelIndex, true );
        collection.GetPhysicsEngine().WakeBody( body );
    }
    """
    if check_editor_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Editor/RunEditorTools.cpp"),
        commented_editor_adapter_command,
    ):
        failures.append("comment-only editor adapter command synthetic text was rejected")

    old_mouse_pickup_model_index_command = """
    void Run::ApplyMousePickupPhysicsStep()
    {
        m_cGameModelCollection.ApplyBodyImpulse( modelIndex, impulse, ZERO_VECTOR );
        m_cGameModelCollection.TrySetModelAngularVelocity( modelIndex, angularVelocity );
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
        const PhysicsBodyRecord* body = bodyStore.RecordForHandle( pickup.body );
        m_cGameModelCollection.GetPhysicsEngine().SetBodyVelocity( pickup.body,
                                                                   body->linearVelocity,
                                                                   pickup.preservedAngularVelocity,
                                                                   false );
        m_cGameModelCollection.GetPhysicsEngine().ApplyBodyImpulse( pickup.body, impulse, ZERO_VECTOR );
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
        // m_cGameModelCollection.TrySetModelAngularVelocity(modelIndex, angularVelocity) used to run here.
        m_cGameModelCollection.GetPhysicsEngine().ApplyBodyImpulse( pickup.body, impulse, ZERO_VECTOR );
    }
    """
    if check_mouse_pickup_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Editor/RunMousePickupTools.inl"),
        commented_mouse_pickup_model_index_command,
    ):
        failures.append("comment-only mouse pickup model-index command synthetic text was rejected")

    old_mouse_pickup_game_model_body_read = """
    void Run::ApplyMousePickupPhysicsStep()
    {
        const GameModel* picked = m_cGameModelCollection.TryGetModel( pickedIndex );
        grabOffset = grabPoint - picked->GetPosition();
        preservedAngularVelocity = picked->GetAngularVelocity();
        if ( model->IsFixed() )
        {
            return;
        }
        Vector3 impulse = pull - model->GetVelocity();
    }
    """
    if not any(
        error.message == "mouse pickup GameModel body read is blocked"
        for error in check_mouse_pickup_model_index_physics_command_guardrails_text(
            Path("SkullbonezSource/Runtime/Editor/RunMousePickupTools.inl"),
            old_mouse_pickup_game_model_body_read,
        )
    ):
        failures.append("old mouse pickup GameModel body read synthetic surface was not rejected")

    allowed_mouse_pickup_body_store_read = """
    void Run::ApplyMousePickupPhysicsStep()
    {
        const PhysicsBodyRecord* body = bodyStore.RecordForHandle( pickup.body );
        const Vector3 grabPoint = body->position + pickup.grabOffset;
        const Vector3 impulse = pull - body->linearVelocity;
        if ( body->isFixed )
        {
            return;
        }
    }
    """
    if check_mouse_pickup_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Editor/RunMousePickupTools.inl"),
        allowed_mouse_pickup_body_store_read,
    ):
        failures.append("store-owned mouse pickup body read synthetic surface was rejected")

    commented_mouse_pickup_game_model_body_read = """
    void DocumentOldMousePickupBodyRead()
    {
        // picked->GetPosition() and model->GetVelocity() used to run here.
        const Vector3 grabPoint = body->position + pickup.grabOffset;
    }
    """
    if check_mouse_pickup_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Editor/RunMousePickupTools.inl"),
        commented_mouse_pickup_game_model_body_read,
    ):
        failures.append("comment-only mouse pickup GameModel body read synthetic text was rejected")

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
        GameModelCollectionPhysicsAdapter physicsBodies( collection );
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForVelocityCommand( modelHitIndex, true );
        collection.GetPhysicsEngine().ApplyBodyImpulse( body, impulse, localPoint );
        collection.GetPhysicsEngine().WakeBody( body );
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

    old_launcher_adapter_command = """
    void RuntimeTools::FireLauncherLaser( GameModelCollection& collection )
    {
        GameModelCollectionPhysicsAdapter physicsBodies( collection );
        physicsBodies.ApplyBodyImpulseForModelIndex( modelHitIndex, impulse, localPoint );
        physicsBodies.WakeBodyForModelIndex( projectileIndex );
    }
    """
    if not any(
        error.message == "launcher adapter command wrapper is blocked"
        for error in check_launcher_model_index_physics_command_guardrails_text(
            Path("SkullbonezSource/Runtime/Tools/RuntimeTools.cpp"),
            old_launcher_adapter_command,
        )
    ):
        failures.append("old launcher adapter command synthetic surface was not rejected")

    commented_launcher_adapter_command = """
    void DocumentOldLauncherAdapterCommand()
    {
        // physicsBodies.ApplyBodyImpulseForModelIndex(modelHitIndex, impulse, localPoint) used to run here.
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForVelocityCommand( modelHitIndex, true );
        collection.GetPhysicsEngine().ApplyBodyImpulse( body, impulse, localPoint );
    }
    """
    if check_launcher_model_index_physics_command_guardrails_text(
        Path("SkullbonezSource/Runtime/Tools/RuntimeTools.cpp"),
        commented_launcher_adapter_command,
    ):
        failures.append("comment-only launcher adapter command synthetic text was rejected")

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
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForVelocityCommand( modelIndex, true );
        modelCollection.GetPhysicsEngine().SetBodyVelocity( body, linearVelocity, angularVelocity, true );
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
        modelCollection.GetPhysicsEngine().SetBodyVelocity( body, linearVelocity, angularVelocity, true );
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
        GameModelCollectionPhysicsAdapter physicsBodies( m_cGameModelCollection );
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForVelocityCommand( event.value0, true );
        if ( body.IsValid() )
        {
            m_cGameModelCollection.GetPhysicsEngine().WakeBody( body );
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
        m_cGameModelCollection.GetPhysicsEngine().WakeBody( body );
    }
    """
    if check_run_frame_replay_editor_transform_wake_guardrails_text(
        Path("SkullbonezSource/Runtime/RunFrame.cpp"),
        commented_run_frame_replay_editor_transform_wake,
    ):
        failures.append("comment-only RunFrame replay editor transform wake synthetic text was rejected")

    old_run_frame_replay_editor_transform_adapter_wake = """
    bool ApplyReplayEditorTransformEvent()
    {
        if ( !model.IsFixed() )
        {
            GameModelCollectionPhysicsAdapter( m_cGameModelCollection ).WakeBodyForModelIndex( event.value0 );
        }
        return true;
    }
    """
    if not any(
        error.message == "RunFrame replay editor transform adapter wake wrapper is blocked"
        for error in check_run_frame_replay_editor_transform_wake_guardrails_text(
            Path("SkullbonezSource/Runtime/RunFrame.cpp"),
            old_run_frame_replay_editor_transform_adapter_wake,
        )
    ):
        failures.append("old RunFrame replay editor transform adapter wake synthetic surface was not rejected")

    commented_run_frame_replay_editor_transform_adapter_wake = """
    void DocumentOldReplayEditorTransformAdapterWake()
    {
        // GameModelCollectionPhysicsAdapter( m_cGameModelCollection ).WakeBodyForModelIndex(event.value0) used to run here.
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForVelocityCommand( event.value0, true );
        m_cGameModelCollection.GetPhysicsEngine().WakeBody( body );
    }
    """
    if check_run_frame_replay_editor_transform_wake_guardrails_text(
        Path("SkullbonezSource/Runtime/RunFrame.cpp"),
        commented_run_frame_replay_editor_transform_adapter_wake,
    ):
        failures.append(
            "comment-only RunFrame replay editor transform adapter wake synthetic text was rejected"
        )

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
        GameModelCollectionPhysicsAdapter physicsBodies( *this );
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForVelocityCommand( sourceIndex, true );
        m_physicsEngine.WakeBody( body );
    }
    """
    if check_deleted_game_model_collection_physics_wrapper_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        allowed_game_model_collection_adapter_use,
    ):
        failures.append("GameModelCollection adapter handle resolver synthetic surface was rejected")

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

    old_game_model_collection_adapter_command_declaration = """
    class GameModelCollectionPhysicsAdapter
    {
        void WakeBodyForModelIndex( int modelIndex ) const;
        void ApplyBodyImpulseForModelIndex( int modelIndex ) const;
    };
    """
    if not any(
        error.message == "deleted GameModelCollectionPhysicsAdapter command wrapper is blocked"
        for error in check_deleted_game_model_collection_physics_adapter_command_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollectionPhysicsAdapter.h"),
            old_game_model_collection_adapter_command_declaration,
        )
    ):
        failures.append("old GameModelCollectionPhysicsAdapter command declaration synthetic surface was not rejected")

    old_game_model_collection_adapter_command_definition = """
    void GameModelCollectionPhysicsAdapter::SetPendingBodyImpulseForModelIndex( int index ) const
    {
        m_collection.GetPhysicsEngine().SetPendingBodyImpulse( BodyHandleForModelIndex( index ), impulse, point );
    }
    """
    if not any(
        error.message == "deleted GameModelCollectionPhysicsAdapter command wrapper is blocked"
        for error in check_deleted_game_model_collection_physics_adapter_command_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollectionPhysicsAdapter.cpp"),
            old_game_model_collection_adapter_command_definition,
        )
    ):
        failures.append("old GameModelCollectionPhysicsAdapter command definition synthetic surface was not rejected")

    old_game_model_collection_adapter_command_call = """
    void GameModelCollection::ReleaseAttachedFixedTreeParts( int sourceIndex )
    {
        GameModelCollectionPhysicsAdapter( *this ).WakeBodyForModelIndex( sourceIndex );
    }
    """
    if not any(
        error.message == "deleted GameModelCollectionPhysicsAdapter command wrapper is blocked"
        for error in check_deleted_game_model_collection_physics_adapter_command_guardrails_text(
            Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
            old_game_model_collection_adapter_command_call,
        )
    ):
        failures.append("old GameModelCollectionPhysicsAdapter command call synthetic surface was not rejected")

    allowed_game_model_collection_adapter_handle_resolver = """
    void GameModelCollection::ReleaseAttachedFixedTreeParts( int sourceIndex )
    {
        GameModelCollectionPhysicsAdapter physicsBodies( *this );
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForVelocityCommand( sourceIndex, true );
        m_physicsEngine.WakeBody( body );
    }
    """
    if check_deleted_game_model_collection_physics_adapter_command_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        allowed_game_model_collection_adapter_handle_resolver,
    ):
        failures.append("GameModelCollectionPhysicsAdapter handle resolver synthetic surface was rejected")

    commented_game_model_collection_adapter_command = """
    void DocumentDeletedAdapterCommands()
    {
        // GameModelCollectionPhysicsAdapter( *this ).WakeBodyForModelIndex(index) used to live here.
        const PhysicsBodyHandle body = physicsBodies.BodyHandleForVelocityCommand( index, true );
    }
    """
    if check_deleted_game_model_collection_physics_adapter_command_guardrails_text(
        Path("SkullbonezSource/GameObjects/GameModelCollection.cpp"),
        commented_game_model_collection_adapter_command,
    ):
        failures.append("comment-only GameModelCollectionPhysicsAdapter command synthetic text was rejected")

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
    errors.extend(check_runtime_pick_service_store_authority_guardrails(repo))
    errors.extend(check_scene_snapshot_store_authority_guardrails(repo))
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
    errors.extend(check_public_physics_descriptor_model_index_guardrails(repo))
    errors.extend(check_standalone_physics_implementation_game_object_guardrails(repo))
    errors.extend(check_deleted_migration_artifact_guardrails(repo))
    errors.extend(check_deleted_physics_model_view_guardrails(repo))
    errors.extend(check_persistent_solver_context_model_access_guardrails(repo))
    errors.extend(check_physics_world_solver_body_writeback_guardrails(repo))
    errors.extend(check_physics_world_solver_model_stream_guardrails(repo))
    errors.extend(check_physics_world_contact_highlight_tick_guardrails(repo))
    errors.extend(check_physics_world_fixed_contact_notify_guardrails(repo))
    errors.extend(check_physics_world_persistent_contact_tree_release_guardrails(repo))
    errors.extend(check_physics_world_tornado_release_model_access_guardrails(repo))
    errors.extend(check_physics_world_step_model_access_signature_guardrails(repo))
    errors.extend(check_physics_engine_step_model_access_signature_guardrails(repo))
    errors.extend(check_physics_scene_step_model_access_signature_guardrails(repo))
    errors.extend(check_simulation_system_owner_borrow_guardrails(repo))
    errors.extend(check_physics_model_access_deleted_step_facade_guardrails(repo))
    errors.extend(check_physics_world_store_seed_model_access_guardrails(repo))
    errors.extend(check_physics_world_store_wake_model_access_guardrails(repo))
    errors.extend(check_physics_world_deleted_model_stream_wake_seed_guardrails(repo))
    errors.extend(check_physics_world_run_invalidation_guardrails(repo))
    errors.extend(check_physics_world_run_writeback_guardrails(repo))
    errors.extend(check_physics_world_run_diagnostics_guardrails(repo))
    errors.extend(check_physics_world_step_diagnostics_model_access_guardrails(repo))
    errors.extend(check_render_instance_store_authority_guardrails(repo))
    errors.extend(check_game_model_renderer_render_instance_authority_guardrails(repo))
    errors.extend(check_dxr_render_instance_matrix_authority_guardrails(repo))
    errors.extend(check_deleted_game_model_stream_project_guardrails(repo))
    errors.extend(check_game_model_collection_body_store_read_authority_guardrails(repo))
    errors.extend(check_game_model_collection_run_physics_model_access_guardrails(repo))
    errors.extend(check_physics_diagnostics_store_authority_guardrails(repo))
    errors.extend(check_physics_collision_time_name_guardrails(repo))
    errors.extend(check_deleted_model_force_bridge_guardrails(repo))
    errors.extend(check_replay_recorder_store_authority_guardrails(repo))
    errors.extend(check_replay_prediction_body_capture_store_authority_guardrails(repo))
    errors.extend(check_replay_restore_store_authority_guardrails(repo))
    errors.extend(check_physics_scene_step_body_reload_guardrails(repo))
    errors.extend(check_physics_scene_pending_impulse_model_mirror_guardrails(repo))
    errors.extend(check_physics_scene_velocity_model_mirror_guardrails(repo))
    errors.extend(check_physics_velocity_model_access_guardrails(repo))
    errors.extend(check_physics_scene_wake_body_model_mirror_guardrails(repo))
    errors.extend(check_physics_wake_apply_model_access_guardrails(repo))
    errors.extend(check_physics_seed_body_asleep_model_access_guardrails(repo))
    errors.extend(check_physics_pending_impulse_model_access_guardrails(repo))
    errors.extend(check_command_side_body_refresh_guardrails(repo))
    errors.extend(check_scene_setup_model_index_physics_command_guardrails(repo))
    errors.extend(check_editor_model_index_physics_command_guardrails(repo))
    errors.extend(check_mouse_pickup_model_index_physics_command_guardrails(repo))
    errors.extend(check_launcher_model_index_physics_command_guardrails(repo))
    errors.extend(check_replay_velocity_model_state_physics_command_guardrails(repo))
    errors.extend(check_run_frame_replay_editor_transform_wake_guardrails(repo))
    errors.extend(check_ragdoll_model_index_physics_command_guardrails(repo))
    errors.extend(check_deleted_game_model_collection_physics_wrapper_guardrails(repo))
    errors.extend(check_deleted_game_model_collection_physics_adapter_command_guardrails(repo))
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
