#!/usr/bin/env python3
#
# File: tools/check_runtime_boundaries.py
# Purpose:
#   Check that Run.h stays a runtime composition root instead of regrowing
#   extracted subsystem ownership, and prevent new physics dependencies on the
#   legacy GameModelCollection world container.
#
# Mental model:
#   Runtime decomposition is easy to regress by adding one convenient field or
#   helper back to Run. Physics data ownership is similarly easy to regress by
#   threading GameModelCollection into one more API. This check is intentionally
#   small: it watches the boundaries named by the active architecture plans.
#
# Invariants:
#   - Run.h may own subsystem objects, but not their extracted transient state.
#   - Subsystems may borrow explicit service/context structs, but not store Run.
#   - Physics may only keep the current GameModelCollection compatibility
#     surface while stores and handles become authoritative.
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
RUN_INPUT_SOURCE = Path("SkullbonezSource/Runtime/RunInput.cpp")
RUN_SCENE_SOURCE = Path("SkullbonezSource/Runtime/Scene/RunScene.cpp")
RUNTIME_RENDER_HOST_HEADER = Path("SkullbonezSource/Runtime/Render/RuntimeRenderHost.h")
FIELD_TAIL_PATTERN = r"(?=[^;{}]*\bm_[A-Za-z_]\w*)[^;{}]*;"
RUN_NAME_PATTERN = r"(?:(?:[A-Za-z_]\w*::)*Run)\b"
RUN_CV_PATTERN = rf"(?:const\s+{RUN_NAME_PATTERN}|{RUN_NAME_PATTERN}\s+const|{RUN_NAME_PATTERN})"
GAME_MODEL_COLLECTION_PATTERN = re.compile(r"\bGameModelCollection\b")
MAX_RUN_PRIVATE_METHOD_DECLARATIONS = 184
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
        ( "SkullbonezSource/Physics/PersistentContactSolver.cpp", '#include "../GameObjects/GameModelCollection.h"' ),
        ( "SkullbonezSource/Physics/PersistentContactSolver.cpp", "GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PersistentContactSolver.h", "class GameModelCollection;" ),
        (
            "SkullbonezSource/Physics/PersistentContactSolver.h",
            "void Solve( PersistentContactSolverContext& context, GameObjects::GameModelCollection& collection, float dt );",
        ),
        ( "SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp", '#include "../GameObjects/GameModelCollection.h"' ),
        (
            "SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp",
            "void PhysicsDiagnosticsSink::EmitRegressionLog( PhysicsWorld& world, GameModelCollection& collection )",
        ),
        ( "SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp", "void PhysicsDiagnosticsSink::EmitFrame( GameModelCollection& collection, float dt )" ),
        ( "SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp", "void PhysicsDiagnosticsSink::EmitCollisionTime( GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsDiagnosticsSink.h", "class GameModelCollection;" ),
        (
            "SkullbonezSource/Physics/PhysicsDiagnosticsSink.h",
            "void EmitRegressionLog( PhysicsWorld& world, GameObjects::GameModelCollection& collection );",
        ),
        ( "SkullbonezSource/Physics/PhysicsDiagnosticsSink.h", "void EmitFrame( GameObjects::GameModelCollection& collection, float dt );" ),
        ( "SkullbonezSource/Physics/PhysicsDiagnosticsSink.h", "void EmitCollisionTime( GameObjects::GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsEngine.cpp", '#include "../GameObjects/GameModelCollection.h"' ),
        ( "SkullbonezSource/Physics/PhysicsEngine.cpp", "using SkullbonezCore::GameObjects::GameModelCollection;" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.cpp", "void PhysicsEngine::RefreshStores( GameModelCollection& collection )" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.cpp", "void PhysicsEngine::RefreshPhysicsStores( GameModelCollection& collection )" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.cpp", "void PhysicsEngine::RefreshBodyStore( GameModelCollection& collection )" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.cpp", "void PhysicsEngine::RefreshColliderStore( GameModelCollection& collection )" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.cpp", "void PhysicsEngine::RefreshRenderStore( GameModelCollection& collection )" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.cpp", "void PhysicsEngine::Step( GameModelCollection& collection, float deltaSeconds )" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.cpp", "void PhysicsEngine::WakeBody( GameModelCollection& collection, int bodyIndex )" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.cpp", "void PhysicsEngine::SeedBodyAsleep( GameModelCollection& collection, int bodyIndex )" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.cpp", "void PhysicsEngine::ApplyBodyImpulse( GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsEngine.cpp", "void PhysicsEngine::SetPendingBodyImpulse( GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsEngine.h", "class GameModelCollection;" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.h", "void RefreshStores( GameObjects::GameModelCollection& collection );" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.h", "void RefreshPhysicsStores( GameObjects::GameModelCollection& collection );" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.h", "void RefreshBodyStore( GameObjects::GameModelCollection& collection );" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.h", "void RefreshColliderStore( GameObjects::GameModelCollection& collection );" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.h", "void RefreshRenderStore( GameObjects::GameModelCollection& collection );" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.h", "void Step( GameObjects::GameModelCollection& collection, float deltaSeconds );" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.h", "void WakeBody( GameObjects::GameModelCollection& collection, int bodyIndex );" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.h", "void SeedBodyAsleep( GameObjects::GameModelCollection& collection, int bodyIndex );" ),
        ( "SkullbonezSource/Physics/PhysicsEngine.h", "void ApplyBodyImpulse( GameObjects::GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsEngine.h", "void SetPendingBodyImpulse( GameObjects::GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsScene.cpp", '#include "../GameObjects/GameModelCollection.h"' ),
        ( "SkullbonezSource/Physics/PhysicsScene.cpp", "using SkullbonezCore::GameObjects::GameModelCollection;" ),
        ( "SkullbonezSource/Physics/PhysicsScene.cpp", "void PhysicsScene::RefreshStores( GameModelCollection& collection )" ),
        ( "SkullbonezSource/Physics/PhysicsScene.cpp", "void PhysicsScene::RefreshPhysicsStores( GameModelCollection& collection )" ),
        ( "SkullbonezSource/Physics/PhysicsScene.cpp", "void PhysicsScene::RefreshBodyStore( GameModelCollection& collection )" ),
        ( "SkullbonezSource/Physics/PhysicsScene.cpp", "void PhysicsScene::RefreshColliderStore( GameModelCollection& collection )" ),
        ( "SkullbonezSource/Physics/PhysicsScene.cpp", "void PhysicsScene::RefreshRenderStore( GameModelCollection& collection )" ),
        ( "SkullbonezSource/Physics/PhysicsScene.cpp", "void PhysicsScene::RunPhysics( GameModelCollection& collection, float fChangeInTime )" ),
        ( "SkullbonezSource/Physics/PhysicsScene.cpp", "void PhysicsScene::WakeModel( GameModelCollection& collection, int index )" ),
        ( "SkullbonezSource/Physics/PhysicsScene.cpp", "void PhysicsScene::SeedModelAsleep( GameModelCollection& collection, int index )" ),
        ( "SkullbonezSource/Physics/PhysicsScene.cpp", "void PhysicsScene::ApplyBodyImpulse( GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsScene.cpp", "void PhysicsScene::SetPendingBodyImpulse( GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsScene.h", "class GameModelCollection;" ),
        ( "SkullbonezSource/Physics/PhysicsScene.h", "void RefreshStores( GameObjects::GameModelCollection& collection );" ),
        ( "SkullbonezSource/Physics/PhysicsScene.h", "void RefreshPhysicsStores( GameObjects::GameModelCollection& collection );" ),
        ( "SkullbonezSource/Physics/PhysicsScene.h", "void RefreshBodyStore( GameObjects::GameModelCollection& collection );" ),
        ( "SkullbonezSource/Physics/PhysicsScene.h", "void RefreshColliderStore( GameObjects::GameModelCollection& collection );" ),
        ( "SkullbonezSource/Physics/PhysicsScene.h", "void RefreshRenderStore( GameObjects::GameModelCollection& collection );" ),
        ( "SkullbonezSource/Physics/PhysicsScene.h", "void RunPhysics( GameObjects::GameModelCollection& collection, float fChangeInTime );" ),
        ( "SkullbonezSource/Physics/PhysicsScene.h", "void WakeModel( GameObjects::GameModelCollection& collection, int index );" ),
        ( "SkullbonezSource/Physics/PhysicsScene.h", "void SeedModelAsleep( GameObjects::GameModelCollection& collection, int index );" ),
        ( "SkullbonezSource/Physics/PhysicsScene.h", "void ApplyBodyImpulse( GameObjects::GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsScene.h", "void SetPendingBodyImpulse( GameObjects::GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", '#include "../GameObjects/GameModelCollection.h"' ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "bool PhysicsWorld::IsFullySubmergedBall( GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "void PhysicsWorld::LockUnderwaterSleeperIfReady( GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "bool PhysicsWorld::IsUnderwaterSleepLocked( GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "void PhysicsWorld::MarkFixedContact( GameModelCollection& collection, int index )" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "void PersistentContactSolverContext::MarkFixedContact( GameModelCollection& collection, int index ) const" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "void PersistentContactSolverContext::WakeModel( GameModelCollection& collection, int index ) const" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "void PhysicsWorld::RunPhysics( GameModelCollection& collection, PhysicsBodyStore& bodyStore, float fChangeInTime )" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "void PhysicsWorld::WakeModel( GameModelCollection& collection, int index )" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "void PhysicsWorld::SeedModelAsleep( GameModelCollection& collection, int index )" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "void PhysicsWorld::ApplyTornadoField( GameModelCollection& collection, PhysicsBodyStore& bodyStore, float dt )" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "void PhysicsWorld::EmitPhysicsDiagnosticsFrame( GameModelCollection& collection, float dt )" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "void PhysicsWorld::EmitPhysicsCollisionTime( GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "void PhysicsWorld::PropagateSleepSupport( GameModelCollection& collection )" ),
        (
            "SkullbonezSource/Physics/PhysicsWorld.cpp",
            "bool PhysicsWorld::WakeDynamicBodyState( GameModelCollection& collection,",
        ),
        (
            "SkullbonezSource/Physics/PhysicsWorld.cpp",
            "void PhysicsWorld::WakeSleepVisualIsland( GameModelCollection& collection,",
        ),
        (
            "SkullbonezSource/Physics/PhysicsWorld.cpp",
            "void PhysicsWorld::WakePointJointIsland( GameModelCollection& collection,",
        ),
        (
            "SkullbonezSource/Physics/PhysicsWorld.cpp",
            "void PhysicsWorld::WakeRestingContactIsland( GameModelCollection& collection,",
        ),
        (
            "SkullbonezSource/Physics/PhysicsWorld.cpp",
            "void PhysicsWorld::WakePointJointConnectedBodies( GameModelCollection& collection,",
        ),
        ( "SkullbonezSource/Physics/PhysicsWorld.cpp", "void PhysicsWorld::RunSolverPhysics( GameModelCollection& collection, PhysicsBodyStore& bodyStore, float dt )" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "class GameModelCollection;" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "void RunSolverPhysics( GameObjects::GameModelCollection& collection, PhysicsBodyStore& bodyStore, float dt );" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "void SolvePersistentObjectContacts( GameObjects::GameModelCollection& collection, float dt );" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "void EmitPhysicsDiagnosticsFrame( GameObjects::GameModelCollection& collection, float dt );" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "void EmitPhysicsCollisionTime( GameObjects::GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "bool IsFullySubmergedBall( GameObjects::GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "void LockUnderwaterSleeperIfReady( GameObjects::GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "bool IsUnderwaterSleepLocked( GameObjects::GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "void MarkFixedContact( GameObjects::GameModelCollection& collection, int index );" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "void ApplyTornadoField( GameObjects::GameModelCollection& collection, PhysicsBodyStore& bodyStore, float dt );" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "void PropagateSleepSupport( GameObjects::GameModelCollection& collection );" ),
        (
            "SkullbonezSource/Physics/PhysicsWorld.h",
            "bool WakeDynamicBodyState( GameObjects::GameModelCollection& collection,",
        ),
        (
            "SkullbonezSource/Physics/PhysicsWorld.h",
            "void WakeSleepVisualIsland( GameObjects::GameModelCollection& collection,",
        ),
        (
            "SkullbonezSource/Physics/PhysicsWorld.h",
            "void WakePointJointIsland( GameObjects::GameModelCollection& collection,",
        ),
        (
            "SkullbonezSource/Physics/PhysicsWorld.h",
            "void WakeRestingContactIsland( GameObjects::GameModelCollection& collection,",
        ),
        (
            "SkullbonezSource/Physics/PhysicsWorld.h",
            "void WakePointJointConnectedBodies( GameObjects::GameModelCollection& collection,",
        ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "void RunPhysics( GameObjects::GameModelCollection& collection, PhysicsBodyStore& bodyStore, float fChangeInTime );" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "void WakeModel( GameObjects::GameModelCollection& collection, int index );" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "void SeedModelAsleep( GameObjects::GameModelCollection& collection, int index );" ),
        (
            "SkullbonezSource/Physics/PhysicsWorld.h",
            "void MarkSolverFixedContact( GameObjects::GameModelCollection& collection, int index )",
        ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "void MarkFixedContact( GameObjects::GameModelCollection& collection, int index ) const;" ),
        ( "SkullbonezSource/Physics/PhysicsWorld.h", "void WakeModel( GameObjects::GameModelCollection& collection, int index ) const;" ),
        ( "SkullbonezSource/Physics/Ragdoll.cpp", '#include "../GameObjects/GameModelCollection.h"' ),
        ( "SkullbonezSource/Physics/Ragdoll.cpp", "void Ragdoll::AddSimpleHumanoid( GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/Ragdoll.cpp", "void Ragdoll::SolvePointJoints( GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/Ragdoll.h", "class GameModelCollection;" ),
        ( "SkullbonezSource/Physics/Ragdoll.h", "static void AddSimpleHumanoid( GameObjects::GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/Ragdoll.h", "static void SolvePointJoints( GameObjects::GameModelCollection& collection," ),
        ( "SkullbonezSource/Physics/SimulationSystem.cpp", '#include "../GameObjects/GameModelCollection.h"' ),
        ( "SkullbonezSource/Physics/SimulationSystem.h", "class GameModelCollection;" ),
        ( "SkullbonezSource/Physics/SimulationSystem.h", "GameObjects::GameModelCollection* models = nullptr;" ),
        ( "SkullbonezSource/Physics/SleepIslandSystem.cpp", '#include "../GameObjects/GameModelCollection.h"' ),
        (
            "SkullbonezSource/Physics/SleepIslandSystem.cpp",
            "void SleepIslandSystem::PropagateSupport( SleepSupportPropagationContext& context, GameModelCollection& collection )",
        ),
        ( "SkullbonezSource/Physics/SleepIslandSystem.h", "class GameModelCollection;" ),
        (
            "SkullbonezSource/Physics/SleepIslandSystem.h",
            "void PropagateSupport( SleepSupportPropagationContext& context, GameObjects::GameModelCollection& collection );",
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
        "scene-control wrappers must stay out of Run.h",
        r"\b(?:LoadSceneFromBrowserIndex|LoadDemoSceneFromUI|ApplyAdjacentCinematicMode|"
        r"LoadAdjacentSceneFromBrowser|ResetCurrentScene|AdvanceScene)\s*\(",
        "Call SceneRuntimeCoordinator directly while scene load ownership moves out of Run.",
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
    "systems",
    "debug",
    "timers",
    "runtimeSettings",
    "gameModelCollection",
    "worldEnvironment",
    "collisionVisualizer",
    "broadphaseVisualizer",
    "physicsDebugVisualizer",
    "dxrReflectionTransforms",
    "rayCastTest",
    "editor",
    "mousePickup",
    "replayRuntime",
    "launcherLaser",
    "ui",
    "runtimeInput",
    "camera",
    "runtimeViewModel",
    "sceneController",
    "sceneBrowser",
}

ALLOWED_RENDER_HOST_CALLBACK_TYPEDEFS = {
    "ActiveCinematicConfigFn",
    "BoolFn",
    "TextureHandleFn",
    "SelectRenderTextureFn",
    "IntFn",
    "LogLifecycleStepFn",
    "RenderEditorOverlayFn",
    "VoidFn",
    "SceneStateFn",
    "CameraModeEnabledMaskFn",
    "CameraModeLabelFn",
    "MainMemoryStatsFn",
}

ALLOWED_MUTABLE_RENDER_HOST_CALLBACK_TYPEDEFS = {
    "ActiveCinematicConfigFn",
}

ALLOWED_RENDER_HOST_CALLBACK_FIELDS = {
    "user",
    "activeCinematicConfig",
    "isCinematicRenderingEnabled",
    "isLauncherCameraMode",
    "textureHandle",
    "selectRenderTexture",
    "windowScreenWidth",
    "windowScreenHeight",
    "logRenderResourceLifecycleStep",
    "renderEditorOverlay",
    "refreshRuntimeViewModel",
    "sceneState",
    "currentSceneBrowserIndex",
    "cameraModeEnabledMask",
    "cameraModeLabel",
    "refreshMainMemoryStats",
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
                        "Split render-facing state into a narrow view instead of growing RuntimeRenderHost.",
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
                        "Move the callback behind a subsystem-owned service before wiring render host access.",
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
                        "Narrow the render service surface instead of adding another Run callback.",
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
    struct RuntimeRenderHostBindings
    {
        RunSubsystemState* systems = nullptr;
        RunDebugState* debug = nullptr;
    };
    struct RuntimeRenderHostCallbacks
    {
        using ActiveCinematicConfigFn = CinematicRenderConfig& (*)( void* user );
        using BoolFn = bool ( * )( void* user );
        void* user = nullptr;
        ActiveCinematicConfigFn activeCinematicConfig = nullptr;
        BoolFn isLauncherCameraMode = nullptr;
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

    old_scene_control_header_helper = allowed_run_header.replace(
        "void Render();",
        "void Render();\n        bool AdvanceScene();",
    )
    if not any(
        error.message == "scene-control wrappers must stay out of Run.h"
        for error in check_text_rules(Path("synthetic/Run.h"), old_scene_control_header_helper, RUN_HEADER_RULES)
    ):
        failures.append("scene-control header wrapper synthetic surface was not rejected")

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

    new_binding = allowed_host.replace(
        "RunDebugState* debug = nullptr;",
        "RunDebugState* debug = nullptr;\n        RunSceneState* newSceneState = nullptr;",
    )
    if not any(
        error.message == "new RuntimeRenderHostBindings fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, new_binding)
    ):
        failures.append("new RuntimeRenderHostBindings synthetic field was not rejected")

    bare_new_binding = allowed_host.replace(
        "RunDebugState* debug = nullptr;",
        "RunDebugState* debug = nullptr;\n        RunSceneState* bareSceneState;",
    )
    if not any(
        error.message == "new RuntimeRenderHostBindings fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, bare_new_binding)
    ):
        failures.append("bare RuntimeRenderHostBindings synthetic field was not rejected")

    old_replay_binding = allowed_host.replace(
        "RunDebugState* debug = nullptr;",
        "RunDebugState* debug = nullptr;\n        RunReplayScrubberState* replayScrubber = nullptr;",
    )
    if not any(
        error.message == "new RuntimeRenderHostBindings fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, old_replay_binding)
    ):
        failures.append("old RuntimeRenderHost replay binding synthetic field was not rejected")

    new_callback = allowed_host.replace(
        "BoolFn isLauncherCameraMode = nullptr;",
        "BoolFn isLauncherCameraMode = nullptr;\n        BoolFn newRenderCallback = nullptr;",
    )
    if not any(
        error.message == "new RuntimeRenderHostCallbacks fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, new_callback)
    ):
        failures.append("new RuntimeRenderHostCallbacks synthetic field was not rejected")

    old_scrubber_callback_field = allowed_host.replace(
        "BoolFn isLauncherCameraMode = nullptr;",
        "BoolFn isLauncherCameraMode = nullptr;\n        BoolFn shouldRenderReplayScrubber = nullptr;",
    )
    if not any(
        error.message == "new RuntimeRenderHostCallbacks fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, old_scrubber_callback_field)
    ):
        failures.append("old RuntimeRenderHost scrubber callback field was not rejected")

    old_replay_scrubber_overlay_callback_field = allowed_host.replace(
        "BoolFn isLauncherCameraMode = nullptr;",
        "BoolFn isLauncherCameraMode = nullptr;\n        VoidFn renderReplayScrubberOverlay = nullptr;",
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
        "BoolFn isLauncherCameraMode = nullptr;",
        "BoolFn isLauncherCameraMode = nullptr;\n        VoidFn renderReplayPredictionGhosts = nullptr;",
    )
    if not any(
        error.message == "new RuntimeRenderHostCallbacks fields are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, old_prediction_ghost_callback_field)
    ):
        failures.append("old RuntimeRenderHost replay prediction ghost callback field was not rejected")

    mutable_callback = allowed_host.replace(
        "using BoolFn = bool ( * )( void* user );",
        "using BoolFn = bool ( * )( void* user );\n        using MutableSceneFn = RunSceneState& (*)( void* user );",
    )
    mutable_errors = check_runtime_render_host_guardrails_text(synthetic_path, mutable_callback)
    if not any(error.message == "new RuntimeRenderHostCallbacks typedefs are blocked" for error in mutable_errors):
        failures.append("new RuntimeRenderHostCallbacks synthetic typedef was not rejected")
    if not any(error.message == "mutable RuntimeRenderHostCallbacks returns are blocked" for error in mutable_errors):
        failures.append("mutable RuntimeRenderHostCallbacks synthetic return was not rejected")

    mutable_pointer_callback = allowed_host.replace(
        "using BoolFn = bool ( * )( void* user );",
        "using BoolFn = bool ( * )( void* user );\n        using MutableScenePtrFn = RunSceneState* (*)( void* user );",
    )
    pointer_errors = check_runtime_render_host_guardrails_text(synthetic_path, mutable_pointer_callback)
    if not any(error.message == "new RuntimeRenderHostCallbacks typedefs are blocked" for error in pointer_errors):
        failures.append("new pointer RuntimeRenderHostCallbacks synthetic typedef was not rejected")
    if not any(error.message == "mutable RuntimeRenderHostCallbacks returns are blocked" for error in pointer_errors):
        failures.append("mutable pointer RuntimeRenderHostCallbacks synthetic return was not rejected")

    old_replay_callback = allowed_host.replace(
        "using BoolFn = bool ( * )( void* user );",
        "using BoolFn = bool ( * )( void* user );\n        using ReplayPresentationSampleFn = const ReplayPresentationSample* (*)( void* user );",
    )
    if not any(
        error.message == "new RuntimeRenderHostCallbacks typedefs are blocked"
        for error in check_runtime_render_host_guardrails_text(synthetic_path, old_replay_callback)
    ):
        failures.append("old RuntimeRenderHost replay callback typedef was not rejected")

    old_prediction_ghost_callback_typedef = allowed_host.replace(
        "using BoolFn = bool ( * )( void* user );",
        "using BoolFn = bool ( * )( void* user );\n"
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
    errors.extend(check_run_diagnostics_source_guardrails(repo))
    errors.extend(check_run_diagnostics_perf_tick_source_guardrails(repo))
    errors.extend(check_run_scene_perf_log_lifecycle_guardrails(repo))
    errors.extend(check_run_scene_control_source_guardrails(repo))
    errors.extend(check_run_scene_coordinator_callback_source_guardrails(repo))
    errors.extend(check_scene_runtime_coordinator_callback_guardrails(repo))
    errors.extend(check_run_ui_text_pass_replay_overlay_guardrails(repo))
    errors.extend(check_interaction_guardrails(repo))
    errors.extend(check_physics_game_model_collection_guardrails(repo))
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
