"""File: tools/check_related_paths.py

Purpose:
  Reports repository-relative entries in existing source `Related:`
  blocks that do not resolve to existing repository paths.

Summary:
  `Related:` is useful navigation metadata, so this advisory report resolves
  path-shaped entries from the repository root, declaring source directory, a
  source ancestor, or one unique source name. It is intentionally absent from
  blocking validation because documentation moves are review work.

Glossary:
  Related block: A source-comment section introduced by `Related:`.
  Repository-relative entry: A path rooted at a known repository directory or
    one of the permanent root documentation files.

Invariants:
  - The checker is read-only.
  - Every failure names the source file, line, and unresolved path.
  - Bare topics and external URLs are ignored rather than guessed into paths.
  - The default inventory combines tracked and untracked source-bearing files,
    then ignores deleted worktree paths so in-progress moves remain checkable.

Related:
  - Agentic/Reference/comment-style-guide.md
  - tools/README.md
"""

from __future__ import annotations

import argparse
import re
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl", ".hlsl", ".py", ".bat", ".ps1"}
REPOSITORY_PATH_SUFFIXES = SOURCE_SUFFIXES | {
    ".cfg",
    ".csv",
    ".filters",
    ".json",
    ".md",
    ".props",
    ".sln",
    ".targets",
    ".txt",
    ".vcxproj",
}
REPOSITORY_PREFIXES = (
    "Agentic/",
    "SkullbonezData/",
    "SkullbonezSource/",
    "SkullbonezTests/",
    "TestOutput/",
    "ThirdPtySource/",
    "tools/",
)
REPOSITORY_ROOT_FILES = {
    ".clang-format",
    "AGENTS.md",
    "FIRST_TIME_SETUP.md",
    "README.md",
}
LINE_SUFFIX = re.compile(r":\d+(?::\d+)?$")


@dataclass(frozen=True)
class RelatedEntry:
    source: Path
    line: int
    path: str


def comment_content(line: str) -> str:
    content = line.strip()
    for prefix in ("//", "/*", "*"):
        if content.startswith(prefix):
            content = content[len(prefix) :].strip()
            break
    if content.endswith("*/"):
        content = content[:-2].rstrip()
    return content


def entry_candidate(item: str) -> str:
    code_span = re.match(r"`([^`]+)`", item)
    candidate = code_span.group(1) if code_span else item.split(maxsplit=1)[0]
    candidate = candidate.strip("`'\"()[]<>.,;")
    candidate = candidate.replace("\\", "/")
    candidate = candidate.split("#", maxsplit=1)[0]
    return LINE_SUFFIX.sub("", candidate)


def is_repository_relative(candidate: str) -> bool:
    if not candidate or "://" in candidate or candidate.startswith(("/", "../", "./")):
        return False
    suffix = Path(candidate).suffix.lower()
    return (
        candidate in REPOSITORY_ROOT_FILES
        or candidate.startswith(REPOSITORY_PREFIXES)
        or suffix in REPOSITORY_PATH_SUFFIXES
    )


def related_entries(source: Path, text: str) -> list[RelatedEntry]:
    entries: list[RelatedEntry] = []
    in_related_block = False

    for line_number, line in enumerate(text.splitlines(), start=1):
        content = comment_content(line)
        if content == "Related:":
            in_related_block = True
            continue
        if not in_related_block:
            continue
        if not content:
            in_related_block = False
            continue
        if not content.startswith("-"):
            in_related_block = False
            continue

        candidate = entry_candidate(content[1:].strip())
        if is_repository_relative(candidate):
            entries.append(RelatedEntry(source, line_number, candidate))

    return entries


def repository_source_files(repo: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", "SkullbonezSource"],
        cwd=repo,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"git ls-files failed: {message}")

    relative_paths = result.stdout.decode("utf-8", errors="replace").split("\0")
    return sorted(
        repo / relative
        for relative in relative_paths
        if relative and Path(relative).suffix.lower() in SOURCE_SUFFIXES and ( repo / relative ).is_file()
    )


def explicit_source_files(paths: Iterable[Path]) -> list[Path]:
    files: set[Path] = set()
    for path in paths:
        resolved = path.resolve()
        if resolved.is_file() and resolved.suffix.lower() in SOURCE_SUFFIXES:
            files.add(resolved)
        elif resolved.is_dir():
            files.update(
                candidate.resolve()
                for candidate in resolved.rglob("*")
                if candidate.is_file() and candidate.suffix.lower() in SOURCE_SUFFIXES
            )
    return sorted(files)


def entry_resolves(repo: Path, entry: RelatedEntry, source_files_by_name: dict[str, list[Path]]) -> bool:
    relative = Path(*entry.path.split("/"))
    base = entry.source.parent.resolve()
    repo = repo.resolve()

    while True:
        if (base / relative).exists():
            return True
        if base == repo or repo not in base.parents:
            break
        base = base.parent

    if "/" not in entry.path:
        return len(source_files_by_name.get(relative.name, [])) == 1
    return False


def unresolved_entries(repo: Path, source_files: Iterable[Path]) -> tuple[int, list[RelatedEntry]]:
    source_files = list(source_files)
    source_files_by_name: dict[str, list[Path]] = {}
    for source in source_files:
        source_files_by_name.setdefault(source.name, []).append(source)

    entry_count = 0
    findings: list[RelatedEntry] = []

    for source in source_files:
        text = source.read_text(encoding="utf-8-sig")
        for entry in related_entries(source, text):
            entry_count += 1
            if not entry_resolves(repo, entry, source_files_by_name):
                findings.append(entry)

    return entry_count, findings


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="related_paths_") as directory:
        repo = Path(directory)
        owner = repo / "SkullbonezSource" / "Core" / "Owner.h"
        owner.parent.mkdir(parents=True)
        owner.write_text("// fixture\n", encoding="utf-8")
        root_props = repo / "FixtureSettings.props"
        root_props.write_text("<!-- fixture -->\n", encoding="utf-8")

        source = repo / "SkullbonezSource" / "Runtime" / "Fixture.h"
        source = repo / "SkullbonezSource" / "Runtime" / "App" / "Fixture.h"
        source.parent.mkdir(parents=True)
        local_owner = source.parent / "LocalOwner.h"
        local_owner.write_text("// fixture\n", encoding="utf-8")
        ancestor_owner = repo / "SkullbonezSource" / "Runtime" / "Scene" / "Request.cpp"
        ancestor_owner.parent.mkdir(parents=True)
        ancestor_owner.write_text("// fixture\n", encoding="utf-8")
        unique_owner = repo / "SkullbonezSource" / "Input" / "UniqueOwner.h"
        unique_owner.parent.mkdir(parents=True)
        unique_owner.write_text("// fixture\n", encoding="utf-8")
        duplicate_a = repo / "SkullbonezSource" / "A" / "Duplicate.h"
        duplicate_b = repo / "SkullbonezSource" / "B" / "Duplicate.h"
        duplicate_a.parent.mkdir(parents=True)
        duplicate_b.parent.mkdir(parents=True)
        duplicate_a.write_text("// fixture\n", encoding="utf-8")
        duplicate_b.write_text("// fixture\n", encoding="utf-8")
        fixture = """/*
Related:
  - SkullbonezSource/Core/Owner.h
  - FixtureSettings.props
  - LocalOwner.h
  - Scene/Request.cpp
  - UniqueOwner.h
  - ownership vocabulary
  - Duplicate.h
  - SkullbonezSource/Core/Missing.h
*/
"""
        source.write_text(fixture, encoding="utf-8")

        entries = related_entries(source, fixture)
        assert [entry.path for entry in entries] == [
            "SkullbonezSource/Core/Owner.h",
            "FixtureSettings.props",
            "LocalOwner.h",
            "Scene/Request.cpp",
            "UniqueOwner.h",
            "Duplicate.h",
            "SkullbonezSource/Core/Missing.h",
        ]

        source_files = [
            source,
            local_owner,
            ancestor_owner,
            unique_owner,
            duplicate_a,
            duplicate_b,
        ]
        entry_count, findings = unresolved_entries(repo, source_files)
        assert entry_count == 7
        assert [(finding.line, finding.path) for finding in findings] == [
            (9, "Duplicate.h"),
            (10, "SkullbonezSource/Core/Missing.h"),
        ]

    print(
        "SELF_TEST_PASS: Related paths resolve root/local/ancestor/unique targets, "
        "ignore bare topics, and reject ambiguous or dead targets."
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="Repository root. Defaults to cwd.")
    parser.add_argument("--self-test", action="store_true", help="Run positive and negative fixtures.")
    parser.add_argument("paths", nargs="*", type=Path, help="Optional source files or directories.")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        return 0

    repo = args.repo.resolve()
    try:
        source_files = explicit_source_files(args.paths) if args.paths else repository_source_files(repo)
        entry_count, findings = unresolved_entries(repo, source_files)
    except (OSError, RuntimeError, UnicodeError) as error:
        print(f"FAIL: Related path check could not complete: {error}")
        return 1

    for finding in findings:
        print(f"{finding.source.relative_to(repo)}:{finding.line}: dead Related path: {finding.path}")

    print(
        f"Related path summary: scanned={len(source_files)} "
        f"repository_paths={entry_count} findings={len(findings)}"
    )
    if findings:
        print("FAIL: Resolve every repository-relative Related path.")
        return 1

    print("PASS: All repository-relative Related paths resolve.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
