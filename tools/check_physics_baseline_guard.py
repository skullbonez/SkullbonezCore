#
# File: tools/check_physics_baseline_guard.py
# Purpose:
#   Protects the deterministic physics golden with an exact owner-approved digest.
#
# Summary:
#   Validation checks the working tree or Git index before expensive builds. A
#   staged golden or approval-record change requires a one-content approval
#   receipt created by the repository owner's interactive approval command.
#
# Glossary:
#   Approval record: Tracked JSON binding the accepted golden path and SHA-256.
#   Approval receipt: Local Git metadata binding one exact staged golden and
#   approval-record pair after an interactive owner confirmation.
#   Bootstrap approval: The repository owner's explicit approval of the golden
#   that predates this guard and therefore has no earlier tracked record.
#
# Invariants:
#   - Ordinary validation never writes the golden or approval record.
#   - Staged content is read from the Git index, not the working tree.
#   - An approval receipt is valid for one exact golden/record byte pair.
#   - Only the one pinned bootstrap digest can add the first approval record.
#
# Related:
#   - AGENTS.md
#   - tools/check_physics_regression.py
#   - tools/validate_physics.bat
#   - tools/physics_baseline_approval.json
#
"""Fail closed when the deterministic physics golden lacks owner approval."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from datetime import datetime, timezone


APPROVAL_RECORD = "tools/physics_baseline_approval.json"
BASELINE_PATH = "TestOutput/baselines/physics_regression_varied.csv"
BOOTSTRAP_APPROVED_SHA256 = "debf57f744774d4e7c1eb5cc61f05ba6e41dc6dc997ad20db6c91b02b0958c32"
BOOTSTRAP_SOURCE_COMMIT = "7d46a6c3ea75e2f1e2a6e149a23b632aaa6b79b2"
RECEIPT_RELATIVE_PATH = Path("skore-approvals") / "physics-baseline.json"


class GuardFailure(RuntimeError):
    """A bounded, user-actionable approval or integrity failure."""


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


def parse_record(data: bytes) -> dict[str, object]:
    try:
        record = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise GuardFailure(f"{APPROVAL_RECORD} is not valid UTF-8 JSON: {exc}") from exc

    required = {
        "schema_version",
        "baseline_path",
        "sha256",
        "approved_by",
        "approved_at_utc",
        "source_commit",
        "reason",
    }
    if not isinstance(record, dict) or set(record) != required:
        missing = sorted(required - set(record)) if isinstance(record, dict) else sorted(required)
        extra = sorted(set(record) - required) if isinstance(record, dict) else []
        raise GuardFailure(f"{APPROVAL_RECORD} schema mismatch; missing={missing}, extra={extra}")
    if record["schema_version"] != 1:
        raise GuardFailure(f"{APPROVAL_RECORD} has unsupported schema_version")
    if record["baseline_path"] != BASELINE_PATH:
        raise GuardFailure(f"{APPROVAL_RECORD} must protect exactly {BASELINE_PATH}")
    digest = record["sha256"]
    if not isinstance(digest, str) or len(digest) != 64 or any(ch not in "0123456789abcdef" for ch in digest):
        raise GuardFailure(f"{APPROVAL_RECORD} sha256 must be 64 lowercase hexadecimal characters")
    for key in ("approved_by", "approved_at_utc", "source_commit", "reason"):
        if not isinstance(record[key], str) or not record[key].strip():
            raise GuardFailure(f"{APPROVAL_RECORD} field {key} must be a non-empty string")
    return record


def verify_pair(record_data: bytes, baseline_data: bytes) -> tuple[dict[str, object], str]:
    record = parse_record(record_data)
    actual = sha256_bytes(baseline_data)
    if actual != record["sha256"]:
        raise GuardFailure(
            f"physics golden SHA-256 {actual} does not match owner-approved {record['sha256']}"
        )
    return record, actual


def index_bytes(repo: Path, path: str) -> bytes:
    result = run_git(repo, "show", f":{path}", check=False)
    if result.returncode != 0:
        raise GuardFailure(f"{path} is absent from the Git index; stage the complete guard change")
    return result.stdout


def head_bytes(repo: Path, path: str) -> bytes | None:
    result = run_git(repo, "show", f"HEAD:{path}", check=False)
    return result.stdout if result.returncode == 0 else None


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


def verify_receipt(repo: Path, baseline_digest: str, record_data: bytes) -> None:
    receipt = load_receipt(repo)
    expected_record_digest = sha256_bytes(record_data)
    if receipt is None:
        raise GuardFailure(
            "physics baseline approval is required; the repository owner must run "
            f"python {Path(__file__).name} --repo . --approve-output Debug/physics_regression_varied.csv"
        )
    expected = {
        "schema_version": 1,
        "baseline_sha256": baseline_digest,
        "approval_record_sha256": expected_record_digest,
    }
    for key, value in expected.items():
        if receipt.get(key) != value:
            raise GuardFailure(
                "physics approval receipt does not match the exact staged golden and approval record; "
                "the repository owner must approve this content again"
            )


def check_worktree(repo: Path) -> str:
    record_path = repo / APPROVAL_RECORD
    baseline_path = repo / BASELINE_PATH
    if not record_path.is_file():
        raise GuardFailure(f"missing physics approval record {APPROVAL_RECORD}")
    if not baseline_path.is_file():
        raise GuardFailure(f"missing physics golden {BASELINE_PATH}")
    _, digest = verify_pair(record_path.read_bytes(), baseline_path.read_bytes())
    return digest


def check_staged(repo: Path) -> str:
    record_data = index_bytes(repo, APPROVAL_RECORD)
    baseline_data = index_bytes(repo, BASELINE_PATH)
    record, digest = verify_pair(record_data, baseline_data)

    previous_record = head_bytes(repo, APPROVAL_RECORD)
    previous_baseline = head_bytes(repo, BASELINE_PATH)
    record_changed = previous_record != record_data
    baseline_changed = previous_baseline != baseline_data
    if not record_changed and not baseline_changed:
        return digest

    # Why: this is the sole transition allowed without a receipt. The owner
    # approved these exact pre-guard bytes in the conversation that introduced
    # the guard; every later transition has a tracked predecessor to compare.
    bootstrap = (
        previous_record is None
        and not baseline_changed
        and digest == BOOTSTRAP_APPROVED_SHA256
        and record["source_commit"] == BOOTSTRAP_SOURCE_COMMIT
    )
    if not bootstrap:
        verify_receipt(repo, digest, record_data)
    return digest


def canonical_complete_run(data: bytes, artifact_name: str) -> bytes:
    # Invariant: an approval covers one complete deterministic playback. A
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
    print(f"Approved complete rows:    {len(previous_lines)}")
    print(f"Proposed complete rows:    {len(proposed_lines)}")
    for line_number, old, new in paired_differences[:5]:
        print(f"  first differences, row {line_number}:")
        print(f"    approved: {old.decode(errors='replace')}")
        print(f"    proposed: {new.decode(errors='replace')}")


def approve_output(repo: Path, output: Path, owner_approved_sha256: str | None = None) -> str:
    # Hazard: this is a workflow authorization boundary, not cryptographic user
    # authentication. The exact phrase prevents accidental/scripted updates;
    # repository access control still determines who may invoke it deliberately.
    output_path = (output if output.is_absolute() else repo / output).resolve()
    required_output = (repo / "Debug" / "physics_regression_varied.csv").resolve()
    if output_path != required_output:
        raise GuardFailure(f"owner approval accepts only the final Debug artifact: {required_output}")
    if not output_path.is_file():
        raise GuardFailure(f"approval output does not exist: {output_path}")
    executable = repo / "Debug" / "SKULLBONEZ_CORE.exe"
    if not executable.is_file() or output_path.stat().st_mtime_ns < executable.stat().st_mtime_ns:
        raise GuardFailure("generated CSV predates the final Debug executable; rerun physics validation first")

    proposed = canonical_complete_run(output_path.read_bytes(), output_path.name)
    proposed_digest = sha256_bytes(proposed)
    baseline_path = repo / BASELINE_PATH
    previous = canonical_complete_run(baseline_path.read_bytes(), baseline_path.name) if baseline_path.is_file() else b""
    previous_digest = sha256_bytes(previous) if previous else "missing"
    if proposed == previous:
        raise GuardFailure("generated CSV already matches the approved golden; no approval update is needed")
    print(f"Current approved SHA-256: {previous_digest}")
    print(f"Proposed golden SHA-256: {proposed_digest}")
    print_bounded_change_summary(previous, proposed)
    if owner_approved_sha256 is not None:
        approved_digest = owner_approved_sha256.lower()
        if (
            len(approved_digest) != 64
            or any(ch not in "0123456789abcdef" for ch in approved_digest)
            or approved_digest != proposed_digest
        ):
            raise GuardFailure(
                "owner-approved SHA-256 override does not match the generated physics baseline"
            )
        reason = "Repository owner explicitly approved the deterministic physics behavior change by exact SHA-256 override."
        print("Owner-approved SHA-256 override matches the generated physics baseline.")
    else:
        if not sys.stdin.isatty() or not sys.stdout.isatty():
            raise GuardFailure("owner approval requires an interactive terminal or an exact SHA-256 override")
        phrase = f"APPROVE PHYSICS BASELINE {proposed_digest}"
        print("Type the following exact phrase to authorize the behavior change:")
        print(phrase)
        if input("> ").strip() != phrase:
            raise GuardFailure("approval phrase did not match; no files were changed")
        reason = "Repository owner interactively approved the deterministic physics behavior change."

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
        "approved_by": "repository owner",
        "approved_at_utc": now,
        "source_commit": head,
        "reason": reason,
    }
    record_data = write_json_atomic(repo / APPROVAL_RECORD, record)
    receipt = {
        "schema_version": 1,
        "baseline_sha256": proposed_digest,
        "approval_record_sha256": sha256_bytes(record_data),
        "approved_at_utc": now,
    }
    write_json_atomic(receipt_path(repo), receipt)
    print("APPROVED: stage the golden and approval record together, then rerun physics validation.")
    return proposed_digest


def configure_test_identity(repo: Path) -> None:
    run_git(repo, "config", "user.email", "physics-guard@example.invalid")
    run_git(repo, "config", "user.name", "Physics Guard Self Test")


def self_test(source_repo: Path) -> None:
    source_baseline = run_git(
        source_repo, "show", f"{BOOTSTRAP_SOURCE_COMMIT}:{BASELINE_PATH}"
    ).stdout
    if sha256_bytes(source_baseline) != BOOTSTRAP_APPROVED_SHA256:
        raise GuardFailure("self-test source golden no longer matches the pinned bootstrap digest")

    with tempfile.TemporaryDirectory(prefix="physics-baseline-guard-") as temp:
        repo = Path(temp)
        run_git(repo, "init")
        configure_test_identity(repo)
        (repo / BASELINE_PATH).parent.mkdir(parents=True)
        (repo / BASELINE_PATH).write_bytes(source_baseline)
        run_git(repo, "add", BASELINE_PATH)
        run_git(repo, "commit", "-m", "seed golden")

        bootstrap_record = parse_record((source_repo / APPROVAL_RECORD).read_bytes())
        bootstrap_record["sha256"] = BOOTSTRAP_APPROVED_SHA256
        bootstrap_record["source_commit"] = BOOTSTRAP_SOURCE_COMMIT
        record_data = (json.dumps(bootstrap_record, indent=2) + "\n").encode("utf-8")
        (repo / APPROVAL_RECORD).parent.mkdir(parents=True, exist_ok=True)
        (repo / APPROVAL_RECORD).write_bytes(record_data)
        run_git(repo, "add", APPROVAL_RECORD)
        check_staged(repo)
        run_git(repo, "commit", "-m", "bootstrap approval")

        changed = (source_repo / BASELINE_PATH).read_bytes()
        if changed == source_baseline:
            changed += b"# deliberate self-test change\n"
        changed_digest = sha256_bytes(changed)
        (repo / BASELINE_PATH).write_bytes(changed)
        record = parse_record(record_data)
        record["sha256"] = changed_digest
        record["approved_at_utc"] = "2099-01-01T00:00:00Z"
        changed_record_data = write_json_atomic(repo / APPROVAL_RECORD, record)
        run_git(repo, "add", BASELINE_PATH, APPROVAL_RECORD)
        try:
            check_staged(repo)
        except GuardFailure as exc:
            if "approval is required" not in str(exc):
                raise
        else:
            raise GuardFailure("self-test accepted an unapproved staged golden")

        run_git(repo, "restore", "--staged", BASELINE_PATH, APPROVAL_RECORD)
        (repo / BASELINE_PATH).write_bytes(source_baseline)
        (repo / APPROVAL_RECORD).write_bytes(record_data)
        output_path = repo / "Debug" / "physics_regression_varied.csv"
        executable_path = repo / "Debug" / "SKULLBONEZ_CORE.exe"
        output_path.parent.mkdir(parents=True)
        executable_path.write_bytes(b"self-test executable")
        output_path.write_bytes(changed)
        output_timestamp = executable_path.stat().st_mtime_ns + 1_000_000_000
        os.utime(output_path, ns=(output_timestamp, output_timestamp))
        try:
            approve_output(repo, output_path, "0" * 64)
        except GuardFailure as exc:
            if "does not match" not in str(exc):
                raise
        else:
            raise GuardFailure("self-test accepted an incorrect owner-approved SHA-256 override")
        approve_output(repo, output_path, changed_digest)
        run_git(repo, "add", BASELINE_PATH, APPROVAL_RECORD)
        check_staged(repo)

        (repo / BASELINE_PATH).write_bytes(changed + b"tamper\n")
        try:
            check_worktree(repo)
        except GuardFailure:
            pass
        else:
            raise GuardFailure("self-test accepted a tampered worktree golden")

    print("PASS: physics baseline guard self-tests")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parent.parent)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--staged", action="store_true", help="check exact Git-index content for commit")
    action.add_argument("--approve-output", type=Path, help="interactive owner approval of generated CSV")
    action.add_argument("--self-test", action="store_true")
    parser.add_argument(
        "--owner-approved-sha256",
        help="noninteractive owner override; must exactly match the generated complete-run SHA-256",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.owner_approved_sha256 is not None and args.approve_output is None:
        parser.error("--owner-approved-sha256 requires --approve-output")
    repo = args.repo.resolve()
    try:
        if args.self_test:
            self_test(repo)
        elif args.approve_output is not None:
            approve_output(repo, args.approve_output, args.owner_approved_sha256)
        elif args.staged:
            digest = check_staged(repo)
            print(f"PASS: staged physics golden has owner approval ({digest})")
        else:
            digest = check_worktree(repo)
            print(f"PASS: physics golden matches owner-approved SHA-256 ({digest})")
    except GuardFailure as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
