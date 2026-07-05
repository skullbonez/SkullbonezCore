#!/usr/bin/env python3
"""
File: tools/check_allocation_policy.py
Purpose:
  Enforce the first static slice of the runtime allocation policy.

Mental model:
  This checker catches direct heap calls that are easy to detect textually.
  STL/container growth is measured by the runtime allocation guard; direct
  new/delete/malloc/free use must either disappear or carry allowlist metadata.

Glossary:
  Allowlist row: Metadata explaining owner, phase, reason, cap, and the wrapper
    or deletion plan for one direct allocation pattern.
  Reserve bump: Future RuntimeReserveAllocator growth request. The checker
    already knows the textual shape so self-tests cover the intended policy.

Invariants:
  - Comments and string literals do not count as source findings.
  - Allowlist entries must stay live; stale patterns fail the check.
  - The tool does not mutate the repository.

Related:
  - Agentic/Plans/In_Progress/runtime-static-allocation-policy-plan.md
  - tools/allocation_policy_allowlist.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SOURCE_ROOTS = (
    "SkullbonezSource/Core",
    "SkullbonezSource/Runtime",
    "SkullbonezSource/Physics",
    "SkullbonezSource/Rendering",
    "SkullbonezSource/GameObjects",
)
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl", ".hlsl"}
REQUIRED_ALLOWLIST_FIELDS = ("owner", "phase", "reason", "cap", "removal_or_wrapper_plan")


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    kind: str
    text: str
    column: int = 0
    end_column: int = 0


BANNED_PATTERNS = (
    ("malloc", re.compile(r"\bmalloc\s*\(")),
    ("calloc", re.compile(r"\bcalloc\s*\(")),
    ("realloc", re.compile(r"\brealloc\s*\(")),
    ("free", re.compile(r"\bfree\s*\(")),
    ("new", re.compile(r"(?<![\w])(?:::)?new(?:\s*\(\s*std::nothrow\s*\))?\s+(?!\()")),
    ("delete", re.compile(r"(?<![\w])(?:::)?delete(?:\s*\[\])?\s+(?!;)")),
    ("make-shared", re.compile(r"\bstd::make_shared\s*<")),
    ("make-unique", re.compile(r"\bstd::make_unique\s*<")),
    ("reserve-bump", re.compile(r"\bRuntimeReserveAllocator::Request(?:Replay)?Growth\s*\(")),
)


def normalize_path(path: Path | str) -> str:
    return str(path).replace("\\", "/")


def strip_comments_and_strings(source: str) -> str:
    result: list[str] = []
    i = 0
    state = "code"
    while i < len(source):
        ch = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""

        if state == "code":
            if ch == "/" and nxt == "/":
                result.extend((" ", " "))
                i += 2
                state = "line_comment"
                continue
            if ch == "/" and nxt == "*":
                result.extend((" ", " "))
                i += 2
                state = "block_comment"
                continue
            if ch == '"':
                result.append(" ")
                i += 1
                state = "string"
                continue
            if ch == "'":
                result.append(" ")
                i += 1
                state = "char"
                continue
            result.append(ch)
            i += 1
            continue

        if state == "line_comment":
            result.append("\n" if ch == "\n" else " ")
            state = "code" if ch == "\n" else state
            i += 1
            continue

        if state == "block_comment":
            if ch == "*" and nxt == "/":
                result.extend((" ", " "))
                i += 2
                state = "code"
                continue
            result.append("\n" if ch == "\n" else " ")
            i += 1
            continue

        if state == "string":
            if ch == "\\" and nxt:
                result.extend((" ", " "))
                i += 2
                continue
            result.append("\n" if ch == "\n" else " ")
            state = "code" if ch == '"' else state
            i += 1
            continue

        if state == "char":
            if ch == "\\" and nxt:
                result.extend((" ", " "))
                i += 2
                continue
            result.append("\n" if ch == "\n" else " ")
            state = "code" if ch == "'" else state
            i += 1
            continue

    return "".join(result)


def iter_source_files(repo: Path) -> Iterable[Path]:
    for root in SOURCE_ROOTS:
        base = repo / root
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                yield path


def find_banned_patterns_in_text(relative_path: str, text: str) -> list[Finding]:
    stripped = strip_comments_and_strings(text)
    original_lines = text.splitlines()
    findings: list[Finding] = []
    for line_number, line in enumerate(stripped.splitlines(), start=1):
        for kind, pattern in BANNED_PATTERNS:
            for match in pattern.finditer(line):
                original = original_lines[line_number - 1] if line_number <= len(original_lines) else line
                findings.append(
                    Finding(
                        relative_path,
                        line_number,
                        kind,
                        original,
                        match.start(),
                        match.end(),
                    )
                )
    return findings


def find_banned_patterns(path: Path, repo: Path) -> list[Finding]:
    text = path.read_text(encoding="utf-8", errors="replace")
    return find_banned_patterns_in_text(normalize_path(path.relative_to(repo)), text)


def load_allowlist(path: Path) -> tuple[dict[str, list[dict[str, object]]], list[str]]:
    errors: list[str] = []
    if not path.exists():
        return {}, [f"allowlist not found: {path}"]
    data = json.loads(path.read_text(encoding="utf-8"))
    rows = data.get("allowed", [])
    by_path: dict[str, list[dict[str, object]]] = {}
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            errors.append(f"allowlist row {index} must be an object")
            continue
        row_path = row.get("path")
        patterns = row.get("patterns")
        if not isinstance(row_path, str) or not row_path:
            errors.append(f"allowlist row {index} missing string path")
            continue
        if not isinstance(patterns, list) or not patterns or not all(isinstance(item, str) and item for item in patterns):
            errors.append(f"allowlist row {index} ({row_path}) missing non-empty string patterns")
            continue
        for field in REQUIRED_ALLOWLIST_FIELDS:
            value = row.get(field)
            if not isinstance(value, str) or not value.strip():
                errors.append(f"allowlist row {index} ({row_path}) missing {field}")
        normalized = normalize_path(row_path)
        by_path.setdefault(normalized, []).append(row)
    return by_path, errors


def pattern_overlaps_finding(finding: Finding, pattern: str) -> bool:
    search_from = 0
    while True:
        pattern_start = finding.text.find(pattern, search_from)
        if pattern_start < 0:
            return False
        pattern_end = pattern_start + len(pattern)
        if pattern_start <= finding.column < pattern_end or finding.column <= pattern_start < finding.end_column:
            return True
        search_from = pattern_start + 1


def is_allowed(finding: Finding, rows_by_path: dict[str, list[dict[str, object]]], matched: set[tuple[str, str]]) -> bool:
    for row in rows_by_path.get(finding.path, []):
        for pattern in row.get("patterns", []):
            if isinstance(pattern, str) and pattern_overlaps_finding(finding, pattern):
                matched.add((finding.path, pattern))
                return True
    return False


def check_sources(repo: Path, allowlist_path: Path) -> tuple[list[str], int, int]:
    rows_by_path, errors = load_allowlist(allowlist_path)
    findings: list[Finding] = []
    scanned = 0
    for path in iter_source_files(repo):
        scanned += 1
        findings.extend(find_banned_patterns(path, repo))

    matched: set[tuple[str, str]] = set()
    unallowed = [finding for finding in findings if not is_allowed(finding, rows_by_path, matched)]
    for finding in unallowed:
        errors.append(f"{finding.path}:{finding.line}: banned {finding.kind}: {finding.text.strip()}")

    for path_key, rows in rows_by_path.items():
        for row in rows:
            for pattern in row.get("patterns", []):
                if isinstance(pattern, str) and (path_key, pattern) not in matched:
                    errors.append(f"{path_key}: stale allowlist pattern did not match a finding: {pattern}")

    return errors, scanned, len(findings)


def run_self_tests() -> int:
    repo = Path("__self_test_repo__")
    rows: dict[str, list[dict[str, object]]] = {
        "SkullbonezSource/Runtime/Init.cpp": [
            {
                "path": "SkullbonezSource/Runtime/Init.cpp",
                "patterns": ["free( envValue )"],
                "owner": "startup",
                "phase": "startup",
                "reason": "synthetic startup cleanup",
                "cap": "one buffer",
                "removal_or_wrapper_plan": "wrap when real code grows",
            }
        ],
        "SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp": [
            {
                "path": "SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp",
                "patterns": ["RuntimeReserveAllocator::RequestReplayGrowth("],
                "owner": "replay",
                "phase": "replay",
                "reason": "synthetic registered replay growth",
                "cap": "4096 bytes",
                "removal_or_wrapper_plan": "route through RuntimeReserveAllocator",
            }
        ],
    }
    direct_heap_forms = find_banned_patterns_in_text(
        "SkullbonezSource/Runtime/RunFrame.cpp",
        "auto* a = new int; auto* b = ::new Widget; auto* c = new (std::nothrow) Widget;\n",
    )
    if sum(1 for finding in direct_heap_forms if finding.kind == "new") != 3:
        print("SELF_TEST_FAIL: direct heap new forms were not all rejected", file=sys.stderr)
        return 1

    placement_new = find_banned_patterns_in_text(
        "SkullbonezSource/Runtime/RunFrame.cpp",
        "void* storage = nullptr; auto* value = new (storage) Widget;\n",
    )
    if any(finding.kind == "new" for finding in placement_new):
        print("SELF_TEST_FAIL: placement new was rejected as heap allocation", file=sys.stderr)
        return 1

    bad = Finding("SkullbonezSource/Runtime/RunFrame.cpp", 1, "new", "auto* value = new int;", 14, 18)
    matched: set[tuple[str, str]] = set()
    if is_allowed(bad, rows, matched):
        print("SELF_TEST_FAIL: unallowlisted direct new was accepted", file=sys.stderr)
        return 1

    good = Finding("SkullbonezSource/Runtime/Init.cpp", 2, "free", "free( envValue );", 0, 5)
    if not is_allowed(good, rows, matched):
        print("SELF_TEST_FAIL: allowlisted startup cleanup was rejected", file=sys.stderr)
        return 1

    same_line = find_banned_patterns_in_text(
        "SkullbonezSource/Runtime/Init.cpp",
        "void f() { free( envValue ); free( hotPathValue ); }\n",
    )
    same_line_unallowed = [finding for finding in same_line if not is_allowed(finding, rows, matched)]
    if len(same_line) != 2 or len(same_line_unallowed) != 1:
        print("SELF_TEST_FAIL: same-line allowlist collision hid a second heap call", file=sys.stderr)
        return 1

    reserve = Finding(
        "SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp",
        3,
        "reserve-bump",
        "RuntimeReserveAllocator::RequestReplayGrowth( owner, bytes );",
        0,
        len("RuntimeReserveAllocator::RequestReplayGrowth("),
    )
    if not is_allowed(reserve, rows, matched):
        print("SELF_TEST_FAIL: allowlisted replay reserve bump was rejected", file=sys.stderr)
        return 1

    comment_source = "void f() { /* new int */ const char* s = \"malloc(\"; }\n"
    if BANNED_PATTERNS[4][1].search(strip_comments_and_strings(comment_source)):
        print("SELF_TEST_FAIL: comment/string stripping left a false new finding", file=sys.stderr)
        return 1

    print("SELF_TEST_PASS: allocation policy checker synthetic cases passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Check direct runtime heap allocation policy.")
    parser.add_argument("--repo", default=".", help="Repository root")
    parser.add_argument(
        "--allowlist",
        default="tools/allocation_policy_allowlist.json",
        help="Allocation policy allowlist JSON path relative to repo",
    )
    parser.add_argument("--self-test", action="store_true", help="Run synthetic checker tests")
    args = parser.parse_args()

    if args.self_test:
        return run_self_tests()

    repo = Path(args.repo).resolve()
    allowlist_path = Path(args.allowlist)
    if not allowlist_path.is_absolute():
        allowlist_path = repo / allowlist_path
    errors, scanned, findings = check_sources(repo, allowlist_path)
    if errors:
        print(f"FAIL: runtime allocation policy found {len(errors)} issue(s).", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    print(
        f"Runtime allocation policy summary: scanned={scanned} direct_heap_findings={findings} "
        "allowlist_errors=0"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
