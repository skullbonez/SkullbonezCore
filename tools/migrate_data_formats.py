# File: tools/migrate_data_formats.py
# Purpose:
#   Upgrade authored scenes, asset libraries, convex hulls, and engine config
#   files.
#
# Summary:
#   This cold tool owns deterministic rewrites from legacy authored inputs to
#   each format's current native stamp; runtime compatibility remains specific
#   to that format and version. Migration steps are sequenced by source version
#   so an already-crossed quaternion or vocabulary boundary is never applied
#   twice.
#
# Glossary:
#   Native stamp: The integer version field/directive owned by one file format.
#   Check mode: Read-only CI/editor probe that reports files needing migration.
#   Legacy v0: An asset library or engine config written before version stamps.
#   Config v2: Adds the enabled-by-default deterministic mutual-gravity worker
#     key after the sibling apply-forces execution key.
#   Config v3: Adds the default-off pinned SIMD-kernel key after the integration
#     execution key; old content remains on the byte-exact scalar path.
#   Config v4: Removes the rejected SIMD-kernel key; the retained engine path is
#     scalar-only over the byte-certified SoA body layout.
#   Config v5: Removes every contact-audio setting after the subsystem and its
#     authored content were retired.
#   Config v6: Removes the render-only terrain sampling key after rendering and
#     collision returned to one authoritative post grid.
#   Scene v4: Renames the authored impulse application value so the key states
#     that its world-space vector is relative to the body's center.
#
# Invariants:
#   - Rewriting an already-current file is byte-idempotent.
#   - Future versions fail and are never downgraded.
#   - Scene v1/v2 raw orientations are conjugated exactly once on the v3 step.
#   - Scene v3 impulse offsets retain their numbers while only the key changes.
#   - Config migrations edit only their owned rows and preserve unrelated rows.
#
# Related:
#   - tools/bake_hulls.py
#   - SkullbonezSource/Scene/AuthoredSceneParserAssets.cpp
#   - SkullbonezSource/Core/Config.cpp

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

import bake_hulls


ASSET_LIBRARY_VERSION = 1
CONFIG_VERSION = 6
SCENE_VERSION = 4
ASSET_FORMAT = "skullbonez.asset_library.json"
SCENE_FORMAT = "skullbonez.scene.json"
CONFIG_VERSION_RE = re.compile(r"^(?P<indent>\s*)format_version\s*=\s*(?P<version>[^#\s]+)(?P<tail>\s*(?:#.*)?)$")
JSON_NUMBER = r"-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?"
ORIENTATION_RE = re.compile(
    rf'("orientation"\s*:\s*\[\s*)({JSON_NUMBER})(\s*,\s*)({JSON_NUMBER})'
    rf'(\s*,\s*)({JSON_NUMBER})(\s*,\s*)({JSON_NUMBER})(\s*\])'
)
SCENE_VERSION_RE = re.compile(r'("version"\s*:\s*)(\d+)')
LEGACY_IMPULSE_OFFSET_KEY_RE = re.compile(r'"forcePosition"(?=\s*:)')


class MigrationError(RuntimeError):
    pass


def _negate_json_number(token: str) -> str:
    if token.startswith("-"):
        positive = token[1:]
        return "0.0" if float(positive) == 0.0 and "." not in positive and "e" not in positive.lower() else positive
    if float(token) == 0.0 and "." not in token and "e" not in token.lower():
        return "-0.0"
    return f"-{token}"


def _count_scene_orientations(value: object) -> int:
    if isinstance(value, dict):
        count = 0
        for key, child in value.items():
            if key == "orientation":
                if (
                    not isinstance(child, list)
                    or len(child) != 4
                    or any(not isinstance(component, (int, float)) or isinstance(component, bool) for component in child)
                ):
                    raise MigrationError("scene orientation must be a four-number array")
                count += 1
            count += _count_scene_orientations(child)
        return count
    if isinstance(value, list):
        return sum(_count_scene_orientations(child) for child in value)
    return 0


def _count_scene_key(value: object, target: str) -> int:
    if isinstance(value, dict):
        return sum((1 if key == target else 0) + _count_scene_key(child, target) for key, child in value.items())
    if isinstance(value, list):
        return sum(_count_scene_key(child, target) for child in value)
    return 0


def migrate_scene_text(text: str, path: Path) -> str:
    try:
        document = json.loads(text)
    except json.JSONDecodeError as exc:
        raise MigrationError(f"{path}: invalid scene JSON: {exc}") from exc
    if not isinstance(document, dict) or document.get("format") != SCENE_FORMAT:
        raise MigrationError(f"{path}: expected format {SCENE_FORMAT!r}")

    version = document.get("version")
    if not isinstance(version, int) or isinstance(version, bool) or version < 1:
        raise MigrationError(f"{path}: scene version must be a positive integer")
    if version > SCENE_VERSION:
        raise MigrationError(f"{path}: scene version {version} is newer than current version {SCENE_VERSION}")
    migrated = text

    # Named v1/v2->v3 step: stored quaternions changed convention. A v3 file
    # has already crossed this boundary and must never be conjugated again.
    if version < 3:
        expected_count = _count_scene_orientations(document)

        def conjugate(match: re.Match[str]) -> str:
            return (
                match.group(1)
                + _negate_json_number(match.group(2))
                + match.group(3)
                + _negate_json_number(match.group(4))
                + match.group(5)
                + _negate_json_number(match.group(6))
                + match.group(7)
                + match.group(8)
                + match.group(9)
            )

        migrated, count = ORIENTATION_RE.subn(conjugate, migrated)
        if count != expected_count:
            raise MigrationError(f"{path}: parsed {expected_count} orientations but rewrote {count}")

    # Named v3->v4 step: the old key sounded like an absolute world point, but
    # the value has always crossed into Physics as a world-space offset from
    # the body's center. Preserve every number and change only that key token.
    if version < 4:
        current_key_count = _count_scene_key(document, "impulseWorldOffsetFromCenter")
        if current_key_count:
            raise MigrationError(
                f"{path}: scene version {version} uses impulseWorldOffsetFromCenter, which requires version 4"
            )
        expected_count = _count_scene_key(document, "forcePosition")
        migrated, count = LEGACY_IMPULSE_OFFSET_KEY_RE.subn('"impulseWorldOffsetFromCenter"', migrated)
        if count != expected_count:
            raise MigrationError(f"{path}: parsed {expected_count} forcePosition keys but rewrote {count}")

    if version == SCENE_VERSION:
        return migrated

    migrated, version_count = SCENE_VERSION_RE.subn(rf"\g<1>{SCENE_VERSION}", migrated, count=1)
    if version_count != 1:
        raise MigrationError(f"{path}: scene version field was not found")
    return migrated


def migrate_asset_text(text: str, path: Path) -> str:
    try:
        document = json.loads(text)
    except json.JSONDecodeError as exc:
        raise MigrationError(f"{path}: invalid asset-library JSON: {exc}") from exc
    if not isinstance(document, dict) or document.get("format") != ASSET_FORMAT:
        raise MigrationError(f"{path}: expected format {ASSET_FORMAT!r}")

    version = document.get("version", 0)
    if not isinstance(version, int) or isinstance(version, bool) or version < 0:
        raise MigrationError(f"{path}: asset-library version must be a non-negative integer")
    if version > ASSET_LIBRARY_VERSION:
        raise MigrationError(
            f"{path}: asset-library version {version} is newer than current version {ASSET_LIBRARY_VERSION}"
        )

    # Named v0->v1 step: the recipe grammar is unchanged; insert the owned
    # stamp directly after format while preserving all remaining key order.
    migrated: dict[str, object] = {}
    for key, value in document.items():
        migrated[key] = value
        if key == "format":
            migrated["version"] = ASSET_LIBRARY_VERSION
    if "version" not in migrated:
        migrated = {"version": ASSET_LIBRARY_VERSION, **migrated}
    migrated["version"] = ASSET_LIBRARY_VERSION
    return json.dumps(migrated, indent=2, ensure_ascii=False) + "\n"


def migrate_config_text(text: str, path: Path) -> str:
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines()
    version_rows: list[tuple[int, re.Match[str]]] = []
    for index, line in enumerate(lines):
        match = CONFIG_VERSION_RE.match(line)
        if match:
            version_rows.append((index, match))
    if len(version_rows) > 1:
        raise MigrationError(f"{path}: duplicate format_version rows")

    if version_rows:
        index, match = version_rows[0]
        try:
            version = int(match.group("version"), 10)
        except ValueError as exc:
            raise MigrationError(f"{path}: invalid format_version") from exc
        if version < 0:
            raise MigrationError(f"{path}: format_version must be non-negative")
        if version > CONFIG_VERSION:
            raise MigrationError(f"{path}: config version {version} is newer than current version {CONFIG_VERSION}")
        source_version = version
        lines[index] = f"{match.group('indent')}format_version = {CONFIG_VERSION}{match.group('tail')}"
    else:
        # Named v0->v1 step: preserve the established key/value grammar and
        # place the stamp after the introductory comment block.
        source_version = 0
        insert_at = next((index for index, line in enumerate(lines) if not line.startswith("#")), len(lines))
        lines.insert(insert_at, f"format_version = {CONFIG_VERSION}")

    # Named v1->v2 step: make the new worker lane explicit while preserving the
    # v1 runtime default. Place it beside the existing physics execution keys.
    mutual_gravity_key = "physics_parallel_mutual_gravity"
    if source_version < 2 and not any(line.partition("=")[0].strip() == mutual_gravity_key for line in lines):
        apply_forces_row = next(
            (index for index, line in enumerate(lines) if line.partition("=")[0].strip() == "physics_parallel_apply_forces"),
            None,
        )
        insert_at = apply_forces_row + 1 if apply_forces_row is not None else len(lines)
        lines.insert(insert_at, f"{mutual_gravity_key} = 1")

    # Named v2->v3 step: keep legacy content on the certified scalar path and
    # place the dark-kernel switch beside its integration execution owner.
    simd_kernel_key = "physics_simd_kernels"
    if source_version < 3 and not any(line.partition("=")[0].strip() == simd_kernel_key for line in lines):
        integrate_row = next(
            (index for index, line in enumerate(lines) if line.partition("=")[0].strip() == "physics_parallel_integrate"),
            None,
        )
        insert_at = integrate_row + 1 if integrate_row is not None else len(lines)
        lines.insert(insert_at, f"{simd_kernel_key} = 0")

    # Named v3->v4 step: the owner rejected the SIMD cutover. Remove every
    # obsolete toggle row so migrated content cannot imply a dormant path that
    # no longer exists. Applying this normalization to current files also makes
    # --check catch a manually retained v4 row.
    lines = [line for line in lines if line.partition("=")[0].strip() != simd_kernel_key]

    # Named v4->v5 step: remove the complete retired contact-audio namespace.
    # Applying this normalization to current files also makes --check reject a
    # manually retained v5 row rather than preserving a setting with no owner.
    lines = [line for line in lines if not line.partition("=")[0].strip().startswith("contact_audio_")]

    # Named v5->v6 step: the render-only dense height sampler was retired so
    # visual geometry cannot diverge from the collision-authoritative posts.
    terrain_render_step_key = "terrain_render_step_size"
    lines = [line for line in lines if line.partition("=")[0].strip() != terrain_render_step_key]

    return newline.join(lines) + newline


def migrate_hull(path: Path) -> str:
    try:
        source = bake_hulls.read_source_hull(path, allow_unversioned=True)
        return bake_hulls.serialize_hull(bake_hulls.bake_source_hull(source))
    except bake_hulls.HullError as exc:
        raise MigrationError(str(exc)) from exc


def classify(path: Path) -> str:
    name = path.name.lower()
    if name.endswith(".assets.json"):
        return "asset"
    if name.endswith(".scene.json"):
        return "scene"
    if name.endswith(".hull"):
        return "hull"
    if name == "engine.cfg":
        return "config"
    raise MigrationError(f"{path}: unsupported authored data format")


def expected_text(path: Path) -> str:
    kind = classify(path)
    if kind == "hull":
        return migrate_hull(path)
    current = path.read_bytes().decode("utf-8")
    if kind == "asset":
        return migrate_asset_text(current, path)
    if kind == "scene":
        return migrate_scene_text(current, path)
    return migrate_config_text(current, path)


def discover(repo: Path, explicit: list[Path]) -> list[Path]:
    if explicit:
        return [path.resolve() if path.is_absolute() else (repo / path).resolve() for path in explicit]
    # Why: the default census follows content that needs a named migration. A
    # current v3 orientation-only scene is outside the v4 vocabulary change and
    # may be provenance-bound to an artifact that this phase cannot regenerate.
    scenes = []
    for path in sorted((repo / "SkullbonezData" / "scenes").glob("*.scene.json")):
        text = path.read_bytes().decode("utf-8")
        version_match = SCENE_VERSION_RE.search(text)
        version = int(version_match.group(2)) if version_match else 0
        needs_quaternion_step = version < 3 and (
            '"orientation"' in text or path.name == "buoyancy_inertia_orientation.scene.json"
        )
        needs_impulse_offset_step = '"forcePosition"' in text or '"impulseWorldOffsetFromCenter"' in text
        if needs_quaternion_step or needs_impulse_offset_step:
            scenes.append(path)
    return (
        sorted((repo / "SkullbonezData" / "assets").glob("*.assets.json"))
        + sorted((repo / "SkullbonezData" / "hulls").glob("*.hull"))
        + scenes
        + [repo / "SkullbonezData" / "engine.cfg"]
    )


def self_test() -> None:
    scene_path = Path("legacy.scene.json")
    legacy_scene = (
        '{"format":"skullbonez.scene.json","version":2,'
        '"orientation":[0,-0.0,1.25e-3,-4.0],"forcePosition":[1,2,3]}'
    )
    scene = migrate_scene_text(legacy_scene, scene_path)
    assert scene == (
        '{"format":"skullbonez.scene.json","version":4,'
        '"orientation":[-0.0,0.0,-1.25e-3,-4.0],"impulseWorldOffsetFromCenter":[1,2,3]}'
    )
    assert migrate_scene_text(scene, scene_path) == scene
    v3_scene = migrate_scene_text(
        '{"format":"skullbonez.scene.json","version":3,'
        '"orientation":[0.25,-0.5,0.75,1.0],"forcePosition":[4,5,6]}',
        scene_path,
    )
    assert v3_scene == (
        '{"format":"skullbonez.scene.json","version":4,'
        '"orientation":[0.25,-0.5,0.75,1.0],"impulseWorldOffsetFromCenter":[4,5,6]}'
    )
    try:
        migrate_scene_text('{"format":"skullbonez.scene.json","version":5}', scene_path)
    except MigrationError as exc:
        assert "newer than current version 4" in str(exc)
    else:
        raise AssertionError("future scene version must fail")
    try:
        migrate_scene_text(
            '{"format":"skullbonez.scene.json","version":3,'
            '"impulseWorldOffsetFromCenter":[1,2,3]}',
            scene_path,
        )
    except MigrationError as exc:
        assert "requires version 4" in str(exc)
    else:
        raise AssertionError("future scene key in a legacy version must fail")

    asset_path = Path("legacy.assets.json")
    asset = migrate_asset_text('{"format":"skullbonez.asset_library.json","assets":[]}', asset_path)
    assert '"version": 1' in asset
    assert migrate_asset_text(asset, asset_path) == asset
    try:
        migrate_asset_text('{"format":"skullbonez.asset_library.json","version":2,"assets":[]}', asset_path)
    except MigrationError as exc:
        assert "newer than current version 1" in str(exc)
    else:
        raise AssertionError("future asset version must fail")

    config_path = Path("engine.cfg")
    config = migrate_config_text("# config\nscreen_x = 1\n", config_path)
    assert "format_version = 6\n" in config
    assert "physics_parallel_mutual_gravity = 1\n" in config
    assert "physics_simd_kernels" not in config
    assert migrate_config_text(config, config_path) == config
    v3_config = migrate_config_text(
        "format_version = 3\nphysics_parallel_integrate = 1\nphysics_simd_kernels = 1\nscreen_x = 2\n",
        config_path,
    )
    assert "format_version = 6\n" in v3_config
    assert "physics_simd_kernels" not in v3_config
    assert "screen_x = 2\n" in v3_config
    v4_config = migrate_config_text(
        "format_version = 4\ncontact_audio_enabled = 1\ncontact_audio_master_gain = 0.5\nscreen_x = 3\n",
        config_path,
    )
    assert "format_version = 6\n" in v4_config
    assert "contact_audio_" not in v4_config
    assert "screen_x = 3\n" in v4_config
    v5_config = migrate_config_text(
        "format_version = 5\nterrain_render_step_size = 1\nscreen_x = 4\n",
        config_path,
    )
    assert "format_version = 6\n" in v5_config
    assert "terrain_render_step_size" not in v5_config
    assert "screen_x = 4\n" in v5_config
    try:
        migrate_config_text("format_version = 7\n", config_path)
    except MigrationError as exc:
        assert "newer than current version 6" in str(exc)
    else:
        raise AssertionError("future config version must fail")

    with tempfile.TemporaryDirectory() as directory:
        hull_path = Path(directory) / "legacy.hull"
        hull_path.write_text(
            "name tetra\n"
            "source_vertex 0 0 0\nsource_vertex 1 0 0\nsource_vertex 0 1 0\nsource_vertex 0 0 1\n"
            "source_face 0 2 1\nsource_face 0 1 3\nsource_face 0 3 2\nsource_face 1 2 3\n",
            encoding="utf-8",
        )
        hull = migrate_hull(hull_path)
        assert f"hull_version {bake_hulls.CURRENT_HULL_VERSION}" in hull
        hull_path.write_text(hull, encoding="utf-8", newline="\n")
        assert migrate_hull(hull_path) == hull
        hull_path.write_text(hull.replace("hull_version 2", "hull_version 3", 1), encoding="utf-8")
        try:
            migrate_hull(hull_path)
        except MigrationError as exc:
            assert "newer than current version 2" in str(exc)
        else:
            raise AssertionError("future hull version must fail")


def main() -> int:
    parser = argparse.ArgumentParser(description="Migrate authored Skullbonez data formats to current versions.")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="Rewrite supported files to current versions.")
    mode.add_argument("--check", action="store_true", help="Fail when a supported file is not current.")
    mode.add_argument("--self-test", action="store_true", help="Run deterministic migration unit probes.")
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("paths", nargs="*", type=Path)
    args = parser.parse_args()

    if args.self_test:
        self_test()
        print("MIGRATE_DATA_FORMATS_SELF_TEST: OK")
        return 0

    repo = args.repo.resolve()
    stale: list[Path] = []
    failures: list[str] = []
    paths = discover(repo, args.paths)
    for path in paths:
        try:
            current = path.read_bytes().decode("utf-8")
            expected = expected_text(path)
            if current == expected:
                if args.write:
                    print(f"current {path.relative_to(repo)}")
                continue
            if args.write:
                path.write_bytes(expected.encode("utf-8"))
                print(f"migrated {path.relative_to(repo)}")
            else:
                stale.append(path)
        except (OSError, MigrationError) as exc:
            failures.append(str(exc))

    if failures:
        print("ERROR: authored data migration failed:")
        for failure in failures:
            print(f"  {failure}")
        return 2
    if stale:
        print("ERROR: authored data files need migration:")
        for path in stale:
            print(f"  {path.relative_to(repo)}")
        return 1
    if args.check:
        print(f"MIGRATE_DATA_FORMATS: OK ({len(paths)} files checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
