#!/usr/bin/env python3
#
# File: tools/validate_project_filters.py
# Purpose:
#   Validate Visual Studio project item filters and path casing.
#
# Mental model:
#   The .vcxproj controls what Visual Studio builds or displays, while the
#   .vcxproj.filters file controls where those items appear in Solution
#   Explorer. This check keeps source, headers, scenes, shaders, and style data
#   in predictable filters so project edits do not slowly drift.
#
# Glossary:
#   Filter: A Visual Studio virtual folder stored in .vcxproj.filters.
#   Project item: A build or content entry such as ClCompile, ClInclude, or None.
#   Validation gate: Repository script that proves a class of changes before
#   commit or PR.
#
# Invariants:
#   - Every project item that belongs in Solution Explorer has one expected
#     filter entry.
#   - Source and header pairs stay in matching source/header filter categories.
#   - Project paths use the exact casing of the file on disk.
#
# Related:
#   - AGENTS.md
#   - tools/README.md
#
"""Validate SKULLBONEZ_CORE.vcxproj item filters."""

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
SCENE_FILTER = "Scene Files"
STYLE_FILTER = "Style Files"
PROJECT_FILTER = "Project Files"
SHADER_FILTER = "Resource Files\\HLSL"
SOURCE_PROJECT_ROOT = "SkullbonezSource"
SOURCE_PROJECT_SUFFIX_TYPES = {
    ".cpp": "ClCompile",
    ".h": "ClInclude",
}

MATH_PREFIXES = (
    "GeometricMath",
    "GeometricStructures",
    "Matrix4",
    "Quaternion",
    "RotationMatrix",
    "Vector3",
)

ASSET_PREFIXES = (
    "AssetSystem",
    "TextureCollection",
)

GAME_OBJECT_PREFIXES = (
    "GameModelCollection",
    "GameModelSoACache",
    "GameModelStreams",
    "GameModel",
)

PHYSICS_PREFIXES = (
    "BoundingBox",
    "BoundingSphere",
    "CollisionShape",
    "ColliderStore",
    "ContactSolverCommon",
    "ConvexHullShape",
    "ObjectContactManifold",
    "PersistentContactSolver",
    "PhysicsBodyStore",
    "PhysicsDiagnosticsSink",
    "PhysicsMass",
    "Ragdoll",
    "PhysicsScene",
    "PhysicsWorld",
    "ResponseInformation",
    "RigidBody",
    "SimulationSystem",
    "SleepIslandSystem",
    "SpatialGrid",
    "TornadoField",
)

PHYSICS_DEBUG_PREFIXES = (
    "BroadphaseVisualizer",
    "CollisionVisualizer",
    "PhysicsDebugVisualizer",
)

DX12_RENDERING_PREFIXES = (
    "BLASDX12",
    "Dx12RenderGraphExecutor",
    "FramebufferDX12",
    "MeshDX12",
    "RenderBackendDX12",
    "RenderDeviceDX12",
    "SBTDX12",
    "ShaderDX12",
    "TLASDX12",
)

RENDERING_PREFIXES = (
    "DrawCallTrace",
    "GameModelRenderer",
    "Helper",
    "IFramebuffer",
    "IMesh",
    "IRenderBackend",
    "IShader",
    "PrimitiveMeshBuilder",
    "RenderGraph",
    "RenderPipeline",
    "RenderInstanceStore",
    "RenderSceneSnapshot",
    "RenderSceneView",
    "RenderMaterial",
    "ShaderContracts",
    "Shadow",
    "Text",
)

SCENE_PREFIXES = (
    "SceneSnapshotWriter",
    "TestScene",
    "TestSceneParser",
)

WORLD_PREFIXES = (
    "SkyBox",
    "Terrain",
    "TerrainSupportClassifier",
    "WorldEnvironment",
)

RUNTIME_PREFIXES = (
    "Camera",
    "CameraCollection",
    "CaptureController",
    "CaptureSystem",
    "DiagnosticsController",
    "EngineContext",
    "Init",
    "Input",
    "InputController",
    "Run",
    "RunCapture",
    "RunFrame",
    "RunInput",
    "RunInternal",
    "RunLiveStyle",
    "RunPasses",
    "RunRender",
    "RunStress",
    "RunUiTextPass",
    "RuntimeCommandQueue",
    "RuntimeDiagnostics",
    "RuntimeFileWriter",
    "RuntimeInteractionController",
    "RuntimeTuning",
    "RuntimeViewModel",
    "SimulationController",
    "Window",
)

RUNTIME_SCENE_PREFIXES = (
    "RunScene",
    "SceneController",
    "SceneGeneratedSetup",
    "SceneRuntime",
    "SceneRuntimeCoordinator",
)

RUNTIME_REPLAY_PREFIXES = (
    "ReplayExporter",
    "ReplayRecorder",
    "ReplaySolverSnapshot",
    "ReplayV2Artifact",
    "RunReplayTools",
)

RUNTIME_RENDER_PREFIXES = (
    "RuntimeRenderHost",
    "RuntimeRenderInputs",
    "RuntimeRenderPasses",
    "RuntimeRenderer",
)

RUNTIME_EDITOR_PREFIXES = (
    "EditorTools",
    "EditorHullAssets",
    "LauncherLaser",
    "LauncherTools",
    "RunEditorTools",
)

CORE_PREFIXES = (
    "AmortizedTask",
    "Common",
    "Config",
    "Fence",
    "LockOrderValidator",
    "Log",
    "PlatformProfiler",
    "Profiler",
    "SkullScope",
    "Timer",
    "WorkerPool",
)

AREA_PREFIXES = (
    ("Rendering\\DX12", DX12_RENDERING_PREFIXES),
    ("Runtime\\Scene", RUNTIME_SCENE_PREFIXES),
    ("Runtime\\Replay", RUNTIME_REPLAY_PREFIXES),
    ("Runtime\\Render", RUNTIME_RENDER_PREFIXES),
    ("Runtime\\Editor", RUNTIME_EDITOR_PREFIXES),
    ("Physics\\Debug", PHYSICS_DEBUG_PREFIXES),
    ("Rendering", RENDERING_PREFIXES),
    ("Physics", PHYSICS_PREFIXES),
    ("World", WORLD_PREFIXES),
    ("GameObjects", GAME_OBJECT_PREFIXES),
    ("Assets", ASSET_PREFIXES),
    ("Maths", MATH_PREFIXES),
    ("Scene", SCENE_PREFIXES),
    ("Runtime", RUNTIME_PREFIXES),
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
    if len(parts) >= 3 and parts[0].lower() == "skullbonezsource" and parts[1].lower() == "ui":
        return "UI"

    stem = path.stem
    for area, prefixes in AREA_PREFIXES:
        if any(stem == prefix or stem.startswith(f"{prefix}.") for prefix in prefixes):
            return area
    return None


def expected_filter_for(item: ProjectItem) -> str | None:
    include = normalize_path(item.include)
    lower = include.lower()
    suffix = PureWindowsPath(include).suffix.lower()

    if item.item_type == "ClCompile":
        area = source_area(include)
        if area is None:
            return None
        if area == EXTERNAL_FILTER:
            return EXTERNAL_FILTER
        return f"{SOURCE_FILTER_ROOT}\\{area}"

    if item.item_type == "ClInclude":
        area = source_area(include)
        if area is None:
            return None
        if area == EXTERNAL_FILTER:
            return EXTERNAL_FILTER
        return f"{HEADER_FILTER_ROOT}\\{area}"

    if item.item_type == "None":
        if lower == "packages.config":
            return PROJECT_FILTER
        if lower.startswith("skullbonezdata\\shaders\\") and suffix in {".hlsl", ".dxil"}:
            return SHADER_FILTER
        if lower.startswith("skullbonezdata\\scenes\\") and (lower.endswith(".scene.json") or lower.endswith(".suite.json")):
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
        source_suffix = source.filter_name.removeprefix(f"{SOURCE_FILTER_ROOT}\\")
        header_suffix = header.filter_name.removeprefix(f"{HEADER_FILTER_ROOT}\\")
        if source.filter_name == EXTERNAL_FILTER and header.filter_name == EXTERNAL_FILTER:
            continue
        if source_suffix != header_suffix:
            errors.append(
                f"{source.include} and {header.include} must use matching source/header filters "
                f"(found {source.filter_name} and {header.filter_name})."
            )
    return errors


def validate_project_filters(repo: Path, project_path: Path, filters_path: Path) -> tuple[list[str], dict[str, int]]:
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

    errors.extend(duplicate_item_errors("project", project_items))
    errors.extend(duplicate_item_errors("filters", filter_items))

    project_by_key = {item.key: item for item in project_items}
    filter_by_key = {item.key: item for item in filter_items}
    source_files_on_disk = read_source_files_on_disk(repo)

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

        expected_filter = expected_filter_for(item)
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


def write_summary(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--project", type=Path, default=None)
    parser.add_argument("--filters", type=Path, default=None)
    parser.add_argument("--json-out", type=Path, default=None)
    parser.add_argument("--max-errors", type=int, default=80)
    args = parser.parse_args()

    repo = args.repo.resolve()
    project_path = args.project or repo / "SKULLBONEZ_CORE.vcxproj"
    filters_path = args.filters or repo / "SKULLBONEZ_CORE.vcxproj.filters"
    summary_path = args.json_out or repo / "TestOutput" / "validation" / "project_filters" / "summary.json"

    errors, stats = validate_project_filters(repo, project_path, filters_path)
    summary = {
        "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
        "project": repo_relative(repo, project_path),
        "filters": repo_relative(repo, filters_path),
        "status": "pass" if not errors else "fail",
        **stats,
        "errors": errors,
    }
    write_summary(summary_path, summary)

    for error in errors[: args.max_errors]:
        print(f"ERROR: {error}")
    if len(errors) > args.max_errors:
        print(f"ERROR: suppressed {len(errors) - args.max_errors} additional project filter issue(s).")

    print(
        f"Project filter summary: {repo_relative(repo, summary_path)} "
        f"({len(errors)} errors, {stats['projectItemCount']} project items, "
        f"{stats['filterItemCount']} filter items)"
    )

    if errors:
        print("FAIL: Project filter validation failed.")
        return 1

    print("PASS: Project filter validation passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
