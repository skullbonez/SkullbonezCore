#
# File: tools/check_replay_v2_artifact.py
# Purpose:
#   Validates runtime-written replay v2 artifacts and bounded query export.
#
# Mental model:
#   The runtime owns replay capture and artifact writing. This script drives the
#   Debug executable through the CLI save probe, then asks replay_query and
#   physics_query small questions instead of reading the whole binary artifact
#   into the model.
#
# Glossary:
#   Replay v2: Chunked binary presentation .skreplay artifact.
#   SkullScope slice: Bounded NDJSON exported from selected replay frames.
#
# Related:
#   - tools/replay_query.py
#   - tools/physics_query.py
#
import json
import os
from pathlib import Path
import subprocess
import sys


REPO = Path(os.environ.get("SKORE_REPO", Path(__file__).resolve().parents[1])).resolve()
OUT_DIR = REPO / "TestOutput" / "validation" / "replay_v2"
ARTIFACT = OUT_DIR / "replay_save_probe.skreplay"
TRACE = OUT_DIR / "replay_save_probe.physicsdiag.ndjson"
RUNTIME_TRACE = OUT_DIR / "replay_save_probe_runtime.physicsdiag.ndjson"
SCENE_ARG = "SkullbonezData/scenes/physics_roll.scene.json"
EXE = REPO / "Debug" / "SKULLBONEZ_CORE.exe"
REPLAY_QUERY_BAT = REPO / "tools" / "replay_query.bat"
PHYSICS_QUERY_BAT = REPO / "tools" / "physics_query.bat"


def remove_if_exists(path):
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def run_checked(args, cwd):
    result = subprocess.run(
        args,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        raise RuntimeError(f"command failed with exit code {result.returncode}: {' '.join(map(str, args))}")
    return result.stdout


def run_json(args, cwd):
    stdout = run_checked(args, cwd)
    return stdout, json.loads(stdout)


def generate_artifact():
    if not EXE.exists():
        raise RuntimeError(f"Debug executable not found: {EXE}")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    remove_if_exists(ARTIFACT)
    remove_if_exists(TRACE)
    remove_if_exists(TRACE.with_suffix(".sqlite"))
    remove_if_exists(TRACE.with_suffix(".sqlite.lock"))
    remove_if_exists(RUNTIME_TRACE)
    remove_if_exists(RUNTIME_TRACE.with_suffix(".sqlite"))
    remove_if_exists(RUNTIME_TRACE.with_suffix(".sqlite.lock"))

    command = [
        str(EXE),
        "--renderer",
        "dx12",
        "--vsync",
        "off",
        "--shadows",
        "off",
        "--scene",
        SCENE_ARG,
        "--frames",
        "120",
        "--replay",
        "on",
        "--replay-seconds",
        "1",
        "--replay-save-probe",
        str(ARTIFACT),
        "--physics-diag",
        str(RUNTIME_TRACE),
    ]
    print("  Artifact command:")
    print("    " + " ".join(command))
    runtime_stdout = run_checked(command, REPO)
    probe_lines = [line for line in runtime_stdout.splitlines() if "[replay] Save probe" in line]
    for line in probe_lines:
        print(f"  {line}")
    if not any("Save probe loaded" in line for line in probe_lines):
        raise RuntimeError("runtime save probe did not report v2 load/seek proof")
    if not ARTIFACT.exists():
        raise RuntimeError(f"replay artifact was not produced: {ARTIFACT}")


def probe_loaded_artifact():
    command = [
        str(EXE),
        "--renderer",
        "dx12",
        "--vsync",
        "off",
        "--shadows",
        "off",
        "--scene",
        SCENE_ARG,
        "--replay-load-probe",
        str(ARTIFACT),
    ]
    print("  Load probe command:")
    print("    " + " ".join(command))
    runtime_stdout = run_checked(command, REPO)
    probe_lines = [
        line
        for line in runtime_stdout.splitlines()
        if "[replay] Loaded v2 presentation artifact" in line or "[replay] Load probe" in line
    ]
    for line in probe_lines:
        print(f"  {line}")
    if not any("Load probe passed" in line for line in probe_lines):
        raise RuntimeError("runtime load probe did not report loaded-file scrub proof")
    return len(runtime_stdout.encode("utf-8"))


def query_artifact():
    summary_command = [str(REPLAY_QUERY_BAT), str(ARTIFACT), "summary"]
    print("  Summary command:")
    print("    tools\\replay_query.bat TestOutput\\validation\\replay_v2\\replay_save_probe.skreplay summary")
    summary_stdout, summary = run_json(summary_command, REPO)

    if summary.get("version") != 2 or summary.get("track") != "presentation":
        raise RuntimeError(f"unexpected replay v2 summary: {summary}")
    if int(summary.get("frameCount") or 0) < 24:
        raise RuntimeError(f"expected at least 24 replay frames, found {summary.get('frameCount')}")
    if int(summary.get("bodyDictionaryCount") or 0) <= 0:
        raise RuntimeError("expected at least one body dictionary entry")
    if int(summary.get("bodyPoseBytes") or 0) != 32:
        raise RuntimeError(f"expected 32-byte pose rows, found {summary.get('bodyPoseBytes')}")
    if int(summary.get("solverHashBytes") or 0) != 48:
        raise RuntimeError(f"expected 48-byte solver hash rows, found {summary.get('solverHashBytes')}")
    if int(summary.get("solverHashCount") or 0) < int(summary.get("frameCount") or 0):
        raise RuntimeError(
            f"expected a solver hash per replay frame, found {summary.get('solverHashCount')} hashes for "
            f"{summary.get('frameCount')} frames"
        )

    first_frame = int(summary["firstFrame"])
    last_frame = int(summary["lastFrame"])
    frame_command = [str(REPLAY_QUERY_BAT), str(ARTIFACT), "frame", str(first_frame), "--limit", "4"]
    print("  Frame command:")
    print(f"    tools\\replay_query.bat TestOutput\\validation\\replay_v2\\replay_save_probe.skreplay frame {first_frame} --limit 4")
    frame_stdout, frame = run_json(frame_command, REPO)
    if int(frame.get("bodyCount") or 0) <= 0 or not frame.get("bodies"):
        raise RuntimeError("replay frame query did not return any bodies")

    body_id = int(frame["bodies"][0]["bodyId"])
    body_command = [
        str(REPLAY_QUERY_BAT),
        str(ARTIFACT),
        "body",
        str(body_id),
        "--frames",
        f"{first_frame}:{last_frame}",
        "--limit",
        "8",
    ]
    print("  Body command:")
    print(
        "    tools\\replay_query.bat TestOutput\\validation\\replay_v2\\replay_save_probe.skreplay "
        f"body {body_id} --frames {first_frame}:{last_frame} --limit 8"
    )
    body_stdout, body = run_json(body_command, REPO)
    if int(body.get("sampleCount") or 0) <= 0:
        raise RuntimeError("replay body query did not return samples")

    hash_command = [
        str(REPLAY_QUERY_BAT),
        str(ARTIFACT),
        "hashes",
        "--frames",
        f"{first_frame}:{last_frame}",
        "--limit",
        "8",
    ]
    print("  Hash command:")
    print(
        "    tools\\replay_query.bat TestOutput\\validation\\replay_v2\\replay_save_probe.skreplay "
        f"hashes --frames {first_frame}:{last_frame} --limit 8"
    )
    hash_stdout, hashes = run_json(hash_command, REPO)
    if int(hashes.get("hashCount") or 0) < int(summary.get("frameCount") or 0):
        raise RuntimeError("replay hashes query did not report the full solver hash track")
    hash_samples = hashes.get("samples") or []
    if not hash_samples or not all(sample.get("solverHash") for sample in hash_samples):
        raise RuntimeError("replay hashes query did not return solver hash samples")

    export_end = min(first_frame + 5, last_frame)
    export_command = [
        str(REPLAY_QUERY_BAT),
        str(ARTIFACT),
        "export-skullscope",
        "--frames",
        f"{first_frame}:{export_end}",
        "--out",
        str(TRACE),
        "--run-id",
        "replay_v2_artifact",
    ]
    print("  Export command:")
    print(
        "    tools\\replay_query.bat TestOutput\\validation\\replay_v2\\replay_save_probe.skreplay "
        f"export-skullscope --frames {first_frame}:{export_end} --out "
        "TestOutput\\validation\\replay_v2\\replay_save_probe.physicsdiag.ndjson --run-id replay_v2_artifact"
    )
    export_stdout, export_payload = run_json(export_command, REPO)
    if int(export_payload.get("rows") or 0) <= int(export_payload.get("frames") or 0):
        raise RuntimeError("export-skullscope did not include body rows")

    physics_command = [str(PHYSICS_QUERY_BAT), str(TRACE), "summary"]
    print("  Physics query command:")
    print("    tools\\physics_query.bat TestOutput\\validation\\replay_v2\\replay_save_probe.physicsdiag.ndjson summary")
    physics_stdout, physics = run_json(physics_command, REPO)
    if not physics.get("runs"):
        raise RuntimeError("physics_query summary did not import the exported SkullScope slice")

    query_bytes = {
        "replay_summary": len(summary_stdout.encode("utf-8")),
        "replay_frame": len(frame_stdout.encode("utf-8")),
        "replay_body": len(body_stdout.encode("utf-8")),
        "replay_hashes": len(hash_stdout.encode("utf-8")),
        "replay_export": len(export_stdout.encode("utf-8")),
        "physics_summary": len(physics_stdout.encode("utf-8")),
    }
    return summary, query_bytes


def main():
    try:
        print("  Generating replay v2 artifact...")
        generate_artifact()
        print("  Probing loaded replay v2 artifact...")
        load_probe_bytes = probe_loaded_artifact()
        print("  Querying replay v2 artifact...")
        summary, query_bytes = query_artifact()
        sqlite_path = TRACE.with_suffix(".sqlite")
        print(f"  PASS: replay v2 artifact frames={summary.get('frameCount')} bodies={summary.get('bodyDictionaryCount')}")
        print(f"  Artifact bytes: {ARTIFACT.stat().st_size}")
        print(f"  Runtime trace bytes: {RUNTIME_TRACE.stat().st_size if RUNTIME_TRACE.exists() else 0}")
        print(f"  Trace bytes: {TRACE.stat().st_size}")
        print(f"  SQLite bytes: {sqlite_path.stat().st_size if sqlite_path.exists() else 0}")
        print(f"  Load probe output bytes: {load_probe_bytes}")
        print(f"  Query output bytes: {json.dumps(query_bytes, sort_keys=True)}")
        print(f"  Query output bytes total: {sum(query_bytes.values())}")
        return 0
    except Exception as exc:
        print(f"  FAIL: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
