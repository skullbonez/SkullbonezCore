#
# File: tools/check_physics_baseline_guard.py
# Purpose:
#   Protects Physics-plan golden transitions with exact content and retained binaries.
#
# Summary:
#   Ordinary validation checks the committed golden without writing it. A Physics
#   plan may replace a golden noninteractively when the caller supplies the exact
#   candidate SHA-256 and an append-only transition manifest that retains both the
#   old and new launchable executables. The Git-index check binds that manifest and
#   every declared executable/DLL to the staged golden bytes.
#
# Glossary:
#   Acceptance record: Tracked compatibility JSON binding the core Physics golden
#     path and SHA-256. Its historical filename remains stable for existing gates.
#   Transition manifest: Machine-readable JSON inside an immutable Physics-plan
#     artifact bundle, binding old/new golden hashes to retained launch payloads.
#   Transition receipt: Local Git metadata binding one exact staged golden,
#     acceptance-record, and transition-manifest byte set.
#   Bootstrap acceptance: The repository owner's accepted golden that predates
#     this guard and therefore has no earlier tracked transition bundle.
#
# Invariants:
#   - Ordinary validation never writes a golden or acceptance record.
#   - Every automated write is content-bound and noninteractive.
#   - Staged content is read from the Git index, not the working tree.
#   - A transition bundle is new for one transition and can never be overwritten.
#   - Every declared executable and DLL is verified by path, byte size, and SHA-256.
#
# Related:
#   - AGENTS.md
#   - Agentic/Plans/Artifacts/README.md
#   - tools/check_physics_regression.py
#   - tools/validate_physics.bat
#   - tools/physics_baseline_approval.json
#
"""Fail closed when a Physics golden or retained transition bundle is inconsistent."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tempfile
from datetime import datetime, timezone


ACCEPTANCE_RECORD = "tools/physics_baseline_approval.json"
BASELINE_PATH = "TestOutput/baselines/physics_regression_varied.csv"
ARTIFACT_ROOT = "Agentic/Plans/Artifacts"
TRANSITION_DIRECTORY = "golden-transitions"
ALWAYS_ARCHIVED_PHYSICS_GOLDENS = {
    BASELINE_PATH,
    "TestOutput/baselines/bullet_sweep_object.csv",
    "TestOutput/baselines/bullet_sweep_terrain.csv",
    "TestOutput/baselines/bullet_sweep_wall.csv",
    "TestOutput/baselines/physics_bench_perf.json",
    "TestOutput/baselines/physics_known_issue_signatures.json",
    "TestOutput/baselines/physics_query_varied.json",
    "TestOutput/baselines/shooting_reaction_volley.csv",
    "TestOutput/baselines/space_three_body_chaos.csv",
}
BOOTSTRAP_ACCEPTED_SHA256 = "debf57f744774d4e7c1eb5cc61f05ba6e41dc6dc997ad20db6c91b02b0958c32"
BOOTSTRAP_SOURCE_COMMIT = "7d46a6c3ea75e2f1e2a6e149a23b632aaa6b79b2"
RECEIPT_RELATIVE_PATH = Path("skore-transitions") / "physics-baseline.json"


class GuardFailure(RuntimeError):
    """A bounded, user-actionable transition or integrity failure."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run_git(repo: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess[bytes]:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if check and result.returncode != 0:
        detail = result.stderr.decode(errors="replace").strip()
        raise GuardFailure(f"git {' '.join(args)} failed: {detail}")
    return result


def require_sha256(value: object, label: str) -> str:
    if (
        not isinstance(value, str)
        or len(value) != 64
        or any(character not in "0123456789abcdef" for character in value)
    ):
        raise GuardFailure(f"{label} must be 64 lowercase hexadecimal characters")
    return value


def require_commit(value: object, label: str) -> str:
    if (
        not isinstance(value, str)
        or len(value) != 40
        or any(character not in "0123456789abcdef" for character in value)
    ):
        raise GuardFailure(f"{label} must be a full lowercase Git commit hash")
    return value


def require_existing_commit(repo: Path, value: object, label: str) -> str:
    commit = require_commit(value, label)
    result = run_git(repo, "cat-file", "-e", f"{commit}^{{commit}}", check=False)
    if result.returncode != 0:
        raise GuardFailure(f"{label} does not resolve to a commit in this repository: {commit}")
    return commit


def require_exact_keys(value: object, required: set[str], label: str) -> dict[str, object]:
    if not isinstance(value, dict):
        raise GuardFailure(f"{label} must be a JSON object")
    actual = set(value)
    if actual != required:
        raise GuardFailure(
            f"{label} schema mismatch; missing={sorted(required - actual)}, extra={sorted(actual - required)}"
        )
    return value


def normalize_relative_path(value: object, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise GuardFailure(f"{label} must be a non-empty repository-relative path")
    normalized = value.replace("\\", "/")
    path = PurePosixPath(normalized)
    if path.is_absolute() or ".." in path.parts or "." in path.parts or ":" in path.parts[0]:
        raise GuardFailure(f"{label} must stay inside the repository: {value}")
    return path.as_posix()


def manifest_relative_path(repo: Path, manifest: Path) -> str:
    resolved = (manifest if manifest.is_absolute() else repo / manifest).resolve()
    try:
        relative = resolved.relative_to(repo.resolve())
    except ValueError as exc:
        raise GuardFailure(f"artifact manifest must stay inside the repository: {resolved}") from exc
    return normalize_relative_path(relative.as_posix(), "artifact manifest path")


def read_repo_bytes(repo: Path, relative_path: str, staged: bool) -> bytes:
    if staged:
        return index_bytes(repo, relative_path)
    path = (repo / relative_path).resolve()
    try:
        path.relative_to(repo.resolve())
    except ValueError as exc:
        raise GuardFailure(f"artifact escapes repository: {relative_path}") from exc
    if not path.is_file():
        raise GuardFailure(f"retained artifact is missing: {relative_path}")
    return path.read_bytes()


def parse_record(data: bytes) -> dict[str, object]:
    try:
        record = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise GuardFailure(f"{ACCEPTANCE_RECORD} is not valid UTF-8 JSON: {exc}") from exc

    required = {
        "schema_version",
        "baseline_path",
        "sha256",
        "approved_by",
        "approved_at_utc",
        "source_commit",
        "reason",
    }
    record = require_exact_keys(record, required, ACCEPTANCE_RECORD)
    if record["schema_version"] != 1:
        raise GuardFailure(f"{ACCEPTANCE_RECORD} has unsupported schema_version")
    if record["baseline_path"] != BASELINE_PATH:
        raise GuardFailure(f"{ACCEPTANCE_RECORD} must protect exactly {BASELINE_PATH}")
    require_sha256(record["sha256"], f"{ACCEPTANCE_RECORD} sha256")
    for key in ("approved_by", "approved_at_utc", "source_commit", "reason"):
        if not isinstance(record[key], str) or not record[key].strip():
            raise GuardFailure(f"{ACCEPTANCE_RECORD} field {key} must be a non-empty string")
    return record


def verify_pair(record_data: bytes, baseline_data: bytes) -> tuple[dict[str, object], str]:
    record = parse_record(record_data)
    actual = sha256_bytes(baseline_data)
    if actual != record["sha256"]:
        raise GuardFailure(
            f"physics golden SHA-256 {actual} does not match accepted {record['sha256']}"
        )
    return record, actual


def index_bytes(repo: Path, path: str) -> bytes:
    result = run_git(repo, "show", f":{path}", check=False)
    if result.returncode != 0:
        raise GuardFailure(f"{path} is absent from the Git index; stage the complete transition")
    return result.stdout


def head_bytes(repo: Path, path: str) -> bytes | None:
    result = run_git(repo, "show", f"HEAD:{path}", check=False)
    return result.stdout if result.returncode == 0 else None


def bundle_root_from_manifest(relative_path: str) -> tuple[str, str]:
    parts = PurePosixPath(relative_path).parts
    if (
        len(parts) < 8
        or parts[:3] != ("Agentic", "Plans", "Artifacts")
        or parts[-3] != TRANSITION_DIRECTORY
        or parts[-1] != "manifest.json"
    ):
        raise GuardFailure(
            "artifact manifest must be under "
            "Agentic/Plans/Artifacts/<physics-plan>/<phase>/golden-transitions/<transition-id>/manifest.json"
        )
    transition_id = parts[-2]
    if not transition_id or transition_id in (".", ".."):
        raise GuardFailure("artifact transition id must be non-empty")
    return PurePosixPath(*parts[:-1]).as_posix(), transition_id


def parse_file_record(
    repo: Path,
    value: object,
    label: str,
    bundle_side: str,
    staged: bool,
    expected_suffix: str,
) -> tuple[dict[str, object], bytes]:
    record = require_exact_keys(value, {"path", "size", "sha256"}, label)
    path = normalize_relative_path(record["path"], f"{label}.path")
    expected_prefix = f"{bundle_side}/"
    if not path.startswith(expected_prefix):
        raise GuardFailure(f"{label}.path must stay inside {bundle_side}")
    if PurePosixPath(path).suffix.lower() != expected_suffix:
        raise GuardFailure(f"{label}.path must end in {expected_suffix}: {path}")
    size = record["size"]
    if not isinstance(size, int) or isinstance(size, bool) or size < 0:
        raise GuardFailure(f"{label}.size must be a non-negative integer")
    digest = require_sha256(record["sha256"], f"{label}.sha256")
    data = read_repo_bytes(repo, path, staged)
    if len(data) != size:
        raise GuardFailure(f"{label} size mismatch for {path}: manifest={size}, actual={len(data)}")
    actual_digest = sha256_bytes(data)
    if actual_digest != digest:
        raise GuardFailure(
            f"{label} SHA-256 mismatch for {path}: manifest={digest}, actual={actual_digest}"
        )
    return record, data


def parse_behavior(
    repo: Path,
    value: object,
    label: str,
    bundle_root: str,
    golden_hashes: dict[str, str],
    staged: bool,
) -> list[dict[str, object]]:
    behavior = require_exact_keys(value, {"source_commit", "golden_sha256", "executables"}, label)
    require_existing_commit(repo, behavior["source_commit"], f"{label}.source_commit")
    recorded_hashes = behavior["golden_sha256"]
    if not isinstance(recorded_hashes, dict) or recorded_hashes != golden_hashes:
        raise GuardFailure(f"{label}.golden_sha256 must exactly match the manifest golden set")
    executables = behavior["executables"]
    if not isinstance(executables, list) or not executables:
        raise GuardFailure(f"{label}.executables must retain at least one launchable executable")

    side = "old" if label == "old_behavior" else "new"
    bundle_side = f"{bundle_root}/{side}"
    parsed: list[dict[str, object]] = []
    paths: set[str] = set()
    configurations: set[str] = set()
    for index, raw_executable in enumerate(executables):
        executable_label = f"{label}.executables[{index}]"
        executable = require_exact_keys(
            raw_executable,
            {
                "configuration",
                "path",
                "size",
                "sha256",
                "launch_command",
                "dependency_scan_command",
                "required_dlls",
            },
            executable_label,
        )
        configuration = executable["configuration"]
        if not isinstance(configuration, str) or not configuration.strip() or configuration in configurations:
            raise GuardFailure(f"{executable_label}.configuration must be non-empty and unique")
        configurations.add(configuration)
        executable_record, executable_data = parse_file_record(
            repo,
            {key: executable[key] for key in ("path", "size", "sha256")},
            executable_label,
            bundle_side,
            staged,
            ".exe",
        )
        executable_path = str(executable_record["path"])
        if executable_path in paths:
            raise GuardFailure(f"duplicate retained artifact path: {executable_path}")
        paths.add(executable_path)
        for field in ("launch_command", "dependency_scan_command"):
            command = executable[field]
            if not isinstance(command, str) or not command.strip():
                raise GuardFailure(f"{executable_label}.{field} must be a non-empty string")
            if PurePosixPath(executable_path).name not in command:
                raise GuardFailure(
                    f"{executable_label}.{field} must name {PurePosixPath(executable_path).name}"
                )
        required_dlls = executable["required_dlls"]
        if not isinstance(required_dlls, list):
            raise GuardFailure(f"{executable_label}.required_dlls must be a JSON array")
        parsed_dlls: list[dict[str, object]] = []
        for dll_index, raw_dll in enumerate(required_dlls):
            dll_label = f"{executable_label}.required_dlls[{dll_index}]"
            dll_record, _ = parse_file_record(
                repo, raw_dll, dll_label, bundle_side, staged, ".dll"
            )
            dll_path = str(dll_record["path"])
            if dll_path in paths:
                raise GuardFailure(f"duplicate retained artifact path: {dll_path}")
            paths.add(dll_path)
            parsed_dlls.append(dll_record)
        parsed.append(
            {
                **executable,
                "path": executable_path,
                "data": executable_data,
                "required_dlls": parsed_dlls,
            }
        )
    return parsed


def parse_transition_manifest(
    repo: Path,
    relative_path: str,
    data: bytes,
    staged: bool,
) -> tuple[dict[str, object], list[dict[str, object]], list[dict[str, object]]]:
    try:
        raw = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise GuardFailure(f"artifact manifest is not valid UTF-8 JSON: {exc}") from exc
    manifest = require_exact_keys(
        raw,
        {
            "schema_version",
            "physics_plan",
            "phase",
            "transition_id",
            "source_commit",
            "goldens",
            "old_behavior",
            "new_behavior",
        },
        "artifact manifest",
    )
    if manifest["schema_version"] != 1:
        raise GuardFailure("artifact manifest has unsupported schema_version")
    bundle_root, transition_id = bundle_root_from_manifest(relative_path)
    if manifest["transition_id"] != transition_id:
        raise GuardFailure("artifact manifest transition_id must match its directory name")
    plan = normalize_relative_path(manifest["physics_plan"], "artifact manifest physics_plan")
    if not plan.startswith("Agentic/Plans/TODO/") or not plan.endswith(".md"):
        raise GuardFailure(
            "artifact manifest physics_plan must name an active Markdown plan under Agentic/Plans/TODO"
        )
    if staged:
        indexed_plan = run_git(repo, "show", f":{plan}", check=False)
        if indexed_plan.returncode != 0 and head_bytes(repo, plan) is None:
            raise GuardFailure(f"artifact manifest physics_plan is neither staged nor tracked: {plan}")
    elif not (repo / plan).is_file():
        raise GuardFailure(f"artifact manifest physics_plan does not exist: {plan}")
    if not isinstance(manifest["phase"], str) or not manifest["phase"].strip():
        raise GuardFailure("artifact manifest phase must be non-empty")
    require_existing_commit(repo, manifest["source_commit"], "artifact manifest source_commit")

    raw_goldens = manifest["goldens"]
    if not isinstance(raw_goldens, list) or not raw_goldens:
        raise GuardFailure("artifact manifest goldens must contain at least one transition")
    goldens: list[dict[str, object]] = []
    seen_paths: set[str] = set()
    old_hashes: dict[str, str] = {}
    new_hashes: dict[str, str] = {}
    for index, raw_golden in enumerate(raw_goldens):
        label = f"artifact manifest goldens[{index}]"
        golden = require_exact_keys(raw_golden, {"path", "old_sha256", "new_sha256"}, label)
        path = normalize_relative_path(golden["path"], f"{label}.path")
        if path in seen_paths:
            raise GuardFailure(f"artifact manifest has duplicate golden path: {path}")
        seen_paths.add(path)
        old_digest = require_sha256(golden["old_sha256"], f"{label}.old_sha256")
        new_digest = require_sha256(golden["new_sha256"], f"{label}.new_sha256")
        if old_digest == new_digest:
            raise GuardFailure(f"artifact manifest transition does not change {path}")
        row = {"path": path, "old_sha256": old_digest, "new_sha256": new_digest}
        goldens.append(row)
        old_hashes[path] = old_digest
        new_hashes[path] = new_digest

    old_executables = parse_behavior(
        repo, manifest["old_behavior"], "old_behavior", bundle_root, old_hashes, staged
    )
    new_executables = parse_behavior(
        repo, manifest["new_behavior"], "new_behavior", bundle_root, new_hashes, staged
    )
    old_paths = {str(record["path"]) for record in old_executables}
    new_paths = {str(record["path"]) for record in new_executables}
    if old_paths & new_paths:
        raise GuardFailure("old and new behavior executables must be distinct retained files")
    return manifest, old_executables, new_executables


def assert_transition_bundle_is_new(repo: Path, bundle_root: str) -> None:
    tracked = run_git(repo, "ls-tree", "-r", "--name-only", "HEAD", "--", bundle_root).stdout
    if tracked.strip():
        raise GuardFailure(
            f"transition bundle already exists in HEAD and is immutable; create a new transition id: {bundle_root}"
        )


def validate_physics_plan_transition(
    repo: Path,
    artifact_manifest: Path,
    golden_path: str,
    old_sha256: str,
    new_sha256: str,
    producing_executable: Path,
    configuration: str,
    staged: bool = False,
) -> tuple[str, str]:
    """Validate one manifest row and the retained copy of the producing runtime."""

    relative_path = manifest_relative_path(repo, artifact_manifest)
    bundle_root, _ = bundle_root_from_manifest(relative_path)
    assert_transition_bundle_is_new(repo, bundle_root)
    data = read_repo_bytes(repo, relative_path, staged)
    manifest, _, new_executables = parse_transition_manifest(repo, relative_path, data, staged)
    normalized_golden = normalize_relative_path(golden_path, "golden path")
    expected_old = require_sha256(old_sha256, "old golden SHA-256")
    expected_new = require_sha256(new_sha256, "candidate golden SHA-256")
    matches = [row for row in manifest["goldens"] if row["path"] == normalized_golden]
    if len(matches) != 1:
        raise GuardFailure(f"artifact manifest must contain exactly one row for {normalized_golden}")
    if matches[0]["old_sha256"] != expected_old or matches[0]["new_sha256"] != expected_new:
        raise GuardFailure(
            f"artifact manifest hashes do not match {normalized_golden}: "
            f"expected={expected_old}->{expected_new}, "
            f"manifest={matches[0]['old_sha256']}->{matches[0]['new_sha256']}"
        )

    producer = (producing_executable if producing_executable.is_absolute() else repo / producing_executable).resolve()
    if not producer.is_file():
        raise GuardFailure(f"producing executable is missing: {producer}")
    producer_data = producer.read_bytes()
    retained = [record for record in new_executables if record["configuration"] == configuration]
    if len(retained) != 1:
        raise GuardFailure(
            f"new_behavior must retain exactly one {configuration} executable; found={len(retained)}"
        )
    retained_record = retained[0]
    if retained_record["data"] != producer_data:
        raise GuardFailure(
            f"retained {configuration} executable does not byte-match producing executable {producer}"
        )
    for dll in retained_record["required_dlls"]:
        source_dll = producer.parent / PurePosixPath(str(dll["path"])).name
        if not source_dll.is_file():
            raise GuardFailure(f"required producing DLL is missing beside executable: {source_dll}")
        source_data = source_dll.read_bytes()
        if len(source_data) != dll["size"] or sha256_bytes(source_data) != dll["sha256"]:
            raise GuardFailure(
                f"retained DLL does not byte-match producing runtime dependency: {source_dll.name}"
            )
    return relative_path, sha256_bytes(data)


def verify_existing_transition_bundles_immutable(repo: Path, staged: bool) -> None:
    arguments = ["diff"]
    if staged:
        arguments.append("--cached")
    arguments.extend(["--name-only", "--diff-filter=ACDMRTUXB", "HEAD", "--", ARTIFACT_ROOT])
    changed = run_git(repo, *arguments).stdout.decode(errors="replace").splitlines()
    for relative_path in changed:
        if f"/{TRANSITION_DIRECTORY}/" in relative_path and head_bytes(repo, relative_path) is not None:
            raise GuardFailure(f"retained transition bundle is immutable and changed: {relative_path}")


def verify_added_transition_manifests(repo: Path) -> set[str]:
    added = run_git(repo, "diff", "--cached", "--name-only", "--diff-filter=A").stdout
    manifests = [
        path
        for path in added.decode(errors="replace").splitlines()
        if f"/{TRANSITION_DIRECTORY}/" in path and path.endswith("/manifest.json")
    ]
    covered_goldens: set[str] = set()
    for relative_path in manifests:
        bundle_root, _ = bundle_root_from_manifest(relative_path)
        assert_transition_bundle_is_new(repo, bundle_root)
        data = index_bytes(repo, relative_path)
        manifest, _, _ = parse_transition_manifest(repo, relative_path, data, staged=True)
        changed_rows = 0
        for golden in manifest["goldens"]:
            path = str(golden["path"])
            previous = head_bytes(repo, path)
            if previous is None:
                raise GuardFailure(f"Physics transition golden has no tracked predecessor: {path}")
            current = index_bytes(repo, path)
            if sha256_bytes(previous) != golden["old_sha256"]:
                raise GuardFailure(f"staged transition old SHA-256 does not match HEAD for {path}")
            if sha256_bytes(current) != golden["new_sha256"]:
                raise GuardFailure(f"staged transition new SHA-256 does not match the Git index for {path}")
            if previous == current:
                raise GuardFailure(f"staged transition manifest names an unchanged golden: {path}")
            changed_rows += 1
            covered_goldens.add(path)
        if changed_rows == 0:
            raise GuardFailure(f"staged transition manifest has no changed goldens: {relative_path}")
    return covered_goldens


def verify_physics_goldens_have_transition_manifests(repo: Path, covered_goldens: set[str]) -> None:
    for path in sorted(ALWAYS_ARCHIVED_PHYSICS_GOLDENS):
        previous = head_bytes(repo, path)
        if previous is None:
            continue
        current = index_bytes(repo, path)
        if current != previous and path not in covered_goldens:
            raise GuardFailure(
                f"staged Physics golden change requires a new append-only transition manifest: {path}"
            )


def receipt_path(repo: Path) -> Path:
    common_dir = run_git(repo, "rev-parse", "--git-common-dir").stdout.decode().strip()
    common_path = Path(common_dir)
    if not common_path.is_absolute():
        common_path = repo / common_path
    return common_path.resolve() / RECEIPT_RELATIVE_PATH


def load_receipt(repo: Path) -> dict[str, object] | None:
    path = receipt_path(repo)
    if not path.is_file():
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def verify_receipt(
    repo: Path,
    baseline_digest: str,
    record_data: bytes,
    previous_baseline_digest: str,
) -> None:
    receipt = load_receipt(repo)
    if receipt is None:
        raise GuardFailure(
            "physics baseline transition receipt is missing; run the automated override with "
            "--candidate-sha256 and --artifact-manifest"
        )
    required = {
        "schema_version",
        "baseline_sha256",
        "acceptance_record_sha256",
        "artifact_manifest_path",
        "artifact_manifest_sha256",
        "accepted_at_utc",
    }
    receipt = require_exact_keys(receipt, required, "physics transition receipt")
    if receipt["schema_version"] != 2:
        raise GuardFailure("physics transition receipt has unsupported schema_version")
    manifest_path = normalize_relative_path(
        receipt["artifact_manifest_path"], "physics transition receipt artifact_manifest_path"
    )
    manifest_data = index_bytes(repo, manifest_path)
    expected = {
        "baseline_sha256": baseline_digest,
        "acceptance_record_sha256": sha256_bytes(record_data),
        "artifact_manifest_sha256": sha256_bytes(manifest_data),
    }
    for key, value in expected.items():
        if receipt.get(key) != value:
            raise GuardFailure(
                f"physics transition receipt {key} does not match the exact staged transition"
            )
    parse_transition_manifest(repo, manifest_path, manifest_data, staged=True)
    manifest = json.loads(manifest_data.decode("utf-8"))
    matches = [row for row in manifest["goldens"] if row["path"] == BASELINE_PATH]
    if (
        len(matches) != 1
        or matches[0]["old_sha256"] != previous_baseline_digest
        or matches[0]["new_sha256"] != baseline_digest
    ):
        raise GuardFailure("physics transition receipt manifest does not bind the staged core golden transition")


def check_worktree(repo: Path) -> str:
    verify_existing_transition_bundles_immutable(repo, staged=False)
    record_path = repo / ACCEPTANCE_RECORD
    baseline_path = repo / BASELINE_PATH
    if not record_path.is_file():
        raise GuardFailure(f"missing physics acceptance record {ACCEPTANCE_RECORD}")
    if not baseline_path.is_file():
        raise GuardFailure(f"missing physics golden {BASELINE_PATH}")
    _, digest = verify_pair(record_path.read_bytes(), baseline_path.read_bytes())
    return digest


def check_staged(repo: Path) -> str:
    verify_existing_transition_bundles_immutable(repo, staged=True)
    covered_goldens = verify_added_transition_manifests(repo)
    verify_physics_goldens_have_transition_manifests(repo, covered_goldens)
    record_data = index_bytes(repo, ACCEPTANCE_RECORD)
    baseline_data = index_bytes(repo, BASELINE_PATH)
    record, digest = verify_pair(record_data, baseline_data)

    previous_record = head_bytes(repo, ACCEPTANCE_RECORD)
    previous_baseline = head_bytes(repo, BASELINE_PATH)
    record_changed = previous_record != record_data
    baseline_changed = previous_baseline != baseline_data
    if not record_changed and not baseline_changed:
        return digest

    # Why: this is the sole transition allowed without an artifact manifest. The
    # exact bytes predate the guard, so no earlier producing executable can be
    # reconstructed honestly. Every later transition has a tracked predecessor.
    bootstrap = (
        previous_record is None
        and not baseline_changed
        and digest == BOOTSTRAP_ACCEPTED_SHA256
        and record["source_commit"] == BOOTSTRAP_SOURCE_COMMIT
    )
    if not bootstrap:
        if previous_baseline is None:
            raise GuardFailure("core Physics golden transition has no tracked predecessor")
        verify_receipt(repo, digest, record_data, sha256_bytes(previous_baseline))
    return digest


def canonical_complete_run(data: bytes, artifact_name: str) -> bytes:
    # Invariant: an acceptance covers one complete deterministic playback. A
    # diagnostic process may append repetitions only when every run is identical.
    first_newline = data.find(b"\n")
    if first_newline < 0:
        raise GuardFailure(f"{artifact_name} has no complete CSV header")
    header = data[: first_newline + 1]
    starts = [0]
    next_start = data.find(header, len(header))
    while next_start >= 0:
        starts.append(next_start)
        next_start = data.find(header, next_start + len(header))
    runs = [data[start:end] for start, end in zip(starts, starts[1:] + [len(data)])]
    if any(run != runs[0] for run in runs[1:]):
        raise GuardFailure(f"{artifact_name} contains {len(runs)} non-identical complete runs")
    return runs[0]


def write_json_atomic(path: Path, value: dict[str, object]) -> bytes:
    data = (json.dumps(value, indent=2) + "\n").encode("utf-8")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)
    return data


def print_bounded_change_summary(previous: bytes, proposed: bytes) -> None:
    previous_lines = previous.splitlines()
    proposed_lines = proposed.splitlines()
    paired_differences = [
        (line_number, old, new)
        for line_number, (old, new) in enumerate(zip(previous_lines, proposed_lines), start=1)
        if old != new
    ]
    differing_lines = len(paired_differences) + abs(len(previous_lines) - len(proposed_lines))
    print(f"Changed text rows:         {differing_lines}")
    print(f"Accepted complete rows:    {len(previous_lines)}")
    print(f"Proposed complete rows:    {len(proposed_lines)}")
    for line_number, old, new in paired_differences[:5]:
        print(f"  first differences, row {line_number}:")
        print(f"    accepted: {old.decode(errors='replace')}")
        print(f"    proposed: {new.decode(errors='replace')}")


def automated_override_output(
    repo: Path,
    output: Path,
    candidate_sha256: str,
    artifact_manifest: Path,
    emit_summary: bool = True,
) -> str:
    output_path = (output if output.is_absolute() else repo / output).resolve()
    required_output = (repo / "Debug" / "physics_regression_varied.csv").resolve()
    if output_path != required_output:
        raise GuardFailure(f"automated override accepts only the final Debug artifact: {required_output}")
    if not output_path.is_file():
        raise GuardFailure(f"override output does not exist: {output_path}")
    executable = repo / "Debug" / "SKULLBONEZ_CORE.exe"
    if not executable.is_file() or output_path.stat().st_mtime_ns < executable.stat().st_mtime_ns:
        raise GuardFailure("generated CSV predates the final Debug executable; rerun physics validation first")

    proposed = canonical_complete_run(output_path.read_bytes(), output_path.name)
    proposed_digest = sha256_bytes(proposed)
    candidate_digest = require_sha256(candidate_sha256.lower(), "candidate SHA-256")
    if candidate_digest != proposed_digest:
        raise GuardFailure("candidate SHA-256 does not match the generated complete Physics baseline")
    baseline_path = repo / BASELINE_PATH
    if not baseline_path.is_file():
        raise GuardFailure("automated override requires the tracked core Physics golden")
    previous = canonical_complete_run(baseline_path.read_bytes(), baseline_path.name)
    tracked_previous = head_bytes(repo, BASELINE_PATH)
    if tracked_previous is None or baseline_path.read_bytes() != tracked_previous:
        raise GuardFailure("restore the accepted core golden before starting an automated transition")
    previous_digest = sha256_bytes(previous) if previous else "missing"
    if proposed == previous:
        raise GuardFailure("generated CSV already matches the accepted golden; no transition is needed")
    if previous_digest == "missing":
        raise GuardFailure("automated override requires a tracked predecessor golden")

    manifest_path, manifest_digest = validate_physics_plan_transition(
        repo,
        artifact_manifest,
        BASELINE_PATH,
        previous_digest,
        proposed_digest,
        executable,
        "Debug|x64",
    )
    if emit_summary:
        print(f"Current accepted SHA-256: {previous_digest}")
        print(f"Candidate golden SHA-256: {proposed_digest}")
        print(f"Artifact manifest SHA-256: {manifest_digest}")
        print_bounded_change_summary(previous, proposed)

    baseline_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = baseline_path.with_suffix(baseline_path.suffix + ".tmp")
    temporary.write_bytes(proposed)
    os.replace(temporary, baseline_path)
    now = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    head = run_git(repo, "rev-parse", "HEAD").stdout.decode().strip()
    record = {
        "schema_version": 1,
        "baseline_path": BASELINE_PATH,
        "sha256": proposed_digest,
        "approved_by": "automated physics-plan override",
        "approved_at_utc": now,
        "source_commit": head,
        "reason": "Standing Physics-plan policy accepted the exact candidate with an immutable retained-runtime bundle.",
    }
    record_data = write_json_atomic(repo / ACCEPTANCE_RECORD, record)
    receipt = {
        "schema_version": 2,
        "baseline_sha256": proposed_digest,
        "acceptance_record_sha256": sha256_bytes(record_data),
        "artifact_manifest_path": manifest_path,
        "artifact_manifest_sha256": manifest_digest,
        "accepted_at_utc": now,
    }
    write_json_atomic(receipt_path(repo), receipt)
    if emit_summary:
        print(
            "ACCEPTED: stage the golden, acceptance record, and complete transition bundle, "
            "then rerun the gate."
        )
    return proposed_digest


def automated_override_file(
    repo: Path,
    candidate: Path,
    golden_path: str,
    candidate_sha256: str,
    artifact_manifest: Path,
    producing_executable: Path,
    configuration: str,
    emit_summary: bool = True,
) -> str:
    """Replace a non-core Physics-plan golden when no domain writer owns serialization."""

    normalized_golden = normalize_relative_path(golden_path, "golden path")
    if normalized_golden == BASELINE_PATH:
        raise GuardFailure("use --automated-override-output for the core Physics CSV")
    target = (repo / normalized_golden).resolve()
    try:
        target.relative_to(repo.resolve())
    except ValueError as exc:
        raise GuardFailure(f"golden path escapes repository: {normalized_golden}") from exc
    if not target.is_file():
        raise GuardFailure(f"automated override requires a tracked predecessor golden: {normalized_golden}")
    tracked_previous = head_bytes(repo, normalized_golden)
    if tracked_previous is None or target.read_bytes() != tracked_previous:
        raise GuardFailure(f"restore the tracked golden before starting a transition: {normalized_golden}")
    candidate_path = (candidate if candidate.is_absolute() else repo / candidate).resolve()
    if not candidate_path.is_file() or candidate_path == target:
        raise GuardFailure("automated override candidate must be a separate existing file")
    candidate_data = candidate_path.read_bytes()
    candidate_digest = sha256_bytes(candidate_data)
    supplied_digest = require_sha256(candidate_sha256.lower(), "candidate SHA-256")
    if supplied_digest != candidate_digest:
        raise GuardFailure(
            f"candidate SHA-256 does not match {candidate_path}: "
            f"expected={candidate_digest} supplied={supplied_digest}"
        )
    old_digest = sha256_bytes(tracked_previous)
    if candidate_digest == old_digest:
        raise GuardFailure(f"candidate already matches {normalized_golden}; no transition is needed")
    producer = (
        producing_executable
        if producing_executable.is_absolute()
        else repo / producing_executable
    ).resolve()
    if not producer.is_file():
        raise GuardFailure(f"producing executable is missing: {producer}")
    if candidate_path.stat().st_mtime_ns < producer.stat().st_mtime_ns:
        raise GuardFailure("candidate predates the producing executable; regenerate it first")
    manifest_path, manifest_digest = validate_physics_plan_transition(
        repo,
        artifact_manifest,
        normalized_golden,
        old_digest,
        candidate_digest,
        producer,
        configuration,
    )
    temporary = target.with_suffix(target.suffix + ".tmp")
    temporary.write_bytes(candidate_data)
    os.replace(temporary, target)
    if emit_summary:
        print(f"Current golden SHA-256:     {old_digest}")
        print(f"Candidate golden SHA-256:   {candidate_digest}")
        print(f"Artifact manifest SHA-256: {manifest_digest}")
        print(
            f"ACCEPTED: {normalized_golden}; stage it with {manifest_path} and its complete bundle."
        )
    return candidate_digest


def configure_test_identity(repo: Path) -> None:
    run_git(repo, "config", "user.email", "physics-guard@example.invalid")
    run_git(repo, "config", "user.name", "Physics Guard Self Test")


def file_record(path: str, data: bytes) -> dict[str, object]:
    return {"path": path, "size": len(data), "sha256": sha256_bytes(data)}


def self_test_manifest(
    repo: Path,
    old_digest: str,
    new_digest: str,
    source_commit: str,
    golden_path: str = BASELINE_PATH,
    phase: str = "ST0",
) -> tuple[Path, dict[str, bytes]]:
    root = (
        f"Agentic/Plans/Artifacts/self-test-physics/{phase}/golden-transitions/"
        f"{old_digest[:12]}-to-{new_digest[:12]}"
    )
    payloads = {
        f"{root}/old/SKULLBONEZ_CORE-Debug.exe": b"old self-test executable",
        f"{root}/old/runtime.dll": b"old self-test dll",
        f"{root}/new/SKULLBONEZ_CORE-Debug.exe": b"new self-test executable",
        f"{root}/new/runtime.dll": b"new self-test dll",
    }
    for path, data in payloads.items():
        target = repo / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    old_exe = f"{root}/old/SKULLBONEZ_CORE-Debug.exe"
    new_exe = f"{root}/new/SKULLBONEZ_CORE-Debug.exe"
    manifest = {
        "schema_version": 1,
        "physics_plan": "Agentic/Plans/TODO/self-test-physics.md",
        "phase": phase,
        "transition_id": f"{old_digest[:12]}-to-{new_digest[:12]}",
        "source_commit": source_commit,
        "goldens": [
            {"path": golden_path, "old_sha256": old_digest, "new_sha256": new_digest}
        ],
        "old_behavior": {
            "source_commit": source_commit,
            "golden_sha256": {golden_path: old_digest},
            "executables": [
                {
                    "configuration": "Debug|x64",
                    **file_record(old_exe, payloads[old_exe]),
                    "launch_command": "old/SKULLBONEZ_CORE-Debug.exe --physics-standalone-smoke",
                    "dependency_scan_command": "dumpbin /DEPENDENTS old/SKULLBONEZ_CORE-Debug.exe",
                    "required_dlls": [file_record(f"{root}/old/runtime.dll", payloads[f"{root}/old/runtime.dll"])],
                }
            ],
        },
        "new_behavior": {
            "source_commit": source_commit,
            "golden_sha256": {golden_path: new_digest},
            "executables": [
                {
                    "configuration": "Debug|x64",
                    **file_record(new_exe, payloads[new_exe]),
                    "launch_command": "new/SKULLBONEZ_CORE-Debug.exe --physics-standalone-smoke",
                    "dependency_scan_command": "dumpbin /DEPENDENTS new/SKULLBONEZ_CORE-Debug.exe",
                    "required_dlls": [file_record(f"{root}/new/runtime.dll", payloads[f"{root}/new/runtime.dll"])],
                }
            ],
        },
    }
    manifest_path = repo / root / "manifest.json"
    write_json_atomic(manifest_path, manifest)
    return manifest_path, payloads


def expect_failure(action, expected_text: str) -> None:
    try:
        action()
    except GuardFailure as exc:
        if expected_text not in str(exc):
            raise
    else:
        raise GuardFailure(f"self-test accepted invalid state; expected failure containing {expected_text!r}")


def self_test(source_repo: Path) -> None:
    source_baseline = run_git(
        source_repo, "show", f"{BOOTSTRAP_SOURCE_COMMIT}:{BASELINE_PATH}"
    ).stdout
    if sha256_bytes(source_baseline) != BOOTSTRAP_ACCEPTED_SHA256:
        raise GuardFailure("self-test source golden no longer matches the pinned bootstrap digest")

    with tempfile.TemporaryDirectory(prefix="physics-baseline-guard-") as temp:
        repo = Path(temp)
        run_git(repo, "init")
        configure_test_identity(repo)
        (repo / BASELINE_PATH).parent.mkdir(parents=True)
        (repo / BASELINE_PATH).write_bytes(source_baseline)
        run_git(repo, "add", BASELINE_PATH)
        run_git(repo, "commit", "-m", "seed golden")

        bootstrap_record = parse_record((source_repo / ACCEPTANCE_RECORD).read_bytes())
        bootstrap_record["sha256"] = BOOTSTRAP_ACCEPTED_SHA256
        bootstrap_record["source_commit"] = BOOTSTRAP_SOURCE_COMMIT
        record_data = (json.dumps(bootstrap_record, indent=2) + "\n").encode("utf-8")
        (repo / ACCEPTANCE_RECORD).parent.mkdir(parents=True, exist_ok=True)
        (repo / ACCEPTANCE_RECORD).write_bytes(record_data)
        run_git(repo, "add", ACCEPTANCE_RECORD)
        check_staged(repo)
        run_git(repo, "commit", "-m", "bootstrap acceptance")

        plan = repo / "Agentic/Plans/TODO/self-test-physics.md"
        plan.parent.mkdir(parents=True, exist_ok=True)
        plan.write_text("# Self-test Physics plan\n", encoding="utf-8")
        deep_golden_path = "TestOutput/baselines/bullet_sweep_wall.csv"
        deep_golden = repo / deep_golden_path
        deep_golden.write_bytes(b"old deep Physics golden\n")
        run_git(repo, "add", plan.relative_to(repo).as_posix(), deep_golden_path)
        run_git(repo, "commit", "-m", "add self-test plan")
        source_commit = run_git(repo, "rev-parse", "HEAD").stdout.decode().strip()

        changed = (source_repo / BASELINE_PATH).read_bytes()
        if changed == source_baseline:
            changed += b"# deliberate self-test change\n"
        changed_digest = sha256_bytes(changed)
        output_path = repo / "Debug" / "physics_regression_varied.csv"
        executable_path = repo / "Debug" / "SKULLBONEZ_CORE.exe"
        runtime_dll_path = repo / "Debug" / "runtime.dll"
        output_path.parent.mkdir(parents=True)
        executable_path.write_bytes(b"new self-test executable")
        runtime_dll_path.write_bytes(b"new self-test dll")
        output_path.write_bytes(changed)
        output_timestamp = executable_path.stat().st_mtime_ns + 1_000_000_000
        os.utime(output_path, ns=(output_timestamp, output_timestamp))

        expect_failure(
            lambda: automated_override_output(
                repo,
                output_path,
                changed_digest,
                Path(
                    "Agentic/Plans/Artifacts/self-test-physics/ST0/golden-transitions/"
                    "missing-transition/manifest.json"
                ),
            ),
            "retained artifact is missing",
        )
        manifest_path, payloads = self_test_manifest(
            repo, BOOTSTRAP_ACCEPTED_SHA256, changed_digest, source_commit
        )
        expect_failure(
            lambda: automated_override_output(repo, output_path, "0" * 64, manifest_path),
            "candidate SHA-256 does not match",
        )

        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["goldens"][0]["new_sha256"] = "1" * 64
        write_json_atomic(manifest_path, manifest)
        expect_failure(
            lambda: automated_override_output(repo, output_path, changed_digest, manifest_path),
            "golden_sha256 must exactly match",
        )
        manifest_path, payloads = self_test_manifest(
            repo, BOOTSTRAP_ACCEPTED_SHA256, changed_digest, source_commit
        )
        new_executable = next(path for path in payloads if path.endswith("new/SKULLBONEZ_CORE-Debug.exe"))
        (repo / new_executable).unlink()
        expect_failure(
            lambda: automated_override_output(repo, output_path, changed_digest, manifest_path),
            "retained artifact is missing",
        )
        manifest_path, payloads = self_test_manifest(
            repo, BOOTSTRAP_ACCEPTED_SHA256, changed_digest, source_commit
        )
        new_executable = next(path for path in payloads if path.endswith("new/SKULLBONEZ_CORE-Debug.exe"))
        (repo / new_executable).write_bytes(b"NEW self-test executable")
        expect_failure(
            lambda: automated_override_output(repo, output_path, changed_digest, manifest_path),
            "SHA-256 mismatch",
        )
        manifest_path, payloads = self_test_manifest(
            repo, BOOTSTRAP_ACCEPTED_SHA256, changed_digest, source_commit
        )
        new_dll = next(path for path in payloads if path.endswith("new/runtime.dll"))
        (repo / new_dll).write_bytes(b"NEW self-test dll")
        expect_failure(
            lambda: automated_override_output(repo, output_path, changed_digest, manifest_path),
            "SHA-256 mismatch",
        )
        manifest_path, payloads = self_test_manifest(
            repo, BOOTSTRAP_ACCEPTED_SHA256, changed_digest, source_commit
        )

        changed_record = parse_record(record_data)
        changed_record["sha256"] = changed_digest
        changed_record["approved_at_utc"] = "2099-01-01T00:00:00Z"
        write_json_atomic(repo / ACCEPTANCE_RECORD, changed_record)
        (repo / BASELINE_PATH).write_bytes(changed)
        run_git(repo, "add", BASELINE_PATH, ACCEPTANCE_RECORD)
        expect_failure(lambda: check_staged(repo), "requires a new append-only transition manifest")
        run_git(repo, "restore", "--staged", BASELINE_PATH, ACCEPTANCE_RECORD)
        (repo / BASELINE_PATH).write_bytes(source_baseline)
        (repo / ACCEPTANCE_RECORD).write_bytes(record_data)

        automated_override_output(
            repo, output_path, changed_digest, manifest_path, emit_summary=False
        )
        bundle_root, _ = bundle_root_from_manifest(manifest_relative_path(repo, manifest_path))
        run_git(repo, "add", BASELINE_PATH, ACCEPTANCE_RECORD, bundle_root)
        check_staged(repo)
        run_git(repo, "commit", "-m", "accept archived automated transition")

        deep_candidate = repo / "Debug" / "bullet_sweep_wall.csv"
        deep_candidate.write_bytes(b"new deep Physics golden\n")
        deep_timestamp = executable_path.stat().st_mtime_ns + 2_000_000_000
        os.utime(deep_candidate, ns=(deep_timestamp, deep_timestamp))
        deep_old_digest = sha256_bytes(deep_golden.read_bytes())
        deep_new_digest = sha256_bytes(deep_candidate.read_bytes())
        deep_source_commit = run_git(repo, "rev-parse", "HEAD").stdout.decode().strip()
        deep_manifest, _ = self_test_manifest(
            repo,
            deep_old_digest,
            deep_new_digest,
            deep_source_commit,
            deep_golden_path,
            "ST1",
        )
        automated_override_file(
            repo,
            deep_candidate,
            deep_golden_path,
            deep_new_digest,
            deep_manifest,
            executable_path,
            "Debug|x64",
            emit_summary=False,
        )
        deep_bundle_root, _ = bundle_root_from_manifest(
            manifest_relative_path(repo, deep_manifest)
        )
        run_git(repo, "add", deep_golden_path, deep_bundle_root)
        check_staged(repo)
        run_git(repo, "commit", "-m", "accept generic archived transition")

        retained_new_dll = next(path for path in payloads if path.endswith("new/runtime.dll"))
        (repo / retained_new_dll).write_bytes(b"later overwrite")
        run_git(repo, "add", retained_new_dll)
        expect_failure(lambda: check_staged(repo), "immutable and changed")
        (repo / retained_new_dll).write_bytes(payloads[retained_new_dll])

        (repo / BASELINE_PATH).write_bytes(changed + b"tamper\n")
        expect_failure(lambda: check_worktree(repo), "does not match accepted")

    print("PASS: physics baseline guard self-tests")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parent.parent)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--staged", action="store_true", help="check exact Git-index content for commit")
    action.add_argument(
        "--automated-override-output",
        "--approve-output",
        dest="override_output",
        type=Path,
        help="replace the core golden through the archived automated Physics-plan lane",
    )
    action.add_argument(
        "--automated-override-file",
        type=Path,
        help="candidate file for a non-core Physics-plan golden transition",
    )
    action.add_argument("--self-test", action="store_true")
    parser.add_argument(
        "--candidate-sha256",
        "--owner-approved-sha256",
        dest="candidate_sha256",
        help="exact SHA-256 of the generated complete-run candidate",
    )
    parser.add_argument(
        "--artifact-manifest",
        type=Path,
        help="append-only Physics-plan golden-transition manifest.json",
    )
    parser.add_argument("--golden-path", help="repository-relative non-core golden path")
    parser.add_argument("--producing-executable", type=Path)
    parser.add_argument("--configuration", help="manifest configuration, for example Debug|x64")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    override_requested = args.override_output is not None or args.automated_override_file is not None
    if override_requested and (args.candidate_sha256 is None or args.artifact_manifest is None):
        parser.error(
            "automated override requires --candidate-sha256 and --artifact-manifest"
        )
    if not override_requested and (args.candidate_sha256 is not None or args.artifact_manifest is not None):
        parser.error("candidate and artifact-manifest arguments require an automated override action")
    generic_arguments = (args.golden_path, args.producing_executable, args.configuration)
    if args.automated_override_file is not None and any(value is None for value in generic_arguments):
        parser.error(
            "--automated-override-file requires --golden-path, --producing-executable, and --configuration"
        )
    if args.automated_override_file is None and any(value is not None for value in generic_arguments):
        parser.error("non-core golden arguments require --automated-override-file")
    repo = args.repo.resolve()
    try:
        if args.self_test:
            self_test(repo)
        elif args.override_output is not None:
            automated_override_output(
                repo,
                args.override_output,
                args.candidate_sha256,
                args.artifact_manifest,
            )
        elif args.automated_override_file is not None:
            automated_override_file(
                repo,
                args.automated_override_file,
                args.golden_path,
                args.candidate_sha256,
                args.artifact_manifest,
                args.producing_executable,
                args.configuration,
            )
        elif args.staged:
            digest = check_staged(repo)
            print(f"PASS: staged Physics golden and retained transitions are exact ({digest})")
        else:
            digest = check_worktree(repo)
            print(f"PASS: Physics golden matches accepted SHA-256 ({digest})")
    except GuardFailure as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
