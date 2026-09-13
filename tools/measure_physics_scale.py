"""Measure identified Physics workloads with a preserved or current executable.

Generated scenes are ordinary authored input, never golden outputs. Runs are
serial so they do not compete for CPU/GPU resources. Existing profiler parsing
owns repeated CSV headers and separates counters from elapsed milliseconds.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import platform
from pathlib import Path
import shutil
import subprocess
import time
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WORKLOADS = ("200", "520", "1000", "2000", "sleepy_5000", "gravity_511", "gravity_512", "gravity_513", "gravity_1024", "joints_320")
MEASUREMENT_WINDOW = "all passes, frames 60..600 inclusive"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def perf_parser():
    spec = importlib.util.spec_from_file_location("scale_perf", ROOT / "Agentic/Skills/skore-render-test/analyze_perf.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def workload_scene(name: str, directory: Path) -> Path:
    if name in WORKLOADS[:5]:
        return ROOT / f"SkullbonezData/scenes/physics_scale_{name}.scene.json"
    scene = json.loads((ROOT / "SkullbonezData/scenes/space_field_200.scene.json").read_text())
    scene["cinematic"] = {}
    scene["simulation"].pop("predictionPathPresentation", None)
    scene["simulation"]["modelCapacity"] = 2048
    scene["playback"] = {"fixedStep": True, "frames": 600, "pauseSnapshotState": False, "exitOnComplete": True}
    scene["runtime"]["pipelineSync"] = False
    scene["logging"] = {"perfLog": f"Profile/physics_scale_{name}_perf_log.csv", "perfLogFlush": False, "perfLogFlushInterval": 0}
    if name.startswith("gravity_"):
        count = int(name.split("_")[1])
        scene["simulation"]["world"]["mutualGravity"]["gravitationalConstant"] = 0.01
        scene["objects"] = [
            {"type": "ballState", "name": f"gravity_{i:04d}",
             "position": [float((i % 16) * 4) + 0.25, float(100 + (i // 16) * 4) + 0.25, float((i % 7) * 4) + 0.25],
             "velocity": [0.3, 0.0, 0.0], "angularVelocity": [0.0, 0.0, 0.0],
             "orientation": [0.0, 0.0, 0.0, 1.0], "radius": 0.1, "mass": 1.0 + (i % 11) * 0.25,
             "restitution": 0.0, "inertia": [0.004, 0.004, 0.004], "fixed": i % 17 == 0, "sleeping": False}
            for i in range(count)]
    elif name == "joints_320":
        scene["simulation"]["world"]["mutualGravity"]["enabled"] = False
        scene["objects"] = [
            {"type": "ragdoll", "name": f"joint_group_{i:02d}", "position": [(i % 8) * 12.0, 100.0, (i // 8) * 12.0],
             "scale": 1.0, "sleeping": False} for i in range(32)]
    else:
        raise ValueError(f"Unknown workload: {name}")
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"physics_scale_{name}.scene.json"
    path.write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8")
    return path


def summarize_csv(path: Path, name: str) -> dict:
    parser = perf_parser()
    columns, rows, memory = parser.parse_csv(path)
    selected = [row for row in rows if 60 <= row["frame"] <= 600]
    if not selected or "Frame/Physics" not in columns:
        raise ValueError(f"Missing Physics measurements: {path}")
    if any("Frame/Physics" not in row for row in selected):
        raise ValueError(f"Missing Physics measurements within window: {path}")
    expected_bodies = 5000 if name == "sleepy_5000" else int(name.split("_")[-1])
    counts = {row.get("Counter/Physics/TotalBodies") for row in selected}
    if counts != {float(expected_bodies)}:
        raise ValueError(f"Wrong body identity for {name}: {counts}")
    expected_frames = {(run, frame) for run in (1, 2) for frame in range(60, 601)}
    actual_frames = [(row["pass"], row["frame"]) for row in selected]
    if len(actual_frames) != len(expected_frames) or set(actual_frames) != expected_frames:
        raise ValueError(f"Incomplete or duplicated measurement window for {name}")
    metrics = {column: parser.compute_stats([row[column] for row in selected if column in row])
               for column in columns if "Physics" in column}
    samples = {column: sum(column in row for row in selected) for column in metrics}
    return {"rows": len(selected), "window": MEASUREMENT_WINDOW, "metrics": metrics,
            "metric_samples": samples, "memory": memory}


def self_test() -> None:
    header = "pass,frame,Frame/Physics,Counter/Physics/TotalBodies\n"
    samples = [f"{run},{frame},0.1,200\n" for run in (1, 2) for frame in range(60, 601)]
    with tempfile.TemporaryDirectory(prefix="physics-scale-sampling-") as temporary:
        path = Path(temporary) / "perf.csv"
        path.write_text(header + "".join(samples))
        assert summarize_csv(path, "200")["rows"] == 1082
        for broken in (samples[:-1], samples + samples[:1], [sample.replace(",200", ",199") for sample in samples]):
            path.write_text(header + "".join(broken))
            try:
                summarize_csv(path, "200")
            except ValueError:
                continue
            raise AssertionError("Incomplete, duplicated or wrong-body CSV passed")


def measure(exe: Path, output: Path, names: list[str], repeats: int, workers: int) -> None:
    output.mkdir(parents=True, exist_ok=True)
    scene_dir = output / "scenes"
    producer = {"executable": str(exe), "exe_sha256": digest(exe), "machine": platform.node(),
                "platform": platform.platform(), "workers": workers,
                # This is checkout context, not a claim that a preserved exe was
                # built from the current tree. Its byte hash identifies the producer.
                "checkout_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "checkout_tracked_diff_sha256": hashlib.sha256(subprocess.check_output(["git", "diff", "HEAD", "--binary"], cwd=ROOT)).hexdigest(),
                "engine_config_sha256": digest(ROOT / "SkullbonezData/engine.cfg")}
    results = []
    for repeat in range(repeats):
        for name in names:
            scene = workload_scene(name, scene_dir)
            target = output / f"{name}-{repeat}"
            target.mkdir(parents=True, exist_ok=True)
            csv_path = ROOT / f"Profile/physics_scale_{name}_perf_log.csv"
            # A stale CSV must never satisfy a failed or non-reporting launch.
            if csv_path.exists():
                csv_path.unlink()
            command = [str(exe), "--vsync", "off", "--fixed-step", "--shadows", "off", "--workers", str(workers), "--scene", str(scene)]
            started = time.monotonic()
            with (target / "launch.log").open("w", encoding="utf-8") as log:
                result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=600)
            entry = {**producer, "workload": name, "repeat": repeat, "command": command,
                     "scene_sha256": digest(scene), "seconds": time.monotonic() - started, "exit_code": result.returncode}
            (target / "result.json").write_text(json.dumps(entry, indent=2), encoding="utf-8")
            if result.returncode != 0:
                raise RuntimeError(f"{name} failed with {result.returncode}; see {target / 'launch.log'}")
            shutil.copy2(csv_path, target / "perf.csv")
            entry.update(summarize_csv(target / "perf.csv", name))
            (target / "result.json").write_text(json.dumps(entry, indent=2), encoding="utf-8")
            results.append(entry)
            (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
            print(f"{name} run {repeat + 1}: Physics {entry['metrics']['Frame/Physics']['avg']:.4f} ms, launch {entry['seconds']:.2f}s", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--workloads", nargs="+", choices=WORKLOADS, default=list(WORKLOADS))
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    measure(args.exe.resolve(), args.out_dir.resolve(), args.workloads, args.runs, args.workers)


if __name__ == "__main__":
    main()
