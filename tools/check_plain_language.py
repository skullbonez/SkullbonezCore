"""Reject retired repository wording in tracked first-party source and docs.

The check derives its file set from ``git ls-files``. Third-party inputs,
runtime data, golden artifacts, and binary formats are outside the source/doc
policy. Every tracked first-party source, test, tool, shader, governance,
workflow, hook, and repository-root document is checked without site exceptions.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
from typing import Iterable
import xml.etree.ElementTree as ElementTree
import zipfile


THIRD_PARTY_TREES = {"ThirdPtySource"}
DATA_TREES = {"SkullbonezData", "TestOutput"}
DATA_TREE_POLICY_SUFFIXES = {
    ".bat",
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inl",
    ".hlsl",
    ".hlsli",
    ".md",
    ".patch",
    ".ps1",
    ".py",
    ".txt",
}
NON_TEXT_SUFFIXES = {".dxil", ".exe", ".jpg", ".pdf", ".png", ".raw", ".sdf"}
TOKEN_START = (
    r"(?:(?<![A-Za-z0-9])|"
    r"(?<=(?-i:[a-z0-9]))(?=(?-i:[A-Z]))|"
    r"(?<=(?-i:[A-Z]))(?=(?-i:[A-Z][a-z])))"
)


@dataclass(frozen=True)
class Rule:
    name: str
    pattern: re.Pattern[str]


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    column: int
    rule: str


def _joined_pattern(*parts: str) -> re.Pattern[str]:
    return re.compile("".join(parts), re.IGNORECASE)


# Keep the rejected spellings split in this checker so the checker is subject
# to its own rule. Patterns include common inflections and separator variants;
# changing punctuation must not provide a bypass.
RULES = (
    Rule(
        "inflated struct label",
        _joined_pattern(
            TOKEN_START,
            r"authority",
            r"[-_\s]*free(?:[-_\s]*(?:aggregate|struct)s?)?",
        ),
    ),
    Rule(
        "vague refactor label",
        _joined_pattern(TOKEN_START, r"extraction", r"[-_\s]*scars?"),
    ),
    Rule(
        "review ceremony label",
        _joined_pattern(
            TOKEN_START,
            r"(?:owner[-_\s]*rul(?:e[sd]?|ing(?:s)?)|",
            r"adju",
            r"dicat\w*)",
        ),
    ),
    Rule(
        "missed-failure label",
        _joined_pattern(TOKEN_START, r"false", r"[-_\s]*pass(?:es|ed|ing)?"),
    ),
    Rule(
        "inflated test-evidence label",
        _joined_pattern(TOKEN_START, r"authoritative", r"[-_\s]*witness(?:es)?"),
    ),
    Rule(
        "borrowed-reference label",
        _joined_pattern(TOKEN_START, r"owner", r"[-_\s]*borrow\w*"),
    ),
    Rule(
        "parameter-wrapper label",
        _joined_pattern(TOKEN_START, r"cour", r"ier(?:[-_\s]*struct)?s?"),
    ),
    Rule(
        "parameter-bag label",
        _joined_pattern(TOKEN_START, r"context", r"[-_\s]*bags?"),
    ),
    Rule(
        "golden-update ceremony label",
        _joined_pattern(
            TOKEN_START,
            r"standing",
            r"[-_\s]*automated(?:[-_\s]*transition)?[-_\s]*authority",
        ),
    ),
    Rule(
        "completion label",
        _joined_pattern(TOKEN_START, r"closure", r"[-_\s]*failures?"),
    ),
    Rule(
        "importance label",
        _joined_pattern(TOKEN_START, r"load", r"[-_\s]*bearing"),
    ),
)


def tracked_paths(repo: Path) -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(repo), "ls-files", "-z"],
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"git ls-files failed ({result.returncode}): {detail}")
    return [
        item.decode("utf-8", errors="surrogateescape")
        for item in result.stdout.split(b"\0")
        if item
    ]


def is_policy_surface(relative_path: str) -> bool:
    path = PurePosixPath(relative_path)
    if not path.parts:
        return False
    root = path.parts[0]
    if root in THIRD_PARTY_TREES:
        return False
    if root in DATA_TREES:
        return path.suffix.casefold() in DATA_TREE_POLICY_SUFFIXES
    return True


def read_docx_text(path: Path) -> str:
    try:
        with zipfile.ZipFile(path) as archive:
            parts: list[str] = []
            for name in sorted(archive.namelist()):
                if not name.startswith("word/") or not name.endswith(".xml"):
                    continue
                root = ElementTree.fromstring(archive.read(name))
                parts.extend(root.itertext())
            return " ".join(parts)
    except (OSError, ElementTree.ParseError, zipfile.BadZipFile) as error:
        raise RuntimeError(f"cannot inspect tracked DOCX {path}: {error}") from error


def read_text(path: Path) -> str | None:
    suffix = path.suffix.casefold()
    if suffix in NON_TEXT_SUFFIXES:
        return None
    if suffix == ".docx":
        return read_docx_text(path)
    data = path.read_bytes()
    if b"\0" in data:
        raise RuntimeError(f"tracked policy text contains NUL bytes: {path}")
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise RuntimeError(f"tracked policy text is not UTF-8: {path}") from error


def scan_text(relative_path: str, text: str) -> list[Finding]:
    findings: list[Finding] = []
    for rule in RULES:
        for match in rule.pattern.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            line_start = text.rfind("\n", 0, match.start()) + 1
            findings.append(
                Finding(
                    path=relative_path,
                    line=line,
                    column=match.start() - line_start + 1,
                    rule=rule.name,
                )
            )
    return findings


def scan_repo(repo: Path) -> tuple[list[Finding], int]:
    findings: list[Finding] = []
    scanned = 0
    for relative_path in tracked_paths(repo):
        if not is_policy_surface(relative_path):
            continue
        absolute_path = repo / Path(relative_path)
        if not absolute_path.is_file():
            continue
        text = read_text(absolute_path)
        if text is None:
            continue
        scanned += 1
        findings.extend(scan_text(relative_path, text))
    findings.sort(key=lambda item: (item.path.casefold(), item.line, item.column, item.rule))
    return findings, scanned


def run_self_test() -> None:
    cases = (
        ("fixture.cpp", "// " + "authority" + "FreeAggregatePermission", "inflated struct label"),
        ("fixture.md", "The change was " + "adju" + "dicated.", "review ceremony label"),
        ("variant.md", "An " + "extraction" + "\nscar remained.", "vague refactor label"),
        ("variant.md", "An " + "OWNER" + "\tRULING remained.", "review ceremony label"),
        ("variant.md", "The control " + "False" + "PassControl.", "missed-failure label"),
        ("variant.md", "Use an " + "Authoritative" + "WitnessFactory.", "inflated test-evidence label"),
        ("variant.md", "Keep an " + "Owner" + "BorrowedView.", "borrowed-reference label"),
        ("variant.md", "Two " + "cour" + "ier_structs_factory.", "parameter-wrapper label"),
        ("variant.md", "Avoid " + "Context" + "BagFactory.", "parameter-bag label"),
        ("variant.md", "The " + "Standing" + "\nAutomatedTransitionAuthorityPolicy applies.", "golden-update ceremony label"),
        ("variant.md", "This is a " + "Closure" + "FailureCode.", "completion label"),
        ("variant.md", "A " + "Load" + "BearingRule.", "importance label"),
    )
    for path, text, expected_rule in cases:
        findings = scan_text(path, text)
        if len(findings) != 1 or findings[0].rule != expected_rule:
            raise AssertionError(
                f"fixture {path!r} expected {expected_rule!r}, got {findings!r}"
            )

    clean_fixture = (
        "struct Values { int count; }; // Review decisions state concrete invariants.\n"
        "Understanding automated validation keeps payload bearing values and "
        "PayloadBearingRadians unchanged."
    )
    clean_findings = scan_text("clean.md", clean_fixture)
    if clean_findings:
        raise AssertionError(f"clean fixture unexpectedly failed: {clean_findings!r}")


def print_findings(findings: Iterable[Finding]) -> None:
    for finding in findings:
        print(
            f"{finding.path}:{finding.line}:{finding.column}: "
            f"retired wording ({finding.rule})"
        )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Reject retired wording in tracked first-party source and documentation."
    )
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        run_self_test()
        print("PASS: source and documentation fixtures reject retired wording; clean text passes")
        return 0

    repo = args.repo.resolve()
    try:
        findings, scanned = scan_repo(repo)
    except (OSError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    if findings:
        print_findings(findings)
        print(f"FAIL: {len(findings)} retired wording occurrence(s) in {scanned} tracked text files")
        return 1

    print(f"PASS: {scanned} tracked first-party text files use plain language")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
