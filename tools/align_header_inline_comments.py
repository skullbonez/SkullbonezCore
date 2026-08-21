"""File: tools/align_header_inline_comments.py

Purpose:
  Align trailing inline comments in C++ headers after the primary formatter.


  This is a header-only post-pass for clang-format. Public header declarations
  often carry caller-contract comments; keeping them in one file-wide vertical
  column makes declaration blocks easier to scan without touching implementation
  files.

Summary:
  The pass first restores repository-owned assignment-head and compact-call
  layout, then rejoins eligible declarations and aligns their trailing comments
  without changing code tokens.

Glossary:
  Trailing inline comment: A // comment after code on the same line.
  Column limit: Maximum preferred width for code tokens. A trailing comment may
  exceed it rather than force a short declaration's type and name apart.

Invariants:
  - Only C++ header-style trailing comments are aligned; preprocessor lines,
    comment-only lines, and clang-format directives are skipped.
  - A declaration that fits without its comment stays on one line even when the
    complete commented line exceeds the preferred column limit.
  - The script rewrites text layout only and must not change code tokens.
"""

from __future__ import annotations

import argparse
import errno
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from separate_multiline_cpp_declarations import (
    ensure_comment_spacing,
    ensure_control_flow_spacing,
    normalize_assignment_heads,
    normalize_compact_parenthesized_expressions,
    normalize_first_argument_heads,
)


DEFAULT_COLUMN_LIMIT = 125
DEFAULT_MIN_SPACES = 1


@dataclass(frozen=True)
class CommentLine:
    index: int
    code: str
    comment: str

    @property
    def min_column(self) -> int:
        return len(self.code) + DEFAULT_MIN_SPACES


@dataclass
class FormatResult:
    text: str
    aligned_comments: int = 0
    joined_declarations: int = 0


def find_line_comment(line: str) -> int | None:
    """Return the index of // when it is outside simple string/char literals."""

    in_string = False
    in_char = False
    escaped = False

    i = 0
    while i < len(line) - 1:
        ch = line[i]

        if escaped:
            escaped = False
            i += 1
            continue

        if (in_string or in_char) and ch == "\\":
            escaped = True
            i += 1
            continue

        if not in_char and ch == '"':
            in_string = not in_string
            i += 1
            continue

        if not in_string and ch == "'":
            in_char = not in_char
            i += 1
            continue

        if not in_string and not in_char and line[i : i + 2] == "//":
            return i

        i += 1

    return None


def split_trailing_comment(line: str) -> tuple[str, str] | None:
    comment_start = find_line_comment(line)
    if comment_start is None:
        return None

    code = line[:comment_start].rstrip()
    comment = line[comment_start:].strip()
    stripped = code.strip()

    if not stripped:
        return None
    if stripped.startswith("#"):
        return None
    if stripped.startswith("}"):
        return None
    if code.rstrip().endswith("\\"):
        return None
    if "clang-format" in comment:
        return None

    return code, comment


def is_barrier_line(line: str) -> bool:
    stripped = line.strip()
    if not stripped:
        return True
    if stripped.startswith("//"):
        return True
    if stripped.startswith("/*") or stripped.startswith("*"):
        return True
    if stripped.startswith("#"):
        return True
    if stripped in {"public:", "private:", "protected:"}:
        return True
    if stripped in {"{", "}", "};"}:
        return True
    if stripped.startswith("}"):
        return True
    if stripped.endswith("{"):
        return True
    return False


def can_join_previous(line: str) -> bool:
    stripped = line.strip()
    if is_barrier_line(line):
        return False
    if split_trailing_comment(line) is not None:
        return False
    if "{" in stripped or "}" in stripped:
        return False
    if stripped.endswith(";"):
        return False
    if stripped.endswith(":"):
        return False
    return True


def normalize_code_fragments(fragments: Iterable[str]) -> str:
    return " ".join(fragment.strip() for fragment in fragments if fragment.strip())


def can_rejoin_statement(code: str, column_limit: int) -> bool:
    if ";" not in code:
        return False
    if "{" in code or "}" in code:
        return False
    if code.count("(") != code.count(")"):
        return False
    # Invariant: trailing prose never forces clang-format's type/name split.
    # The code itself must still fit, so genuinely wide declarations retain
    # their readable continuation layout.
    return len(code.rstrip()) <= column_limit


def rejoin_commented_declarations(lines: list[str], column_limit: int) -> tuple[list[str], int]:
    output: list[str] = []
    joined = 0

    for line in lines:
        split = split_trailing_comment(line)
        if split is None:
            output.append(line)
            continue

        code, comment = split
        preceding: list[str] = []
        while output and len(preceding) < 8 and can_join_previous(output[-1]):
            preceding.insert(0, output.pop())

        if not preceding:
            output.append(line)
            continue

        indent = preceding[0][: len(preceding[0]) - len(preceding[0].lstrip())]
        merged_code = indent + normalize_code_fragments([*preceding, code])

        if can_rejoin_statement(merged_code, column_limit):
            output.append(f"{merged_code}  {comment}")
            joined += 1
        else:
            output.extend(preceding)
            output.append(line)

    return output, joined


def align_comment_group(lines: list[str], comments: list[CommentLine], column_limit: int) -> int:
    changed = 0
    del column_limit

    column = max(comment.min_column for comment in comments)
    for comment_line in comments:
        replacement = f"{comment_line.code}{' ' * (column - len(comment_line.code))}{comment_line.comment}"
        if replacement != lines[comment_line.index]:
            lines[comment_line.index] = replacement
            changed += 1

    return changed


def align_file_comments(lines: list[str], column_limit: int) -> int:
    comments: list[CommentLine] = []

    for index, line in enumerate(lines):
        split = split_trailing_comment(line)
        if split is None:
            continue
        code, comment = split
        comments.append(CommentLine(index, code, comment))

    if not comments:
        return 0

    return align_comment_group(lines, comments, column_limit)


def format_header_text(text: str, column_limit: int) -> FormatResult:
    trailing_newline = text.endswith("\n")
    lines = text.splitlines()
    lines, joined = rejoin_commented_declarations(lines, column_limit)
    aligned = align_file_comments(lines, column_limit)
    formatted = "\n".join(lines)

    if trailing_newline:
        formatted += "\n"

    return FormatResult(formatted, aligned, joined)


def run_self_tests() -> None:
    long_comment = "// " + "behavior ownership detail " * 6
    split_declaration = [
        "    InteractionAutomationRecorder",
        f"        m_interactionRecorder; {long_comment}",
    ]
    joined, joined_count = rejoin_commented_declarations(split_declaration, DEFAULT_COLUMN_LIMIT)
    expected_code = "    InteractionAutomationRecorder m_interactionRecorder;"
    if joined_count != 1 or len(joined) != 1 or not joined[0].startswith(expected_code):
        raise AssertionError("a fitting declaration was not rejoined ahead of its long trailing comment")
    if len(joined[0]) <= DEFAULT_COLUMN_LIMIT:
        raise AssertionError("long-comment fixture does not prove permitted column overflow")

    oversized_type = "Oversized" + ("Type" * 32)
    oversized_declaration = [
        f"    {oversized_type}",
        f"        m_value; {long_comment}",
    ]
    retained, retained_count = rejoin_commented_declarations(oversized_declaration, DEFAULT_COLUMN_LIMIT)
    if retained_count != 0 or retained != oversized_declaration:
        raise AssertionError("a genuinely oversized declaration was incorrectly collapsed")

    print("SELF_TEST_PASS: long comments cannot split fitting declarations; oversized code remains wrapped.")


def iter_header_files(paths: Iterable[Path]) -> list[Path]:
    headers: set[Path] = set()

    for path in paths:
        resolved = path.resolve()
        if resolved.is_file() and resolved.suffix.lower() == ".h":
            headers.add(resolved)
        elif resolved.is_dir():
            headers.update(file.resolve() for file in resolved.rglob("*.h") if file.is_file())

    return sorted(headers)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


def write_text(path: Path, text: str) -> None:
    # Hazard: Windows can transiently reject a header rewrite while the
    # repository-wide formatter rapidly closes and reopens neighboring files.
    # A bounded retry keeps the deterministic pass moving without hiding a
    # persistent permission or path failure.
    for attempt in range(5):
        try:
            path.write_text(text, encoding="utf-8", newline="\n")
            return
        except OSError as error:
            if error.errno not in {errno.EACCES, errno.EINVAL} or attempt == 4:
                raise
            time.sleep(0.05 * (attempt + 1))


def clang_format_text(clang_format: Path, path: Path, text: str) -> str:
    result = subprocess.run(
        [str(clang_format), f"--assume-filename={path}"],
        input=text.encode("utf-8"),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"clang-format failed for {path}: {stderr}")

    return result.stdout.decode("utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="Repository root. Defaults to cwd.")
    parser.add_argument("--column-limit", type=int, default=DEFAULT_COLUMN_LIMIT)
    parser.add_argument("--check", action="store_true", help="Fail if any header would change.")
    parser.add_argument(
        "--check-pipeline",
        action="store_true",
        help="Fail if clang-format plus this header pass would change any header.",
    )
    parser.add_argument("--clang-format", type=Path, help="clang-format executable for --check-pipeline.")
    parser.add_argument("--write", action="store_true", help="Rewrite headers in place.")
    parser.add_argument("--self-test", action="store_true", help="Run declaration/comment policy fixtures.")
    parser.add_argument("paths", nargs="*", type=Path, help="Header files or directories. Defaults to SkullbonezSource.")
    args = parser.parse_args()

    selected_modes = sum(1 for enabled in (args.check, args.check_pipeline, args.write, args.self_test) if enabled)
    if selected_modes > 1:
        parser.error("--check, --check-pipeline, --write, and --self-test are mutually exclusive")
    if args.check_pipeline and args.clang_format is None:
        parser.error("--check-pipeline requires --clang-format")
    if args.self_test:
        run_self_tests()
        return 0

    mode = "write" if args.write else "pipeline" if args.check_pipeline else "check"
    repo = args.repo.resolve()
    paths = args.paths or [repo / "SkullbonezSource"]
    headers = iter_header_files(paths)

    changed_files: list[Path] = []
    aligned_comments = 0
    joined_declarations = 0

    for header in headers:
        original = read_text(header)
        source = original
        if mode == "pipeline" or (mode == "write" and args.clang_format is not None):
            try:
                source = clang_format_text(args.clang_format, header, original)
            except RuntimeError as error:
                print(f"FAIL: {error}")
                return 1

        source, _ = normalize_assignment_heads(source)
        source, _ = normalize_compact_parenthesized_expressions(source)
        source, _ = normalize_first_argument_heads(source)
        source, _ = ensure_comment_spacing(source)
        source, _ = ensure_control_flow_spacing(source)
        result = format_header_text(source, args.column_limit)
        aligned_comments += result.aligned_comments
        joined_declarations += result.joined_declarations

        if result.text == original:
            continue

        changed_files.append(header)
        if mode == "write":
            write_text(header, result.text)

    if changed_files:
        if mode == "write":
            action = "Updated"
        elif mode == "pipeline":
            action = "Needs format pipeline"
        else:
            action = "Needs alignment"
        print(
            f"{action}: {len(changed_files)} header(s), "
            f"{aligned_comments} comment line(s), {joined_declarations} declaration join(s)."
        )
        for path in changed_files:
            print(f"  {path.relative_to(repo)}")
        return 0 if mode == "write" else 1

    print(f"PASS: {len(headers)} header(s) already have aligned inline comments.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
