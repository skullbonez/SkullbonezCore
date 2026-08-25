#!/usr/bin/env python3
#
# File: tools/update_baselines.py
# Purpose:
#   Documents and runs the update_baselines.py developer/validation helper script.
#
# Summary:
#   Updates visual and performance baselines, and performs a complete
#   content-bound Physics golden transition from one command. The Physics path
#   retains old/new first-party producers, writes the transition manifest,
#   updates acceptance metadata, stages the complete set, and checks the index.
#
# Glossary:
#   JSON (JavaScript Object Notation): Structured text format used by
#   diagnostics, baselines, and tool reports.
#   Validation gate: Repository script that proves a class of changes before
#   commit or PR.
#
# Invariants:
#   - Tool output should be bounded and readable because agents and humans use
#   it for decisions.
#   - A Physics update never accepts a candidate without both retained
#   producers and a manifest binding their bytes to the old/new golden hashes.
#
# Related:
#   - AGENTS.md
#
#
"""Update committed baselines from current generated artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import check_physics_baseline_guard as physics_guard


VISUALS = [
    ("dx12_screenshot.bmp", "baseline_dx12_water_ball_test.png"),
    ("dx12_solver_smoke.bmp", "baseline_dx12_solver_smoke.png"),
    ("dx12_space_three_body.bmp", "baseline_dx12_space_three_body.png"),
]

PERF = [
    ("dx12_perf.json", "dx12_perf.json"),
    ("physics_bench_perf.json", "physics_bench_perf.json"),
]

PHYSICS_OUTPUT = Path("Debug/physics_regression_varied.csv")
PHYSICS_EXECUTABLE = Path("Debug/SKULLBONEZ_CORE.exe")
PHYSICS_CONFIGURATION = "Debug|x64"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_git(repo: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise physics_guard.GuardFailure(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def write_bytes_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)


def require_clean_transition_inputs(repo: Path) -> None:
    staged = run_git(repo, "diff", "--cached", "--name-only")
    if staged:
        raise physics_guard.GuardFailure(
            "Physics update requires an empty Git index so its complete staged transition is unambiguous"
        )
    protected = run_git(
        repo,
        "status",
        "--porcelain=v1",
        "--",
        physics_guard.BASELINE_PATH,
        physics_guard.ACCEPTANCE_RECORD,
    )
    if protected:
        raise physics_guard.GuardFailure(
            "restore the Physics golden and acceptance record before starting a transition"
        )


def active_plan_and_phase(repo: Path) -> tuple[str, str]:
    plans = sorted((repo / "Agentic/Plans/TODO").glob("*.md"))
    if len(plans) != 1:
        raise physics_guard.GuardFailure(
            f"Physics update requires exactly one active TODO plan; found={len(plans)}"
        )
    text = plans[0].read_text(encoding="utf-8")
    status = re.search(r"^Status:.*?\b([A-Z]+\d+)\s+next\b", text, re.MULTILINE)
    if status is None:
        raise physics_guard.GuardFailure("active plan Status must name the next phase")
    return plans[0].relative_to(repo).as_posix(), status.group(1)


def predecessor_manifest(repo: Path, accepted_digest: str) -> tuple[Path, dict[str, object]]:
    matches: list[tuple[Path, dict[str, object]]] = []
    tracked = run_git(repo, "ls-files", "--", physics_guard.ARTIFACT_ROOT).splitlines()
    manifests = [
        path
        for path in tracked
        if f"/{physics_guard.TRANSITION_DIRECTORY}/" in path and path.endswith("/manifest.json")
    ]
    for relative_path in manifests:
        try:
            data = physics_guard.index_bytes(repo, relative_path)
            manifest = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError, physics_guard.GuardFailure):
            continue
        goldens = manifest.get("goldens", []) if isinstance(manifest, dict) else []
        if any(
            isinstance(row, dict)
            and row.get("path") == physics_guard.BASELINE_PATH
            and row.get("new_sha256") == accepted_digest
            for row in goldens
        ):
            bundle_root, _ = physics_guard.bundle_root_from_manifest(relative_path)
            physics_guard.parse_behavior(
                repo,
                manifest.get("new_behavior"),
                "predecessor new_behavior",
                bundle_root,
                {physics_guard.BASELINE_PATH: accepted_digest},
                staged=True,
            )
            matches.append((repo / relative_path, manifest))
    if len(matches) != 1:
        raise physics_guard.GuardFailure(
            f"accepted Physics golden must have exactly one retained predecessor producer; found={len(matches)}"
        )
    return matches[0]


def copied_file_record(repo: Path, source: Path, destination: Path) -> dict[str, object]:
    if not source.is_file():
        raise physics_guard.GuardFailure(f"retained predecessor producer is missing: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    return {
        "path": destination.relative_to(repo).as_posix(),
        "size": destination.stat().st_size,
        "sha256": sha256_file(destination),
    }


def copied_index_file_record(
    repo: Path, raw: dict[str, object], destination: Path
) -> dict[str, object]:
    relative_path = physics_guard.normalize_relative_path(raw.get("path"), "predecessor artifact path")
    data = physics_guard.index_bytes(repo, relative_path)
    expected_size = raw.get("size")
    expected_digest = raw.get("sha256")
    if len(data) != expected_size or physics_guard.sha256_bytes(data) != expected_digest:
        raise physics_guard.GuardFailure(
            f"committed predecessor artifact no longer matches its manifest: {relative_path}"
        )
    destination.parent.mkdir(parents=True, exist_ok=True)
    write_bytes_atomic(destination, data)
    return {
        "path": destination.relative_to(repo).as_posix(),
        "size": len(data),
        "sha256": expected_digest,
    }


def command_with_retained_executable(command: object, side: str, name: str) -> str:
    if not isinstance(command, str) or not command.strip():
        raise physics_guard.GuardFailure("predecessor launch command is missing")
    _, separator, arguments = command.strip().partition(" ")
    return f"{side}/{name}{separator}{arguments}" if separator else f"{side}/{name}"


def archive_previous_behavior(
    repo: Path, predecessor: dict[str, object], bundle_root: Path, accepted_digest: str
) -> dict[str, object]:
    behavior = predecessor.get("new_behavior")
    if not isinstance(behavior, dict):
        raise physics_guard.GuardFailure("predecessor manifest has no new_behavior record")
    executables = behavior.get("executables")
    if not isinstance(executables, list) or not executables:
        raise physics_guard.GuardFailure("predecessor manifest has no retained executable")
    archived = []
    for raw in executables:
        if not isinstance(raw, dict):
            raise physics_guard.GuardFailure("predecessor executable record is invalid")
        source_path = Path(str(raw.get("path", "")))
        name = source_path.name
        record = copied_index_file_record(repo, raw, bundle_root / "old" / name)
        dll_records = []
        for raw_dll in raw.get("first_party_dlls", []):
            if not isinstance(raw_dll, dict):
                raise physics_guard.GuardFailure("predecessor DLL record is invalid")
            dll_name = Path(str(raw_dll.get("path", ""))).name
            dll_records.append(
                copied_index_file_record(repo, raw_dll, bundle_root / "old" / dll_name)
            )
        archived.append(
            {
                "configuration": raw.get("configuration"),
                **record,
                "launch_command": command_with_retained_executable(raw.get("launch_command"), "old", name),
                "dependency_scan_command": f"dumpbin /DEPENDENTS old/{name}",
                "first_party_dlls": dll_records,
            }
        )
    return {
        "source_commit": behavior.get("source_commit"),
        "golden_sha256": {physics_guard.BASELINE_PATH: accepted_digest},
        "executables": archived,
    }


def archive_current_behavior(
    repo: Path,
    predecessor: dict[str, object],
    bundle_root: Path,
    candidate_digest: str,
    source_commit: str,
) -> dict[str, object]:
    executable = repo / PHYSICS_EXECUTABLE
    if not executable.is_file():
        raise physics_guard.GuardFailure(f"current Physics producer is missing: {PHYSICS_EXECUTABLE.as_posix()}")
    retained_name = "SKULLBONEZ_CORE-Debug.exe"
    record = copied_file_record(repo, executable, bundle_root / "new" / retained_name)
    dll_records = [
        copied_file_record(repo, dll, bundle_root / "new" / dll.name)
        for dll in sorted(executable.parent.glob("SKULLBONEZ_*.dll"))
    ]
    previous_behavior = predecessor.get("new_behavior")
    if not isinstance(previous_behavior, dict):
        raise physics_guard.GuardFailure("predecessor manifest has no new_behavior record")
    previous_records = previous_behavior.get("executables")
    if not isinstance(previous_records, list):
        raise physics_guard.GuardFailure("predecessor manifest has no retained executable list")
    debug_records = [row for row in previous_records if row.get("configuration") == PHYSICS_CONFIGURATION]
    if len(debug_records) != 1:
        raise physics_guard.GuardFailure("predecessor manifest must retain exactly one Debug|x64 producer")
    return {
        "source_commit": source_commit,
        "golden_sha256": {physics_guard.BASELINE_PATH: candidate_digest},
        "executables": [
            {
                "configuration": PHYSICS_CONFIGURATION,
                **record,
                "launch_command": command_with_retained_executable(
                    debug_records[0].get("launch_command"), "new", retained_name
                ),
                "dependency_scan_command": f"dumpbin /DEPENDENTS new/{retained_name}",
                "first_party_dlls": dll_records,
            }
        ],
    }


def stage_transition(repo: Path, bundle_relative: str) -> None:
    run_git(
        repo,
        "add",
        "--",
        physics_guard.BASELINE_PATH,
        physics_guard.ACCEPTANCE_RECORD,
    )
    # Why: retained producers are deliberately ignored as ordinary build
    # outputs. Only this content-bound bundle path is force-added.
    run_git(repo, "add", "--force", "--", bundle_relative)


def update_physics(repo: Path) -> int:
    require_clean_transition_inputs(repo)
    output = repo / PHYSICS_OUTPUT
    executable = repo / PHYSICS_EXECUTABLE
    if not output.is_file() or not executable.is_file():
        raise physics_guard.GuardFailure(
            "Physics update requires Debug/physics_regression_varied.csv and Debug/SKULLBONEZ_CORE.exe"
        )
    if output.stat().st_mtime_ns < executable.stat().st_mtime_ns:
        raise physics_guard.GuardFailure("Physics CSV predates the Debug producer; regenerate it first")

    candidate = physics_guard.canonical_complete_run(output.read_bytes(), output.name)
    candidate_digest = physics_guard.sha256_bytes(candidate)
    accepted = physics_guard.canonical_complete_run(
        (repo / physics_guard.BASELINE_PATH).read_bytes(), Path(physics_guard.BASELINE_PATH).name
    )
    accepted_digest = physics_guard.sha256_bytes(accepted)
    if candidate_digest == accepted_digest:
        raise physics_guard.GuardFailure("generated Physics CSV already matches the accepted golden")

    plan, phase = active_plan_and_phase(repo)
    _, predecessor = predecessor_manifest(repo, accepted_digest)
    source_commit = run_git(repo, "rev-parse", "HEAD")
    transition_id = f"{accepted_digest[:8]}-to-{candidate_digest[:8]}"
    plan_slug = Path(plan).stem
    bundle_root = (
        repo
        / physics_guard.ARTIFACT_ROOT
        / plan_slug
        / phase
        / physics_guard.TRANSITION_DIRECTORY
        / transition_id
    )
    if bundle_root.exists():
        raise physics_guard.GuardFailure(f"Physics transition bundle already exists: {bundle_root}")

    baseline_path = repo / physics_guard.BASELINE_PATH
    acceptance_path = repo / physics_guard.ACCEPTANCE_RECORD
    receipt_path = physics_guard.receipt_path(repo)
    baseline_before = baseline_path.read_bytes()
    acceptance_before = acceptance_path.read_bytes()
    receipt_existed = receipt_path.is_file()
    receipt_before = receipt_path.read_bytes() if receipt_existed else b""
    bundle_relative = bundle_root.relative_to(repo).as_posix()

    try:
        old_behavior = archive_previous_behavior(repo, predecessor, bundle_root, accepted_digest)
        new_behavior = archive_current_behavior(
            repo, predecessor, bundle_root, candidate_digest, source_commit
        )
        manifest = {
            "schema_version": 2,
            "physics_plan": plan,
            "phase": phase,
            "transition_id": transition_id,
            "source_commit": source_commit,
            "goldens": [
                {
                    "path": physics_guard.BASELINE_PATH,
                    "old_sha256": accepted_digest,
                    "new_sha256": candidate_digest,
                }
            ],
            "old_behavior": old_behavior,
            "new_behavior": new_behavior,
        }
        manifest_path = bundle_root / "manifest.json"
        physics_guard.write_json_atomic(manifest_path, manifest)
        physics_guard.automated_override_output(
            repo, output, candidate_digest, manifest_path, emit_summary=True
        )
        stage_transition(repo, bundle_relative)
        staged_digest = physics_guard.check_staged(repo)
    except Exception:
        subprocess.run(
            [
                "git",
                "-C",
                str(repo),
                "restore",
                "--staged",
                "--",
                physics_guard.BASELINE_PATH,
                physics_guard.ACCEPTANCE_RECORD,
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        staged_bundle = subprocess.run(
            ["git", "-C", str(repo), "diff", "--cached", "--name-only", "--", bundle_relative],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        if staged_bundle.stdout.strip():
            subprocess.run(
                ["git", "-C", str(repo), "restore", "--staged", "--", bundle_relative],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
        write_bytes_atomic(baseline_path, baseline_before)
        write_bytes_atomic(acceptance_path, acceptance_before)
        if receipt_existed:
            write_bytes_atomic(receipt_path, receipt_before)
        elif receipt_path.exists():
            receipt_path.unlink()
        if bundle_root.exists():
            shutil.rmtree(bundle_root)
        raise

    print(f"STAGED: complete Physics transition {transition_id} ({staged_digest})")
    return 0


def run_self_test() -> int:
    accepted_digest = "a" * 64
    with tempfile.TemporaryDirectory(prefix="skore-update-baselines-") as temporary:
        repo = Path(temporary)
        run_git(repo, "init")
        run_git(repo, "config", "user.email", "baseline-self-test@example.invalid")
        run_git(repo, "config", "user.name", "Baseline Self-Test")
        plan = repo / "Agentic/Plans/TODO/governance.md"
        plan.parent.mkdir(parents=True)
        plan.write_text("Status: Active. 5/8 phases complete; DB5 next.\n", encoding="utf-8")
        if active_plan_and_phase(repo) != ("Agentic/Plans/TODO/governance.md", "DB5"):
            raise RuntimeError("self-test did not discover the sole active plan and phase")

        (repo / ".gitignore").write_text("*.exe\n*.dll\n", encoding="utf-8")
        run_git(repo, "add", ".gitignore", plan.relative_to(repo).as_posix())
        run_git(repo, "commit", "-m", "self-test root")
        source_commit = run_git(repo, "rev-parse", "HEAD")

        bundle_root = (
            repo
            / physics_guard.ARTIFACT_ROOT
            / "prior/FP4/golden-transitions/old-to-accepted"
        )
        producer = bundle_root / "new/SKULLBONEZ_CORE-Debug.exe"
        producer.parent.mkdir(parents=True)
        producer.write_bytes(b"previous producer")
        predecessor = {
            "new_behavior": {
                "source_commit": source_commit,
                "golden_sha256": {physics_guard.BASELINE_PATH: accepted_digest},
                "executables": [
                    {
                        "configuration": PHYSICS_CONFIGURATION,
                        "path": producer.relative_to(repo).as_posix(),
                        "size": producer.stat().st_size,
                        "sha256": sha256_file(producer),
                        "launch_command": "new/SKULLBONEZ_CORE-Debug.exe --workers 0",
                        "dependency_scan_command": "dumpbin /DEPENDENTS new/SKULLBONEZ_CORE-Debug.exe",
                        "first_party_dlls": [],
                    }
                ],
            }
        }
        manifest = bundle_root / "manifest.json"
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(
            json.dumps(
                {
                    "goldens": [
                        {"path": physics_guard.BASELINE_PATH, "new_sha256": accepted_digest}
                    ],
                    **predecessor,
                }
            ),
            encoding="utf-8",
        )
        run_git(repo, "add", "--force", "--", producer.relative_to(repo).as_posix())
        run_git(repo, "add", "--", manifest.relative_to(repo).as_posix())
        run_git(repo, "commit", "-m", "self-test predecessor")

        manifest.write_text('{"goldens": []}\n', encoding="utf-8")
        producer.write_bytes(b"untrusted worktree producer")
        found_path, found = predecessor_manifest(repo, accepted_digest)
        if found_path != manifest or found != predecessor | {
            "goldens": [{"path": physics_guard.BASELINE_PATH, "new_sha256": accepted_digest}]
        }:
            raise RuntimeError("self-test trusted mutable predecessor worktree content")

        bundle = repo / physics_guard.ARTIFACT_ROOT / "current/DB5/golden-transitions/y"
        archived = archive_previous_behavior(repo, found, bundle, accepted_digest)
        record = archived["executables"][0]
        if record["sha256"] != hashlib.sha256(b"previous producer").hexdigest():
            raise RuntimeError("self-test did not copy the committed predecessor producer")
        if record["launch_command"] != "old/SKULLBONEZ_CORE-Debug.exe --workers 0":
            raise RuntimeError("self-test did not rewrite the retained launch command")

        baseline = repo / physics_guard.BASELINE_PATH
        acceptance = repo / physics_guard.ACCEPTANCE_RECORD
        baseline.parent.mkdir(parents=True)
        acceptance.parent.mkdir(parents=True)
        baseline.write_text("candidate\n", encoding="utf-8")
        acceptance.write_text("{}\n", encoding="utf-8")
        staged_bundle = repo / physics_guard.ARTIFACT_ROOT / "stage/DB5/golden-transitions/z"
        staged_executable = staged_bundle / "new/SKULLBONEZ_CORE-Debug.exe"
        staged_executable.parent.mkdir(parents=True)
        staged_executable.write_bytes(b"ignored producer")
        (staged_bundle / "manifest.json").write_text("{}\n", encoding="utf-8")
        stage_transition(repo, staged_bundle.relative_to(repo).as_posix())
        staged_names = run_git(repo, "diff", "--cached", "--name-only").splitlines()
        if staged_executable.relative_to(repo).as_posix() not in staged_names:
            raise RuntimeError("self-test did not force-stage the ignored retained producer")

    print("PASS: baseline update workflow self-tests")
    return 0


def update_visuals(repo: Path, require: bool) -> int:
    try:
        from PIL import Image
    except ModuleNotFoundError as exc:
        raise SystemExit("Pillow is required. Install with: py -m pip install Pillow") from exc

    profile = repo / "Profile"
    baselines = repo / "TestOutput" / "baselines"
    baselines.mkdir(parents=True, exist_ok=True)

    updated = 0
    missing: list[str] = []
    for src_name, dst_name in VISUALS:
        src = profile / src_name
        dst = baselines / dst_name
        if not src.exists():
            missing.append(str(src.relative_to(repo)))
            continue
        Image.open(src).save(dst)
        print(f"updated {dst.relative_to(repo)}")
        updated += 1

    if require and missing:
        for item in missing:
            print(f"missing {item}")
        return 1
    if updated == 0:
        print("no visual artifacts found")
    return 0


def update_perf(repo: Path, require: bool) -> int:
    profile = repo / "Profile"
    baselines = repo / "TestOutput" / "baselines"
    baselines.mkdir(parents=True, exist_ok=True)

    updated = 0
    missing: list[str] = []
    for src_name, dst_name in PERF:
        src = profile / src_name
        dst = baselines / dst_name
        if not src.exists():
            missing.append(str(src.relative_to(repo)))
            continue
        shutil.copy2(src, dst)
        print(f"updated {dst.relative_to(repo)}")
        updated += 1

    if require and missing:
        for item in missing:
            print(f"missing {item}")
        return 1
    if updated == 0:
        print("no perf artifacts found")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--visuals", action="store_true", help="update screenshot PNG baselines")
    parser.add_argument("--perf", action="store_true", help="update perf JSON baselines")
    parser.add_argument(
        "--physics",
        action="store_true",
        help="archive producers, update the core Physics golden, write its manifest, and stage the complete transition",
    )
    parser.add_argument("--self-test", action="store_true", help="run isolated baseline workflow checks")
    parser.add_argument("--require", action="store_true", help="fail if any selected artifact is missing")
    args = parser.parse_args()

    if args.self_test:
        if args.visuals or args.perf or args.physics:
            parser.error("--self-test cannot be combined with update modes")
        return run_self_test()

    if args.physics:
        if args.visuals or args.perf:
            parser.error("--physics cannot be combined with --visuals or --perf")
        try:
            return update_physics(args.repo.resolve())
        except physics_guard.GuardFailure as exc:
            print(f"FAIL: {exc}")
            return 1

    do_visuals = args.visuals or not args.perf
    do_perf = args.perf or not args.visuals

    status = 0
    if do_visuals:
        status |= update_visuals(args.repo.resolve(), args.require)
    if do_perf:
        status |= update_perf(args.repo.resolve(), args.require)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
