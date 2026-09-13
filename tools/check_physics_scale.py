"""Check scale workload identity, bounded work and same-machine timing changes.

Portable checks use operation counters. Broad absolute millisecond ceilings
catch frame-scale regressions without pretending hardware is interchangeable;
optional matched reference runs provide the tighter relative timing check.
No command in this tool writes or refreshes a baseline.
"""
from __future__ import annotations

import argparse
import copy
import json
import math
from pathlib import Path
import statistics
import tempfile

from measure_physics_scale import MEASUREMENT_WINDOW, ROOT, WORKLOADS, digest, self_test as sampling_self_test, workload_scene

# These ceilings are at least twice the September 12 starting medians, with
# extra room on small CPU-bound workloads. Relative comparison remains the
# precise machine-matched requirement for optimization acceptance.
AVERAGE_LIMIT_MS = {"200": 0.75, "520": 1.5, "1000": 3.0, "2000": 6.0, "sleepy_5000": 25.0,
                    "gravity_511": 3.0, "gravity_512": 3.0, "gravity_513": 3.0, "gravity_1024": 8.0, "joints_320": 5.0}
COUNTERS = ("TotalBodies", "SweepMovers", "SweepTargets", "SweepGeometryBodies", "SweepPairProbes",
            "SweepQueryNodes", "SweepFullScanMovers", "SweepScratchBytes", "JointEndpointResolutions", "JointExclusionKeys",
            "GravityPairContributions", "GravityPairBatches", "GravityScratchBytes")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def metric(row: dict, name: str, stat: str = "avg") -> float:
    value = row["metrics"][name][stat]
    require(isinstance(value, (int, float)) and math.isfinite(value) and value >= 0, f"Invalid {name}/{stat}")
    return value


def check_run(row: dict) -> None:
    name = row["workload"]
    require(name in WORKLOADS, f"Unknown workload {name}")
    require(row["exit_code"] == 0 and row["rows"] == 1082, f"Incomplete run {name}")
    require(row["window"] == MEASUREMENT_WINDOW, f"Wrong measurement window: {name}")
    require(row["workers"] == 4, f"Wrong worker policy for {name}")
    for field in ("exe_sha256", "scene_sha256", "engine_config_sha256"):
        value = row[field]
        require(isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value), f"Missing {field}")
    for counter in COUNTERS:
        key = "Counter/Physics/" + counter
        require(row["metric_samples"].get(key) == row["rows"], f"Missing measurements for {key}")
        for stat in ("min", "avg", "p50", "p99", "max"):
            metric(row, key, stat)
    require(row["metric_samples"].get("Frame/Physics") == row["rows"], f"Incomplete Physics timings: {name}")
    for stat in ("min", "avg", "p50", "p99", "max"):
        metric(row, "Frame/Physics", stat)
    count = 5000 if name == "sleepy_5000" else int(name.split("_")[-1])
    value = lambda counter, stat="avg": metric(row, "Counter/Physics/" + counter, stat)
    require(value("TotalBodies", "min") == count == value("TotalBodies", "max"), f"Wrong body count: {name}")
    require(metric(row, "Frame/Physics") <= AVERAGE_LIMIT_MS[name], f"Physics average budget exceeded: {name}")
    require(value("SweepGeometryBodies", "max") <= count, f"Repeated sweep geometry calculations: {name}")
    require(value("SweepFullScanMovers") <= value("SweepMovers"), f"Invalid fallback accounting: {name}")
    require(value("SweepTargets") <= value("SweepMovers") * (count - 1) + 1.0, f"Repeated sweep target scan: {name}")
    require(value("SweepScratchBytes", "max") <= 2 * 1024 * 1024, f"Unbounded sweep scratch: {name}")
    if name in ("520", "1000", "2000", "sleepy_5000"):
        require(value("SweepMovers") > 0 and value("SweepQueryNodes") > 0, f"Missing scale sweep/query work: {name}")
        require(value("SweepTargets") <= value("SweepMovers") * (count - 1) * 0.5, f"Excessive full-world sweep work: {name}")
    if name == "joints_320":
        require(value("JointExclusionKeys", "min") == 288 == value("JointExclusionKeys", "max"), "Joint fixture lost constraints")
        require(576 <= value("JointEndpointResolutions", "min") and value("JointEndpointResolutions", "max") <= 1152,
                "Repeated per-candidate joint resolution")
    if name.startswith("gravity_"):
        fixed = (count + 16) // 17
        expected_pairs = count * (count - 1) // 2 - fixed * (fixed - 1) // 2
        require(value("GravityPairContributions", "min") == expected_pairs == value("GravityPairContributions", "max"),
                f"Gravity fixture lost force contributions: {name}")
        require(value("GravityPairBatches", "min") >= 1, f"Missing gravity batching: {name}")
        if count > 512:
            require(value("GravityPairBatches", "min") >= 2, f"Large gravity workload did not cross a batch: {name}")
        require(value("GravityScratchBytes", "max") <= 130816 * 16 + count * 12, f"Gravity scratch grew beyond the pair cap: {name}")


def check_results(rows: list[dict], reference: list[dict] | None = None) -> list[dict]:
    require({row["workload"] for row in rows} == set(WORKLOADS), "Missing scale workload")
    identities = [(row["workload"], row["repeat"]) for row in rows]
    require(len(set(identities)) == len(identities), "Duplicate workload/run identity")
    for field in ("exe_sha256", "engine_config_sha256", "machine", "window", "workers"):
        require(len({row[field] for row in rows}) == 1, f"Mixed current {field}")
    for row in rows:
        check_run(row)
    comparison = []
    if reference is None:
        return comparison
    require({row["workload"] for row in reference} == set(WORKLOADS), "Missing reference workload")
    reference_ids = [(row["workload"], row["repeat"]) for row in reference]
    require(len(set(reference_ids)) == len(reference_ids), "Duplicate reference workload/run identity")
    require(len({row["exe_sha256"] for row in reference}) == 1, "Mixed reference executable")
    for row in reference:
        # Starting executables predate the new work counters. Their successful
        # complete timed runs still have to meet the same sampling contract.
        require(row["exit_code"] == 0 and row["rows"] == 1082, "Incomplete reference run")
        require(row["window"] == MEASUREMENT_WINDOW, "Wrong reference window")
        require(row["metric_samples"].get("Frame/Physics") == 1082, "Missing reference Physics measurements")
        for field in ("exe_sha256", "scene_sha256", "engine_config_sha256"):
            value = row[field]
            require(isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value), f"Missing reference {field}")
        for stat in ("min", "avg", "p50", "p99", "max"):
            metric(row, "Frame/Physics", stat)
    for name in WORKLOADS:
        current = [row for row in rows if row["workload"] == name]
        before = [row for row in reference if row["workload"] == name]
        require(len(current) >= 3 and len(before) >= 3, f"Need three matched runs: {name}")
        for field in ("scene_sha256", "engine_config_sha256", "machine", "workers", "window"):
            require(len({row[field] for row in current + before}) == 1, f"Mismatched {field}: {name}")
        old_ms = statistics.median(metric(row, "Frame/Physics") for row in before)
        new_ms = statistics.median(metric(row, "Frame/Physics") for row in current)
        require(old_ms > 0 and new_ms <= old_ms * 1.10, f"Physics timing regression exceeds 10%: {name}")
        comparison.append({"workload": name, "before_ms": old_ms, "after_ms": new_ms,
                           "improvement_percent": (1 - new_ms / old_ms) * 100})
    for name in ("2000", "sleepy_5000", "gravity_1024", "joints_320"):
        result = next(row for row in comparison if row["workload"] == name)
        require(result["improvement_percent"] >= 10, f"No meaningful measured Physics benefit: {name}")
    return comparison


def check_input_hashes(rows: list[dict]) -> None:
    # The body counter alone cannot distinguish different layouts or forces.
    # Reconstruct authored fixtures and match their complete input bytes.
    with tempfile.TemporaryDirectory(prefix="physics-scale-identity-") as temporary:
        expected = {name: digest(workload_scene(name, Path(temporary))) for name in WORKLOADS}
    config = digest(ROOT / "SkullbonezData/engine.cfg")
    for row in rows:
        require(row["scene_sha256"] == expected[row["workload"]], f"Wrong scene identity: {row['workload']}")
        require(row["engine_config_sha256"] == config, "Wrong engine configuration")


def self_test() -> None:
    sampling_self_test()
    def stats(value):
        return {key: value for key in ("min", "avg", "p50", "p99", "max")}
    rows = []
    for name in WORKLOADS:
        count = 5000 if name == "sleepy_5000" else int(name.split("_")[-1])
        row = {"workload": name, "repeat": 0, "exit_code": 0, "rows": 1082, "workers": 4, "machine": "fixture", "window": MEASUREMENT_WINDOW,
               "exe_sha256": "a" * 64, "scene_sha256": "b" * 64, "engine_config_sha256": "c" * 64,
               "metrics": {"Frame/Physics": stats(0.1)}, "metric_samples": {"Frame/Physics": 1082}}
        for counter in COUNTERS:
            row["metrics"]["Counter/Physics/" + counter] = stats(0)
            row["metric_samples"]["Counter/Physics/" + counter] = 1082
        row["metrics"]["Counter/Physics/TotalBodies"] = stats(count)
        row["metrics"]["Counter/Physics/SweepMovers"] = stats(8)
        row["metrics"]["Counter/Physics/SweepQueryNodes"] = stats(16)
        if name == "joints_320":
            row["metrics"]["Counter/Physics/JointExclusionKeys"] = stats(288)
            row["metrics"]["Counter/Physics/JointEndpointResolutions"] = stats(576)
        if name.startswith("gravity_"):
            fixed = (count + 16) // 17
            row["metrics"]["Counter/Physics/GravityPairContributions"] = stats(count * (count - 1) // 2 - fixed * (fixed - 1) // 2)
            row["metrics"]["Counter/Physics/GravityPairBatches"] = stats(2)
        rows.append(row)
    check_results(rows)
    controls = []
    bad = copy.deepcopy(rows); bad[3]["metrics"]["Counter/Physics/SweepTargets"] = stats(8 * 1999); controls.append(bad)
    bad = copy.deepcopy(rows); bad[-1]["metrics"]["Counter/Physics/JointEndpointResolutions"] = stats(100000); controls.append(bad)
    bad = copy.deepcopy(rows); del bad[0]["metric_samples"]["Counter/Physics/SweepMovers"]; controls.append(bad)
    bad = copy.deepcopy(rows); bad[0]["metrics"]["Frame/Physics"] = stats(1000); controls.append(bad)
    bad = copy.deepcopy(rows); bad[8]["metrics"]["Counter/Physics/GravityScratchBytes"] = stats(100000000); controls.append(bad)
    controls.append(rows[:-1])
    for index, bad in enumerate(controls):
        try:
            check_results(bad)
        except (ValueError, KeyError):
            continue
        raise AssertionError(f"Negative control {index} passed")
    matched = [dict(copy.deepcopy(row), repeat=repeat) for repeat in range(3) for row in rows]
    reference = copy.deepcopy(matched)
    for row in reference:
        row["metrics"]["Frame/Physics"] = stats(0.2)
    check_results(matched, reference)
    invalid_references = []
    bad = copy.deepcopy(reference); bad[0]["exit_code"] = 1; invalid_references.append((bad, "Incomplete reference"))
    bad = copy.deepcopy(reference); bad[0]["rows"] = 1081; invalid_references.append((bad, "Incomplete reference"))
    bad = copy.deepcopy(reference); bad[10] = copy.deepcopy(bad[0]); invalid_references.append((bad, "Duplicate reference"))
    bad = copy.deepcopy(reference); bad[0]["metric_samples"]["Frame/Physics"] = 1081; invalid_references.append((bad, "Missing reference"))
    for bad, reason in invalid_references:
        try:
            check_results(matched, bad)
        except ValueError as error:
            require(reason in str(error), "Reference control failed for the wrong reason")
        else:
            raise AssertionError(f"Invalid reference passed: {reason}")
    slower = copy.deepcopy(matched)
    slower[0]["metrics"]["Frame/Physics"] = stats(0.3)
    slower[10]["metrics"]["Frame/Physics"] = stats(0.3)
    try:
        check_results(slower, reference)
    except ValueError as error:
        require("timing regression" in str(error), "Relative timing failed for the wrong reason")
    else:
        raise AssertionError("Configured relative timing regression passed")
    matched[0]["machine"] = "another machine"
    try:
        check_results(matched, reference)
    except ValueError:
        pass
    else:
        raise AssertionError("Mismatched machine passed")
    print("PASS: scale work, identity, timing and negative controls")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    if args.results:
        rows = json.loads(args.results.read_text())
        check_input_hashes(rows)
        reference = json.loads(args.reference.read_text()) if args.reference else None
        if reference is not None:
            check_input_hashes(reference)
        comparison = check_results(rows, reference)
        if args.report:
            args.report.write_text(json.dumps(comparison, indent=2), encoding="utf-8")
        print(f"PASS: {len(rows)} scale measurements across {len(WORKLOADS)} workloads")
        for row in comparison:
            print(f"{row['workload']}: {row['before_ms']:.4f} -> {row['after_ms']:.4f} ms ({row['improvement_percent']:.1f}% faster)")
    elif not args.self_test:
        parser.error("--results or --self-test is required")


if __name__ == "__main__":
    main()
