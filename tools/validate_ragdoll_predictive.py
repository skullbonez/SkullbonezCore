#!/usr/bin/env python3
"""Capture speculative ragdoll A/B cost and exact clean-process replay comparisons."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import statistics
import tempfile
import time
from unittest.mock import patch

from physics_ab_capture import CaptureSide, REQUIRED, digest, validate_recording, write_json
from replay_query import ReplayV2

REPO = Path(__file__).resolve().parents[1]
# These are serialized Physics state, including the complete solver checkpoint.
# MANI carries process/path metadata; it is retained but is not a state oracle.
EXACT_CHUNKS = ("BODY", "PRES", "HASH", "SCHK")


def distribution(values: list[float]) -> dict:
    if not values or not all(math.isfinite(value) for value in values):
        raise ValueError("Missing or non-finite measurement")
    ordered = sorted(values)
    return {"samples": len(values), "average": statistics.fmean(values),
            "p95": ordered[math.ceil(len(values) * .95) - 1],
            "p99": ordered[math.ceil(len(values) * .99) - 1], "max": ordered[-1]}


def read_perf(path: Path, measured_ticks: int) -> dict:
    rows = []
    header = None
    # Profiler markers can first appear after warmup. Each new CSV header
    # describes subsequent rows; a single DictReader would mislabel columns.
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.reader(line for line in stream if not line.startswith("#")):
            if row[:2] == ["pass", "frame"]:
                header = row
            elif row:
                if header is None or len(row) != len(header):
                    raise ValueError("Malformed profiler row")
                values = dict(zip(header, map(float, row)))
                if values.get("Frame/Physics/Step", 0) > 0:
                    rows.append(values)
    if len(rows) < measured_ticks:
        raise ValueError(f"Only {len(rows)} Physics profiler rows for {measured_ticks} measured ticks")
    rows = rows[-measured_ticks:]
    markers = ("Frame/Physics", "Frame/Physics/MotionEligibility", "Frame/Physics/Broadphase",
               "Frame/Physics/Narrowphase", "Frame/Physics/Narrowphase/PersistentContacts/BuildManifolds",
               "Frame/Physics/Narrowphase/PersistentContacts/SolveRows")
    markers += ("Frame/Physics/DiagnosticsDump",)
    result = {"units": "milliseconds", "markers": {
        marker: distribution([row[marker] for row in rows]) for marker in markers}}
    result["physicsExcludingDiagnosticDump"] = distribution([
        row["Frame/Physics"] - row["Frame/Physics/DiagnosticsDump"] for row in rows])
    return result


def read_diagnostics(path: Path, warmup: int, ticks: int, enabled: bool) -> dict:
    fields = {"speculative_summary": ("candidate_pairs", "admitted_rows", "eligibility_ns", "articulation_ns"),
              "solver_stats": ("row_count", "solver_iterations"),
              "joint_summary": ("max_anchor_error_before_correction",),
              "frame": ("max_penetration", "max_speed", "max_omega", "sleeping_count")}
    samples = {kind: {} for kind in fields}
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            row = json.loads(line)
            kind, frame = row.get("kind"), row.get("frame", -1)
            if kind in samples and warmup <= frame < ticks:
                if frame in samples[kind]:
                    raise ValueError(f"Duplicate {kind} tick {frame}")
                if kind == "speculative_summary" and (row["enabled"] != enabled or row["articulated_bodies"] == 0):
                    raise ValueError("Selector or articulation state does not match the requested workload")
                samples[kind][frame] = row
    expected = set(range(warmup, ticks))
    for kind, rows in samples.items():
        if set(rows) != expected:
            raise ValueError(f"Incomplete {kind} diagnostics: {len(rows)} / {len(expected)} ticks")
    if not enabled and any(row["admitted_rows"] for row in samples["speculative_summary"].values()):
        raise ValueError("Speculative-off still admitted predictive rows")
    return {kind: {field: distribution([row[field] for row in rows.values()]) for field in fields[kind]}
            for kind, rows in samples.items()}


def read_allocations(path: Path) -> dict:
    text = path.read_text(encoding="utf-8", errors="replace")
    summary = re.search(r"\[allocation-guard\] mode=(\w+) total_allocations=(\d+) total_bytes=(\d+) "
                        r"gameplay_violations=(\d+) foreign_frees=(\d+)", text)
    if not summary:
        raise ValueError("Allocation guard did not publish a shutdown summary")
    names = ("total_allocations", "total_bytes", "gameplay_violations", "foreign_frees")
    result = {key: int(value) for key, value in zip(names, summary.groups()[1:])}
    result["mode"] = summary[1]
    result["phases"] = {}
    for match in re.finditer(r"\[allocation-guard\] phase=(\w+) ([^\r\n]+)", text):
        result["phases"][match[1]] = {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", match[2])}
    result["passed"] = result["gameplay_violations"] == 0 and result["foreign_frees"] == 0
    # The tracker emits only non-empty phases. A complete shutdown summary and
    # no Physics row therefore means zero Physics allocations, not missing data.
    result["physicsAllocations"] = result["phases"].get("physics", {}).get("allocations", 0)
    return result


def replay_metrics(path: Path) -> dict:
    replay = ReplayV2(path)
    if not replay.solver_hashes or not replay.solver_checkpoints:
        raise ValueError("Capture lacks authoritative solver hashes or checkpoints")
    last = replay.read_frame(replay.frames[-1], body_limit=None)
    bodies = [body for body in last["bodies"] if not body["fixed"]]
    linear_sq, angular_sq = [], []
    # JSON projections are adequate for reporting speed, never for equality.
    for frame in replay.frames[-120:]:
        for body in replay.read_frame(frame, body_limit=None)["bodies"]:
            if not body["fixed"]:
                linear_sq.append(sum(value * value for value in body["linearVelocity"]))
                angular_sq.append(sum(value * value for value in body["angularVelocity"]))
    return {"chunks": {ident: {"sha256": hashlib.sha256(replay._chunk_bytes(ident)).hexdigest(),
                                 "bytes": len(replay._chunk_bytes(ident))} for ident in EXACT_CHUNKS},
            "finalStateHash": last["stateHash"], "solverHashCount": len(replay.solver_hashes),
            "checkpointCount": len(replay.solver_checkpoints), "dynamicBodies": len(bodies),
            "sleepingBodies": sum(body["sleeping"] for body in bodies),
            "supportedBodies": sum(body["sleepSupported"] for body in bodies),
            "lastSecondLinearSpeedRms": math.sqrt(statistics.fmean(linear_sq)),
            "lastSecondAngularSpeedRms": math.sqrt(statistics.fmean(angular_sq))}


def run_variant(executable: Path, scene: Path, directory: Path, *, enabled: bool,
                workers: int, ticks: int, warmup: int) -> dict:
    side = CaptureSide(executable, directory, scene)
    perf_path = Path(json.loads(scene.read_text(encoding="utf-8"))["logging"]["perfLog"])
    producer_hash = digest(executable)
    started = time.monotonic()
    try:
        capabilities = side.start(worker_threads=workers, allocation_guard="gameplay")
        missing = (REQUIRED | {"physics.speculative_validation"}) - capabilities
        if missing:
            raise ValueError(f"Missing capabilities: {sorted(missing)}")
        side.command("state.subscribe", {"topics": ["frame.clocks"], "detail": "summary"})
        side.command("run.pause")
        side.command("replay.set_prediction_enabled", {"enabled": False})
        side.command("replay.set_memory_budget_mib", {"mib": 256})
        side.command("replay.set_retention_seconds", {"seconds": max(20, math.ceil(ticks / 120) + 1)})
        side.command("replay.set_recording_enabled", {"enabled": True})
        side.command("scene.reset")
        side.command("physics.speculative_validation", {"enabled": enabled})
        for first in range(0, ticks, 120):
            side.command("run.step", {"count": min(120, ticks - first)})
            write_json(directory.parent / "progress.json", {"run": directory.name,
                       "tick": min(first + 120, ticks), "totalTicks": ticks})
        replay_path = directory / "result.skreplay"
        side.command("replay.save", {"path": str(replay_path)})
        side.stop()
    finally:
        side.stop()
    if digest(executable) != producer_hash:
        raise ValueError("Producer changed during capture")
    shutil.copy2(perf_path, directory / "perf.csv")
    recording = validate_recording(replay_path, ticks)
    write_json(directory / "headers.json", recording.pop("headers"))
    result = {"name": directory.name, "enabled": enabled, "workers": workers,
              "elapsedSeconds": time.monotonic() - started, "executableSha256": producer_hash,
              "recording": recording, "replay": replay_metrics(replay_path),
              "performance": read_perf(directory / "perf.csv", ticks - warmup),
              "diagnostics": read_diagnostics(directory / "physics.physicsdiag.ndjson", warmup, ticks, enabled),
              "allocations": read_allocations(directory / "process.stdout.log"),
              "processExit": json.loads((directory / "process-exit.json").read_text(encoding="utf-8"))}
    write_json(directory / "result.json", result)
    return result


def compare_exact(directories: list[Path]) -> dict:
    reference = ReplayV2(directories[0] / "result.skreplay")
    comparisons = []
    for directory in directories[1:]:
        candidate = ReplayV2(directory / "result.skreplay")
        differences = []
        for ident in EXACT_CHUNKS:
            a, b = reference._chunk_bytes(ident), candidate._chunk_bytes(ident)
            if a != b:
                first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
                differences.append({"chunk": ident, "firstDifferentByte": first, "bytes": [len(a), len(b)]})
        comparisons.append({"reference": directories[0].name, "candidate": directory.name,
                            "equal": not differences, "differences": differences})
    return {"passed": all(row["equal"] for row in comparisons), "comparisons": comparisons}


def summarize_ab(results: list[dict]) -> dict:
    summary = {}
    for label, enabled in (("off", False), ("on", True)):
        selected = [row for row in results if row["enabled"] == enabled]
        summary[label] = {"runs": len(selected), "physicsAverageMs": statistics.fmean(
            row["performance"]["physicsExcludingDiagnosticDump"]["average"] for row in selected),
            "lastSecondLinearSpeedRms": statistics.fmean(row["replay"]["lastSecondLinearSpeedRms"] for row in selected),
            "lastSecondAngularSpeedRms": statistics.fmean(row["replay"]["lastSecondAngularSpeedRms"] for row in selected)}
    delta = summary["on"]["physicsAverageMs"] - summary["off"]["physicsAverageMs"]
    summary["predictiveAverageCostMs"] = delta
    summary["predictiveAverageCostPercent"] = delta / summary["off"]["physicsAverageMs"] * 100
    summary["timingScope"] = "Frame/Physics minus its nested DiagnosticsDump; other measured markers remain in runs"
    return summary


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="ragdoll-measurements-") as temporary:
        directory = Path(temporary)
        perf = directory / "perf.csv"
        markers = ["Frame/Physics", "Frame/Physics/MotionEligibility", "Frame/Physics/Broadphase",
                   "Frame/Physics/Narrowphase", "Frame/Physics/Narrowphase/PersistentContacts/BuildManifolds",
                   "Frame/Physics/Narrowphase/PersistentContacts/SolveRows", "Frame/Physics/DiagnosticsDump"]
        with perf.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["pass", "frame", "Frame"])
            writer.writerow([1, 0, 100])
            writer.writerow(["pass", "frame", "Frame/Physics/Step"] + markers)
            writer.writerow([1, 1, 1] + [99] * len(markers))
            writer.writerow([1, 2, 1] + [3, 1, 1, 1, 1, 1, 2])
            writer.writerow([1, 3, 0] + [0] * len(markers))  # Paused render frame.
        result = read_perf(perf, 1)
        assert result["physicsExcludingDiagnosticDump"]["average"] == 1
        assert result["markers"]["Frame/Physics"]["samples"] == 1
        try:
            read_perf(perf, 3)
        except ValueError:
            pass
        else:
            raise AssertionError("Missing Physics ticks accepted")
        diagnostics = directory / "physics.ndjson"
        rows = [dict(kind="speculative_summary", frame=120, enabled=True, articulated_bodies=2,
                     candidate_pairs=1, admitted_rows=1, eligibility_ns=2, articulation_ns=1),
                dict(kind="solver_stats", frame=120, row_count=1, solver_iterations=2),
                dict(kind="joint_summary", frame=120, max_anchor_error_before_correction=.1),
                dict(kind="frame", frame=120, max_penetration=0, max_speed=1, max_omega=1, sleeping_count=0)]
        diagnostics.write_text("".join(json.dumps(row) + "\n" for row in rows))
        assert read_diagnostics(diagnostics, 120, 121, True)["speculative_summary"]["admitted_rows"]["max"] == 1
        for bad_rows, enabled in ((rows[:-1], True), (rows + rows[:1], True), (rows, False)):
            diagnostics.write_text("".join(json.dumps(row) + "\n" for row in bad_rows))
            try:
                read_diagnostics(diagnostics, 120, 121, enabled)
            except ValueError:
                pass
            else:
                raise AssertionError("Incomplete, duplicate, or wrong-variant diagnostics accepted")
        # Exercise the complete capture exit path with independent producer
        # outcomes. Byte-exact output must still fail on allocation or shutdown.
        data_root = directory / "SkullbonezData"
        data_root.mkdir()
        (data_root / "engine.cfg").write_text("test configuration")
        scene = directory / "source.scene.json"
        scene.write_text("{}")
        executable = directory / "producer.exe"
        executable.write_bytes(b"test producer")
        for index, (exact, allocation, process_exit) in enumerate(
                ((True, True, 0), (True, False, 0), (True, True, 9), (False, True, 0))):
            result = {"performance": {"markers": {"Frame/Physics": {"average": 1.0}}},
                      "replay": {"sleepingBodies": 0, "dynamicBodies": 2},
                      "allocations": {"passed": allocation}, "processExit": {"exitCode": process_exit}}
            args = argparse.Namespace(scene=scene, exe=executable, output=directory / f"outcome-{index}",
                                      ticks=121, warmup=120, mode="determinism", workers=4)
            with patch(__name__ + ".REPO", directory), patch(__name__ + ".run_variant", return_value=result), \
                    patch(__name__ + ".compare_exact", return_value={"passed": exact}):
                status = capture(args)
            assert status == (0 if index == 0 else 1), (exact, allocation, process_exit, status)
    print("PASS: evolving profiler headers, warmup/paused exclusion, missing ticks, duplicate rows, selector identity, "
          "capture exit rejects unequal output, allocation failure and abnormal shutdown")


def capture(args: argparse.Namespace) -> int:
    if args.ticks <= args.warmup or args.warmup < 120:
        raise ValueError("Require at least 120 warmup ticks and a non-empty measurement interval")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = args.scene.resolve(strict=True)
    shutil.copy2(source, output / "source.scene.json")
    data = json.loads(source.read_text(encoding="utf-8"))
    data.setdefault("simulation", {})["seed"] = 12345
    # A perf CSV activates the legacy two-second scene pass when frames is
    # unlimited. Keep its frame limit beyond our capture; Skarness owns stopping.
    data.setdefault("playback", {})["frames"] = args.ticks + 10000
    data["logging"] = {"perfLog": str(output / "current-perf.csv"), "perfLogFlush": False}
    scene = output / "input.scene.json"
    write_json(scene, data)
    inputs = {"source": str(source), "sourceSha256": digest(source), "sceneSha256": digest(scene),
              "ticks": args.ticks, "warmup": args.warmup, "fixedDt": "1/120", "mode": args.mode,
              "executableSha256": digest(args.exe)}
    shared_inputs = [REPO / "SkullbonezData/engine.cfg"]
    for name in ("assets", "hulls", "styles"):
        shared_inputs.extend(path for path in (REPO / "SkullbonezData" / name).rglob("*") if path.is_file())
    inputs["sharedFiles"] = {path.relative_to(REPO).as_posix(): digest(path) for path in shared_inputs}
    write_json(output / "inputs.json", inputs)
    schedule = [("A1", False, args.workers), ("B1", True, args.workers),
                ("A2", False, args.workers), ("B2", True, args.workers)] if args.mode == "ab" else [
                ("w0", True, 0), ("w0-repeat", True, 0), ("w1", True, 1), ("w4", True, 4)]
    results = []
    for name, enabled, workers in schedule:
        result = run_variant(args.exe.resolve(), scene, output / name, enabled=enabled,
                             workers=workers, ticks=args.ticks, warmup=args.warmup)
        results.append(result)
        print(f"{name}: Physics {result['performance']['markers']['Frame/Physics']['average']:.4f} ms; "
              f"sleeping {result['replay']['sleepingBodies']}/{result['replay']['dynamicBodies']}", flush=True)
        if digest(scene) != inputs["sceneSha256"] or digest(args.exe) != inputs["executableSha256"]:
            raise ValueError("Inputs changed between runs")
        if any(digest(REPO / path) != expected for path, expected in inputs["sharedFiles"].items()):
            raise ValueError("Shared engine, rendering or asset inputs changed during capture")
    exact = compare_exact([output / name for name, _, _ in schedule]) if args.mode == "determinism" else {
        "A": compare_exact([output / "A1", output / "A2"]),
        "B": compare_exact([output / "B1", output / "B2"])}
    passed = exact["passed"] if args.mode == "determinism" else exact["A"]["passed"] and exact["B"]["passed"]
    allocation_passed = all(row["allocations"]["passed"] for row in results)
    process_passed = all(row["processExit"]["exitCode"] == 0 for row in results)
    write_json(output / "report.json", {"status": "captured", "inputs": inputs, "runs": results,
               "exactReplay": exact, "abSummary": summarize_ab(results) if args.mode == "ab" else None,
               "allocationGatePassed": allocation_passed, "processExitGatePassed": process_passed})
    # Matching replay bytes cannot excuse a producer that failed its allocation
    # guard or exited abnormally after saving the recording.
    return 0 if passed and allocation_passed and process_passed else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--exe", type=Path, default=REPO / "Automation/SKULLBONEZ_CORE.exe")
    parser.add_argument("--mode", choices=("ab", "determinism"), default="ab")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--ticks", type=int, default=1200)
    parser.add_argument("--warmup", type=int, default=240)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.scene is None or args.output is None:
        parser.error("--scene and --output are required for capture")
    try:
        return capture(args)
    except (OSError, ValueError, RuntimeError) as error:
        if args.output.exists():
            write_json(args.output / "failure.json", {"error": str(error)})
        raise


if __name__ == "__main__":
    raise SystemExit(main())
