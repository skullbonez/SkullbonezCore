#
# File: tools/check_replay_v2_artifact.py
# Purpose:
#   Validates runtime-written replay v2 artifacts, saved checkpoint restore,
#   and bounded query export.
#
# Mental model:
#   The runtime owns replay capture, artifact writing, and checkpoint restore.
#   This script drives the Debug executable through the CLI save/load/restore
#   probes, then asks replay_query and physics_query small questions instead of
#   reading the whole binary artifact into the model.
#
# Glossary:
#   Replay v2: Chunked binary presentation .skreplay artifact.
#   SkullScope slice: Bounded NDJSON exported from selected replay frames.
#
# Invariants:
#   - Runtime-generated artifacts are validated through replay_query and
#     physics_query rather than by hand-parsing every byte in validation logs.
#   - Expected failure coverage proves both preflight rejection and verified
#     post-mutation rollback before the runtime returns control.
#
# Related:
#   - tools/replay_query.py
#   - tools/physics_query.py
#
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

from replay_query import EVENT_RECORD, ReplayV2


REPO = Path(os.environ.get("SKORE_REPO", Path(__file__).resolve().parents[1])).resolve()
OUT_DIR = REPO / "TestOutput" / "validation" / "replay_v2"
ARTIFACT = OUT_DIR / "replay_save_probe.skreplay"
TOPOLOGY_ARTIFACT = OUT_DIR / "replay_generated_topology_probe.skreplay"
MUTATION_ARTIFACT = OUT_DIR / "replay_timeline_mutation_probe.skreplay"
TRACE = OUT_DIR / "replay_save_probe.physicsdiag.ndjson"
RUNTIME_TRACE = OUT_DIR / "replay_save_probe_runtime.physicsdiag.ndjson"
TOPOLOGY_RUNTIME_TRACE = OUT_DIR / "replay_generated_topology_runtime.physicsdiag.ndjson"
RESTORE_FAILURE_TRACE = OUT_DIR / "replay_restore_failure.physicsdiag.ndjson"
SCENE_ARG = "SkullbonezData/scenes/replay_v2_solver_one.scene.json"
TOPOLOGY_SCENE_ARG = "SkullbonezData/scenes/replay_v2_generated_topology.scene.json"
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
    remove_if_exists(TOPOLOGY_ARTIFACT)
    remove_if_exists(MUTATION_ARTIFACT)
    remove_if_exists(TRACE)
    remove_if_exists(TRACE.with_suffix(".sqlite"))
    remove_if_exists(TRACE.with_suffix(".sqlite.lock"))
    remove_if_exists(RUNTIME_TRACE)
    remove_if_exists(RUNTIME_TRACE.with_suffix(".sqlite"))
    remove_if_exists(RUNTIME_TRACE.with_suffix(".sqlite.lock"))
    remove_if_exists(TOPOLOGY_RUNTIME_TRACE)
    remove_if_exists(TOPOLOGY_RUNTIME_TRACE.with_suffix(".sqlite"))
    remove_if_exists(TOPOLOGY_RUNTIME_TRACE.with_suffix(".sqlite.lock"))
    remove_if_exists(RESTORE_FAILURE_TRACE)
    remove_if_exists(RESTORE_FAILURE_TRACE.with_suffix(".sqlite"))
    remove_if_exists(RESTORE_FAILURE_TRACE.with_suffix(".sqlite.lock"))

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


def generate_topology_artifact():
    command = [
        str(EXE),
        "--renderer",
        "dx12",
        "--vsync",
        "off",
        "--shadows",
        "off",
        "--scene",
        TOPOLOGY_SCENE_ARG,
        "--frames",
        "120",
        "--replay",
        "on",
        "--replay-seconds",
        "1",
        "--replay-save-probe",
        str(TOPOLOGY_ARTIFACT),
        "--physics-diag",
        str(TOPOLOGY_RUNTIME_TRACE),
    ]
    print("  Generated topology artifact command:")
    print("    " + " ".join(command))
    runtime_stdout = run_checked(command, REPO)
    probe_lines = [line for line in runtime_stdout.splitlines() if "[replay] Save probe" in line]
    for line in probe_lines:
        print(f"  {line}")
    if not any("Save probe loaded" in line for line in probe_lines):
        raise RuntimeError("generated topology save probe did not report v2 load/seek proof")
    if not TOPOLOGY_ARTIFACT.exists():
        raise RuntimeError(f"generated topology replay artifact was not produced: {TOPOLOGY_ARTIFACT}")
    return len(runtime_stdout.encode("utf-8"))


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


def probe_restored_checkpoint():
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
        "--replay-restore-file-probe",
        str(ARTIFACT),
    ]
    print("  Restore file probe command:")
    print("    " + " ".join(command))
    runtime_stdout = run_checked(command, REPO)
    probe_lines = [line for line in runtime_stdout.splitlines() if "[replay] Restore file probe" in line]
    for line in probe_lines:
        print(f"  {line}")
    if not any("Restore file probe passed" in line for line in probe_lines):
        raise RuntimeError("runtime restore file probe did not report saved checkpoint restore proof")
    return len(runtime_stdout.encode("utf-8"))


def probe_restored_target():
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
        "--replay-restore-target-file-probe",
        str(ARTIFACT),
    ]
    print("  Restore target probe command:")
    print("    " + " ".join(command))
    runtime_stdout = run_checked(command, REPO)
    probe_lines = [line for line in runtime_stdout.splitlines() if "[replay] Restore target probe" in line]
    for line in probe_lines:
        print(f"  {line}")
    if not any("Restore target probe passed" in line for line in probe_lines):
        raise RuntimeError("runtime restore target probe did not report checkpoint-plus-event target proof")
    if not any("events_applied=" in line and "events_applied=0" not in line for line in probe_lines):
        raise RuntimeError("runtime restore target probe did not apply saved v2 events")
    return len(runtime_stdout.encode("utf-8"))


def probe_restored_branch():
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
        "--replay-restore-branch-file-probe",
        str(ARTIFACT),
    ]
    print("  Restore branch probe command:")
    print("    " + " ".join(command))
    runtime_stdout = run_checked(command, REPO)
    probe_lines = [line for line in runtime_stdout.splitlines() if "[replay] Restore branch probe" in line]
    for line in probe_lines:
        print(f"  {line}")
    if not any("Restore branch probe passed" in line for line in probe_lines):
        raise RuntimeError("runtime restore branch probe did not report branch-from-file proof")
    if not any("events_applied=" in line and "events_applied=0" not in line for line in probe_lines):
        raise RuntimeError("runtime restore branch probe did not apply saved v2 events")
    if not any("branch_id=" in line and "branch_id=0" not in line for line in probe_lines):
        raise RuntimeError("runtime restore branch probe did not create a live branch")
    return len(runtime_stdout.encode("utf-8"))


def probe_generated_topology_restore():
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
        "--replay-restore-target-file-probe",
        str(TOPOLOGY_ARTIFACT),
    ]
    print("  Generated topology restore command:")
    print("    " + " ".join(command))
    runtime_stdout = run_checked(command, REPO)
    probe_lines = [line for line in runtime_stdout.splitlines() if "[replay] Restore target probe" in line]
    for line in probe_lines:
        print(f"  {line}")
    if not any("Restore target probe passed" in line for line in probe_lines):
        raise RuntimeError("generated topology restore probe did not report target restore proof")
    if not any("generated_topology_rebuilt=1" in line for line in probe_lines):
        raise RuntimeError("generated topology restore probe did not rebuild topology from the v2 event stream")
    if not any("bodies=" in line and "bodies=6" in line for line in probe_lines):
        raise RuntimeError("generated topology restore probe did not restore the expected multi-body target")
    if not any("events_applied=" in line and "events_applied=0" not in line for line in probe_lines):
        raise RuntimeError("generated topology restore probe did not apply saved v2 events")
    return len(runtime_stdout.encode("utf-8"))


def probe_restore_failure_row():
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
        "--replay-restore-failure-file-probe",
        str(ARTIFACT),
        "--physics-diag",
        str(RESTORE_FAILURE_TRACE),
    ]
    print("  Restore failure probe command:")
    print("    " + " ".join(command))
    runtime_stdout = run_checked(command, REPO)
    probe_lines = [line for line in runtime_stdout.splitlines() if "[replay] Restore failure probe" in line]
    for line in probe_lines:
        print(f"  {line}")
    if not any("Restore failure probe passed" in line for line in probe_lines):
        raise RuntimeError("runtime restore failure probe did not report expected saved-file failure")

    restore_command = [str(PHYSICS_QUERY_BAT), str(RESTORE_FAILURE_TRACE), "restore", "--limit", "4"]
    print("  Restore failure query command:")
    print(
        "    tools\\physics_query.bat "
        "TestOutput\\validation\\replay_v2\\replay_restore_failure.physicsdiag.ndjson restore --limit 4"
    )
    restore_stdout, restore_payload = run_json(restore_command, REPO)
    restores = restore_payload.get("restores") or []
    if len(restores) != 2:
        raise RuntimeError(f"expected preflight and rollback failure rows, found {len(restores)}")
    if any(restore.get("passed") or not restore.get("failed") for restore in restores):
        raise RuntimeError(f"restore failure query unexpectedly marked a row as passed: {restores}")
    if any(restore.get("restore_source") != "v2_file_target" for restore in restores):
        raise RuntimeError(f"unexpected restore failure source: {restores}")

    preflight = next(
        (
            restore
            for restore in restores
            if "found no saved hash for requested target frame" in str(restore.get("failure_reason") or "")
        ),
        None,
    )
    if preflight is None or preflight.get("fallback_attempted"):
        raise RuntimeError(f"preflight failure row was missing or attempted rollback: {restores}")

    rollback = next(
        (
            restore
            for restore in restores
            if "solver hash mismatch" in str(restore.get("failure_reason") or "")
        ),
        None,
    )
    if rollback is None:
        raise RuntimeError(f"post-mutation hash failure row was missing: {restores}")
    if not rollback.get("hash_captured") or rollback.get("hash_matched"):
        raise RuntimeError(f"post-mutation hash failure did not record the injected mismatch: {rollback}")
    if not rollback.get("fallback_attempted") or not rollback.get("fallback_restored"):
        raise RuntimeError(f"post-mutation hash failure did not prove rollback: {rollback}")
    return len(runtime_stdout.encode("utf-8")), len(restore_stdout.encode("utf-8"))


def make_timeline_mutation_artifact():
    shutil.copy2(ARTIFACT, MUTATION_ARTIFACT)
    replay = ReplayV2(MUTATION_ARTIFACT)
    target = next((sample for sample in reversed(replay.solver_hashes) if not sample.checkpoint_boundary), None)
    if target is None:
        raise RuntimeError("timeline mutation probe needs a non-checkpoint target hash")

    checkpoint = None
    for sample in replay.solver_checkpoints:
        if sample.frame_index <= target.frame_index and (
            checkpoint is None or sample.frame_index > checkpoint.frame_index
        ):
            checkpoint = sample
    if checkpoint is None:
        raise RuntimeError("timeline mutation probe needs a checkpoint before the target")

    checkpoint_branch_id = 0
    for branch in replay.branches:
        if branch.first_retained_frame <= checkpoint.frame_index <= branch.last_retained_frame:
            checkpoint_branch_id = branch.branch_id
            break
    if checkpoint_branch_id == 0:
        raise RuntimeError("timeline mutation probe could not resolve the checkpoint branch")

    mutation_index = None
    mutation_event = None
    for index, event in enumerate(replay.events):
        if (
            event.branch_id == checkpoint_branch_id
            and event.frame_index > checkpoint.frame_index
            and event.frame_index <= target.frame_index
            and event.sequence >= checkpoint.event_cursor
        ):
            mutation_index = index
            mutation_event = event
            break
    if mutation_index is None or mutation_event is None:
        raise RuntimeError("timeline mutation probe could not find an event inside the checkpoint replay window")

    event_chunk = replay.chunks.get("EVNT")
    if event_chunk is None:
        raise RuntimeError("timeline mutation probe requires an EVNT chunk")

    data = bytearray(MUTATION_ARTIFACT.read_bytes())
    record_offset = event_chunk.offset + 4 + mutation_index * EVENT_RECORD.size
    struct.pack_into("<H", data, record_offset + 20, 2)
    struct.pack_into("<I", data, record_offset + 24, 0)
    struct.pack_into("<i", data, record_offset + 28, 4)
    struct.pack_into("<iii", data, record_offset + 32, -1, 0, 0)
    struct.pack_into("<Q", data, record_offset + 44, 0)
    text = b"CreateScene"
    data[record_offset + 68 : record_offset + 68 + 128] = text + b"\0" * (128 - len(text))
    MUTATION_ARTIFACT.write_bytes(data)
    return mutation_event, checkpoint, target, checkpoint_branch_id


def probe_timeline_mutation_rejection():
    mutation_event, checkpoint, target, checkpoint_branch_id = make_timeline_mutation_artifact()
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
        "--replay-restore-target-file-probe",
        str(MUTATION_ARTIFACT),
    ]
    print("  Timeline mutation rejection probe command:")
    print("    " + " ".join(command))
    result = subprocess.run(
        command,
        cwd=str(REPO),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    combined = result.stdout + result.stderr
    probe_lines = [
        line
        for line in combined.splitlines()
        if "[replay]" in line or "replay restore target probe failed" in line or "unsupported replay" in line
    ]
    for line in probe_lines[:12]:
        print(f"  {line}")
    if result.returncode == 0:
        raise RuntimeError("timeline mutation rejection probe unexpectedly restored a mutated artifact")
    if "unsupported replay event kind" not in combined:
        raise RuntimeError(f"timeline mutation rejection probe missing expected reason: {combined}")
    if "replay restore target probe failed to apply event sequence" not in combined:
        raise RuntimeError(f"timeline mutation rejection probe did not fail at event replay: {combined}")
    print(
        "  Timeline mutation rejection passed: "
        f"mutated_sequence={mutation_event.sequence} original_kind={mutation_event.kind_name} "
        f"checkpoint_frame={checkpoint.frame_index} target_frame={target.frame_index} branch={checkpoint_branch_id} "
        f"exit_code={result.returncode}"
    )
    return len(combined.encode("utf-8"))


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
    if int(summary.get("branchEntryBytes") or 0) != 64:
        raise RuntimeError(f"expected 64-byte branch rows, found {summary.get('branchEntryBytes')}")
    if int(summary.get("branchCount") or 0) <= 0:
        raise RuntimeError("expected at least one replay branch provenance row")
    if int(summary.get("eventEntryBytes") or 0) != 200:
        raise RuntimeError(f"expected 200-byte event rows, found {summary.get('eventEntryBytes')}")
    if int(summary.get("eventCount") or 0) <= 0:
        raise RuntimeError("expected at least one replay event row")
    if int(summary.get("eventCursorEntryBytes") or 0) != 24:
        raise RuntimeError(f"expected 24-byte event cursor rows, found {summary.get('eventCursorEntryBytes')}")
    if int(summary.get("solverHashBytes") or 0) != 48:
        raise RuntimeError(f"expected 48-byte solver hash rows, found {summary.get('solverHashBytes')}")
    if int(summary.get("solverBodyBytes") or 0) != 112:
        raise RuntimeError(f"expected 112-byte solver body checkpoint rows, found {summary.get('solverBodyBytes')}")
    if int(summary.get("solverHashCount") or 0) < int(summary.get("frameCount") or 0):
        raise RuntimeError(
            f"expected a solver hash per replay frame, found {summary.get('solverHashCount')} hashes for "
            f"{summary.get('frameCount')} frames"
        )
    if int(summary.get("solverCheckpointCount") or 0) <= 0:
        raise RuntimeError("expected at least one solver checkpoint chunk row")
    if int(summary.get("eventCursorCount") or 0) != int(summary.get("solverCheckpointCount") or 0):
        raise RuntimeError(
            f"expected one event cursor per checkpoint, found {summary.get('eventCursorCount')} cursors for "
            f"{summary.get('solverCheckpointCount')} checkpoints"
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

    branch_command = [str(REPLAY_QUERY_BAT), str(ARTIFACT), "branches", "--limit", "8"]
    print("  Branch command:")
    print(
        "    tools\\replay_query.bat TestOutput\\validation\\replay_v2\\replay_save_probe.skreplay "
        "branches --limit 8"
    )
    branch_stdout, branches = run_json(branch_command, REPO)
    if int(branches.get("branchCount") or 0) != int(summary.get("branchCount") or 0):
        raise RuntimeError("replay branch query did not report the full branch provenance track")
    branch_samples = branches.get("samples") or []
    if not branch_samples:
        raise RuntimeError("replay branch query did not return any branch samples")
    first_branch = branch_samples[0]
    if int(first_branch.get("branchId") or 0) != 1 or int(first_branch.get("parentBranchId") or 0) != 0:
        raise RuntimeError(f"expected root branch provenance, found {first_branch}")

    event_command = [
        str(REPLAY_QUERY_BAT),
        str(ARTIFACT),
        "events",
        "--frames",
        f"{first_frame}:{last_frame}",
        "--limit",
        "8",
    ]
    print("  Event command:")
    print(
        "    tools\\replay_query.bat TestOutput\\validation\\replay_v2\\replay_save_probe.skreplay "
        f"events --frames {first_frame}:{last_frame} --limit 8"
    )
    event_stdout, events = run_json(event_command, REPO)
    if int(events.get("eventCount") or 0) != int(summary.get("eventCount") or 0):
        raise RuntimeError("replay event query did not report the full event track")
    event_samples = events.get("samples") or []
    if not event_samples:
        raise RuntimeError("replay event query did not return any event samples")
    if event_samples[0].get("kind") != "timelineStart":
        raise RuntimeError(f"expected timelineStart event first, found {event_samples[0]}")
    event_kinds = {sample.get("kind") for sample in event_samples}
    for expected_kind in (
        "ownerAction",
        "generatedSceneConfig",
        "worldOverride",
        "editorPlace",
        "editorTransform",
        "launcherConfig",
        "launcherFire",
    ):
        if expected_kind not in event_kinds:
            raise RuntimeError(f"expected replay event kind {expected_kind}, found {sorted(event_kinds)}")
    owner_action_samples = [sample for sample in event_samples if sample.get("kind") == "ownerAction"]
    if not any((sample.get("decoded") or {}).get("ownerAction") == "SceneReset" for sample in owner_action_samples):
        raise RuntimeError(f"expected decoded SceneReset owner action, found {owner_action_samples}")
    for sample in event_samples:
        if sample.get("kind") in (
            "generatedSceneConfig",
            "worldOverride",
            "editorPlace",
            "editorTransform",
            "launcherConfig",
            "launcherFire",
        ) and not sample.get("decoded"):
            raise RuntimeError(f"expected decoded payload for {sample.get('kind')}: {sample}")
    editor_transform_samples = [sample for sample in event_samples if sample.get("kind") == "editorTransform"]
    if not any(
        (sample.get("decoded") or {}).get("translated")
        and (sample.get("decoded") or {}).get("rotated")
        and (sample.get("decoded") or {}).get("scaled")
        and (sample.get("decoded") or {}).get("scaleAxis") == 0
        and (sample.get("decoded") or {}).get("scaleFactor") is not None
        for sample in editor_transform_samples
    ):
        raise RuntimeError(f"expected decoded editorTransform translate/rotate/scale sample, found {editor_transform_samples}")

    cursor_command = [
        str(REPLAY_QUERY_BAT),
        str(ARTIFACT),
        "event-cursors",
        "--frames",
        f"{first_frame}:{last_frame}",
        "--limit",
        "8",
    ]
    print("  Event cursor command:")
    print(
        "    tools\\replay_query.bat TestOutput\\validation\\replay_v2\\replay_save_probe.skreplay "
        f"event-cursors --frames {first_frame}:{last_frame} --limit 8"
    )
    cursor_stdout, cursors = run_json(cursor_command, REPO)
    if int(cursors.get("eventCursorCount") or 0) != int(summary.get("eventCursorCount") or 0):
        raise RuntimeError("replay event cursor query did not report the full cursor track")
    cursor_samples = cursors.get("samples") or []
    if not cursor_samples:
        raise RuntimeError("replay event cursor query did not return any cursor samples")
    first_cursor = cursor_samples[0]
    if int(first_cursor.get("eventCursor") or 0) <= 0 or not first_cursor.get("solverHash"):
        raise RuntimeError(f"replay event cursor query returned invalid metadata: {first_cursor}")

    checkpoint_command = [
        str(REPLAY_QUERY_BAT),
        str(ARTIFACT),
        "checkpoints",
        "--frames",
        f"{first_frame}:{last_frame}",
        "--limit",
        "8",
        "--body-limit",
        "2",
    ]
    print("  Checkpoint command:")
    print(
        "    tools\\replay_query.bat TestOutput\\validation\\replay_v2\\replay_save_probe.skreplay "
        f"checkpoints --frames {first_frame}:{last_frame} --limit 8 --body-limit 2"
    )
    checkpoint_stdout, checkpoints = run_json(checkpoint_command, REPO)
    if int(checkpoints.get("checkpointCount") or 0) != int(summary.get("solverCheckpointCount") or 0):
        raise RuntimeError("replay checkpoint query did not report the full solver checkpoint track")
    checkpoint_samples = checkpoints.get("samples") or []
    if not checkpoint_samples:
        raise RuntimeError("replay checkpoint query did not return any checkpoint samples")
    first_checkpoint = checkpoint_samples[0]
    if not first_checkpoint.get("checkpointBoundary") or not first_checkpoint.get("solverHash"):
        raise RuntimeError("replay checkpoint query did not return checkpoint hash metadata")
    if int(first_checkpoint.get("eventCursor") or 0) != int(first_cursor.get("eventCursor") or 0):
        raise RuntimeError("replay checkpoint query did not include matching event cursor metadata")
    if int(first_checkpoint.get("bodyCount") or 0) <= 0 or not first_checkpoint.get("bodies"):
        raise RuntimeError("replay checkpoint query did not return solver body payloads")
    snapshot = first_checkpoint.get("snapshot") or {}
    if int(snapshot.get("version") or 0) not in (1, 2):
        raise RuntimeError("replay checkpoint query returned an unsupported snapshot version")
    if int(snapshot.get("modelCount") or 0) != int(first_checkpoint.get("bodyCount") or 0):
        raise RuntimeError("replay checkpoint snapshot model count did not match body count")

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
        "replay_branches": len(branch_stdout.encode("utf-8")),
        "replay_events": len(event_stdout.encode("utf-8")),
        "replay_event_cursors": len(cursor_stdout.encode("utf-8")),
        "replay_checkpoints": len(checkpoint_stdout.encode("utf-8")),
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
        print("  Probing saved solver checkpoint restore...")
        restore_probe_bytes = probe_restored_checkpoint()
        print("  Probing saved solver target restore...")
        restore_target_probe_bytes = probe_restored_target()
        print("  Probing saved solver branch restore...")
        restore_branch_probe_bytes = probe_restored_branch()
        print("  Probing saved solver restore failure SkullScope row...")
        restore_failure_probe_bytes, restore_failure_query_bytes = probe_restore_failure_row()
        print("  Probing saved solver timeline mutation rejection...")
        timeline_mutation_probe_bytes = probe_timeline_mutation_rejection()
        print("  Generating broader generated-scene topology artifact...")
        topology_save_probe_bytes = generate_topology_artifact()
        print("  Probing generated-scene topology restore from mismatched live scene...")
        topology_restore_probe_bytes = probe_generated_topology_restore()
        print("  Querying replay v2 artifact...")
        summary, query_bytes = query_artifact()
        sqlite_path = TRACE.with_suffix(".sqlite")
        restore_failure_sqlite_path = RESTORE_FAILURE_TRACE.with_suffix(".sqlite")
        print(
            "  PASS: replay v2 artifact frames={frames} bodies={bodies} branches={branches} events={events} event_cursors={event_cursors} checkpoints={checkpoints}".format(
                frames=summary.get("frameCount"),
                bodies=summary.get("bodyDictionaryCount"),
                branches=summary.get("branchCount"),
                events=summary.get("eventCount"),
                event_cursors=summary.get("eventCursorCount"),
                checkpoints=summary.get("solverCheckpointCount"),
            )
        )
        print(f"  Artifact bytes: {ARTIFACT.stat().st_size}")
        print(f"  Runtime trace bytes: {RUNTIME_TRACE.stat().st_size if RUNTIME_TRACE.exists() else 0}")
        print(f"  Trace bytes: {TRACE.stat().st_size}")
        print(f"  SQLite bytes: {sqlite_path.stat().st_size if sqlite_path.exists() else 0}")
        print(f"  Restore failure trace bytes: {RESTORE_FAILURE_TRACE.stat().st_size}")
        print(
            "  Restore failure SQLite bytes: "
            f"{restore_failure_sqlite_path.stat().st_size if restore_failure_sqlite_path.exists() else 0}"
        )
        print(f"  Load probe output bytes: {load_probe_bytes}")
        print(f"  Restore file probe output bytes: {restore_probe_bytes}")
        print(f"  Restore target probe output bytes: {restore_target_probe_bytes}")
        print(f"  Restore branch probe output bytes: {restore_branch_probe_bytes}")
        print(f"  Restore failure probe output bytes: {restore_failure_probe_bytes}")
        print(f"  Restore failure query output bytes: {restore_failure_query_bytes}")
        print(f"  Timeline mutation rejection probe output bytes: {timeline_mutation_probe_bytes}")
        print(f"  Generated topology artifact bytes: {TOPOLOGY_ARTIFACT.stat().st_size}")
        print(f"  Generated topology runtime trace bytes: {TOPOLOGY_RUNTIME_TRACE.stat().st_size}")
        print(f"  Generated topology save probe output bytes: {topology_save_probe_bytes}")
        print(f"  Generated topology restore probe output bytes: {topology_restore_probe_bytes}")
        print(f"  Query output bytes: {json.dumps(query_bytes, sort_keys=True)}")
        print(f"  Query output bytes total: {sum(query_bytes.values()) + restore_failure_query_bytes}")
        return 0
    except Exception as exc:
        print(f"  FAIL: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
