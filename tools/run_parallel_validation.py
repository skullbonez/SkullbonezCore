#!/usr/bin/env python3
"""Run a bounded validation manifest with isolated, timed child processes."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any


SCHEMA_VERSION = 1
WORKDIR_LINKS = ("SkullbonezData", "SkullbonezSource", "Agentic", "tools", "ThirdPtySource")


@dataclass
class StepResult:
    name: str
    launch: str
    pid: int | None
    exit_code: int
    expected_exit: int | str
    passed: bool
    elapsed_seconds: float
    log: str


@dataclass
class LaneResult:
    lane_id: str
    name: str
    passed: bool
    elapsed_seconds: float
    steps: list[StepResult]


class ProcessRegistry:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._processes: dict[int, subprocess.Popen[str]] = {}

    def add(self, process: subprocess.Popen[str]) -> None:
        with self._lock:
            self._processes[process.pid] = process

    def remove(self, process: subprocess.Popen[str]) -> None:
        with self._lock:
            self._processes.pop(process.pid, None)

    def terminate_all(self) -> None:
        with self._lock:
            processes = list(self._processes.values())

        for process in processes:
            if process.poll() is not None:
                continue
            if os.name == "nt":
                subprocess.run(
                    ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False,
                )
            else:
                process.terminate()


def _expand(value: str, variables: dict[str, str]) -> str:
    expanded = value
    for name, replacement in variables.items():
        expanded = expanded.replace("${" + name + "}", replacement)
    if "${" in expanded:
        raise ValueError(f"unresolved validation manifest variable: {expanded}")
    return expanded


def _create_junction(link: Path, target: Path) -> None:
    if os.name != "nt":
        link.symlink_to(target, target_is_directory=True)
        return

    result = subprocess.run(
        ["cmd.exe", "/d", "/c", "mklink", "/J", str(link), str(target)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if result.returncode != 0 or not link.is_dir():
        raise RuntimeError(f"failed to create validation junction {link} -> {target}: {result.stdout.strip()}")


def _prepare_workdir(repo: Path, workdir: Path) -> None:
    workdir.mkdir(parents=True, exist_ok=False)
    for name in WORKDIR_LINKS:
        target = repo / name
        if target.is_dir():
            _create_junction(workdir / name, target)

    test_output = workdir / "TestOutput"
    test_output.mkdir()
    baselines = repo / "TestOutput" / "baselines"
    if baselines.is_dir():
        _create_junction(test_output / "baselines", baselines)

    for name in ("Profile", "Debug", "Release", "Automation", "pso-cache"):
        (workdir / name).mkdir()


def _expected_exit_matches(expected: int | str, actual: int) -> bool:
    if expected == "nonzero":
        return actual != 0
    return actual == int(expected)


def _run_step(
    step: dict[str, Any],
    variables: dict[str, str],
    environment: dict[str, str],
    log_path: Path,
    registry: ProcessRegistry,
) -> StepResult:
    argv = [_expand(str(argument), variables) for argument in step["argv"]]
    cwd = Path(_expand(str(step.get("cwd", "${REPO}")), variables))
    expected_exit: int | str = step.get("expected_exit", 0)
    launch = subprocess.list2cmdline(argv)
    start = time.perf_counter()
    pid: int | None = None
    exit_code = 99

    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8", errors="replace", newline="") as log_file:
        log_file.write(f"name={step['name']}\n")
        log_file.write(f"cwd={cwd}\n")
        log_file.write(f"launch={launch}\n")
        log_file.flush()
        try:
            process = subprocess.Popen(
                argv,
                cwd=cwd,
                env=environment,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                text=True,
            )
            pid = process.pid
            registry.add(process)
            try:
                exit_code = process.wait()
            finally:
                registry.remove(process)
        except OSError as error:
            log_file.write(f"launch_error={error}\n")

    elapsed = time.perf_counter() - start
    return StepResult(
        name=str(step["name"]),
        launch=launch,
        pid=pid,
        exit_code=exit_code,
        expected_exit=expected_exit,
        passed=pid is not None and _expected_exit_matches(expected_exit, exit_code),
        elapsed_seconds=round(elapsed, 3),
        log=str(log_path),
    )


def _run_lane(
    lane: dict[str, Any],
    base_variables: dict[str, str],
    artifact_root: Path,
    registry: ProcessRegistry,
) -> LaneResult:
    lane_start = time.perf_counter()
    lane_id = str(lane["id"])
    lane_root = artifact_root / "lanes" / lane_id
    workdir = artifact_root / "work" / lane_id
    lane_root.mkdir(parents=True, exist_ok=True)
    _prepare_workdir(Path(base_variables["REPO"]), workdir)

    variables = dict(base_variables)
    variables.update(
        {
            "ARTIFACTS": str(lane_root),
            "WORKDIR": str(workdir),
        }
    )
    environment = os.environ.copy()
    environment.update(
        {
            "SKULLBONEZ_PARALLEL_VALIDATION": "1",
            "SKULLBONEZ_VALIDATION_WORKDIR": str(workdir),
            "SKULLBONEZ_TEST_WORKDIR": str(workdir),
            "SKULLBONEZ_PSO_CACHE_DIR": str(workdir / "pso-cache"),
        }
    )
    for name, value in lane.get("env", {}).items():
        environment[str(name)] = _expand(str(value), variables)

    step_results: list[StepResult] = []
    for index, step in enumerate(lane["steps"]):
        result = _run_step(
            step,
            variables,
            environment,
            lane_root / f"{index + 1:02d}-{step['id']}.log",
            registry,
        )
        step_results.append(result)
        if not result.passed:
            break

    return LaneResult(
        lane_id=lane_id,
        name=str(lane["name"]),
        passed=len(step_results) == len(lane["steps"]) and all(result.passed for result in step_results),
        elapsed_seconds=round(time.perf_counter() - lane_start, 3),
        steps=step_results,
    )


def _serialize_result(manifest: dict[str, Any], artifact_root: Path, elapsed: float, results: list[LaneResult]) -> dict[str, Any]:
    return {
        "schema": SCHEMA_VERSION,
        "group": manifest["name"],
        "max_parallel": manifest["max_parallel"],
        "elapsed_seconds": round(elapsed, 3),
        "passed": all(result.passed for result in results),
        "artifact_root": str(artifact_root),
        "lanes": [
            {
                "id": lane.lane_id,
                "name": lane.name,
                "passed": lane.passed,
                "elapsed_seconds": lane.elapsed_seconds,
                "steps": [step.__dict__ for step in lane.steps],
            }
            for lane in results
        ],
    }


def run_manifest(
    manifest: dict[str, Any],
    repo: Path,
    artifact_root: Path,
    extra_variables: dict[str, str] | None = None,
) -> tuple[int, dict[str, Any]]:
    if manifest.get("schema") != SCHEMA_VERSION:
        raise ValueError(f"unsupported parallel validation manifest schema: {manifest.get('schema')}")
    if not manifest.get("lanes"):
        raise ValueError("parallel validation manifest has no lanes")

    artifact_root.mkdir(parents=True, exist_ok=False)
    variables = {
        "REPO": str(repo),
        "TOOLS": str(repo / "tools"),
        "PYTHON": sys.executable,
    }
    variables.update(extra_variables or {})
    registry = ProcessRegistry()
    start = time.perf_counter()
    indexed_results: dict[int, LaneResult] = {}
    try:
        with ThreadPoolExecutor(max_workers=int(manifest["max_parallel"])) as executor:
            futures = {
                executor.submit(_run_lane, lane, variables, artifact_root, registry): index
                for index, lane in enumerate(manifest["lanes"])
            }
            for future in as_completed(futures):
                index = futures[future]
                indexed_results[index] = future.result()
                result = indexed_results[index]
                state = "PASS" if result.passed else "FAIL"
                print(f"[{state}] {result.name} ({result.elapsed_seconds:.3f}s)", flush=True)
    except KeyboardInterrupt:
        registry.terminate_all()
        raise

    results = [indexed_results[index] for index in range(len(manifest["lanes"]))]
    summary = _serialize_result(manifest, artifact_root, time.perf_counter() - start, results)
    summary_path = artifact_root / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    print("\nParallel validation summary:")
    for result in results:
        state = "PASS" if result.passed else "FAIL"
        print(f"  {result.name:<48} {state:<4} {result.elapsed_seconds:8.3f}s")
        for step in result.steps:
            if not step.passed:
                print(f"    launch: {step.launch}")
                print(f"    log:    {step.log}")
    print(f"  wall clock{'':<38} {summary['elapsed_seconds']:8.3f}s")
    print(f"  evidence: {summary_path}")

    for result in results:
        if not result.passed:
            failed_step = next(step for step in result.steps if not step.passed)
            exit_code = failed_step.exit_code
            return (exit_code if 1 <= exit_code <= 255 else 1), summary
    return 0, summary


def _self_test() -> int:
    manifest = {
        "schema": SCHEMA_VERSION,
        "name": "self-test",
        "max_parallel": 3,
        "lanes": [],
    }
    for index in range(3):
        expected: int | str = "nonzero" if index == 2 else 0
        actual = 7 if index == 2 else 0
        manifest["lanes"].append(
            {
                "id": f"lane-{index}",
                "name": f"self-test lane {index}",
                "steps": [
                    {
                        "id": "sleep",
                        "name": "bounded child",
                        "argv": [
                            "${PYTHON}",
                            "-c",
                            f"import time; time.sleep(0.35); raise SystemExit({actual})",
                        ],
                        "expected_exit": expected,
                    }
                ],
            }
        )

    with tempfile.TemporaryDirectory(prefix="skore-parallel-validation-self-test-") as temp:
        repo = Path(temp) / "repo"
        (repo / "tools").mkdir(parents=True)
        (repo / "TestOutput" / "baselines").mkdir(parents=True)
        artifact_root = Path(temp) / "evidence"
        exit_code, summary = run_manifest(manifest, repo, artifact_root)
        if exit_code != 0 or not summary["passed"]:
            print("SELF_TEST_FAIL: expected-exit aggregation failed", file=sys.stderr)
            return 1
        if float(summary["elapsed_seconds"]) >= 0.9:
            print("SELF_TEST_FAIL: bounded children did not overlap", file=sys.stderr)
            return 1
        if len(summary["lanes"]) != 3 or not (artifact_root / "summary.json").is_file():
            print("SELF_TEST_FAIL: structured evidence is incomplete", file=sys.stderr)
            return 1

        failure_manifest = {
            "schema": SCHEMA_VERSION,
            "name": "failure-order-self-test",
            "max_parallel": 2,
            "lanes": [
                {
                    "id": "first-in-manifest",
                    "name": "slower first failure",
                    "steps": [
                        {
                            "id": "fail",
                            "name": "exit 3",
                            "argv": [
                                "${PYTHON}",
                                "-c",
                                "import time; time.sleep(0.25); raise SystemExit(3)",
                            ],
                        }
                    ],
                },
                {
                    "id": "second-in-manifest",
                    "name": "faster second failure",
                    "steps": [
                        {
                            "id": "fail",
                            "name": "exit 7",
                            "argv": [
                                "${PYTHON}",
                                "-c",
                                "import time; time.sleep(0.05); raise SystemExit(7)",
                            ],
                        }
                    ],
                },
            ],
        }
        failure_exit, failure_summary = run_manifest(
            failure_manifest,
            repo,
            Path(temp) / "failure-evidence",
        )
        if failure_exit != 3 or failure_summary["passed"]:
            print("SELF_TEST_FAIL: manifest-order failure aggregation changed", file=sys.stderr)
            return 1
        if len(failure_summary["lanes"]) != 2:
            print("SELF_TEST_FAIL: a sibling lane was cancelled after failure", file=sys.stderr)
            return 1

    print("SELF_TEST_PASS: overlap, expected exits, isolation, complete fan-in, and deterministic failures")
    return 0


def _parse_variables(values: list[str]) -> dict[str, str]:
    variables: dict[str, str] = {}
    for value in values:
        if "=" not in value:
            raise ValueError(f"invalid --variable value: {value}")
        name, replacement = value.split("=", 1)
        if not name:
            raise ValueError("--variable name must not be empty")
        variables[name] = replacement
    return variables


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--artifact-root", type=Path)
    parser.add_argument("--variable", action="append", default=[])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return _self_test()
    if args.manifest is None:
        parser.error("--manifest is required unless --self-test is used")

    repo = args.repo.resolve()
    manifest_path = args.manifest.resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if args.artifact_root is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        artifact_root = repo / "TestOutput" / "validation" / "parallel" / str(manifest["name"]) / f"{stamp}-{os.getpid()}"
    else:
        artifact_root = args.artifact_root.resolve()

    try:
        exit_code, _ = run_manifest(manifest, repo, artifact_root, _parse_variables(args.variable))
        return exit_code
    except (OSError, ValueError, KeyError, RuntimeError, json.JSONDecodeError) as error:
        print(f"PARALLEL_VALIDATION_ERROR: {error}", file=sys.stderr)
        return 99


if __name__ == "__main__":
    raise SystemExit(main())
