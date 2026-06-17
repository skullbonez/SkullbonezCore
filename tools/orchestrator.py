#
# File: tools/orchestrator.py
# Purpose:
#   Enforces the roadmap orchestrator queue and state machine.
#
# Mental model:
#   JSON files are the executable control plane. Markdown explains the workflow
#   for humans and agents, but this script is the mechanical guardrail that
#   decides whether a transition is legal.
#
# Glossary:
#   Queue item: One planned roadmap task from Agentic/Plans.
#   Run state: Per-attempt state saved under Agentic/Runs/<date>/<item-id>.
#   Transition: A legal event from the item state machine.
#
# Invariants:
#   - Do not infer runnable work from every Markdown file in Agentic/Plans.
#   - Active queue items must fit policy parallel-capacity and conflict rules.
#   - Worker/verifier Codex runs are optional wrappers around codex exec; Python
#     still owns state transitions.
#
# Related:
#   - AGENTS.md
#   - Agentic/Orchestrator/README.md
#   - Agentic/Orchestrator/machines/roadmap-item.json
#

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from copy import deepcopy
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ORCH_DIR = Path("Agentic") / "Orchestrator"
POLICY_PATH = ORCH_DIR / "policy.json"
QUEUE_PATH = ORCH_DIR / "queue.json"
DEFAULT_MACHINE_PATH = ORCH_DIR / "machines" / "roadmap-item.json"
WORKER_TEMPLATE = ORCH_DIR / "templates" / "worker-prompt.md"
VERIFIER_TEMPLATE = ORCH_DIR / "templates" / "verifier-prompt.md"
WORKER_SCHEMA = ORCH_DIR / "schemas" / "worker-result.schema.json"
VERIFIER_SCHEMA = ORCH_DIR / "schemas" / "verifier-result.schema.json"
REPORT_TEMPLATE = ORCH_DIR / "templates" / "report.md"
WORKER_RESULT_FIELDS = {
    "status",
    "summary",
    "changed_files",
    "validation",
    "artifacts",
    "timings",
    "plain_language_summary",
    "commit_sha",
    "implementation_commit",
    "blockers",
    "risks",
}
VERIFIER_RESULT_FIELDS = {
    "verdict",
    "blocking_findings",
    "non_blocking_suggestions",
    "missing_evidence",
    "validation_assessment",
    "artifact_assessment",
    "feedback_for_worker",
    "another_round_required",
}
LIVE_LOG_POLL_SECONDS = 1.0
LIVE_LOG_HEARTBEAT_SECONDS = 30.0


class OrchestratorError(RuntimeError):
    pass


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def run_date() -> str:
    return datetime.now().strftime("%Y-%m-%d")


def repo_relative(repo: Path, path: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo.resolve())).replace("/", "\\")
    except ValueError:
        return str(path)


def display_path(path: Path) -> str:
    return str(path).replace("/", "\\")


def resolve_repo_path(repo: Path, path_text: str | Path) -> Path:
    path = Path(path_text)
    if path.is_absolute():
        return path
    return repo / path


def load_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise OrchestratorError(f"Missing JSON file: {display_path(path)}") from exc
    except json.JSONDecodeError as exc:
        raise OrchestratorError(f"Invalid JSON {display_path(path)}:{exc.lineno}: {exc.msg}") from exc


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def tail_lines(text: str, max_lines: int = 30, max_chars: int = 4000) -> str:
    lines = text.strip().splitlines()
    if len(lines) > max_lines:
        lines = lines[-max_lines:]
    tail = "\n".join(lines)
    if len(tail) > max_chars:
        tail = tail[-max_chars:]
    return tail


def codex_exec_log_paths(output_path: Path) -> tuple[Path, Path]:
    return (
        output_path.with_name(output_path.name + ".stdout.log"),
        output_path.with_name(output_path.name + ".stderr.log"),
    )


def codex_exec_transcript_path(output_path: Path) -> Path:
    return output_path.with_name(output_path.name + ".transcript.log")


def codex_exec_artifacts(repo: Path, output_path: Path) -> list[str]:
    raw_path = output_path.with_name(output_path.name + ".raw")
    paths = [
        path
        for path in (*codex_exec_log_paths(output_path), codex_exec_transcript_path(output_path), raw_path)
        if path.exists()
    ]
    return [repo_relative(repo, path) for path in paths]


def codex_exec_failure_detail(output_path: Path) -> str:
    stdout_log, stderr_log = codex_exec_log_paths(output_path)
    transcript_log = codex_exec_transcript_path(output_path)
    for path in (stderr_log, transcript_log, stdout_log):
        if path.exists():
            text = path.read_text(encoding="utf-8", errors="ignore")
            detail = tail_lines(text)
            if detail:
                return detail
    return ""


def decode_text_bytes(data: bytes) -> str:
    if data.startswith(b"\xff\xfe") or data.startswith(b"\xfe\xff"):
        return data.decode("utf-16", errors="replace")
    if data.startswith(b"\xef\xbb\xbf"):
        return data.decode("utf-8-sig", errors="replace")
    if data:
        sample = data[: min(len(data), 4096)]
        if sample.count(0) / len(sample) > 0.10:
            return data.decode("utf-16", errors="replace")
    return data.decode("utf-8", errors="replace")


def read_text_file(path: Path) -> str:
    return decode_text_bytes(path.read_bytes()).replace("\x00", "")


def result_json_has_fields(path: Path, required_fields: set[str]) -> bool:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError):
        return False
    return isinstance(payload, dict) and required_fields.issubset(payload.keys())


def preserve_result_before_overwrite(path: Path) -> None:
    if not path.exists():
        return
    raw_path = path.with_name(path.name + ".raw")
    if raw_path.exists():
        return
    shutil.copyfile(path, raw_path)


def ensure_result_payload(path: Path, payload: dict[str, Any], required_fields: set[str]) -> None:
    if result_json_has_fields(path, required_fields):
        return
    preserve_result_before_overwrite(path)
    write_json(path, payload)


def tee_stream(
    stream: Any,
    console: Any,
    stream_log: Any,
    transcript_log: Any,
    transcript_lock: threading.Lock,
    stream_name: str,
) -> None:
    transcript_buffer: list[str] = []

    def flush_transcript_buffer() -> None:
        if not transcript_buffer:
            return
        text = "".join(transcript_buffer)
        transcript_buffer.clear()
        with transcript_lock:
            transcript_log.write(f"[{stream_name}] {text}")
            if not text.endswith("\n"):
                transcript_log.write("\n")
            transcript_log.flush()

    while True:
        chunk = stream.read(1)
        if not chunk:
            break
        console.write(chunk)
        console.flush()
        stream_log.write(chunk)
        stream_log.flush()
        transcript_buffer.append(chunk)
        if chunk == "\n":
            flush_transcript_buffer()
    flush_transcript_buffer()


def tee_stream_to_file(stream: Any, console: Any, stream_log: Any) -> None:
    while True:
        chunk = stream.read(1)
        if not chunk:
            break
        console.write(chunk)
        console.flush()
        stream_log.write(chunk)
        stream_log.flush()


def elapsed_label(seconds: float) -> str:
    total = max(0, int(seconds))
    hours, remainder = divmod(total, 3600)
    minutes, secs = divmod(remainder, 60)
    if hours:
        return f"{hours}h {minutes}m {secs}s"
    if minutes:
        return f"{minutes}m {secs}s"
    return f"{secs}s"


def size_label(path: Path) -> str:
    try:
        return f"{path.stat().st_size} bytes"
    except OSError:
        return "not created yet"


def emit_file_growth(path: Path, offset: int, console: Any) -> int:
    try:
        size = path.stat().st_size
    except OSError:
        return offset
    if size < offset:
        offset = 0
    if size == offset:
        return offset
    with path.open("rb") as file:
        file.seek(offset)
        data = file.read()
    text = decode_text_bytes(data).replace("\x00", "")
    if text:
        console.write(text)
        console.flush()
    return size


def wait_with_heartbeat(
    process: subprocess.Popen[Any],
    label: str,
    log_path: Path,
    repo: Path,
    timeout_seconds: int | None,
) -> int:
    started = time.monotonic()
    last_heartbeat = started
    while True:
        returncode = process.poll()
        if returncode is not None:
            return returncode
        now = time.monotonic()
        if timeout_seconds is not None and now - started >= timeout_seconds:
            raise subprocess.TimeoutExpired(process.args, timeout_seconds)
        if now - last_heartbeat >= LIVE_LOG_HEARTBEAT_SECONDS:
            print(
                f"[orchestrator] {label} still running after {elapsed_label(now - started)}; "
                f"log: {repo_relative(repo, log_path)} ({size_label(log_path)})",
                flush=True,
            )
            last_heartbeat = now
        time.sleep(LIVE_LOG_POLL_SECONDS)


def wait_visible_helper_with_live_transcript(
    helper: subprocess.Popen[Any],
    transcript_log: Path,
    repo: Path,
    timeout_seconds: int | None,
) -> int:
    started = time.monotonic()
    last_heartbeat = started
    offset = 0
    while True:
        offset = emit_file_growth(transcript_log, offset, sys.stdout)
        returncode = helper.poll()
        if returncode is not None:
            emit_file_growth(transcript_log, offset, sys.stdout)
            return returncode
        now = time.monotonic()
        if timeout_seconds is not None and now - started >= timeout_seconds:
            emit_file_growth(transcript_log, offset, sys.stdout)
            raise subprocess.TimeoutExpired(helper.args, timeout_seconds)
        if now - last_heartbeat >= LIVE_LOG_HEARTBEAT_SECONDS:
            print(
                f"[orchestrator] sub-agent still running after {elapsed_label(now - started)}; "
                f"transcript: {repo_relative(repo, transcript_log)} ({size_label(transcript_log)})",
                flush=True,
            )
            last_heartbeat = now
        time.sleep(LIVE_LOG_POLL_SECONDS)


def normalize_state(state: str) -> str:
    return state.replace("-", "_")


def item_state(item: dict[str, Any]) -> str:
    return normalize_state(str(item.get("state", item.get("status", ""))))


def set_item_state(item: dict[str, Any], state: str) -> None:
    item["state"] = state
    item.pop("status", None)


def nested_get(payload: dict[str, Any], dotted_path: str) -> Any:
    value: Any = payload
    for part in dotted_path.split("."):
        if not isinstance(value, dict) or part not in value:
            return None
        value = value[part]
    return value


def git_status(repo: Path) -> str:
    result = subprocess.run(
        ["git", "status", "--short", "--branch"],
        cwd=repo,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return result.stderr.strip() or "git status failed"
    return result.stdout.strip()


def git_status_porcelain(repo: Path) -> str:
    result = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=repo,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise OrchestratorError(result.stderr.strip() or "Unable to inspect worktree.")
    return result.stdout.strip()


def git_changed_files(repo: Path) -> list[str]:
    result = subprocess.run(
        ["git", "diff", "--name-only"],
        cwd=repo,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return []
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def run_git(repo: Path, git_args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *git_args],
        cwd=repo,
        check=False,
        capture_output=True,
        text=True,
    )


def current_branch(repo: Path) -> str:
    result = run_git(repo, ["rev-parse", "--abbrev-ref", "HEAD"])
    if result.returncode != 0:
        raise OrchestratorError(result.stderr.strip() or "Unable to determine current branch.")
    return result.stdout.strip()


def kill_process_tree_by_pid(pid: int) -> None:
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/PID", str(pid), "/T", "/F"],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        return
    try:
        os.kill(pid, 9)
    except OSError:
        pass


def snapshot_codex_app_server_pids() -> set[int]:
    if os.name != "nt":
        return set()
    command = (
        "Get-CimInstance Win32_Process | "
        "Where-Object { $_.Name -ieq 'codex.exe' -and "
        "$_.CommandLine -like '*app-server --listen stdio://*' -and "
        "$_.CommandLine -like '*\\\\OpenAI\\\\Codex\\\\bin\\\\*' } | "
        "ForEach-Object { $_.ProcessId }"
    )
    result = subprocess.run(
        ["powershell", "-NoProfile", "-Command", command],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        return set()
    pids: set[int] = set()
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            pids.add(int(line))
        except ValueError:
            continue
    return pids


def cleanup_new_codex_app_servers(before_pids: set[int]) -> None:
    for pid in sorted(snapshot_codex_app_server_pids() - before_pids):
        kill_process_tree_by_pid(pid)


def ensure_clean_for_branch(repo: Path, allow_dirty: bool) -> None:
    result = run_git(repo, ["status", "--porcelain"])
    if result.returncode != 0:
        raise OrchestratorError(result.stderr.strip() or "Unable to inspect worktree.")
    if result.stdout.strip() and not allow_dirty:
        raise OrchestratorError("Worktree is dirty; commit/stash or pass --allow-dirty before branch setup.")


def branch_exists(repo: Path, branch: str) -> bool:
    result = run_git(repo, ["rev-parse", "--verify", "--quiet", f"refs/heads/{branch}"])
    return result.returncode == 0


def resolve_parent_branch(policy: dict[str, Any], items: dict[str, dict[str, Any]], item: dict[str, Any]) -> str:
    explicit = item.get("parent_branch") or item.get("stack_base_branch")
    if explicit:
        return str(explicit)
    depends_on = item.get("depends_on", [])
    if depends_on and nested_get(policy, "branching.chained_items_use_stacked_branches") is True:
        last_dependency = items[str(depends_on[-1])]
        return str(last_dependency.get("branch", policy.get("base_branch", "main")))
    return str(policy.get("base_branch", "main"))


def default_worker_sandbox(policy: dict[str, Any]) -> str:
    return str(nested_get(policy, "codex.worker_sandbox") or "workspace-write")


def default_verifier_sandbox(policy: dict[str, Any]) -> str:
    return str(nested_get(policy, "codex.verifier_sandbox") or "workspace-write")


def default_visible_console(policy: dict[str, Any]) -> bool:
    configured = nested_get(policy, "codex.visible_console")
    if configured is not None:
        return bool(configured)
    return os.name == "nt"


def create_or_switch_branch(repo: Path, branch: str, parent_branch: str, allow_dirty: bool) -> None:
    ensure_clean_for_branch(repo, allow_dirty)
    current = current_branch(repo)
    if current == branch:
        return
    if branch_exists(repo, branch):
        result = run_git(repo, ["switch", branch])
    else:
        result = run_git(repo, ["switch", "-c", branch, parent_branch])
    if result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip() or f"Failed to switch to {branch}."
        raise OrchestratorError(message)


def load_state(repo: Path) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    policy = load_json(repo / POLICY_PATH)
    queue = load_json(repo / QUEUE_PATH)
    machine_ref = queue.get("machine", str(DEFAULT_MACHINE_PATH))
    machine = load_json(resolve_repo_path(repo, machine_ref))
    return policy, queue, machine


def items_by_id(queue: dict[str, Any]) -> dict[str, dict[str, Any]]:
    items = queue.get("items", [])
    if not isinstance(items, list):
        raise OrchestratorError("queue.json field 'items' must be a list.")
    result: dict[str, dict[str, Any]] = {}
    for item in items:
        if not isinstance(item, dict):
            raise OrchestratorError("Every queue item must be an object.")
        item_id = item.get("id")
        if not isinstance(item_id, str) or not item_id:
            raise OrchestratorError("Every queue item needs a non-empty string id.")
        if item_id in result:
            raise OrchestratorError(f"Duplicate queue item id: {item_id}")
        result[item_id] = item
    return result


def active_states(machine: dict[str, Any]) -> set[str]:
    return {
        state
        for state, cfg in machine.get("states", {}).items()
        if isinstance(cfg, dict) and cfg.get("kind") == "active"
    }


def active_queue_items(queue: dict[str, Any], machine: dict[str, Any]) -> list[dict[str, Any]]:
    active = active_states(machine)
    return [item for item in items_by_id(queue).values() if item_state(item) in active]


def max_active_items(policy: dict[str, Any]) -> int:
    try:
        return max(1, int(policy.get("max_active_items", 1)))
    except (TypeError, ValueError):
        return 1


def parallelism_enabled(policy: dict[str, Any]) -> bool:
    return nested_get(policy, "parallelism.enabled") is True and max_active_items(policy) > 1


def string_list(value: Any) -> list[str]:
    if not isinstance(value, list):
        return []
    return [str(item) for item in value if str(item)]


def normalized_area_set(item: dict[str, Any]) -> set[str]:
    return {area.strip().lower() for area in string_list(item.get("impact_area")) if area.strip()}


def normalized_glob(pattern: str) -> str:
    return pattern.replace("\\", "/").strip().lower()


def glob_static_prefix(pattern: str) -> str:
    normalized = normalized_glob(pattern)
    wildcard_positions = [pos for pos in (normalized.find("*"), normalized.find("?"), normalized.find("[")) if pos >= 0]
    if not wildcard_positions:
        return normalized
    return normalized[: min(wildcard_positions)]


def glob_patterns_may_overlap(left: str, right: str) -> bool:
    left_norm = normalized_glob(left)
    right_norm = normalized_glob(right)
    if not left_norm or not right_norm:
        return False
    if fnmatch.fnmatchcase(left_norm, right_norm) or fnmatch.fnmatchcase(right_norm, left_norm):
        return True
    left_prefix = glob_static_prefix(left_norm)
    right_prefix = glob_static_prefix(right_norm)
    if not left_prefix or not right_prefix:
        return True
    return left_prefix.startswith(right_prefix) or right_prefix.startswith(left_prefix)


def glob_lists_may_overlap(left: list[str], right: list[str]) -> bool:
    return any(glob_patterns_may_overlap(a, b) for a in left for b in right)


def item_owned_globs(item: dict[str, Any]) -> list[str]:
    return [normalized_glob(pattern) for pattern in string_list(item.get("owned_globs"))]


def item_conflicts_with_areas(item: dict[str, Any]) -> set[str]:
    return {area.strip().lower() for area in string_list(item.get("conflicts_with_areas")) if area.strip()}


def policy_exclusive_globs(policy: dict[str, Any]) -> list[str]:
    return [
        normalized_glob(pattern)
        for pattern in string_list(nested_get(policy, "parallelism.exclusive_globs"))
    ]


def items_conflict(policy: dict[str, Any], left: dict[str, Any], right: dict[str, Any]) -> list[str]:
    reasons: list[str] = []
    left_id = str(left.get("id", "<left>"))
    right_id = str(right.get("id", "<right>"))
    left_globs = item_owned_globs(left)
    right_globs = item_owned_globs(right)

    if not left_globs:
        reasons.append(f"{left_id} has no owned_globs; treat it as exclusive.")
    if not right_globs:
        reasons.append(f"{right_id} has no owned_globs; treat it as exclusive.")

    if left_globs and right_globs and glob_lists_may_overlap(left_globs, right_globs):
        reasons.append(f"{left_id} and {right_id} owned_globs may overlap.")

    exclusive_globs = policy_exclusive_globs(policy)
    if exclusive_globs:
        if left_globs and glob_lists_may_overlap(left_globs, exclusive_globs):
            reasons.append(f"{left_id} touches policy exclusive_globs.")
        if right_globs and glob_lists_may_overlap(right_globs, exclusive_globs):
            reasons.append(f"{right_id} touches policy exclusive_globs.")

    left_areas = normalized_area_set(left)
    right_areas = normalized_area_set(right)
    left_area_conflicts = item_conflicts_with_areas(left) & right_areas
    right_area_conflicts = item_conflicts_with_areas(right) & left_areas
    if left_area_conflicts:
        reasons.append(f"{left_id} conflicts with active areas: {', '.join(sorted(left_area_conflicts))}.")
    if right_area_conflicts:
        reasons.append(f"{right_id} conflicts with candidate areas: {', '.join(sorted(right_area_conflicts))}.")

    return reasons


def area_capacity_conflicts(policy: dict[str, Any], candidate: dict[str, Any], active_items: list[dict[str, Any]]) -> list[str]:
    limits = nested_get(policy, "parallelism.max_workers_by_area")
    if not isinstance(limits, dict):
        return []
    errors: list[str] = []
    candidate_areas = normalized_area_set(candidate)
    for area in candidate_areas:
        raw_limit = limits.get(area) or limits.get(area.upper()) or limits.get(area.capitalize())
        if raw_limit is None:
            continue
        try:
            limit = int(raw_limit)
        except (TypeError, ValueError):
            continue
        if limit < 1:
            errors.append(f"Area {area} has invalid max_workers_by_area limit {raw_limit!r}.")
            continue
        active_count = sum(1 for item in active_items if area in normalized_area_set(item))
        if active_count + 1 > limit:
            errors.append(f"Area {area} would have {active_count + 1} active workers; limit is {limit}.")
    return errors


def start_guard_errors(
    policy: dict[str, Any],
    queue: dict[str, Any],
    machine: dict[str, Any],
    candidate: dict[str, Any],
) -> list[str]:
    active_items = [item for item in active_queue_items(queue, machine) if str(item.get("id")) != str(candidate.get("id"))]
    if len(active_items) >= max_active_items(policy):
        return [f"Parallel capacity is full: {len(active_items)} active items, max_active_items is {max_active_items(policy)}."]

    if active_items and not parallelism_enabled(policy):
        return ["Another item is active and policy.parallelism.enabled is not true."]

    errors = area_capacity_conflicts(policy, candidate, active_items)
    for active in active_items:
        errors.extend(items_conflict(policy, candidate, active))
    return errors


def terminal_states(machine: dict[str, Any]) -> set[str]:
    return {
        state
        for state, cfg in machine.get("states", {}).items()
        if isinstance(cfg, dict) and str(cfg.get("kind", "")).startswith("terminal")
    }


def terminal_failure_states(machine: dict[str, Any]) -> set[str]:
    return {
        state
        for state, cfg in machine.get("states", {}).items()
        if isinstance(cfg, dict) and cfg.get("kind") == "terminal_failure"
    }


def dependency_satisfied(machine: dict[str, Any], dep_state: str, mode: str) -> bool:
    state_cfg = machine.get("states", {}).get(dep_state, {})
    marker = state_cfg.get("dependency_satisfied")
    if marker is True:
        return True
    if marker == "stacked_only":
        return mode == "stacked_pr"
    if marker == "explicit_only":
        return mode == "explicit_only"
    return False


def validate_config(repo: Path, quiet: bool = False) -> tuple[list[str], list[str]]:
    policy, queue, machine = load_state(repo)
    errors: list[str] = []
    warnings: list[str] = []

    states = machine.get("states")
    if not isinstance(states, dict):
        errors.append("Machine field 'states' must be an object.")
        states = {}

    initial = machine.get("initial")
    if initial not in states:
        errors.append(f"Machine initial state is not declared: {initial}")

    for state, cfg in states.items():
        if not isinstance(cfg, dict):
            errors.append(f"Machine state {state} must be an object.")
            continue
        for event, transition in cfg.get("on", {}).items():
            target = transition.get("target") if isinstance(transition, dict) else None
            if target not in states:
                errors.append(f"Transition {state}.{event} targets unknown state {target!r}.")

    queue_states = queue.get("states", [])
    if not isinstance(queue_states, list):
        errors.append("queue.json field 'states' must be a list.")
    else:
        for state in queue_states:
            if normalize_state(str(state)) not in states:
                errors.append(f"queue.json state {state!r} is not in the machine.")

    items = items_by_id(queue)
    active_count = 0
    max_active = max_active_items(policy)
    active = active_states(machine)
    active_items: list[dict[str, Any]] = []

    for item_id, item in items.items():
        state = item_state(item)
        if state not in states:
            errors.append(f"{item_id}: unknown state {state!r}.")
        if state in active:
            active_count += 1
            active_items.append(item)

        plan_path = item.get("plan")
        if not isinstance(plan_path, str) or not plan_path:
            errors.append(f"{item_id}: missing plan path.")
        else:
            resolved = resolve_repo_path(repo, plan_path)
            if not resolved.exists():
                errors.append(f"{item_id}: missing plan path {plan_path}.")
            plan_posix = plan_path.replace("\\", "/")
            is_success = states.get(state, {}).get("kind") == "terminal_success"
            if (
                is_success
                and plan_posix.startswith("Agentic/Plans/")
                and not plan_posix.startswith("Agentic/Plans/Done/")
                and "archive_deferred" not in item
            ):
                errors.append(
                    f"{item_id}: successful terminal item points at active plan "
                    "without archive_deferred."
                )

        depends_on = item.get("depends_on", [])
        if not isinstance(depends_on, list):
            errors.append(f"{item_id}: depends_on must be a list.")
            continue
        mode = str(item.get("dependency_mode", "normal"))
        for dep_id in depends_on:
            dep = items.get(str(dep_id))
            if dep is None:
                errors.append(f"{item_id}: unknown dependency {dep_id!r}.")
                continue
            dep_state = item_state(dep)
            if not dependency_satisfied(machine, dep_state, mode):
                message = f"{item_id}: dependency {dep_id} is {dep_state}, not satisfied for mode {mode}."
                if state == "ready":
                    warnings.append(message)
                else:
                    errors.append(message)

    if active_count > max_active:
        errors.append(f"Queue has {active_count} active items; max_active_items is {max_active}.")

    if active_count > 1 and not parallelism_enabled(policy):
        errors.append("Queue has multiple active items but policy.parallelism.enabled is not true.")

    for index, left in enumerate(active_items):
        for right in active_items[index + 1 :]:
            for reason in items_conflict(policy, left, right):
                errors.append(
                    f"Active item conflict between {left.get('id')} and {right.get('id')}: {reason}"
                )

    if active_items:
        limits = nested_get(policy, "parallelism.max_workers_by_area")
        if isinstance(limits, dict):
            for area_key, raw_limit in limits.items():
                try:
                    limit = int(raw_limit)
                except (TypeError, ValueError):
                    errors.append(f"parallelism.max_workers_by_area.{area_key} must be an integer.")
                    continue
                area = str(area_key).strip().lower()
                active_for_area = sum(1 for item in active_items if area in normalized_area_set(item))
                if active_for_area > limit:
                    errors.append(
                        f"Area {area} has {active_for_area} active items; max_workers_by_area limit is {limit}."
                    )

    if nested_get(policy, "merge.allow") is True:
        agents_text = (repo / "AGENTS.md").read_text(encoding="utf-8", errors="ignore")
        if "merge" not in agents_text.lower() or "explicit" not in agents_text.lower():
            errors.append("merge.allow is true, but AGENTS.md merge permission was not found.")

    if not quiet:
        print(f"Policy enabled: {bool(policy.get('enabled', False))}")
        print(f"Queue items: {len(items)}")
        print(f"Active items: {active_count}")
        for item_id, item in items.items():
            state = item_state(item)
            events = sorted(states.get(state, {}).get("on", {}).keys())
            suffix = f" legal events: {', '.join(events)}" if events else ""
            print(f"- {item_id}: {state}{suffix}")
        for warning in warnings:
            print(f"WARNING: {warning}")
        for error in errors:
            print(f"ERROR: {error}")
        print("PASS: orchestrator check passed." if not errors else "FAIL: orchestrator check failed.")

    return errors, warnings


def select_next_item(
    policy: dict[str, Any],
    queue: dict[str, Any],
    machine: dict[str, Any],
    allow_disabled: bool,
) -> dict[str, Any] | None:
    if not policy.get("enabled", False) and not allow_disabled:
        return None

    items = items_by_id(queue)
    candidates = []
    for item in items.values():
        if item_state(item) != "ready":
            continue
        mode = str(item.get("dependency_mode", "normal"))
        if not all(dependency_satisfied(machine, item_state(items[str(dep)]), mode) for dep in item.get("depends_on", [])):
            continue
        if start_guard_errors(policy, queue, machine, item):
            continue
        candidates.append(item)
    candidates.sort(key=lambda item: (int(item.get("priority", 1000)), str(item.get("id"))))
    return candidates[0] if candidates else None


def run_root(repo: Path, policy: dict[str, Any]) -> Path:
    root = nested_get(policy, "artifact_retention.run_root") or "Agentic/Runs"
    return resolve_repo_path(repo, root)


def item_run_dir(repo: Path, policy: dict[str, Any], item_id: str, date_text: str | None = None) -> Path:
    return run_root(repo, policy) / (date_text or run_date()) / item_id


def find_latest_run_dir(repo: Path, policy: dict[str, Any], item_id: str) -> Path | None:
    root = run_root(repo, policy)
    if not root.exists():
        return None
    matches = [path / item_id for path in root.iterdir() if (path / item_id).is_dir()]
    return sorted(matches)[-1] if matches else None


def load_run_state(run_dir: Path) -> dict[str, Any]:
    return load_json(run_dir / "run.json")


def save_run_state(run_dir: Path, state: dict[str, Any]) -> None:
    write_json(run_dir / "run.json", state)


def ensure_run_dir(repo: Path, policy: dict[str, Any], item_id: str, date_text: str | None) -> Path:
    run_dir = item_run_dir(repo, policy, item_id, date_text) if date_text else find_latest_run_dir(repo, policy, item_id)
    if run_dir is None:
        raise OrchestratorError(f"No run directory found for {item_id}; run start first.")
    return run_dir


def format_lines(value: Any) -> str:
    if isinstance(value, list):
        return "\n".join(str(item) for item in value)
    if value is None:
        return ""
    return str(value)


def render_template_text(template: str, values: dict[str, Any]) -> str:
    text = template
    for key, value in values.items():
        text = text.replace("{{" + key + "}}", format_lines(value))
    return text


def template_values(
    repo: Path,
    policy: dict[str, Any],
    queue: dict[str, Any],
    item: dict[str, Any],
    run_dir: Path | None = None,
) -> dict[str, Any]:
    item_id = str(item["id"])
    date_text = run_dir.parent.name if run_dir else run_date()
    verification_dir = run_dir / "verification-rounds" if run_dir else None
    prior_rounds: list[str] = []
    if verification_dir and verification_dir.exists():
        prior_rounds = [repo_relative(repo, path) for path in sorted(verification_dir.iterdir())]
    artifact_paths: list[str] = []
    if run_dir:
        for subdir in ("screenshots", "artifacts"):
            path = run_dir / subdir
            if path.exists():
                artifact_paths.extend(repo_relative(repo, child) for child in sorted(path.rglob("*")) if child.is_file())
    run_state = load_run_state(run_dir) if run_dir and (run_dir / "run.json").exists() else {}
    parent_branch = item.get("parent_branch", item.get("stack_base_branch", ""))
    if not parent_branch and isinstance(run_state, dict):
        parent_branch = run_state.get("parent_branch", "")

    return {
        "item_id": item_id,
        "plan_path": item.get("plan", ""),
        "branch": item.get("branch", ""),
        "parent_branch": parent_branch,
        "impact_area": ", ".join(str(area) for area in item.get("impact_area", [])),
        "validation_gate": item.get("validation_gate", ""),
        "validation_notes": item.get("validation_notes", ""),
        "screenshot_scenes": item.get("screenshot_scenes", []),
        "artifact_commands": item.get("artifact_commands", []),
        "run_date": date_text,
        "changed_files": git_changed_files(repo),
        "validation_log_path": repo_relative(repo, run_dir / "validation.log") if run_dir else "",
        "artifact_paths": artifact_paths,
        "verification_round_paths": prior_rounds,
    }


def render_worker_prompt(repo: Path, policy: dict[str, Any], queue: dict[str, Any], item: dict[str, Any], run_dir: Path | None) -> str:
    template = resolve_repo_path(repo, WORKER_TEMPLATE).read_text(encoding="utf-8")
    return render_template_text(template, template_values(repo, policy, queue, item, run_dir))


def render_verifier_prompt(repo: Path, policy: dict[str, Any], queue: dict[str, Any], item: dict[str, Any], run_dir: Path) -> str:
    template = resolve_repo_path(repo, VERIFIER_TEMPLATE).read_text(encoding="utf-8")
    return render_template_text(template, template_values(repo, policy, queue, item, run_dir))


def command_check(args: argparse.Namespace) -> int:
    if args.self_test:
        return command_self_test(args)
    try:
        errors, _ = validate_config(args.repo)
    except OrchestratorError as exc:
        print(f"ERROR: {exc}")
        return 1
    return 1 if errors else 0


def command_next(args: argparse.Namespace) -> int:
    try:
        policy, queue, machine = load_state(args.repo)
        errors, _ = validate_config(args.repo, quiet=True)
        if errors:
            for error in errors:
                print(f"ERROR: {error}")
            return 1
        item = select_next_item(policy, queue, machine, args.allow_disabled)
    except OrchestratorError as exc:
        print(f"ERROR: {exc}")
        return 1

    if item is None:
        if not policy.get("enabled", False) and not args.allow_disabled:
            print("No item selected: policy.enabled is false.")
            return 2
        print("No ready item with satisfied dependencies.")
        return 2
    print(json.dumps(item, indent=2))
    return 0


def write_worker_prompt(repo: Path, policy: dict[str, Any], queue: dict[str, Any], item: dict[str, Any], run_dir: Path) -> Path:
    prompt = render_worker_prompt(repo, policy, queue, item, run_dir)
    path = run_dir / "worker-prompt.md"
    path.write_text(prompt, encoding="utf-8")
    return path


def command_start(args: argparse.Namespace) -> int:
    try:
        policy, queue, machine = load_state(args.repo)
        items = items_by_id(queue)
        item = items[args.item_id]
        if item_state(item) != "ready":
            raise OrchestratorError(f"{args.item_id} is {item_state(item)}, not ready.")
        if not policy.get("enabled", False) and not args.allow_disabled:
            raise OrchestratorError("policy.enabled is false; pass --allow-disabled only for a dry-run setup.")
        mode = str(item.get("dependency_mode", "normal"))
        for dep_id in item.get("depends_on", []):
            dep = items[str(dep_id)]
            if not dependency_satisfied(machine, item_state(dep), mode):
                raise OrchestratorError(f"Dependency {dep_id} is not satisfied.")
        guard_errors = start_guard_errors(policy, queue, machine, item)
        if guard_errors:
            raise OrchestratorError("Cannot start item in parallel: " + "; ".join(guard_errors))

        parent_branch = resolve_parent_branch(policy, items, item)
        if not args.no_branch:
            create_or_switch_branch(args.repo, str(item.get("branch")), parent_branch, args.allow_dirty)

        run_dir = item_run_dir(args.repo, policy, args.item_id, args.run_date)
        run_dir.mkdir(parents=True, exist_ok=True)
        (run_dir / "verification-rounds").mkdir(exist_ok=True)
        (run_dir / "screenshots").mkdir(exist_ok=True)
        (run_dir / "artifacts").mkdir(exist_ok=True)

        previous = item_state(item)
        set_item_state(item, "running")
        write_json(args.repo / QUEUE_PATH, queue)
        run_state = {
            "schema_version": 1,
            "item_id": args.item_id,
            "current_state": "running",
            "transition_history": [
                {
                    "at": utc_now(),
                    "event": "start",
                    "from": previous,
                    "to": "running",
                }
            ],
            "source_plan_path": item.get("plan"),
            "archived_plan_path": None,
            "branch": item.get("branch"),
            "parent_branch": parent_branch,
            "started_at": utc_now(),
            "finished_at": None,
            "baseline_git_status": git_status(args.repo),
            "policy_snapshot": deepcopy(policy),
            "queue_item_snapshot": deepcopy(item),
        }
        save_run_state(run_dir, run_state)
        prompt_path = write_worker_prompt(args.repo, policy, queue, item, run_dir)
        run_state["worker_prompt"] = repo_relative(args.repo, prompt_path)
        save_run_state(run_dir, run_state)
    except KeyError:
        print(f"ERROR: Unknown item: {args.item_id}")
        return 1
    except OrchestratorError as exc:
        print(f"ERROR: {exc}")
        return 1

    print(f"Started {args.item_id}; run directory: {repo_relative(args.repo, run_dir)}")
    print(f"Branch: {item.get('branch')} (parent: {parent_branch})")
    print(f"Worker prompt: {repo_relative(args.repo, prompt_path)}")
    return 0


def command_worker_prompt(args: argparse.Namespace) -> int:
    try:
        policy, queue, _ = load_state(args.repo)
        item = items_by_id(queue)[args.item_id]
        run_dir = find_latest_run_dir(args.repo, policy, args.item_id)
        prompt = render_worker_prompt(args.repo, policy, queue, item, run_dir)
        if args.out:
            out_path = resolve_repo_path(args.repo, args.out)
            out_path.parent.mkdir(parents=True, exist_ok=True)
            out_path.write_text(prompt, encoding="utf-8")
            print(repo_relative(args.repo, out_path))
        else:
            print(prompt)
    except KeyError:
        print(f"ERROR: Unknown item: {args.item_id}")
        return 1
    except OrchestratorError as exc:
        print(f"ERROR: {exc}")
        return 1
    return 0


def next_round_number(round_dir: Path) -> int:
    round_dir.mkdir(parents=True, exist_ok=True)
    existing = sorted(round_dir.glob("round-*-verifier-prompt.md"))
    if not existing:
        return 1
    last = existing[-1].name.split("-")[1]
    try:
        return int(last) + 1
    except ValueError:
        return len(existing) + 1


def round_number_from_name(path: Path) -> int | None:
    match = re.match(r"round-(\d+)-", path.name)
    if not match:
        return None
    return int(match.group(1))


def next_verifier_result_number(round_dir: Path) -> int:
    round_dir.mkdir(parents=True, exist_ok=True)
    prompt_numbers = {
        number
        for path in round_dir.glob("round-*-verifier-prompt.md")
        if (number := round_number_from_name(path)) is not None
    }
    result_numbers = {
        number
        for path in round_dir.glob("round-*-verifier-result.*")
        if (number := round_number_from_name(path)) is not None
    }
    for number in sorted(prompt_numbers):
        if number not in result_numbers:
            return number
    return max(prompt_numbers | result_numbers, default=0) + 1


def write_verifier_prompt(repo: Path, policy: dict[str, Any], queue: dict[str, Any], item: dict[str, Any], run_dir: Path) -> Path:
    round_dir = run_dir / "verification-rounds"
    number = next_round_number(round_dir)
    prompt = render_verifier_prompt(repo, policy, queue, item, run_dir)
    path = round_dir / f"round-{number:02d}-verifier-prompt.md"
    path.write_text(prompt, encoding="utf-8")
    return path


def command_verifier_prompt(args: argparse.Namespace) -> int:
    try:
        policy, queue, _ = load_state(args.repo)
        item = items_by_id(queue)[args.item_id]
        run_dir = ensure_run_dir(args.repo, policy, args.item_id, args.run_date)
        prompt = render_verifier_prompt(args.repo, policy, queue, item, run_dir)
        if args.out:
            out_path = resolve_repo_path(args.repo, args.out)
            out_path.parent.mkdir(parents=True, exist_ok=True)
            out_path.write_text(prompt, encoding="utf-8")
            print(repo_relative(args.repo, out_path))
        else:
            print(prompt)
    except KeyError:
        print(f"ERROR: Unknown item: {args.item_id}")
        return 1
    except OrchestratorError as exc:
        print(f"ERROR: {exc}")
        return 1
    return 0


def save_transition_artifact(
    repo: Path,
    run_dir: Path,
    event: str,
    result_path: str | None,
    validation_log_path: str | None,
) -> dict[str, str]:
    artifacts: dict[str, str] = {}
    if result_path:
        source = resolve_repo_path(repo, result_path)
        if not source.exists():
            raise OrchestratorError(f"Result path does not exist: {result_path}")
        if event == "worker_done":
            target = run_dir / f"worker-result{source.suffix or '.md'}"
        elif event in {"accepted", "needs_fixes", "blocked"}:
            round_dir = run_dir / "verification-rounds"
            round_dir.mkdir(parents=True, exist_ok=True)
            if source.parent.resolve() == round_dir.resolve() and "verifier-result" in source.name:
                target = source
            else:
                number = next_verifier_result_number(round_dir)
                suffix = source.suffix or ".md"
                target = round_dir / f"round-{number:02d}-verifier-result{suffix}"
        else:
            target = run_dir / f"{event}-result.md"
        if source.resolve() != target.resolve():
            shutil.copyfile(source, target)
        artifacts["result"] = repo_relative(repo, target)
    if validation_log_path:
        source = resolve_repo_path(repo, validation_log_path)
        if not source.exists():
            raise OrchestratorError(f"Validation log path does not exist: {validation_log_path}")
        target = run_dir / "validation.log"
        if source.resolve() != target.resolve():
            shutil.copyfile(source, target)
        artifacts["validation_log"] = repo_relative(repo, target)
    return artifacts


def guard_passes(policy: dict[str, Any], guard: str, manual_guards: list[str]) -> bool:
    if guard == "policy.enabled":
        return bool(policy.get("enabled", False))
    if guard == "policy.pull_requests.allow_creation":
        return nested_get(policy, "pull_requests.allow_creation") is True
    if guard == "policy.pull_requests.creation_not_required":
        return nested_get(policy, "pull_requests.creation_not_required") is True
    if guard == "policy.merge.allow":
        return nested_get(policy, "merge.allow") is True
    manual_guards.append(guard)
    return True


def apply_transition(
    repo: Path,
    item_id: str,
    event: str,
    result_path: str | None = None,
    validation_log_path: str | None = None,
    run_date_text: str | None = None,
    force_guards: bool = False,
) -> dict[str, Any]:
    policy, queue, machine = load_state(repo)
    items = items_by_id(queue)
    item = items[item_id]
    current = item_state(item)
    transition = machine.get("states", {}).get(current, {}).get("on", {}).get(event)
    if not transition:
        events = sorted(machine.get("states", {}).get(current, {}).get("on", {}).keys())
        raise OrchestratorError(
            f"Illegal event {event!r} from {current}. Legal events: {', '.join(events) or '(none)'}"
        )

    manual_guards: list[str] = []
    failed_guards = [
        guard
        for guard in transition.get("guards", [])
        if not guard_passes(policy, str(guard), manual_guards)
    ]
    if failed_guards and not force_guards:
        raise OrchestratorError(f"Guard(s) failed: {', '.join(failed_guards)}")

    target = normalize_state(str(transition["target"]))
    run_dir = ensure_run_dir(repo, policy, item_id, run_date_text)
    run_state = load_run_state(run_dir)
    artifacts = save_transition_artifact(repo, run_dir, event, result_path, validation_log_path)

    generated: dict[str, str] = {}
    if "prompt.write_verifier" in transition.get("actions", []):
        prompt_path = write_verifier_prompt(repo, policy, queue, item, run_dir)
        generated["verifier_prompt"] = repo_relative(repo, prompt_path)
    if "validation.record_not_required" in transition.get("actions", []):
        validation_path = run_dir / "validation.log"
        validation_path.write_text("No repository validation script required for this transition.\n", encoding="utf-8")
        generated["validation_log"] = repo_relative(repo, validation_path)

    set_item_state(item, target)
    write_json(repo / QUEUE_PATH, queue)
    run_state["current_state"] = target
    run_state.setdefault("transition_history", []).append(
        {
            "at": utc_now(),
            "event": event,
            "from": current,
            "to": target,
            "artifacts": artifacts,
            "generated": generated,
            "manual_guards": manual_guards,
        }
    )
    if target in terminal_states(machine):
        run_state["finished_at"] = utc_now()
    save_run_state(run_dir, run_state)
    return {
        "item_id": item_id,
        "from": current,
        "to": target,
        "event": event,
        "artifacts": artifacts,
        "generated": generated,
        "manual_guards": manual_guards,
        "run_dir": run_dir,
    }


def print_transition_result(result: dict[str, Any]) -> None:
    print(f"Transitioned {result['item_id']}: {result['from']} --{result['event']}--> {result['to']}")
    manual_guards = result.get("manual_guards", [])
    if manual_guards:
        print(f"Manual guard(s) acknowledged by operator: {', '.join(manual_guards)}")
    for label, path in {**result.get("artifacts", {}), **result.get("generated", {})}.items():
        print(f"{label}: {path}")


def command_transition(args: argparse.Namespace) -> int:
    try:
        result = apply_transition(
            args.repo,
            args.item_id,
            args.event,
            result_path=args.result,
            validation_log_path=args.validation_log,
            run_date_text=args.run_date,
            force_guards=args.force_guards,
        )
    except KeyError:
        print(f"ERROR: Unknown item: {args.item_id}")
        return 1
    except OrchestratorError as exc:
        print(f"ERROR: {exc}")
        return 1

    print_transition_result(result)
    return 0


def find_codex(codex_bin: str | None) -> str:
    if codex_bin:
        return codex_bin
    try:
        import codex_cli_bin  # type: ignore[import-not-found]

        bundled = Path(codex_cli_bin.__file__).resolve().parent / "bin" / "codex.exe"
        if bundled.exists():
            return str(bundled)
    except Exception:
        pass
    found = shutil.which("codex")
    if found:
        return found
    raise OrchestratorError("codex executable not found on PATH. Install Codex CLI or pass --codex-bin.")


VISIBLE_CODEX_RUNNER = r'''
from __future__ import annotations

import json
import subprocess
import sys
import threading
from pathlib import Path
from typing import Any


def tee_stream(
    stream: Any,
    console: Any,
    stream_log: Any,
    transcript_log: Any,
    transcript_lock: threading.Lock,
    stream_name: str,
) -> None:
    transcript_buffer: list[str] = []

    def flush_transcript_buffer() -> None:
        if not transcript_buffer:
            return
        text = "".join(transcript_buffer)
        transcript_buffer.clear()
        with transcript_lock:
            transcript_log.write(f"[{stream_name}] {text}")
            if not text.endswith("\n"):
                transcript_log.write("\n")
            transcript_log.flush()

    while True:
        chunk = stream.read(1)
        if not chunk:
            break
        console.write(chunk)
        console.flush()
        stream_log.write(chunk)
        stream_log.flush()
        transcript_buffer.append(chunk)
        if chunk == "\n":
            flush_transcript_buffer()
    flush_transcript_buffer()


def main() -> int:
    config = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    command = config["command"]
    repo = config["repo"]
    prompt = Path(config["prompt_path"]).read_text(encoding="utf-8")
    stdout_log = Path(config["stdout_log"])
    stderr_log = Path(config["stderr_log"])
    transcript_log = Path(config["transcript_log"])

    print("SkullbonezCore sub-agent console")
    print(f"Repo: {repo}")
    print(f"Transcript: {transcript_log}")
    print("")

    with (
        stdout_log.open("w", encoding="utf-8") as stdout_file,
        stderr_log.open("w", encoding="utf-8") as stderr_file,
        transcript_log.open("w", encoding="utf-8") as transcript_file,
    ):
        process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            cwd=repo,
            bufsize=1,
        )
        if process.stdout is None or process.stderr is None or process.stdin is None:
            print("codex exec did not expose expected stdio pipes.", file=sys.stderr)
            return 1
        transcript_lock = threading.Lock()
        stdout_thread = threading.Thread(
            target=tee_stream,
            args=(process.stdout, sys.stdout, stdout_file, transcript_file, transcript_lock, "stdout"),
            daemon=True,
        )
        stderr_thread = threading.Thread(
            target=tee_stream,
            args=(process.stderr, sys.stderr, stderr_file, transcript_file, transcript_lock, "stderr"),
            daemon=True,
        )
        stdout_thread.start()
        stderr_thread.start()
        try:
            process.stdin.write(prompt)
            process.stdin.close()
        except BrokenPipeError:
            pass
        returncode = process.wait()
        stdout_thread.join()
        stderr_thread.join()

    print("")
    print(f"Sub-agent exited with code {returncode}.")
    return returncode


if __name__ == "__main__":
    raise SystemExit(main())
'''


def run_codex_exec_visible_console(
    repo: Path,
    prompt: str,
    command: list[str],
    output_path: Path,
    timeout_seconds: int | None,
) -> int:
    stdout_log, stderr_log = codex_exec_log_paths(output_path)
    transcript_log = codex_exec_transcript_path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    app_server_pids_before = snapshot_codex_app_server_pids()
    with tempfile.TemporaryDirectory(prefix="skore-codex-visible-") as temp:
        temp_dir = Path(temp)
        prompt_path = temp_dir / "prompt.txt"
        config_path = temp_dir / "runner-config.json"
        runner_path = temp_dir / "visible_codex_runner.py"
        prompt_path.write_text(prompt, encoding="utf-8")
        runner_path.write_text(VISIBLE_CODEX_RUNNER, encoding="utf-8")
        write_json(
            config_path,
            {
                "command": command,
                "repo": str(repo),
                "prompt_path": str(prompt_path),
                "stdout_log": str(stdout_log),
                "stderr_log": str(stderr_log),
                "transcript_log": str(transcript_log),
            },
        )
        creationflags = getattr(subprocess, "CREATE_NEW_CONSOLE", 0)
        helper = subprocess.Popen(
            [sys.executable, str(runner_path), str(config_path)],
            cwd=repo,
            creationflags=creationflags,
        )
        try:
            return wait_visible_helper_with_live_transcript(helper, transcript_log, repo, timeout_seconds)
        except subprocess.TimeoutExpired as exc:
            kill_process_tree_by_pid(helper.pid)
            cleanup_new_codex_app_servers(app_server_pids_before)
            helper.wait(timeout=10)
            raise OrchestratorError(f"codex exec timed out after {timeout_seconds}s in visible console.") from exc


def run_codex_exec(
    repo: Path,
    prompt: str,
    output_path: Path,
    schema_path: Path | None,
    sandbox: str,
    codex_bin: str | None,
    timeout_seconds: int | None = None,
    visible_console: bool = False,
) -> int:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    command = [
        find_codex(codex_bin),
        "exec",
        "--cd",
        str(repo),
        "--sandbox",
        sandbox,
        "--output-last-message",
        str(output_path),
    ]
    if schema_path:
        command.extend(["--output-schema", str(schema_path)])
    command.append("-")
    if visible_console and os.name == "nt":
        print("Opening visible sub-agent console and mirroring its transcript here...", flush=True)
        print(f"  result: {repo_relative(repo, output_path)}", flush=True)
        print(f"  transcript: {repo_relative(repo, codex_exec_transcript_path(output_path))}", flush=True)
        returncode = run_codex_exec_visible_console(repo, prompt, command, output_path, timeout_seconds)
        if returncode != 0:
            print(f"codex exec exited {returncode}.", flush=True)
            print(f"  stdout: {repo_relative(repo, codex_exec_log_paths(output_path)[0])}", flush=True)
            print(f"  stderr: {repo_relative(repo, codex_exec_log_paths(output_path)[1])}", flush=True)
            print(f"  transcript: {repo_relative(repo, codex_exec_transcript_path(output_path))}", flush=True)
            detail = codex_exec_failure_detail(output_path)
            if detail:
                print("Last codex output:", flush=True)
                for line in detail.splitlines():
                    print(f"  {line}", flush=True)
        return returncode
    stdout_log, stderr_log = codex_exec_log_paths(output_path)
    transcript_log = codex_exec_transcript_path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    app_server_pids_before = snapshot_codex_app_server_pids()
    with (
        stdout_log.open("w", encoding="utf-8") as stdout_file,
        stderr_log.open("w", encoding="utf-8") as stderr_file,
        transcript_log.open("w", encoding="utf-8") as transcript_file,
    ):
        process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            cwd=repo,
            bufsize=1,
        )
        if process.stdout is None or process.stderr is None or process.stdin is None:
            raise OrchestratorError("codex exec did not expose expected stdio pipes.")
        transcript_lock = threading.Lock()
        stdout_thread = threading.Thread(
            target=tee_stream,
            args=(process.stdout, sys.stdout, stdout_file, transcript_file, transcript_lock, "stdout"),
            daemon=True,
        )
        stderr_thread = threading.Thread(
            target=tee_stream,
            args=(process.stderr, sys.stderr, stderr_file, transcript_file, transcript_lock, "stderr"),
            daemon=True,
        )
        stdout_thread.start()
        stderr_thread.start()
        try:
            process.stdin.write(prompt)
            process.stdin.close()
        except BrokenPipeError:
            pass
        try:
            returncode = wait_with_heartbeat(process, "sub-agent", transcript_log, repo, timeout_seconds)
        except subprocess.TimeoutExpired as exc:
            kill_process_tree_by_pid(process.pid)
            cleanup_new_codex_app_servers(app_server_pids_before)
            process.wait(timeout=10)
            stdout_thread.join(timeout=5)
            stderr_thread.join(timeout=5)
            raise OrchestratorError(f"codex exec timed out after {timeout_seconds}s using sandbox {sandbox}.") from exc
        stdout_thread.join()
        stderr_thread.join()
    if returncode != 0:
        print(f"codex exec exited {returncode}.")
        print(f"  stdout: {repo_relative(repo, stdout_log)}")
        print(f"  stderr: {repo_relative(repo, stderr_log)}")
        print(f"  transcript: {repo_relative(repo, transcript_log)}")
        detail = codex_exec_failure_detail(output_path)
        if detail:
            print("Last codex output:")
            for line in detail.splitlines():
                print(f"  {line}")
    return returncode


def run_worker_agent(
    repo: Path,
    item_id: str,
    run_date_text: str | None,
    sandbox: str | None,
    codex_bin: str | None,
    no_schema: bool,
    visible_console: bool | None,
    timeout_seconds: int | None,
) -> tuple[int, Path]:
    policy, queue, _ = load_state(repo)
    item = items_by_id(queue)[item_id]
    run_dir = ensure_run_dir(repo, policy, item_id, run_date_text)
    prompt = render_worker_prompt(repo, policy, queue, item, run_dir)
    schema = None if no_schema else resolve_repo_path(repo, WORKER_SCHEMA)
    output = run_dir / ("worker-result.md" if no_schema else "worker-result.json")
    sandbox_mode = sandbox or default_worker_sandbox(policy)
    use_visible_console = default_visible_console(policy) if visible_console is None else visible_console
    return run_codex_exec(
        repo,
        prompt,
        output,
        schema,
        sandbox_mode,
        codex_bin,
        timeout_seconds=timeout_seconds,
        visible_console=use_visible_console,
    ), output


def run_verifier_agent(
    repo: Path,
    item_id: str,
    run_date_text: str | None,
    sandbox: str | None,
    codex_bin: str | None,
    no_schema: bool,
    require_clean: bool,
    visible_console: bool | None,
    timeout_seconds: int | None,
) -> tuple[int, Path]:
    policy, queue, _ = load_state(repo)
    item = items_by_id(queue)[item_id]
    run_dir = ensure_run_dir(repo, policy, item_id, run_date_text)
    prompt = render_verifier_prompt(repo, policy, queue, item, run_dir)
    round_dir = run_dir / "verification-rounds"
    number = next_verifier_result_number(round_dir)
    schema = None if no_schema else resolve_repo_path(repo, VERIFIER_SCHEMA)
    suffix = ".md" if no_schema else ".json"
    output = round_dir / f"round-{number:02d}-verifier-result{suffix}"
    before = git_status_porcelain(repo)
    sandbox_mode = sandbox or default_verifier_sandbox(policy)
    use_visible_console = default_visible_console(policy) if visible_console is None else visible_console
    code = run_codex_exec(
        repo,
        prompt,
        output,
        schema,
        sandbox_mode,
        codex_bin,
        timeout_seconds=timeout_seconds,
        visible_console=use_visible_console,
    )
    after = git_status_porcelain(repo)
    if require_clean and after != before:
        raise OrchestratorError("Verifier changed the tracked worktree; inspect and revert/commit intentionally.")
    return code, output


def command_run_worker(args: argparse.Namespace) -> int:
    try:
        code, output = run_worker_agent(
            args.repo,
            args.item_id,
            args.run_date,
            args.sandbox,
            args.codex_bin,
            args.no_schema,
            args.visible_console,
            args.timeout_seconds,
        )
    except (KeyError, OrchestratorError) as exc:
        print(f"ERROR: {exc}")
        return 1
    print(f"Worker result: {repo_relative(args.repo, output)}")
    if code != 0 and not args.no_schema:
        detail = codex_exec_failure_detail(output)
        blockers = [f"codex exec returned {code}"]
        if detail:
            blockers.append("Last codex output:\n" + detail)
        ensure_result_payload(
            output,
            {
                "status": "failed",
                "summary": f"codex exec returned {code}.",
                "changed_files": git_changed_files(args.repo),
                "validation": {"commands": [], "result": "not run"},
                "artifacts": codex_exec_artifacts(args.repo, output),
                "timings": [],
                "plain_language_summary": "The implementation worker did not complete.",
                "commit_sha": None,
                "blockers": blockers,
                "risks": [],
            },
            WORKER_RESULT_FIELDS,
        )
    return code


def command_run_verifier(args: argparse.Namespace) -> int:
    try:
        policy, _, _ = load_state(args.repo)
        require_clean = not args.allow_dirty_verifier and nested_get(policy, "verification.requires_clean_worktree") is not False
        code, output = run_verifier_agent(
            args.repo,
            args.item_id,
            args.run_date,
            args.sandbox,
            args.codex_bin,
            args.no_schema,
            require_clean,
            args.visible_console,
            args.timeout_seconds,
        )
    except (KeyError, OrchestratorError) as exc:
        print(f"ERROR: {exc}")
        return 1
    print(f"Verifier result: {repo_relative(args.repo, output)}")
    if code != 0 and not args.no_schema:
        detail = codex_exec_failure_detail(output)
        findings = [f"codex exec returned {code}"]
        if detail:
            findings.append("Last codex output:\n" + detail)
        artifacts = codex_exec_artifacts(args.repo, output)
        ensure_result_payload(
            output,
            {
                "verdict": "blocked",
                "blocking_findings": findings,
                "non_blocking_suggestions": [],
                "missing_evidence": [],
                "validation_assessment": "not assessed",
                "artifact_assessment": "Codex stdout/stderr logs: " + ", ".join(artifacts),
                "feedback_for_worker": "Verifier did not complete.",
                "another_round_required": True,
            },
            VERIFIER_RESULT_FIELDS,
        )
    return code


def codex_exec_smoke(repo: Path, codex: str, sandbox: str, timeout_seconds: int) -> None:
    with tempfile.TemporaryDirectory() as temp:
        output = Path(temp) / "codex-smoke.txt"
        prompt = (
            "This is a SkullbonezCore orchestrator setup smoke test. "
            "Use the shell to run `git status --short --branch` in the current repo. "
            "If the command succeeds, reply with exactly ORCHESTRATOR_CODEX_SMOKE_OK. "
            "If you cannot run the command, do not print that token."
        )
        command = [
            codex,
            "exec",
            "--cd",
            str(repo),
            "--sandbox",
            sandbox,
            "--output-last-message",
            str(output),
            "-",
        ]
        try:
            result = subprocess.run(
                command,
                input=prompt,
                cwd=repo,
                check=False,
                text=True,
                capture_output=True,
                timeout=timeout_seconds,
            )
        except subprocess.TimeoutExpired as exc:
            raise OrchestratorError(f"codex exec smoke timed out after {timeout_seconds}s for {sandbox}.") from exc
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip()
            raise OrchestratorError(f"codex exec smoke failed for {sandbox}: {detail}")
        text = output.read_text(encoding="utf-8", errors="ignore") if output.exists() else ""
        if "ORCHESTRATOR_CODEX_SMOKE_OK" not in text:
            raise OrchestratorError(f"codex exec smoke did not confirm shell access for {sandbox}.")


def command_doctor(args: argparse.Namespace) -> int:
    errors, warnings = validate_config(args.repo, quiet=True)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    for warning in warnings:
        print(f"WARNING: {warning}")

    try:
        codex = find_codex(args.codex_bin)
    except OrchestratorError as exc:
        print(f"ERROR: {exc}")
        print("Install with: python -m pip install --user openai-codex")
        return 1

    version = subprocess.run([codex, "--version"], cwd=args.repo, check=False, capture_output=True, text=True)
    if version.returncode != 0:
        print(f"ERROR: codex --version failed for {codex}")
        print((version.stderr or version.stdout).strip())
        return version.returncode or 1

    exec_help = subprocess.run([codex, "exec", "--help"], cwd=args.repo, check=False, capture_output=True, text=True)
    if exec_help.returncode != 0:
        print(f"ERROR: codex exec --help failed for {codex}")
        print((exec_help.stderr or exec_help.stdout).strip())
        return exec_help.returncode or 1

    policy, _, _ = load_state(args.repo)
    if not args.skip_codex_smoke:
        sandboxes = sorted({default_worker_sandbox(policy), default_verifier_sandbox(policy)})
        for sandbox in sandboxes:
            try:
                codex_exec_smoke(args.repo, codex, sandbox, args.smoke_timeout)
            except OrchestratorError as exc:
                print(f"ERROR: {exc}")
                print("Run `codex login` if this is an authentication failure.")
                return 1
            print(f"PASS: codex exec smoke passed for sandbox {sandbox}.")

    print("PASS: orchestrator config is valid.")
    print(f"PASS: codex executable: {codex}")
    print(f"PASS: {(version.stdout or version.stderr).strip()}")
    print("PASS: codex exec is available for worker and verifier runs.")
    return 0


REPORT_RE = re.compile(r"^Agentic/Reports/\d{4}-\d{2}-\d{2}/[^/]+/report\.md$")
IMAGE_RE = re.compile(r"^Agentic/Reports/\d{4}-\d{2}-\d{2}/[^/]+/images/[^/]+\.(?:png|jpg|jpeg)$", re.I)
MARKDOWN_IMAGE_RE = re.compile(r"!\[[^\]]*\]\(([^)]+)\)")


def files_from_git(repo: Path, mode: str, rev: str) -> list[str]:
    if mode == "commit":
        command = ["git", "show", "--name-only", "--format=", rev]
    else:
        command = ["git", "diff", "--name-only", rev]
    result = subprocess.run(command, cwd=repo, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise OrchestratorError(result.stderr.strip() or f"git {mode} failed")
    return [line.strip().replace("\\", "/") for line in result.stdout.splitlines() if line.strip()]


def read_report_text_for_check(repo: Path, path: str, commit: str | None) -> str:
    if commit:
        result = subprocess.run(
            ["git", "show", f"{commit}:{path}"],
            cwd=repo,
            check=False,
            capture_output=True,
            text=False,
        )
        if result.returncode != 0:
            detail = decode_text_bytes(result.stderr).strip()
            raise OrchestratorError(detail or f"Unable to read {path} from {commit}.")
        return decode_text_bytes(result.stdout).replace("\x00", "")
    report_path = resolve_repo_path(repo, path)
    return read_text_file(report_path)


def command_report_check(args: argparse.Namespace) -> int:
    try:
        files = [path.replace("\\", "/") for path in args.files]
        if args.commit:
            files.extend(files_from_git(args.repo, "commit", args.commit))
        if args.diff_range:
            files.extend(files_from_git(args.repo, "range", args.diff_range))
        files = sorted(set(files))
        if not files:
            raise OrchestratorError("No files supplied for report check.")

        report_files = [path for path in files if REPORT_RE.match(path)]
        image_files = [path for path in files if IMAGE_RE.match(path)]
        bad_files = [path for path in files if path not in report_files and path not in image_files]
        if bad_files:
            raise OrchestratorError("Report-only commit contains non-report files: " + ", ".join(bad_files))
        if len(report_files) != 1:
            raise OrchestratorError("Report-only check requires exactly one report.md file.")

        report_text = read_report_text_for_check(args.repo, report_files[0], args.commit)
        if "\x00" in report_text:
            raise OrchestratorError(f"Report contains embedded NUL bytes: {report_files[0]}")
        for image in image_files:
            image_name = Path(image).name
            if f"images/{image_name}" not in report_text:
                raise OrchestratorError(f"Committed image is not referenced by report.md: {image}")
    except OrchestratorError as exc:
        print(f"ERROR: {exc}")
        return 1

    print(f"PASS: report-only file list is valid ({len(files)} file(s)).")
    return 0


def referenced_report_files(repo: Path, report_path: Path) -> list[Path]:
    report_text = read_text_file(report_path)
    report_dir = report_path.parent
    files = [report_path]
    for match in MARKDOWN_IMAGE_RE.finditer(report_text):
        target = match.group(1).strip()
        if "://" in target or target.startswith("#"):
            continue
        target = target.strip("<>")
        if not target.lower().startswith("images/"):
            continue
        image_path = (report_dir / target).resolve()
        try:
            image_path.relative_to(repo.resolve())
        except ValueError as exc:
            raise OrchestratorError(f"Report image escapes repository: {target}") from exc
        if not image_path.exists():
            raise OrchestratorError(f"Report references missing image: {target}")
        files.append(image_path)
    return sorted(set(files))


def command_archive_plan(args: argparse.Namespace) -> int:
    try:
        policy, queue, machine = load_state(args.repo)
        items = items_by_id(queue)
        item = items[args.item_id]
        state = item_state(item)
        state_kind = machine.get("states", {}).get(state, {}).get("kind")
        if state_kind != "terminal_success" and not args.force:
            raise OrchestratorError(f"{args.item_id} is {state}; archive only terminal success items.")
        plan_path = str(item.get("plan", ""))
        plan_posix = plan_path.replace("\\", "/")
        if not plan_posix.startswith("Agentic/Plans/") or plan_posix.startswith("Agentic/Plans/Done/"):
            raise OrchestratorError(f"{args.item_id}: plan is not an active Agentic/Plans file.")
        source = resolve_repo_path(args.repo, plan_path)
        target = args.repo / "Agentic" / "Plans" / "Done" / source.name
        if target.exists():
            raise OrchestratorError(f"Archive target already exists: {repo_relative(args.repo, target)}")
        result = run_git(args.repo, ["mv", str(source), str(target)])
        if result.returncode != 0:
            raise OrchestratorError(result.stderr.strip() or "git mv failed.")
        item["plan"] = repo_relative(args.repo, target).replace("\\", "/")
        item.pop("archive_deferred", None)
        write_json(args.repo / QUEUE_PATH, queue)
        run_dir = find_latest_run_dir(args.repo, policy, args.item_id)
        if run_dir:
            run_state = load_run_state(run_dir)
            run_state["archived_plan_path"] = item["plan"]
            save_run_state(run_dir, run_state)
    except KeyError:
        print(f"ERROR: Unknown item: {args.item_id}")
        return 1
    except OrchestratorError as exc:
        print(f"ERROR: {exc}")
        return 1
    print(f"Archived plan: {plan_path} -> {item['plan']}")
    return 0


def read_optional_text(path: Path) -> str:
    if path.exists():
        return read_text_file(path).strip()
    return ""


def read_optional_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    try:
        payload = json.loads(read_text_file(path))
    except json.JSONDecodeError:
        return {}
    return payload if isinstance(payload, dict) else {}


def read_worker_result(run_dir: Path) -> str:
    payload = read_optional_json(run_dir / "worker-result.json")
    if payload:
        summary = prose_value(payload.get("summary", ""))
        plain = prose_value(payload.get("plain_language_summary", ""))
        return "\n\n".join(part for part in (plain, summary) if part)
    json_text = read_optional_text(run_dir / "worker-result.json")
    if json_text:
        return json_text
    return read_optional_text(run_dir / "worker-result.md")


def markdown_list(values: list[Any]) -> str:
    lines = [f"- `{str(value)}`" for value in values if str(value).strip()]
    return "\n".join(lines) if lines else "- None recorded."


def prose_list(values: list[Any]) -> str:
    lines = [f"- {str(value)}" for value in values if str(value).strip()]
    return "\n".join(lines) if lines else "- None recorded."


def prose_value(value: Any) -> str:
    if isinstance(value, list):
        return prose_list(value)
    if isinstance(value, dict):
        lines = [f"- {key}: {entry_value}" for key, entry_value in value.items() if str(entry_value).strip()]
        return "\n".join(lines)
    return str(value).strip()


def validation_details_from_payload(validation_payload: Any) -> tuple[list[str], str, list[str]]:
    if isinstance(validation_payload, dict):
        commands = validation_payload.get("commands", [])
        if not isinstance(commands, list):
            commands = [commands]
        return [str(command) for command in commands if str(command).strip()], str(validation_payload.get("result", "")).strip(), []

    if isinstance(validation_payload, list):
        commands: list[str] = []
        summaries: list[str] = []
        timings: list[str] = []
        for entry in validation_payload:
            if not isinstance(entry, dict):
                continue
            command = str(entry.get("command", "")).strip()
            result = str(entry.get("result", "")).strip()
            elapsed = entry.get("elapsed_seconds")
            log = str(entry.get("log", "")).strip()
            notes = str(entry.get("notes", "")).strip()
            if command:
                commands.append(command)

            parts = []
            if command:
                parts.append(f"`{command}`")
            if result:
                parts.append(result)
            if elapsed is not None:
                parts.append(f"{elapsed}s")
            if log:
                parts.append(f"log `{log}`")
            line = " - ".join(parts)
            if notes:
                line = f"{line}: {notes}" if line else notes
            if line:
                summaries.append(line)
            if command and elapsed is not None:
                timings.append(f"{command}: {elapsed}s")
        return commands, prose_list(summaries), timings

    return [], "", []


def validation_log_excerpt(text: str) -> str:
    interesting = (
        "VALIDATE_",
        "[1/",
        "[2/",
        "[3/",
        "[4/",
        "[5/",
        "[6/",
        "[7/",
        "PASS:",
        "WARNING:",
        "ERROR:",
        "DX12 validation",
        "DX12 baseline",
        "avg_diff=",
        "InfoQueue",
        "ALL PASSED",
        "COMPLETE",
        "EXIT_CODE=",
        "ELAPSED_SECONDS=",
    )
    lines = [line.rstrip() for line in text.splitlines()]
    filtered = [line for line in lines if any(token in line for token in interesting)]
    if not filtered:
        filtered = lines[-80:]
    if len(filtered) > 120:
        filtered = filtered[:60] + ["... omitted ..."] + filtered[-60:]
    excerpt = "\n".join(filtered).strip()
    return excerpt[:12000] + "\n... truncated ..." if len(excerpt) > 12000 else excerpt


def read_result_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise OrchestratorError(f"Missing result file: {display_path(path)}") from exc
    except json.JSONDecodeError as exc:
        raise OrchestratorError(f"Invalid result JSON {display_path(path)}:{exc.lineno}: {exc.msg}") from exc


def latest_verifier_result_path(run_dir: Path) -> Path | None:
    round_dir = run_dir / "verification-rounds"
    if not round_dir.exists():
        return None
    results = sorted(round_dir.glob("round-*-verifier-result.json"))
    return results[-1] if results else None


def validation_not_required(item: dict[str, Any]) -> bool:
    gate = str(item.get("validation_gate", "")).strip().lower()
    return gate in {"", "none", "no validation required", "n/a", "not required"}


def run_validation_gate(repo: Path, item: dict[str, Any], run_dir: Path) -> tuple[str, Path]:
    command_text = str(item.get("validation_gate", "")).strip()
    log_path = run_dir / "validation.log"
    if validation_not_required(item):
        log_path.write_text("No repository validation script required for this item.\n", encoding="utf-8")
        return "not_required", log_path
    if not command_text:
        raise OrchestratorError("Validation gate is empty; cannot run validation.")

    started = utc_now()
    started_monotonic = time.monotonic()
    stdout_path = run_dir / "validation.stdout.tmp"
    stderr_path = run_dir / "validation.stderr.tmp"
    print(f"Running validation gate: {command_text}", flush=True)
    print(f"  log: {repo_relative(repo, log_path)}", flush=True)
    with (
        stdout_path.open("w", encoding="utf-8") as stdout_file,
        stderr_path.open("w", encoding="utf-8") as stderr_file,
    ):
        process = subprocess.Popen(
            command_text,
            cwd=repo,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        if process.stdout is None or process.stderr is None:
            raise OrchestratorError("Validation command did not expose expected stdio pipes.")
        stdout_thread = threading.Thread(
            target=tee_stream_to_file,
            args=(process.stdout, sys.stdout, stdout_file),
            daemon=True,
        )
        stderr_thread = threading.Thread(
            target=tee_stream_to_file,
            args=(process.stderr, sys.stderr, stderr_file),
            daemon=True,
        )
        stdout_thread.start()
        stderr_thread.start()
        returncode = wait_with_heartbeat(process, "validation gate", stdout_path, repo, None)
        stdout_thread.join()
        stderr_thread.join()
    finished = utc_now()
    stdout_text = read_text_file(stdout_path) if stdout_path.exists() else ""
    stderr_text = read_text_file(stderr_path) if stderr_path.exists() else ""
    log_path.write_text(
        "\n".join(
            [
                f"command: {command_text}",
                f"started_at: {started}",
                f"finished_at: {finished}",
                f"exit_code: {returncode}",
                "",
                "stdout:",
                stdout_text,
                "",
                "stderr:",
                stderr_text,
            ]
        ),
        encoding="utf-8",
    )
    for temp_path in (stdout_path, stderr_path):
        try:
            temp_path.unlink()
        except FileNotFoundError:
            pass
    print(f"Validation gate exited {returncode} after {elapsed_label(time.monotonic() - started_monotonic)}.", flush=True)
    return ("passed" if returncode == 0 else "failed"), log_path


def latest_commit(repo: Path) -> str:
    result = run_git(repo, ["rev-parse", "--short", "HEAD"])
    return result.stdout.strip() if result.returncode == 0 else ""


def git_has_staged_changes(repo: Path) -> bool:
    result = run_git(repo, ["diff", "--cached", "--quiet"])
    return result.returncode == 1


def commit_paths(repo: Path, paths: list[Path], message: str, allow_main: bool) -> str | None:
    branch = current_branch(repo)
    if branch == "main" and not allow_main:
        raise OrchestratorError("Refusing to commit on main from orchestrator automation.")
    rel_paths = [repo_relative(repo, path) for path in paths if path.exists()]
    if not rel_paths:
        return None
    add = run_git(repo, ["add", *rel_paths])
    if add.returncode != 0:
        raise OrchestratorError(add.stderr.strip() or "git add failed.")
    if not git_has_staged_changes(repo):
        return None
    commit = run_git(repo, ["commit", "-m", message])
    if commit.returncode != 0:
        raise OrchestratorError(commit.stderr.strip() or commit.stdout.strip() or "git commit failed.")
    return latest_commit(repo)


def command_report_draft(args: argparse.Namespace) -> int:
    try:
        policy, queue, _ = load_state(args.repo)
        item = items_by_id(queue)[args.item_id]
        run_dir = ensure_run_dir(args.repo, policy, args.item_id, args.run_date)
        run_state = load_run_state(run_dir)
        report_root = resolve_repo_path(
            args.repo,
            nested_get(policy, "artifact_retention.report_root") or "Agentic/Reports",
        )
        report_dir = report_root / run_dir.parent.name / args.item_id
        image_dir = report_dir / "images"
        image_dir.mkdir(parents=True, exist_ok=True)
        report_path = report_dir / "report.md"
        template = resolve_repo_path(args.repo, REPORT_TEMPLATE).read_text(encoding="utf-8")
        worker_payload = read_optional_json(run_dir / "worker-result.json")
        worker_result = read_worker_result(run_dir)
        validation = read_optional_text(run_dir / "validation.log")
        verifier_path = latest_verifier_result_path(run_dir)
        verifier_payload = read_optional_json(verifier_path) if verifier_path else {}
        validation_commands, validation_summary, validation_timings = validation_details_from_payload(worker_payload.get("validation", {}))
        validation_excerpt = validation_log_excerpt(validation) if validation else ""
        validation_result = validation_summary
        if validation_excerpt and validation_excerpt not in validation_result:
            validation_result = "\n\n".join(part for part in (validation_summary, "Validation log excerpt:\n" + validation_excerpt) if part)
        changed_files = worker_payload.get("changed_files", [])
        if not isinstance(changed_files, list):
            changed_files = []
        artifacts = worker_payload.get("artifacts", [])
        if not isinstance(artifacts, list):
            artifacts = []
        timings = worker_payload.get("timings", [])
        if not isinstance(timings, list):
            timings = []
        if not timings and validation_timings:
            timings = validation_timings
        risks = worker_payload.get("risks", [])
        if not isinstance(risks, list):
            risks = []
        commit_sha = str(worker_payload.get("commit_sha") or worker_payload.get("implementation_commit") or latest_commit(args.repo))
        transitions = run_state.get("transition_history", [])
        timeline = "\n".join(
            f"- {entry.get('at', '')}: `{entry.get('event', '')}` "
            f"{entry.get('from', '')} -> {entry.get('to', '')}"
            for entry in transitions
        )
        values = {
            "item_id": args.item_id,
            "layman_summary": worker_result or "Report drafted before worker summary was captured.",
            "plan_path": item.get("plan", ""),
            "archived_plan_path": run_state.get("archived_plan_path") or item.get("plan", ""),
            "branch": item.get("branch", ""),
            "commit_sha": commit_sha,
            "report_commit_sha": "pending",
            "report_web_url": "pending until report-only commit is pushed",
            "pr_link": "",
            "merge_sha": "",
            "final_state": item_state(item),
            "queue_state": item_state(item),
            "queue_state_commit_sha": "pending",
            "started_at": run_state.get("started_at", ""),
            "finished_at": run_state.get("finished_at") or "",
            "elapsed": "pending",
            "progress_timeline": timeline,
            "timings": prose_list(timings),
            "implementation_summary": worker_result or "Pending worker summary.",
            "changed_files": markdown_list(changed_files),
            "validation_gate": item.get("validation_gate", ""),
            "validation_commands": "\n".join(str(command) for command in validation_commands) or item.get("validation_gate", ""),
            "validation_result": validation_result or "Pending validation evidence.",
            "verification_loop": "See `verification-rounds/` under the run directory.",
            "artifacts": "\n".join([f"- Run directory: `{repo_relative(args.repo, run_dir)}`", *[f"- `{artifact}`" for artifact in artifacts]]),
            "code_snippets": "Pending final report curation.",
            "pr_status": "Pending.",
            "merge_status": "Not permitted unless AGENTS.md and policy allow it.",
            "conflicts": "None recorded.",
            "residual_risk": prose_list(risks),
            "sub_agent_summary": worker_result or "Pending worker result.",
            "verifier_summary": str(verifier_payload.get("feedback_for_worker") or verifier_payload.get("validation_assessment") or "Pending verifier result."),
            "next_queue_action": "Pending terminal transition.",
        }
        report_path.write_text(render_template_text(template, values), encoding="utf-8")
    except KeyError:
        print(f"ERROR: Unknown item: {args.item_id}")
        return 1
    except OrchestratorError as exc:
        print(f"ERROR: {exc}")
        return 1
    print(f"Draft report: {repo_relative(args.repo, report_path)}")
    return 0


def transition_and_print(
    repo: Path,
    item_id: str,
    event: str,
    result_path: Path | None = None,
    validation_log_path: Path | None = None,
    run_date_text: str | None = None,
    force_guards: bool = False,
) -> dict[str, Any]:
    result = apply_transition(
        repo,
        item_id,
        event,
        result_path=str(result_path) if result_path else None,
        validation_log_path=str(validation_log_path) if validation_log_path else None,
        run_date_text=run_date_text,
        force_guards=force_guards,
    )
    print_transition_result(result)
    return result


def command_run_loop(args: argparse.Namespace) -> int:
    try:
        policy, queue, machine = load_state(args.repo)
        errors, _ = validate_config(args.repo, quiet=True)
        if errors:
            raise OrchestratorError("; ".join(errors))
        item = items_by_id(queue).get(args.item_id) if args.item_id else select_next_item(
            policy,
            queue,
            machine,
            args.allow_disabled,
        )
        if item is None:
            raise OrchestratorError("No ready item selected.")
        item_id = str(item["id"])

        if item_state(item) == "ready":
            start_args = argparse.Namespace(
                repo=args.repo,
                item_id=item_id,
                allow_disabled=args.allow_disabled,
                allow_dirty=args.allow_dirty,
                no_branch=False,
                run_date=args.run_date,
            )
            code = command_start(start_args)
            if code != 0:
                return code

        verifier_rounds = 0
        while True:
            policy, queue, _ = load_state(args.repo)
            item = items_by_id(queue)[item_id]
            state = item_state(item)
            run_dir = ensure_run_dir(args.repo, policy, item_id, args.run_date)

            if state == "running":
                code, worker_output = run_worker_agent(
                    args.repo,
                    item_id,
                    args.run_date,
                    args.worker_sandbox,
                    args.codex_bin,
                    no_schema=False,
                    visible_console=args.visible_console,
                    timeout_seconds=args.worker_timeout_seconds,
                )
                print(f"Worker result: {repo_relative(args.repo, worker_output)}")
                if code != 0:
                    detail = codex_exec_failure_detail(worker_output)
                    blockers = [f"codex exec returned {code}"]
                    if detail:
                        blockers.append("Last codex output:\n" + detail)
                    ensure_result_payload(
                        worker_output,
                        {
                            "status": "failed",
                            "summary": f"codex exec returned {code}.",
                            "changed_files": git_changed_files(args.repo),
                            "validation": {"commands": [], "result": "not run"},
                            "artifacts": codex_exec_artifacts(args.repo, worker_output),
                            "timings": [],
                            "plain_language_summary": "The implementation worker did not complete.",
                            "commit_sha": None,
                            "blockers": blockers,
                            "risks": [],
                        },
                        WORKER_RESULT_FIELDS,
                    )
                    transition_and_print(args.repo, item_id, "worker_failed", worker_output, run_date_text=args.run_date)
                    return code or 1
                worker_result = read_result_json(worker_output)
                status = str(worker_result.get("status", "failed"))
                if status == "completed":
                    transition_and_print(args.repo, item_id, "worker_done", worker_output, run_date_text=args.run_date)
                    transition_and_print(args.repo, item_id, "review_ready", run_date_text=args.run_date)
                    continue
                if status == "blocked":
                    transition_and_print(args.repo, item_id, "worker_blocked", worker_output, run_date_text=args.run_date)
                    return 2
                transition_and_print(args.repo, item_id, "worker_failed", worker_output, run_date_text=args.run_date)
                return 1

            if state == "verifying":
                if verifier_rounds >= args.max_verifier_rounds:
                    raise OrchestratorError(f"Verifier exceeded max rounds: {args.max_verifier_rounds}")
                verifier_rounds += 1
                require_clean = nested_get(policy, "verification.requires_clean_worktree") is not False
                code, verifier_output = run_verifier_agent(
                    args.repo,
                    item_id,
                    args.run_date,
                    args.verifier_sandbox,
                    args.codex_bin,
                    no_schema=False,
                    require_clean=require_clean,
                    visible_console=args.visible_console,
                    timeout_seconds=args.verifier_timeout_seconds,
                )
                print(f"Verifier result: {repo_relative(args.repo, verifier_output)}")
                if code != 0:
                    detail = codex_exec_failure_detail(verifier_output)
                    findings = [f"codex exec returned {code}"]
                    if detail:
                        findings.append("Last codex output:\n" + detail)
                    ensure_result_payload(
                        verifier_output,
                        {
                            "verdict": "blocked",
                            "blocking_findings": findings,
                            "non_blocking_suggestions": [],
                            "missing_evidence": [],
                            "validation_assessment": "not assessed",
                            "artifact_assessment": "Codex stdout/stderr logs: "
                            + ", ".join(codex_exec_artifacts(args.repo, verifier_output)),
                            "feedback_for_worker": "Verifier did not complete.",
                            "another_round_required": True,
                        },
                        VERIFIER_RESULT_FIELDS,
                    )
                    transition_and_print(args.repo, item_id, "blocked", verifier_output, run_date_text=args.run_date)
                    return code or 1
                verifier_result = read_result_json(verifier_output)
                verdict = str(verifier_result.get("verdict", "blocked"))
                if verdict == "accepted":
                    transition_and_print(args.repo, item_id, "accepted", verifier_output, run_date_text=args.run_date)
                    continue
                if verdict == "needs_fixes":
                    transition_and_print(args.repo, item_id, "needs_fixes", verifier_output, run_date_text=args.run_date)
                    continue
                transition_and_print(args.repo, item_id, "blocked", verifier_output, run_date_text=args.run_date)
                return 2

            if state == "validating":
                if args.skip_validation:
                    print(f"Validation pending for {item_id}; rerun with validation enabled or transition manually.")
                    return 2
                event, log_path = run_validation_gate(args.repo, item, run_dir)
                transition_and_print(args.repo, item_id, event, validation_log_path=log_path, run_date_text=args.run_date)
                continue

            if state == "reporting":
                if args.finalize:
                    finalize_args = argparse.Namespace(
                        repo=args.repo,
                        item_id=item_id,
                        run_date=args.run_date,
                        commit=args.commit_finalize,
                        allow_main_commit=args.allow_main_commit,
                        archive_deferred=args.archive_deferred,
                    )
                    return command_finalize(finalize_args)
                print(f"{item_id} is ready for finalization.")
                return 0

            print(f"{item_id} stopped in state {state}.")
            if state in terminal_failure_states(machine):
                return 2
            return 0 if state in terminal_states(machine) else 2
    except (KeyError, OrchestratorError) as exc:
        print(f"ERROR: {exc}")
        return 1


def command_finalize(args: argparse.Namespace) -> int:
    try:
        policy, queue, _ = load_state(args.repo)
        item = items_by_id(queue)[args.item_id]
        state = item_state(item)
        if state == "reporting":
            transition_and_print(args.repo, args.item_id, "report_committed_no_pr", run_date_text=args.run_date)
            policy, queue, _ = load_state(args.repo)
            item = items_by_id(queue)[args.item_id]
        elif state not in {"done", "pr_open", "merged", "blocked", "failed", "skipped"}:
            raise OrchestratorError(f"{args.item_id} is {state}; finalize requires reporting or terminal state.")

        plan_paths: list[Path] = [args.repo / QUEUE_PATH]
        plan_path = str(item.get("plan", ""))
        plan_posix = plan_path.replace("\\", "/")
        successful = item_state(item) in {"done", "pr_open", "merged"}
        if successful and plan_posix.startswith("Agentic/Plans/") and not plan_posix.startswith("Agentic/Plans/Done/"):
            if "archive_deferred" in item and not args.archive_deferred:
                print(f"Plan archive deferred for {args.item_id}: {item['archive_deferred'].get('reason', '')}")
            else:
                archive_args = argparse.Namespace(repo=args.repo, item_id=args.item_id, force=False)
                code = command_archive_plan(archive_args)
                if code != 0:
                    return code
                _, queue, _ = load_state(args.repo)
                item = items_by_id(queue)[args.item_id]
                plan_paths.append(resolve_repo_path(args.repo, item["plan"]))

        state_commit = None
        if args.commit:
            state_commit = commit_paths(
                args.repo,
                plan_paths,
                f"docs: finalize {args.item_id} orchestration state",
                args.allow_main_commit,
            )
            if state_commit:
                print(f"State commit: {state_commit}")

        report_args = argparse.Namespace(repo=args.repo, item_id=args.item_id, run_date=args.run_date)
        code = command_report_draft(report_args)
        if code != 0:
            return code
        run_dir = ensure_run_dir(args.repo, policy, args.item_id, args.run_date)
        report_root = resolve_repo_path(
            args.repo,
            nested_get(policy, "artifact_retention.report_root") or "Agentic/Reports",
        )
        report_dir = report_root / run_dir.parent.name / args.item_id
        report_path = report_dir / "report.md"
        report_files = referenced_report_files(args.repo, report_path)
        check_args = argparse.Namespace(
            repo=args.repo,
            files=[repo_relative(args.repo, path).replace("\\", "/") for path in report_files],
            commit=None,
            diff_range=None,
        )
        code = command_report_check(check_args)
        if code != 0:
            return code
        if args.commit:
            report_commit = commit_paths(
                args.repo,
                report_files,
                f"docs: add {args.item_id} orchestration report",
                args.allow_main_commit,
            )
            if report_commit:
                print(f"Report commit: {report_commit}")
                verify_args = argparse.Namespace(repo=args.repo, files=[], commit="HEAD", diff_range=None)
                return command_report_check(verify_args)
        print(f"Report path: {repo_relative(args.repo, report_path)}")
        return 0
    except (KeyError, OrchestratorError) as exc:
        print(f"ERROR: {exc}")
        return 1


def command_self_test(args: argparse.Namespace) -> int:
    with tempfile.TemporaryDirectory() as temp:
        repo = Path(temp)
        (repo / ORCH_DIR / "machines").mkdir(parents=True)
        (repo / "Agentic" / "Plans" / "Done").mkdir(parents=True)
        (repo / "Agentic" / "Plans" / "Done" / "done.md").write_text("# Done\n", encoding="utf-8")
        (repo / "Agentic" / "Plans" / "active.md").write_text("# Active\n", encoding="utf-8")
        (repo / "Agentic" / "Plans" / "parallel.md").write_text("# Parallel\n", encoding="utf-8")
        (repo / "Agentic" / "Plans" / "conflict.md").write_text("# Conflict\n", encoding="utf-8")
        (repo / "AGENTS.md").write_text("merge requires explicit user request\n", encoding="utf-8")
        machine = load_json(args.repo / DEFAULT_MACHINE_PATH)
        write_json(repo / DEFAULT_MACHINE_PATH, machine)
        write_json(
            repo / POLICY_PATH,
            {
                "schema_version": 1,
                "enabled": True,
                "max_active_items": 2,
                "parallelism": {
                    "enabled": True,
                    "max_workers_by_area": {"docs": 1, "verification": 1},
                    "exclusive_globs": ["Agentic/Orchestrator/*"],
                },
                "merge": {"allow": False},
                "artifact_retention": {"run_root": "Agentic/Runs"},
            },
        )
        write_json(
            repo / QUEUE_PATH,
            {
                "schema_version": 1,
                "machine": str(DEFAULT_MACHINE_PATH).replace("\\", "/"),
                "states": list(machine["states"].keys()),
                "items": [
                    {
                        "id": "done",
                        "plan": "Agentic/Plans/Done/done.md",
                        "state": "done",
                        "priority": 1,
                        "branch": "codex/done",
                        "impact_area": ["docs"],
                        "validation_gate": "none",
                        "depends_on": [],
                    },
                    {
                        "id": "active",
                        "plan": "Agentic/Plans/active.md",
                        "state": "ready",
                        "priority": 2,
                        "branch": "codex/active",
                        "impact_area": ["docs"],
                        "validation_gate": "none",
                        "owned_globs": ["Agentic/Plans/active.md"],
                        "depends_on": ["done"],
                    },
                    {
                        "id": "parallel",
                        "plan": "Agentic/Plans/parallel.md",
                        "state": "ready",
                        "priority": 3,
                        "branch": "codex/parallel",
                        "impact_area": ["verification"],
                        "validation_gate": "none",
                        "owned_globs": ["Agentic/Plans/parallel.md"],
                        "depends_on": ["done"],
                    },
                    {
                        "id": "conflict",
                        "plan": "Agentic/Plans/conflict.md",
                        "state": "ready",
                        "priority": 4,
                        "branch": "codex/conflict",
                        "impact_area": ["docs"],
                        "validation_gate": "none",
                        "owned_globs": ["Agentic/Plans/active.md"],
                        "depends_on": ["done"],
                    },
                ],
            },
        )
        errors, _ = validate_config(repo, quiet=True)
        if errors:
            for error in errors:
                print(f"ERROR: self-test config: {error}")
            return 1
        run_dir = item_run_dir(repo, load_json(repo / POLICY_PATH), "active", "2026-06-16")
        (run_dir / "verification-rounds").mkdir(parents=True)
        verifier_result = run_dir / "verification-rounds" / "round-01-verifier-result.json"
        verifier_result.write_text(
            json.dumps(
                {
                    "verdict": "accepted",
                    "blocking_findings": [],
                    "non_blocking_suggestions": [],
                    "validation_assessment": "ok",
                    "artifact_assessment": "ok",
                    "feedback_for_worker": "ok",
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        artifacts = save_transition_artifact(repo, run_dir, "accepted", str(verifier_result), None)
        if artifacts.get("result", "").replace("\\", "/") != "Agentic/Runs/2026-06-16/active/verification-rounds/round-01-verifier-result.json":
            print("ERROR: self-test verifier result artifact was renumbered.")
            return 1
        if (run_dir / "verification-rounds" / "round-02-verifier-result.json").exists():
            print("ERROR: self-test duplicated verifier result into a new round.")
            return 1
        policy, queue, machine = load_state(repo)
        item = select_next_item(policy, queue, machine, allow_disabled=False)
        if item is None or item["id"] != "active":
            print("ERROR: self-test next item selection failed.")
            return 1
        set_item_state(items_by_id(queue)["active"], "running")
        item = select_next_item(policy, queue, machine, allow_disabled=False)
        if item is None or item["id"] != "parallel":
            print("ERROR: self-test parallel item selection failed.")
            return 1
        conflict_errors = start_guard_errors(policy, queue, machine, items_by_id(queue)["conflict"])
        if not conflict_errors:
            print("ERROR: self-test overlapping owned_globs did not block parallel start.")
            return 1

        report_dir = repo / "Agentic" / "Reports" / "2026-06-16" / "item"
        image_dir = report_dir / "images"
        image_dir.mkdir(parents=True)
        (report_dir / "report.md").write_text("![Image](images/a.png)\n", encoding="utf-8")
        (image_dir / "a.png").write_text("fake", encoding="utf-8")
        (image_dir / "unreferenced.png").write_text("fake", encoding="utf-8")
        referenced = [repo_relative(repo, path).replace("\\", "/") for path in referenced_report_files(repo, report_dir / "report.md")]
        if "Agentic/Reports/2026-06-16/item/images/unreferenced.png" in referenced:
            print("ERROR: self-test included an unreferenced report image.")
            return 1
        args.repo = repo
        args.files = referenced
        args.commit = None
        args.diff_range = None
        if command_report_check(args) != 0:
            return 1
        init = run_git(repo, ["init"])
        if init.returncode != 0:
            print(init.stderr.strip() or "ERROR: self-test git init failed.")
            return 1
        for key, value in {"user.email": "orchestrator@example.invalid", "user.name": "Orchestrator Self Test"}.items():
            config = run_git(repo, ["config", key, value])
            if config.returncode != 0:
                print(config.stderr.strip() or f"ERROR: self-test git config {key} failed.")
                return 1
        add = run_git(repo, ["add", "Agentic/Reports/2026-06-16/item/report.md", "Agentic/Reports/2026-06-16/item/images/a.png"])
        if add.returncode != 0:
            print(add.stderr.strip() or "ERROR: self-test git add failed.")
            return 1
        commit = run_git(repo, ["commit", "-m", "report"])
        if commit.returncode != 0:
            print(commit.stderr.strip() or commit.stdout.strip() or "ERROR: self-test git commit failed.")
            return 1
        (report_dir / "report.md").write_text("working tree no longer references image\n", encoding="utf-8")
        args.files = []
        args.commit = "HEAD"
        if command_report_check(args) != 0:
            return 1

    print("PASS: orchestrator self-test passed.")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    sub = parser.add_subparsers(dest="command", required=True)

    check = sub.add_parser("check", help="Validate policy, queue, machine, and plan paths.")
    check.add_argument("--self-test", action="store_true")
    check.set_defaults(func=command_check)

    next_cmd = sub.add_parser("next", help="Print the next ready item as JSON.")
    next_cmd.add_argument("--allow-disabled", action="store_true")
    next_cmd.set_defaults(func=command_next)

    start = sub.add_parser("start", help="Start a ready item and create run state.")
    start.add_argument("item_id")
    start.add_argument("--allow-disabled", action="store_true")
    start.add_argument("--allow-dirty", action="store_true")
    start.add_argument("--no-branch", action="store_true")
    start.add_argument("--run-date")
    start.set_defaults(func=command_start)

    worker_prompt = sub.add_parser("worker-prompt", help="Render a worker prompt.")
    worker_prompt.add_argument("item_id")
    worker_prompt.add_argument("--out")
    worker_prompt.set_defaults(func=command_worker_prompt)

    verifier_prompt = sub.add_parser("verifier-prompt", help="Render a verifier prompt.")
    verifier_prompt.add_argument("item_id")
    verifier_prompt.add_argument("--run-date")
    verifier_prompt.add_argument("--out")
    verifier_prompt.set_defaults(func=command_verifier_prompt)

    transition = sub.add_parser("transition", help="Apply a legal state-machine event.")
    transition.add_argument("item_id")
    transition.add_argument("event")
    transition.add_argument("--result")
    transition.add_argument("--validation-log")
    transition.add_argument("--run-date")
    transition.add_argument("--force-guards", action="store_true")
    transition.set_defaults(func=command_transition)

    run_worker = sub.add_parser("run-worker", help="Render worker prompt and call codex exec.")
    run_worker.add_argument("item_id")
    run_worker.add_argument("--run-date")
    run_worker.add_argument("--sandbox", choices=["read-only", "workspace-write", "danger-full-access"])
    run_worker.add_argument("--codex-bin")
    run_worker.add_argument("--no-schema", action="store_true")
    run_worker.add_argument("--timeout-seconds", type=int)
    run_worker.add_argument("--visible-console", dest="visible_console", action="store_true", default=None)
    run_worker.add_argument("--no-visible-console", dest="visible_console", action="store_false")
    run_worker.set_defaults(func=command_run_worker)

    run_verifier = sub.add_parser("run-verifier", help="Render verifier prompt and call codex exec.")
    run_verifier.add_argument("item_id")
    run_verifier.add_argument("--run-date")
    run_verifier.add_argument("--sandbox", choices=["read-only", "workspace-write", "danger-full-access"])
    run_verifier.add_argument("--codex-bin")
    run_verifier.add_argument("--no-schema", action="store_true")
    run_verifier.add_argument("--allow-dirty-verifier", action="store_true")
    run_verifier.add_argument("--timeout-seconds", type=int)
    run_verifier.add_argument("--visible-console", dest="visible_console", action="store_true", default=None)
    run_verifier.add_argument("--no-visible-console", dest="visible_console", action="store_false")
    run_verifier.set_defaults(func=command_run_verifier)

    doctor = sub.add_parser("doctor", help="Check orchestrator config and Codex CLI availability.")
    doctor.add_argument("--codex-bin")
    doctor.add_argument("--skip-codex-smoke", action="store_true")
    doctor.add_argument("--smoke-timeout", type=int, default=180)
    doctor.set_defaults(func=command_doctor)

    report_check = sub.add_parser("report-check", help="Validate report-only commit file lists.")
    report_check.add_argument("--files", nargs="*", default=[])
    report_check.add_argument("--commit")
    report_check.add_argument("--range", dest="diff_range")
    report_check.set_defaults(func=command_report_check)

    archive_plan = sub.add_parser("archive-plan", help="Move a successful item plan to Agentic/Plans/Done.")
    archive_plan.add_argument("item_id")
    archive_plan.add_argument("--force", action="store_true")
    archive_plan.set_defaults(func=command_archive_plan)

    report_draft = sub.add_parser("report-draft", help="Create a draft report.md from run state.")
    report_draft.add_argument("item_id")
    report_draft.add_argument("--run-date")
    report_draft.set_defaults(func=command_report_draft)

    run_loop = sub.add_parser("run-loop", help="Run the formal worker/verifier/validation loop for one queue item.")
    run_loop.add_argument("item_id", nargs="?")
    run_loop.add_argument("--allow-disabled", action="store_true")
    run_loop.add_argument("--allow-dirty", action="store_true")
    run_loop.add_argument("--run-date")
    run_loop.add_argument("--worker-sandbox", choices=["read-only", "workspace-write", "danger-full-access"])
    run_loop.add_argument("--verifier-sandbox", choices=["read-only", "workspace-write", "danger-full-access"])
    run_loop.add_argument("--codex-bin")
    run_loop.add_argument("--worker-timeout-seconds", type=int)
    run_loop.add_argument("--verifier-timeout-seconds", type=int)
    run_loop.add_argument("--visible-console", dest="visible_console", action="store_true", default=None)
    run_loop.add_argument("--no-visible-console", dest="visible_console", action="store_false")
    run_loop.add_argument("--max-verifier-rounds", type=int, default=5)
    run_loop.add_argument("--skip-validation", action="store_true")
    run_loop.add_argument("--finalize", action="store_true")
    run_loop.add_argument("--commit-finalize", action="store_true")
    run_loop.add_argument("--allow-main-commit", action="store_true")
    run_loop.add_argument("--archive-deferred", action="store_true")
    run_loop.set_defaults(func=command_run_loop)

    finalize = sub.add_parser("finalize", help="Move reporting item to terminal success, archive plan, draft report, and optionally commit.")
    finalize.add_argument("item_id")
    finalize.add_argument("--run-date")
    finalize.add_argument("--commit", action="store_true")
    finalize.add_argument("--allow-main-commit", action="store_true")
    finalize.add_argument("--archive-deferred", action="store_true")
    finalize.set_defaults(func=command_finalize)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    args.repo = args.repo.resolve()
    try:
        return int(args.func(args))
    except OrchestratorError as exc:
        print(f"ERROR: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
