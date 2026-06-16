#
# File: tools/orchestrator.py
# Purpose:
#   Enforces the roadmap orchestrator queue and state machine.
#
# Mental model:
#   JSON files are the executable control plane. YAML and Markdown explain the
#   workflow for humans and agents, but this script is the mechanical guardrail
#   that decides whether a transition is legal.
#
# Glossary:
#   Queue item: One planned roadmap task from Agentic/Plans.
#   Run state: Per-attempt state saved under Agentic/Runs/<date>/<item-id>.
#   Transition: A legal event from the item state machine.
#
# Invariants:
#   - Do not infer runnable work from every Markdown file in Agentic/Plans.
#   - At most one queue item may be active.
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
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
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


def terminal_states(machine: dict[str, Any]) -> set[str]:
    return {
        state
        for state, cfg in machine.get("states", {}).items()
        if isinstance(cfg, dict) and str(cfg.get("kind", "")).startswith("terminal")
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
    max_active = int(policy.get("max_active_items", 1))
    active = active_states(machine)

    for item_id, item in items.items():
        state = item_state(item)
        if state not in states:
            errors.append(f"{item_id}: unknown state {state!r}.")
        if state in active:
            active_count += 1

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
                errors.append(
                    f"{item_id}: dependency {dep_id} is {dep_state}, "
                    f"not satisfied for mode {mode}."
                )

    if active_count > max_active:
        errors.append(f"Queue has {active_count} active items; max_active_items is {max_active}.")

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
        if all(dependency_satisfied(machine, item_state(items[dep]), mode) for dep in item.get("depends_on", [])):
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

    return {
        "item_id": item_id,
        "plan_path": item.get("plan", ""),
        "branch": item.get("branch", ""),
        "parent_branch": item.get("parent_branch", item.get("stack_base_branch", "")),
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
        prompt_path = write_worker_prompt(args.repo, policy, queue, item, run_dir)
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
            "worker_prompt": repo_relative(args.repo, prompt_path),
        }
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
            number = next_round_number(round_dir)
            suffix = source.suffix or ".md"
            target = round_dir / f"round-{number:02d}-verifier-result{suffix}"
        else:
            target = run_dir / f"{event}-result.md"
        shutil.copyfile(source, target)
        artifacts["result"] = repo_relative(repo, target)
    if validation_log_path:
        source = resolve_repo_path(repo, validation_log_path)
        if not source.exists():
            raise OrchestratorError(f"Validation log path does not exist: {validation_log_path}")
        target = run_dir / "validation.log"
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


def command_transition(args: argparse.Namespace) -> int:
    try:
        policy, queue, machine = load_state(args.repo)
        items = items_by_id(queue)
        item = items[args.item_id]
        current = item_state(item)
        transition = machine.get("states", {}).get(current, {}).get("on", {}).get(args.event)
        if not transition:
            events = sorted(machine.get("states", {}).get(current, {}).get("on", {}).keys())
            raise OrchestratorError(
                f"Illegal event {args.event!r} from {current}. Legal events: {', '.join(events) or '(none)'}"
            )

        manual_guards: list[str] = []
        failed_guards = [
            guard
            for guard in transition.get("guards", [])
            if not guard_passes(policy, str(guard), manual_guards)
        ]
        if failed_guards and not args.force_guards:
            raise OrchestratorError(f"Guard(s) failed: {', '.join(failed_guards)}")

        target = normalize_state(str(transition["target"]))
        run_dir = ensure_run_dir(args.repo, policy, args.item_id, args.run_date)
        run_state = load_run_state(run_dir)
        artifacts = save_transition_artifact(args.repo, run_dir, args.event, args.result, args.validation_log)

        generated: dict[str, str] = {}
        if "prompt.write_verifier" in transition.get("actions", []):
            prompt_path = write_verifier_prompt(args.repo, policy, queue, item, run_dir)
            generated["verifier_prompt"] = repo_relative(args.repo, prompt_path)
        if "validation.record_not_required" in transition.get("actions", []):
            validation_path = run_dir / "validation.log"
            validation_path.write_text("No repository validation script required for this transition.\n", encoding="utf-8")
            generated["validation_log"] = repo_relative(args.repo, validation_path)

        set_item_state(item, target)
        write_json(args.repo / QUEUE_PATH, queue)
        run_state["current_state"] = target
        run_state.setdefault("transition_history", []).append(
            {
                "at": utc_now(),
                "event": args.event,
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
    except KeyError:
        print(f"ERROR: Unknown item: {args.item_id}")
        return 1
    except OrchestratorError as exc:
        print(f"ERROR: {exc}")
        return 1

    print(f"Transitioned {args.item_id}: {current} --{args.event}--> {target}")
    if manual_guards:
        print(f"Manual guard(s) acknowledged by operator: {', '.join(manual_guards)}")
    for label, path in {**artifacts, **generated}.items():
        print(f"{label}: {path}")
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


def run_codex_exec(
    repo: Path,
    prompt: str,
    output_path: Path,
    schema_path: Path | None,
    sandbox: str,
    codex_bin: str | None,
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
    result = subprocess.run(command, input=prompt, text=True, cwd=repo, check=False)
    return result.returncode


def command_run_worker(args: argparse.Namespace) -> int:
    try:
        policy, queue, _ = load_state(args.repo)
        item = items_by_id(queue)[args.item_id]
        run_dir = ensure_run_dir(args.repo, policy, args.item_id, args.run_date)
        prompt = render_worker_prompt(args.repo, policy, queue, item, run_dir)
        schema = None if args.no_schema else resolve_repo_path(args.repo, WORKER_SCHEMA)
        output = run_dir / ("worker-result.md" if args.no_schema else "worker-result.json")
        return run_codex_exec(args.repo, prompt, output, schema, args.sandbox, args.codex_bin)
    except (KeyError, OrchestratorError) as exc:
        print(f"ERROR: {exc}")
        return 1


def command_run_verifier(args: argparse.Namespace) -> int:
    try:
        policy, queue, _ = load_state(args.repo)
        item = items_by_id(queue)[args.item_id]
        run_dir = ensure_run_dir(args.repo, policy, args.item_id, args.run_date)
        prompt = render_verifier_prompt(args.repo, policy, queue, item, run_dir)
        round_dir = run_dir / "verification-rounds"
        number = next_round_number(round_dir)
        schema = None if args.no_schema else resolve_repo_path(args.repo, VERIFIER_SCHEMA)
        suffix = ".md" if args.no_schema else ".json"
        output = round_dir / f"round-{number:02d}-verifier-result{suffix}"
        return run_codex_exec(args.repo, prompt, output, schema, args.sandbox, args.codex_bin)
    except (KeyError, OrchestratorError) as exc:
        print(f"ERROR: {exc}")
        return 1


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

    print("PASS: orchestrator config is valid.")
    print(f"PASS: codex executable: {codex}")
    print(f"PASS: {(version.stdout or version.stderr).strip()}")
    print("PASS: codex exec is available for worker and verifier runs.")
    return 0


REPORT_RE = re.compile(r"^Agentic/Reports/\d{4}-\d{2}-\d{2}/[^/]+/report\.md$")
IMAGE_RE = re.compile(r"^Agentic/Reports/\d{4}-\d{2}-\d{2}/[^/]+/images/[^/]+\.(?:png|jpg|jpeg)$", re.I)


def files_from_git(repo: Path, mode: str, rev: str) -> list[str]:
    if mode == "commit":
        command = ["git", "show", "--name-only", "--format=", rev]
    else:
        command = ["git", "diff", "--name-only", rev]
    result = subprocess.run(command, cwd=repo, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise OrchestratorError(result.stderr.strip() or f"git {mode} failed")
    return [line.strip().replace("\\", "/") for line in result.stdout.splitlines() if line.strip()]


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

        report_path = resolve_repo_path(args.repo, report_files[0])
        report_text = report_path.read_text(encoding="utf-8", errors="ignore")
        for image in image_files:
            image_name = Path(image).name
            if f"images/{image_name}" not in report_text:
                raise OrchestratorError(f"Committed image is not referenced by report.md: {image}")
    except OrchestratorError as exc:
        print(f"ERROR: {exc}")
        return 1

    print(f"PASS: report-only file list is valid ({len(files)} file(s)).")
    return 0


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
        return path.read_text(encoding="utf-8", errors="ignore").strip()
    return ""


def read_worker_result(run_dir: Path) -> str:
    json_text = read_optional_text(run_dir / "worker-result.json")
    if json_text:
        try:
            payload = json.loads(json_text)
            summary = payload.get("summary", "")
            plain = payload.get("plain_language_summary", "")
            return "\n\n".join(part for part in (plain, summary) if part)
        except json.JSONDecodeError:
            return json_text
    return read_optional_text(run_dir / "worker-result.md")


def latest_commit(repo: Path) -> str:
    result = run_git(repo, ["rev-parse", "--short", "HEAD"])
    return result.stdout.strip() if result.returncode == 0 else ""


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
        worker_result = read_worker_result(run_dir)
        validation = read_optional_text(run_dir / "validation.log")
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
            "commit_sha": latest_commit(args.repo),
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
            "timings": "See worker/verifier results.",
            "implementation_summary": worker_result or "Pending worker summary.",
            "changed_files": "\n".join(f"- `{path}`" for path in git_changed_files(args.repo)),
            "validation_gate": item.get("validation_gate", ""),
            "validation_commands": item.get("validation_gate", ""),
            "validation_result": validation or "Pending validation evidence.",
            "verification_loop": "See `verification-rounds/` under the run directory.",
            "artifacts": f"Run directory: `{repo_relative(args.repo, run_dir)}`",
            "code_snippets": "Pending final report curation.",
            "pr_status": "Pending.",
            "merge_status": "Not permitted unless AGENTS.md and policy allow it.",
            "conflicts": "None recorded.",
            "residual_risk": "Pending final report curation.",
            "sub_agent_summary": worker_result or "Pending worker result.",
            "verifier_summary": "Pending verifier result.",
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


def command_self_test(args: argparse.Namespace) -> int:
    with tempfile.TemporaryDirectory() as temp:
        repo = Path(temp)
        (repo / ORCH_DIR / "machines").mkdir(parents=True)
        (repo / "Agentic" / "Plans" / "Done").mkdir(parents=True)
        (repo / "Agentic" / "Plans" / "Done" / "done.md").write_text("# Done\n", encoding="utf-8")
        (repo / "Agentic" / "Plans" / "active.md").write_text("# Active\n", encoding="utf-8")
        (repo / "AGENTS.md").write_text("merge requires explicit user request\n", encoding="utf-8")
        machine = load_json(args.repo / DEFAULT_MACHINE_PATH)
        write_json(repo / DEFAULT_MACHINE_PATH, machine)
        write_json(
            repo / POLICY_PATH,
            {
                "schema_version": 1,
                "enabled": True,
                "max_active_items": 1,
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
        policy, queue, machine = load_state(repo)
        item = select_next_item(policy, queue, machine, allow_disabled=False)
        if item is None or item["id"] != "active":
            print("ERROR: self-test next item selection failed.")
            return 1

        report_dir = repo / "Agentic" / "Reports" / "2026-06-16" / "item"
        image_dir = report_dir / "images"
        image_dir.mkdir(parents=True)
        (report_dir / "report.md").write_text("![Image](images/a.png)\n", encoding="utf-8")
        (image_dir / "a.png").write_text("fake", encoding="utf-8")
        args.repo = repo
        args.files = [
            "Agentic/Reports/2026-06-16/item/report.md",
            "Agentic/Reports/2026-06-16/item/images/a.png",
        ]
        args.commit = None
        args.diff_range = None
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
    run_worker.add_argument("--sandbox", default="workspace-write", choices=["read-only", "workspace-write", "danger-full-access"])
    run_worker.add_argument("--codex-bin")
    run_worker.add_argument("--no-schema", action="store_true")
    run_worker.set_defaults(func=command_run_worker)

    run_verifier = sub.add_parser("run-verifier", help="Render verifier prompt and call codex exec.")
    run_verifier.add_argument("item_id")
    run_verifier.add_argument("--run-date")
    run_verifier.add_argument("--sandbox", default="read-only", choices=["read-only", "workspace-write", "danger-full-access"])
    run_verifier.add_argument("--codex-bin")
    run_verifier.add_argument("--no-schema", action="store_true")
    run_verifier.set_defaults(func=command_run_verifier)

    doctor = sub.add_parser("doctor", help="Check orchestrator config and Codex CLI availability.")
    doctor.add_argument("--codex-bin")
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
