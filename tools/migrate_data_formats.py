# File: tools/migrate_data_formats.py
# Purpose: Upgrade authored asset libraries, convex hulls, and engine config files.
#
# Mental model:
#   Runtime readers retain only their current/current-1 compatibility window.
#   This cold tool owns durable rewrites from legacy authored inputs to the
#   current native stamp without changing scene version history.
#
# Glossary:
#   Native stamp: The integer version field/directive owned by one file format.
#   Check mode: Read-only CI/editor probe that reports files needing migration.
#   Legacy v0: An asset library or engine config written before version stamps.
#
# Invariants:
#   - Rewriting an already-current file is byte-idempotent.
#   - Future versions fail and are never downgraded.
#   - Scene/style JSON is outside this tool; its v1->v2 path remains parser-owned.
#
# Related:
#   - tools/bake_hulls.py
#   - SkullbonezSource/Scene/TestSceneParserAssets.cpp
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
CONFIG_VERSION = 1
ASSET_FORMAT = "skullbonez.asset_library.json"
CONFIG_VERSION_RE = re.compile(r"^(?P<indent>\s*)format_version\s*=\s*(?P<version>[^#\s]+)(?P<tail>\s*(?:#.*)?)$")


class MigrationError(RuntimeError):
    pass


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
        lines[index] = f"{match.group('indent')}format_version = {CONFIG_VERSION}{match.group('tail')}"
    else:
        # Named v0->v1 step: preserve the established key/value grammar and
        # place the stamp after the introductory comment block.
        insert_at = next((index for index, line in enumerate(lines) if not line.startswith("#")), len(lines))
        lines.insert(insert_at, f"format_version = {CONFIG_VERSION}")

    return "\n".join(lines) + "\n"


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
    if name.endswith(".hull"):
        return "hull"
    if name == "engine.cfg":
        return "config"
    raise MigrationError(f"{path}: unsupported authored data format")


def expected_text(path: Path) -> str:
    kind = classify(path)
    if kind == "hull":
        return migrate_hull(path)
    current = path.read_text(encoding="utf-8")
    if kind == "asset":
        return migrate_asset_text(current, path)
    return migrate_config_text(current, path)


def discover(repo: Path, explicit: list[Path]) -> list[Path]:
    if explicit:
        return [path.resolve() if path.is_absolute() else (repo / path).resolve() for path in explicit]
    return sorted((repo / "SkullbonezData" / "assets").glob("*.assets.json")) + sorted(
        (repo / "SkullbonezData" / "hulls").glob("*.hull")
    ) + [repo / "SkullbonezData" / "engine.cfg"]


def self_test() -> None:
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
    assert "format_version = 1\n" in config
    assert migrate_config_text(config, config_path) == config
    try:
        migrate_config_text("format_version = 2\n", config_path)
    except MigrationError as exc:
        assert "newer than current version 1" in str(exc)
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
            current = path.read_text(encoding="utf-8")
            expected = expected_text(path)
            if current == expected:
                if args.write:
                    print(f"current {path.relative_to(repo)}")
                continue
            if args.write:
                path.write_text(expected, encoding="utf-8", newline="\n")
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
