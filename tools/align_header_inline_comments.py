"""File: tools/align_header_inline_comments.py
Purpose:
  Align trailing inline comments in C++ headers.

Mental model:
  This is a header-only post-pass for clang-format. Public header declarations
  often carry caller-contract comments; keeping them in one file-wide vertical
  column makes declaration blocks easier to scan without touching implementation
  files.

Glossary:
  Trailing inline comment: A // comment after code on the same line.
  Column limit: Maximum preferred line width used when aligning comments.

Invariants:
  - Only C++ header-style trailing comments are aligned; preprocessor lines,
    comment-only lines, and clang-format directives are skipped.
  - The script rewrites text layout only and must not change code tokens.

Related:
  - Agentic/Reference/comment-style-guide.md
"""

from __future__ import annotations

import argparse
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


DEFAULT_COLUMN_LIMIT = 120
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


def can_rejoin_statement(code: str, comment: str, column_limit: int) -> bool:
    if ";" not in code:
        return False
    if "{" in code or "}" in code:
        return False
    if code.count("(") != code.count(")"):
        return False
    return len(code) + DEFAULT_MIN_SPACES + len(comment) <= column_limit


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

        if can_rejoin_statement(merged_code.strip(), comment, column_limit):
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
    path.write_text(text, encoding="utf-8", newline="\n")


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
    parser.add_argument("paths", nargs="*", type=Path, help="Header files or directories. Defaults to SkullbonezSource.")
    args = parser.parse_args()

    selected_modes = sum(1 for enabled in (args.check, args.check_pipeline, args.write) if enabled)
    if selected_modes > 1:
        parser.error("--check, --check-pipeline, and --write are mutually exclusive")
    if args.check_pipeline and args.clang_format is None:
        parser.error("--check-pipeline requires --clang-format")

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
        if mode == "pipeline":
            try:
                source = clang_format_text(args.clang_format, header, original)
            except RuntimeError as error:
                print(f"FAIL: {error}")
                return 1

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
