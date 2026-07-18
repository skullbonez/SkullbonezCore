#!/usr/bin/env python3
#
# File: tools/validate_project_filters.py
# Purpose:
#   Validate Visual Studio project item filters and path casing.
#
# Summary:
#   The .vcxproj controls what Visual Studio builds or displays, while the
#   .vcxproj.filters file controls where those items appear in Solution
#   Explorer. This check keeps source, headers, scenes, shaders, and style data
#   in predictable semantic filters so project edits do not slowly drift or
#   accumulate flat subsystem roots. The default production gate validates the
#   app and any extracted production libraries as a set, because library
#   layering deliberately moves files out of the app project without removing
#   them from the solution build.
#
# Glossary:
#   Filter: A Visual Studio virtual folder stored in .vcxproj.filters.
#   Project item: A build or content entry such as ClCompile, ClInclude, or None.
#   Production project set: The app plus extracted static libraries that
#     together own SkullbonezSource build/header coverage.
#   Semantic filter: A virtual folder named for responsibility rather than the
#     source file's physical directory alone.
#
# Invariants:
#   - Every project item that belongs in Solution Explorer has one expected
#     filter entry.
#   - Every SkullbonezSource build/header file appears in exactly one production
#     project in default mode.
#   - Source and header pairs stay in matching source/header filter categories.
#   - Runtime and Physics items resolve to named semantic descendants instead
#     of collecting directly under their subsystem roots.
#   - Project paths use the exact casing of the file on disk.
#
# Related:
#   - AGENTS.md
#   - tools/README.md
#
"""Validate Visual Studio project item filters."""

from __future__ import annotations

import argparse
import json
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path, PureWindowsPath


EXPECTED_MSBUILD_NAMESPACE = "http://schemas.microsoft.com/developer/msbuild/2003"
PROJECT_ITEM_TYPES = ("ClCompile", "ClInclude", "None", "ResourceCompile")
SOURCE_FILTER_ROOT = "Source Files"
HEADER_FILTER_ROOT = "Header Files"
EXTERNAL_FILTER = "External"
TEST_FILTER = "Tests"
SCENE_FILTER = "Scene Files"
STYLE_FILTER = "Style Files"
PROJECT_FILTER = "Project Files"
RESOURCE_FILTER = "Resource Files"
SHADER_FILTER = "Resource Files\\HLSL"
SOURCE_PROJECT_ROOT = "SkullbonezSource"
# Invariant: the default production set excludes SKULLBONEZ_TESTS because tests
# intentionally compile or reference focused engine slices for unit coverage.
DEFAULT_PRODUCTION_PROJECTS = (
    ("SKULLBONEZ_CORE.vcxproj", "SKULLBONEZ_CORE.vcxproj.filters"),
    ("SKULLBONEZ_MATHS.vcxproj", "SKULLBONEZ_MATHS.vcxproj.filters"),
    ("SKULLBONEZ_PHYSICS.vcxproj", "SKULLBONEZ_PHYSICS.vcxproj.filters"),
)
# Concept: extracted single-area libraries already name their subsystem at the
# project node in Solution Explorer. Their filters should stay shallow instead
# of nesting every item under Source Files\<same area>.
FLATTENED_LIBRARY_PROJECT_AREAS = {
    "SKULLBONEZ_MATHS.vcxproj": "Maths",
    "SKULLBONEZ_PHYSICS.vcxproj": "Physics",
}
# Concept: `.inl` files are source-bearing include slices, not build units.
# Keep them as ClInclude items so Visual Studio shows ownership splits while
# the including `.cpp` preserves linkage and compile order.
SOURCE_PROJECT_SUFFIX_TYPES = {
    ".cpp": "ClCompile",
    ".h": "ClInclude",
    ".inl": "ClInclude",
}

MATH_PREFIXES = (
    "Frustum",
    "GeometricMath",
    "GeometricStructures",
    "MathsCommon",
    "Matrix4",
    "Quaternion",
    "RotationMatrix",
    "Vector3",
)

ASSET_PREFIXES = (
    "AssetKeys",
    "AssetSystem",
    "TextureCollection",
)

PHYSICS_CORE_PREFIXES = (
    "PhysicsApi",
    "PhysicsEngine",
    "PhysicsScene",
    "PhysicsWorld",
    "SimulationSystem",
)

PHYSICS_BODY_PREFIXES = (
    "PhysicsBodyStore",
    "PhysicsHandles",
    "PhysicsMass",
    "PhysicsObjectPolicy",
    "Ragdoll",
)

PHYSICS_COLLISION_PREFIXES = (
    "BoundingBox",
    "BoundingSphere",
    "ColliderStore",
    "CollisionShape",
    "ConvexHullShape",
    "ObjectContactManifold",
    "SpatialGrid",
    "TerrainContactManifold",
)

PHYSICS_FORCE_PREFIXES = (
    "BuoyancySystem",
    "PhysicsWorldForces",
    "TornadoField",
    "TornadoGameplay",
)

PHYSICS_SOLVER_PREFIXES = (
    "ContactSolverCommon",
    "DisjointSet",
    "PersistentContactSolver",
    "SleepIslandSystem",
    "SolverBroadphaseStage",
)

PHYSICS_STAGE_PREFIXES = (
    "PhysicsBroadphaseStage",
    "PhysicsContactSolverStage",
    "PhysicsForceStage",
    "PhysicsNarrowphaseStage",
    "PhysicsSleepController",
    "PhysicsStageContexts",
    "PhysicsStepDiagnostics",
    "PhysicsTerrainStage",
)

PHYSICS_DIAGNOSTICS_PREFIXES = (
    "PhysicsDebugData",
    "PhysicsDiagnosticsModel",
    "PhysicsDiagnosticsSink",
)

PHYSICS_SUPPORT_PREFIXES = (
    "PhysicsFixedList",
    "PhysicsTimestep",
)

PHYSICS_DEBUG_PREFIXES = (
    "BroadphaseVisualizer",
    "CollisionVisualizer",
    "PhysicsDebugVisualizer",
)

# Why: the visualizer filenames still describe physics overlays, but render
# submission now lives under Runtime\Debug after the physics project split.
RUNTIME_DEBUG_PREFIXES = (*PHYSICS_DEBUG_PREFIXES, "RunDebugState")

DX12_RENDERING_PREFIXES = (
    "BLASDX12",
    "Dx12BackbufferCapture",
    "Dx12GraphTransientPool",
    "Dx12CachedPsoStore",
    "Dx12DeferredReleaseOwner",
    "Dx12DescriptorHeaps",
    "Dx12Diagnostics",
    "Dx12ShaderDevelopment",
    "Dx12FrameOwner",
    "Dx12RenderGraphExecutor",
    "Dx12TextureRegistry",
    "FramebufferDX12",
    "GeneratedShaderReflection",
    "MeshDX12",
    "RenderBackendDX12",
    "RenderGraphTransientDX12",
    "RenderDeviceDX12",
    "SBTDX12",
    "ShaderBytecodeManifest",
    "ShaderDX12",
    "TLASDX12",
)

RENDERING_PREFIXES = (
    "DrawCallTrace",
    "RenderInstanceRenderer",
    "Helper",
    "IFramebuffer",
    "IMesh",
    "IRenderBackend",
    "IRenderCaptureBackend",
    "IRenderCommandContext",
    "IRenderDeviceLifecycle",
    "IRenderDiagnostics",
    "IRenderRayTracing",
    "IRenderShaderDevelopment",
    "IRenderResourceFactory",
    "IShader",
    "PrimitiveBatchRenderer",
    "PrimitiveMeshBuilder",
    "RenderGraph",
    "RenderRasterBindingContract",
    "RenderPipeline",
    "RenderInstanceStore",
    "RenderSceneSnapshot",
    "RenderSceneView",
    "RenderMaterial",
    "ShaderContracts",
    "ShaderReflectionContracts",
    "Shadow",
    "Text",
)

SCENE_PREFIXES = (
    "SceneRequestExecution",
    "SceneSnapshotWriter",
    "AuthoredScene",
    "AuthoredSceneParser",
    "AuthoredSceneParserAssets",
    "AuthoredSceneParserBodies",
    "AuthoredSceneParserPresentation",
    "AuthoredSceneParserRuntime",
    "AuthoredSceneParserSchema",
)

WORLD_PREFIXES = (
    "FluidSurfaceAdjustment",
    "SkyBox",
    "Terrain",
    "TerrainSupportClassifier",
    "WorldEnvironment",
)

RUNTIME_LIFECYCLE_PREFIXES = (
    "ApplicationExitState",
    "Init",
    "Run",
    "RunFrame",
    "RunLaunchOptions",
    "RunStartupState",
    "RunTimerState",
    "RuntimeFrameViews",
    "RuntimeOverlayDiagnostics",
    "Window",
    "WindowConstants",
)

# Startup units are process-entry policy owners rather than Run lifecycle
# implementations. Keep their physical directory and Solution Explorer filter
# aligned as the Init decomposition adds each planned unit.
RUNTIME_STARTUP_PREFIXES = (
    "StartupCommandLine",
    "StartupCrashLogging",
    "StartupLaunchResolution",
    "StartupProbeHarnesses",
)

RUNTIME_CAMERA_PREFIXES = (
    "AttachedCameraController",
    "Camera",
    "CameraCollection",
    "RunCameraState",
    "RuntimeCameraMode",
)

RUNTIME_CAPTURE_PREFIXES = (
    "CaptureController",
    "CaptureSystem",
)

RUNTIME_DEMO_PREFIXES = (
    "DemoDirector",
    "RunDemoDirector",
)

RUNTIME_INPUT_PREFIXES = (
    "Input",
    "InputController",
    "InputFrame",
    "InputFrameExecution",
    "InputRouter",
    "RunInput",
)

RUNTIME_AUTOMATION_PREFIXES = (
    "GraphicsStressController",
    "InteractionAutomationController",
    "InteractionAutomationInputDriver",
    "InteractionAutomationReportWriter",
    "RuntimeStressController",
    "RuntimeValidationHarness",
)

RUNTIME_INTERACTION_PREFIXES = (
    "RuntimeInteractionCommands",
    "RuntimeInteractionController",
    "RuntimePickGeometry",
    "RuntimePickService",
)

RUNTIME_SETTINGS_PREFIXES = (
    "LiveStyleController",
    "OperatorCommandApplier",
)

RUNTIME_SCENE_PREFIXES = (
    "RunScene",
    "SceneCapacity",
    "SceneAutomationGateConfiguration",
    "SceneController",
    "SceneControllerState",
    "SceneWorld",
    "SceneTerrain",
    "SceneAuthoredSetup",
    "SceneGeneratedSetup",
    "SceneRuntime",
    "SceneRuntimeCreate",
    "SceneRuntimeCoordinator",
    "SceneRuntimeDefaults",
    "SceneRequestQueue",
    "SceneEntityStore",
    "SceneNavigationModel",
    "SceneRuntimeGeneratedControls",
    "SceneRuntimeLoad",
    "SceneRuntimeReset",
    "SceneRuntimeStyle",
    "SceneRuntimeUiOptions",
)

RUNTIME_AUDIO_PREFIXES = (
    "ContactAudioService",
)

RUNTIME_ALLOCATION_PREFIXES = (
    "RuntimeAllocationTracker",
    "RuntimeReserveAllocator",
)

RUNTIME_REPLAY_PREFIXES = (
    "ReplayAuthoring",
    "ReplayAuthoringCauseTree",
    "ReplayAuthoringVelocity",
    "ReplayCoordination",
    "ReplayEventCommand",
    "ReplayIdentity",
    "ReplayInteractionController",
    "ReplayOverlayLayout",
    "ReplayOverlayRenderer",
    "ReplayPredictionArchive",
    "ReplayPrediction",
    "ReplayPredictionDrawing",
    "ReplayPredictionReserve",
    "ReplayPredictionScheduling",
    "ReplayPredictionView",
    "ReplayRetainedMemory",
    "ReplayRecorder",
    "ReplayPresentation",
    "ReplayProbeState",
    "ReplayRestoreService",
    "ReplayRuntime",
    "ReplayRestoreTransactions",
    "ReplayScrubber",
    "ReplayScrubberTools",
    "ReplayTimeline",
    "ReplayVisualPacket",
    "ReplayVisualPacketFingerprint",
    "ReplaySolverSnapshot",
    "ReplayV2Artifact",
    "ReplayValidation",
    "TrajectoryStore",
)

RUNTIME_RENDER_PREFIXES = (
    "RenderDefaultsStore",
    "RenderPresentationSettings",
    "RunRender",
    "RuntimeRenderHost",
    "RuntimeRenderInputs",
    "RuntimeRenderPasses",
    "RuntimeRenderResources",
    "RuntimeRenderer",
)

RUNTIME_EDITOR_PREFIXES = (
    "EditorCommandHistory",
    "EditorPlacementAssets",
    "EditorTools",
    "EditorOverlayTools",
    "EditorHullAssets",
    "LauncherLaser",
    "LauncherTools",
    "RunEditorGizmoTools",
    "RunEditorHistory",
    "RunEditorObjectPlacement",
    "RunEditorOverlayTools",
    "RunEditorPlacementAssets",
    "RunEditorTracer",
    "RunEditorTools",
    "RunMousePickupTools",
)

RUNTIME_TOOLS_PREFIXES = (
    "RuntimeFileWriter",
    "RuntimeTools",
)

RUNTIME_DIAGNOSTICS_PREFIXES = (
    "DiagnosticsController",
    "DiagnosticsRuntime",
    "RuntimeDiagnostics",
    "SceneMemoryDiagnostics",
)

# Why: shared runtime UI values have their own physical owner and Solution
# Explorer filter; keeping this explicit prevents them drifting into Runtime.
RUNTIME_UI_PREFIXES = (
    "RunUiTextPass",
    "RuntimeViewModel",
    "RuntimeUiSurface",
)

CORE_PREFIXES = (
    "AmortizedTask",
    "Common",
    "Config",
    "FatalError",
    "Fence",
    "FloatingPointContract",
    "LockOrderValidator",
    "Log",
    "MainMemoryStats",
    "PlatformProfiler",
    "PlatformWin32",
    "Profiler",
    "SbResult",
    "SkullScope",
    "Timer",
    "WorkerPool",
)

AREA_PREFIXES = (
    ("Rendering\\DX12", DX12_RENDERING_PREFIXES),
    ("Physics\\Core", PHYSICS_CORE_PREFIXES),
    ("Physics\\Bodies", PHYSICS_BODY_PREFIXES),
    ("Physics\\Collision", PHYSICS_COLLISION_PREFIXES),
    ("Physics\\Forces", PHYSICS_FORCE_PREFIXES),
    ("Physics\\Solver", PHYSICS_SOLVER_PREFIXES),
    ("Physics\\Stages", PHYSICS_STAGE_PREFIXES),
    ("Physics\\Diagnostics", PHYSICS_DIAGNOSTICS_PREFIXES),
    ("Physics\\Support", PHYSICS_SUPPORT_PREFIXES),
    ("Runtime\\Scene", RUNTIME_SCENE_PREFIXES),
    ("Runtime\\Allocation", RUNTIME_ALLOCATION_PREFIXES),
    ("Runtime\\Audio", RUNTIME_AUDIO_PREFIXES),
    ("Runtime\\Replay", RUNTIME_REPLAY_PREFIXES),
    ("Runtime\\Render", RUNTIME_RENDER_PREFIXES),
    ("Runtime\\Editor", RUNTIME_EDITOR_PREFIXES),
    ("Runtime\\Tools", RUNTIME_TOOLS_PREFIXES),
    ("Runtime\\Diagnostics", RUNTIME_DIAGNOSTICS_PREFIXES),
    ("Runtime\\Debug", RUNTIME_DEBUG_PREFIXES),
    ("Runtime\\UI", RUNTIME_UI_PREFIXES),
    ("Runtime\\Lifecycle", RUNTIME_LIFECYCLE_PREFIXES),
    ("Runtime\\Startup", RUNTIME_STARTUP_PREFIXES),
    ("Runtime\\Camera", RUNTIME_CAMERA_PREFIXES),
    ("Runtime\\Capture", RUNTIME_CAPTURE_PREFIXES),
    ("Runtime\\Demo", RUNTIME_DEMO_PREFIXES),
    ("Runtime\\Input", RUNTIME_INPUT_PREFIXES),
    ("Runtime\\Automation", RUNTIME_AUTOMATION_PREFIXES),
    ("Runtime\\Interaction", RUNTIME_INTERACTION_PREFIXES),
    ("Runtime\\Settings", RUNTIME_SETTINGS_PREFIXES),
    ("Physics\\Debug", PHYSICS_DEBUG_PREFIXES),
    ("Rendering", RENDERING_PREFIXES),
    ("World", WORLD_PREFIXES),
    ("Assets", ASSET_PREFIXES),
    ("Maths", MATH_PREFIXES),
    ("Scene", SCENE_PREFIXES),
    ("Core", CORE_PREFIXES),
)


@dataclass(frozen=True)
class ProjectItem:
    item_type: str
    include: str
    filter_name: str | None = None

    @property
    def key(self) -> tuple[str, str]:
        return (self.item_type, normalize_path(self.include).lower())


@dataclass(frozen=True)
class ProjectValidationSpec:
    project_path: Path
    filters_path: Path


def normalize_path(path: str) -> str:
    return path.replace("/", "\\")


def repo_relative(repo: Path, path: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo.resolve())).replace("/", "\\")
    except ValueError:
        return str(path)


def load_xml(path: Path) -> ET.Element:
    try:
        return ET.parse(path).getroot()
    except FileNotFoundError:
        raise SystemExit(f"ERROR: XML file not found: {path}")
    except ET.ParseError as exc:
        raise SystemExit(f"ERROR: XML parse failed: {path}:{exc.position[0]}: {exc}") from exc


def namespace_for(root: ET.Element) -> str:
    if root.tag.startswith("{"):
        return root.tag.split("}", 1)[0][1:]
    return ""


def tag(namespace: str, name: str) -> str:
    return f"{{{namespace}}}{name}" if namespace else name


def read_project_items(root: ET.Element, namespace: str, include_filters: bool) -> list[ProjectItem]:
    items: list[ProjectItem] = []
    for item_type in PROJECT_ITEM_TYPES:
        for element in root.iter(tag(namespace, item_type)):
            include = element.get("Include")
            if not include:
                continue
            filter_name: str | None = None
            if include_filters:
                filter_element = element.find(tag(namespace, "Filter"))
                if filter_element is not None and filter_element.text:
                    filter_name = filter_element.text
            items.append(ProjectItem(item_type, normalize_path(include), filter_name))
    return items


def read_declared_filters(root: ET.Element, namespace: str) -> set[str]:
    filters: set[str] = set()
    for element in root.iter(tag(namespace, "Filter")):
        include = element.get("Include")
        if include:
            filters.add(include)
    return filters


def unused_declared_filter_errors(
    declared_filters: set[str],
    filter_items: list[ProjectItem],
    filters_label: str,
) -> list[str]:
    used_filters = {item.filter_name for item in filter_items if item.filter_name}
    errors: list[str] = []
    for declared_filter in sorted(declared_filters, key=str.lower):
        # Why: a parent filter is useful when a descendant owns items, but an
        # entirely empty subtree is a stale Solution Explorer folder. Item
        # destination checks alone cannot detect that shell.
        subtree_prefix = f"{declared_filter}\\"
        if any(
            used_filter == declared_filter or used_filter.startswith(subtree_prefix)
            for used_filter in used_filters
        ):
            continue
        errors.append(
            f"{filters_label}: declared filter '{declared_filter}' has no project items in its subtree."
        )
    return errors


def read_source_files_on_disk(repo: Path) -> list[ProjectItem]:
    source_root = repo / SOURCE_PROJECT_ROOT
    if not source_root.exists():
        return []

    items: list[ProjectItem] = []
    for path in sorted(source_root.rglob("*")):
        if not path.is_file():
            continue
        item_type = SOURCE_PROJECT_SUFFIX_TYPES.get(path.suffix.lower())
        if not item_type:
            continue
        items.append(ProjectItem(item_type, repo_relative(repo, path)))
    return items


def default_production_project_specs(repo: Path) -> list[ProjectValidationSpec]:
    specs: list[ProjectValidationSpec] = []
    for project_name, filters_name in DEFAULT_PRODUCTION_PROJECTS:
        project_path = repo / project_name
        filters_path = repo / filters_name
        if project_name == "SKULLBONEZ_CORE.vcxproj" or project_path.exists() or filters_path.exists():
            specs.append(ProjectValidationSpec(project_path, filters_path))
    return specs


def flattened_library_area(project_path: Path) -> str | None:
    return FLATTENED_LIBRARY_PROJECT_AREAS.get(project_path.name)


def read_project_items_from_path(project_path: Path) -> list[ProjectItem]:
    project_root = load_xml(project_path)
    project_namespace = namespace_for(project_root)
    return read_project_items(project_root, project_namespace, include_filters=False)


def production_source_coverage_errors(
    repo: Path,
    project_specs: list[ProjectValidationSpec],
) -> tuple[list[str], dict[str, int]]:
    errors: list[str] = []
    owner_by_key: dict[tuple[str, str], list[str]] = {}
    disk_items = read_source_files_on_disk(repo)

    # Why: after Maths becomes a static library, "is every source in the app?"
    # is the wrong question. Default validation now asks whether each tracked
    # source-bearing file has one production owner across the solution slice.
    for spec in project_specs:
        project_name = repo_relative(repo, spec.project_path)
        for item in read_project_items_from_path(spec.project_path):
            if normalize_path(item.include).lower().startswith(f"{SOURCE_PROJECT_ROOT.lower()}\\"):
                owner_by_key.setdefault(item.key, []).append(project_name)

    for item in disk_items:
        owners = owner_by_key.get(item.key, [])
        if not owners:
            errors.append(f"{item.include}: source/header file missing from production project set.")
        elif len(owners) > 1:
            errors.append(
                f"{item.include}: source/header file listed in multiple production projects "
                f"({', '.join(owners)})."
            )

    stats = {
        "diskSourceItemCount": len(disk_items),
        "productionSourceOwnerCount": len(owner_by_key),
    }
    return errors, stats


def exact_path_on_disk(repo: Path, include: str) -> str | None:
    current = repo
    actual_parts: list[str] = []
    for part in PureWindowsPath(include).parts:
        if not current.exists() or not current.is_dir():
            return None
        matches = [child.name for child in current.iterdir() if child.name.lower() == part.lower()]
        if not matches:
            return None
        actual_name = matches[0]
        actual_parts.append(actual_name)
        current = current / actual_name
    return "\\".join(actual_parts)


def source_area(include: str) -> str | None:
    path = PureWindowsPath(include)
    parts = tuple(path.parts)
    if len(parts) >= 2 and parts[0].lower() == "thirdptysource":
        return EXTERNAL_FILTER
    if len(parts) >= 2 and parts[0].lower() == "skullboneztests":
        return TEST_FILTER
    if len(parts) >= 3 and parts[0].lower() == "skullbonezsource" and parts[1].lower() == "ui":
        return "UI"

    stem = path.stem
    for area, prefixes in AREA_PREFIXES:
        if any(stem == prefix or stem.startswith(f"{prefix}.") for prefix in prefixes):
            return area
    return None


def expected_filter_for(item: ProjectItem, project_flat_area: str | None = None) -> str | None:
    include = normalize_path(item.include)
    lower = include.lower()
    suffix = PureWindowsPath(include).suffix.lower()

    if item.item_type == "ClCompile":
        area = source_area(include)
        if area is None:
            return None
        if area == EXTERNAL_FILTER:
            return EXTERNAL_FILTER
        if area == project_flat_area:
            return SOURCE_FILTER_ROOT
        # Why: extracted library projects omit their redundant owner name from
        # Solution Explorer while retaining semantic descendants. For example,
        # Physics\Collision becomes Source Files\Collision inside the Physics
        # project, rather than Source Files\Physics\Collision.
        flat_prefix = f"{project_flat_area}\\" if project_flat_area else None
        if flat_prefix and area.startswith(flat_prefix):
            area = area.removeprefix(flat_prefix)
        return f"{SOURCE_FILTER_ROOT}\\{area}"

    if item.item_type == "ClInclude":
        area = source_area(include)
        if area is None:
            return None
        if area == EXTERNAL_FILTER:
            return EXTERNAL_FILTER
        if area == project_flat_area:
            return HEADER_FILTER_ROOT
        flat_prefix = f"{project_flat_area}\\" if project_flat_area else None
        if flat_prefix and area.startswith(flat_prefix):
            area = area.removeprefix(flat_prefix)
        return f"{HEADER_FILTER_ROOT}\\{area}"

    if item.item_type == "None":
        if lower == "packages.config":
            return PROJECT_FILTER
        if lower.startswith("thirdptysource\\"):
            return EXTERNAL_FILTER
        if lower.startswith("skullbonezdata\\audio\\") and suffix in {".json", ".md", ".ogg"}:
            return RESOURCE_FILTER
        if lower.startswith("skullbonezdata\\shaders\\") and suffix in {".hlsl", ".dxil", ".json"}:
            return SHADER_FILTER
        if lower.startswith("skullbonezdata\\scenes\\") and (lower.endswith(".scene.json") or lower.endswith(".suite.json")):
            return SCENE_FILTER
        if lower.startswith("skullbonezdata\\interaction\\") and lower.endswith(".json"):
            return SCENE_FILTER
        if lower.startswith("skullbonezdata\\hulls\\") and suffix == ".hull":
            return SCENE_FILTER
        if lower == "skullbonezdata\\engine.cfg":
            return SCENE_FILTER
        if lower.startswith("skullbonezdata\\styles\\") and lower.endswith(".style.json"):
            return STYLE_FILTER

    return None


def duplicate_item_errors(label: str, items: list[ProjectItem]) -> list[str]:
    errors: list[str] = []
    seen: dict[tuple[str, str], ProjectItem] = {}
    for item in items:
        existing = seen.get(item.key)
        if existing:
            errors.append(
                f"{label}: duplicate {item.item_type} item for {item.include} "
                f"(also listed as {existing.include})."
            )
        else:
            seen[item.key] = item
    return errors


def pair_filter_errors(items_by_key: dict[tuple[str, str], ProjectItem]) -> list[str]:
    errors: list[str] = []
    source_by_stem: dict[str, ProjectItem] = {}
    header_by_stem: dict[str, ProjectItem] = {}

    for item in items_by_key.values():
        lower = item.include.lower()
        if not lower.startswith("skullbonezsource\\"):
            continue
        stem_key = str(PureWindowsPath(item.include).with_suffix("")).lower()
        if item.item_type == "ClCompile":
            source_by_stem[stem_key] = item
        elif item.item_type == "ClInclude":
            header_by_stem[stem_key] = item

    for stem_key, source in sorted(source_by_stem.items()):
        header = header_by_stem.get(stem_key)
        if not header or not source.filter_name or not header.filter_name:
            continue
        source_suffix = source_header_filter_suffix(source.filter_name, SOURCE_FILTER_ROOT)
        header_suffix = source_header_filter_suffix(header.filter_name, HEADER_FILTER_ROOT)
        if source.filter_name == EXTERNAL_FILTER and header.filter_name == EXTERNAL_FILTER:
            continue
        if source_suffix != header_suffix:
            errors.append(
                f"{source.include} and {header.include} must use matching source/header filters "
                f"(found {source.filter_name} and {header.filter_name})."
            )
    return errors


def source_header_filter_suffix(filter_name: str, root_filter: str) -> str:
    if filter_name == root_filter:
        return ""
    return filter_name.removeprefix(f"{root_filter}\\")


def validate_project_filters(
    repo: Path,
    project_path: Path,
    filters_path: Path,
    require_all_source_files: bool = True,
) -> tuple[list[str], dict[str, int]]:
    errors: list[str] = []

    project_root = load_xml(project_path)
    filters_root = load_xml(filters_path)
    project_namespace = namespace_for(project_root)
    filters_namespace = namespace_for(filters_root)

    if project_namespace != EXPECTED_MSBUILD_NAMESPACE:
        errors.append(
            f"{repo_relative(repo, project_path)} uses XML namespace '{project_namespace}', "
            f"expected '{EXPECTED_MSBUILD_NAMESPACE}'."
        )
    if filters_namespace != EXPECTED_MSBUILD_NAMESPACE:
        errors.append(
            f"{repo_relative(repo, filters_path)} uses XML namespace '{filters_namespace}', "
            f"expected '{EXPECTED_MSBUILD_NAMESPACE}'."
        )
    if project_root.get("DefaultTargets") not in (None, "Build"):
        errors.append(
            f"{repo_relative(repo, project_path)} DefaultTargets is "
            f"'{project_root.get('DefaultTargets')}', expected 'Build'."
        )

    project_items = read_project_items(project_root, project_namespace, include_filters=False)
    filter_items = read_project_items(filters_root, filters_namespace, include_filters=True)
    declared_filters = read_declared_filters(filters_root, filters_namespace)
    project_flat_area = flattened_library_area(project_path)

    errors.extend(duplicate_item_errors("project", project_items))
    errors.extend(duplicate_item_errors("filters", filter_items))
    errors.extend(
        unused_declared_filter_errors(
            declared_filters,
            filter_items,
            repo_relative(repo, filters_path),
        )
    )

    project_by_key = {item.key: item for item in project_items}
    filter_by_key = {item.key: item for item in filter_items}
    # Why: auxiliary projects such as SKULLBONEZ_TESTS intentionally compile a
    # subset of source files while still needing casing and filter drift checks.
    source_files_on_disk = read_source_files_on_disk(repo) if require_all_source_files else []

    for item in source_files_on_disk:
        if item.key not in project_by_key:
            errors.append(
                f"{item.include}: source/header file missing from "
                f"{repo_relative(repo, project_path)} as {item.item_type}."
            )

    for item in sorted(project_items, key=lambda entry: entry.include.lower()):
        actual = exact_path_on_disk(repo, item.include)
        if actual is None:
            errors.append(f"{item.include}: project item does not exist on disk.")
        elif actual != item.include:
            errors.append(f"{item.include}: project item casing should be {actual}.")

        filter_item = filter_by_key.get(item.key)
        if filter_item is None:
            errors.append(f"{item.include}: missing .vcxproj.filters entry.")
            continue
        if not filter_item.filter_name:
            errors.append(f"{item.include}: .vcxproj.filters entry has no Filter value.")
            continue
        if filter_item.filter_name not in declared_filters:
            errors.append(
                f"{item.include}: filter '{filter_item.filter_name}' is not declared in "
                f"{repo_relative(repo, filters_path)}."
            )

        expected_filter = expected_filter_for(item, project_flat_area)
        if expected_filter is None:
            errors.append(f"{item.include}: no project filter rule covers this item.")
        elif filter_item.filter_name != expected_filter:
            errors.append(
                f"{item.include}: expected filter '{expected_filter}', "
                f"found '{filter_item.filter_name}'."
            )

    for item in sorted(filter_items, key=lambda entry: entry.include.lower()):
        if item.key not in project_by_key:
            errors.append(f"{item.include}: .vcxproj.filters entry has no matching project item.")
        actual = exact_path_on_disk(repo, item.include)
        if actual is None:
            errors.append(f"{item.include}: filters item does not exist on disk.")
        elif actual != item.include:
            errors.append(f"{item.include}: filters item casing should be {actual}.")

    filter_project_items = [
        ProjectItem(item.item_type, item.include, filter_by_key[item.key].filter_name)
        for item in project_items
        if item.key in filter_by_key
    ]
    errors.extend(pair_filter_errors({item.key: item for item in filter_project_items}))

    stats = {
        "projectItemCount": len(project_items),
        "filterItemCount": len(filter_items),
        "declaredFilterCount": len(declared_filters),
        "diskSourceItemCount": len(source_files_on_disk),
        "errorCount": len(errors),
    }
    return errors, stats


def validate_production_project_filters(
    repo: Path,
    project_specs: list[ProjectValidationSpec],
) -> tuple[list[str], dict[str, int], list[dict[str, object]]]:
    errors: list[str] = []
    project_summaries: list[dict[str, object]] = []
    project_item_count = 0
    filter_item_count = 0
    declared_filter_count = 0

    for spec in project_specs:
        project_errors, stats = validate_project_filters(
            repo,
            spec.project_path,
            spec.filters_path,
            require_all_source_files=False,
        )
        project_label = repo_relative(repo, spec.project_path)
        project_summaries.append(
            {
                "project": project_label,
                "filters": repo_relative(repo, spec.filters_path),
                "status": "pass" if not project_errors else "fail",
                **stats,
            }
        )
        project_item_count += stats["projectItemCount"]
        filter_item_count += stats["filterItemCount"]
        declared_filter_count += stats["declaredFilterCount"]
        errors.extend(f"{project_label}: {error}" for error in project_errors)

    coverage_errors, coverage_stats = production_source_coverage_errors(repo, project_specs)
    errors.extend(coverage_errors)

    stats = {
        "projectCount": len(project_specs),
        "projectItemCount": project_item_count,
        "filterItemCount": filter_item_count,
        "declaredFilterCount": declared_filter_count,
        **coverage_stats,
        "errorCount": len(errors),
    }
    return errors, stats, project_summaries


def write_summary(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--project", type=Path, default=None)
    parser.add_argument("--filters", type=Path, default=None)
    parser.add_argument("--json-out", type=Path, default=None)
    parser.add_argument(
        "--partial-project",
        action="store_true",
        help="Validate only the items listed in the project; skip full SkullbonezSource coverage.",
    )
    parser.add_argument("--max-errors", type=int, default=80)
    args = parser.parse_args()

    repo = args.repo.resolve()
    summary_path = args.json_out or repo / "TestOutput" / "validation" / "project_filters" / "summary.json"

    # Why: explicit project mode preserves targeted validation for auxiliary
    # projects, while default mode proves full production source ownership.
    explicit_project_mode = args.project is not None or args.filters is not None
    if explicit_project_mode:
        project_path = args.project or repo / "SKULLBONEZ_CORE.vcxproj"
        filters_path = args.filters or repo / "SKULLBONEZ_CORE.vcxproj.filters"
        errors, stats = validate_project_filters(
            repo,
            project_path,
            filters_path,
            require_all_source_files=not args.partial_project,
        )
        summary = {
            "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
            "project": repo_relative(repo, project_path),
            "filters": repo_relative(repo, filters_path),
            "status": "pass" if not errors else "fail",
            **stats,
            "errors": errors,
        }
    else:
        project_specs = default_production_project_specs(repo)
        errors, stats, project_summaries = validate_production_project_filters(repo, project_specs)
        summary = {
            "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
            "projectSet": "production",
            "projects": project_summaries,
            "status": "pass" if not errors else "fail",
            **stats,
            "errors": errors,
        }
    write_summary(summary_path, summary)

    for error in errors[: args.max_errors]:
        print(f"ERROR: {error}")
    if len(errors) > args.max_errors:
        print(f"ERROR: suppressed {len(errors) - args.max_errors} additional project filter issue(s).")

    if explicit_project_mode:
        print(
            f"Project filter summary: {repo_relative(repo, summary_path)} "
            f"({len(errors)} errors, {stats['projectItemCount']} project items, "
            f"{stats['filterItemCount']} filter items)"
        )
    else:
        print(
            f"Project filter summary: {repo_relative(repo, summary_path)} "
            f"({len(errors)} errors, {stats['projectItemCount']} project items, "
            f"{stats['filterItemCount']} filter items across {stats['projectCount']} production projects)"
        )

    if errors:
        print("FAIL: Project filter validation failed.")
        return 1

    print("PASS: Project filter validation passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
