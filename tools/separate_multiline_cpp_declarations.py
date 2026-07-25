"""File: tools/separate_multiline_cpp_declarations.py

Purpose:
  Adds a paragraph break after a wrapped local declaration once clang-format
  has made the statement's opening and closing delimiters structurally clear.

Summary:
  clang-format owns token layout but intentionally does not create semantic
  paragraph breaks. This post-pass recognizes only indented declarations whose
  multiline statement ends with `);` or `};`, then separates that declaration
  from the next statement.

Glossary:
  Paragraph break: One empty line between adjacent local statements.
  Brace-line initializer: Aggregate layout whose first value follows `{` and
    whose continuation values align beneath that first value.

Invariants:
  - The pass inserts blank lines only; it never changes or reorders code tokens.
  - Global declarations, single-line declarations, and control-flow blocks are
    outside the rule.
  - Running the pass repeatedly produces byte-identical output.

Related:
  - .clang-format owns the primary C++ layout.
  - tools/format_fix.bat applies this pass after clang-format.
  - tools/validate_format.bat checks both formatting stages.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


MULTILINE_DECLARATION_END = re.compile(
    r"^(?P<indent>[ \t]*)(?:(?P<aligned_paren>\))|(?P<braced>.*\}));(?:\s*//.*)?$"
)
DECLARATION_START = re.compile(
    r"^(?:(?:const|constexpr|consteval|constinit|static|thread_local|volatile)\s+)*"
    r"(?:[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s*<.*>)?)"
    r"(?:\s*[*&]+\s*|\s+)"
    r"[A-Za-z_]\w*(?:\s*(?:=|\{|\[))"
)
FOLLOWING_CLOSERS = ("}", "else", "catch", "while (", "while(", ")", "]", ",")
MAX_REPORTED_FILES = 40


@dataclass(frozen=True)
class SpacingResult:
    text: str
    inserted_breaks: int = 0


def indentation(line: str) -> str:
    return line[: len(line) - len(line.lstrip(" \t"))]


def is_local_declaration_start(line: str, expected_indent: str | None) -> bool:
    line_indent = indentation(line)
    if not line_indent:
        return False
    if expected_indent is not None and line_indent != expected_indent:
        return False
    return DECLARATION_START.match(line.strip()) is not None


def find_declaration_start(lines: list[str], end_index: int, expected_indent: str | None) -> int | None:
    for index in range(end_index - 1, -1, -1):
        line = lines[index]
        if not line.strip():
            return None

        line_indent = indentation(line)
        if expected_indent is not None and len(line_indent) < len(expected_indent):
            return None
        if is_local_declaration_start(line, expected_indent):
            return index

    return None


def should_separate_after(lines: list[str], end_index: int) -> bool:
    match = MULTILINE_DECLARATION_END.match(lines[end_index])
    if match is None:
        return False

    expected_indent = match.group("indent") if match.group("aligned_paren") else None
    if find_declaration_start(lines, end_index, expected_indent) is None:
        return False

    next_index = end_index + 1
    if next_index >= len(lines) or not lines[next_index].strip():
        return False

    next_line = lines[next_index].lstrip()
    return not next_line.startswith(FOLLOWING_CLOSERS)


def separate_multiline_declarations(text: str) -> SpacingResult:
    trailing_newline = text.endswith("\n")
    lines = text.splitlines()
    output: list[str] = []
    inserted_breaks = 0

    for index, line in enumerate(lines):
        output.append(line)
        if should_separate_after(lines, index):
            output.append("")
            inserted_breaks += 1

    formatted = "\n".join(output)
    if trailing_newline:
        formatted += "\n"

    return SpacingResult(formatted, inserted_breaks)


def iter_implementation_files(paths: Iterable[Path]) -> list[Path]:
    implementations: set[Path] = set()

    for path in paths:
        resolved = path.resolve()
        if resolved.is_file() and resolved.suffix.lower() == ".cpp":
            implementations.add(resolved)
        elif resolved.is_dir():
            implementations.update(file.resolve() for file in resolved.rglob("*.cpp") if file.is_file())

    return sorted(implementations)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


def write_text(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8", newline="\n")


def run_self_test() -> int:
    compact = """void Example()
{
    const Result first = Build(
        input
    );
    Owners owners{
        first
    };
    const Input second{
        owners
    };
#ifdef ENABLE_PROBE
    Probe();
#endif
}
"""
    expected = """void Example()
{
    const Result first = Build(
        input
    );

    Owners owners{
        first
    };

    const Input second{
        owners
    };

#ifdef ENABLE_PROBE
    Probe();
#endif
}
"""
    first = separate_multiline_declarations(compact)
    second = separate_multiline_declarations(first.text)
    if first.text != expected or first.inserted_breaks != 3:
        print("SELF_TEST_FAIL: multiline declarations were not separated as expected.", file=sys.stderr)
        return 1
    if second.text != expected or second.inserted_breaks != 0:
        print("SELF_TEST_FAIL: the spacing pass is not idempotent.", file=sys.stderr)
        return 1

    brace_line = """void Example()
{
    Owners owners { first,
                    second };
    Use( owners );
}
"""
    brace_line_expected = """void Example()
{
    Owners owners { first,
                    second };

    Use( owners );
}
"""
    if separate_multiline_declarations(brace_line).text != brace_line_expected:
        print("SELF_TEST_FAIL: brace-line initializer was not separated.", file=sys.stderr)
        return 1

    untouched = """void Example()
{
    const Result value = Build();
    if ( value )
    {
        Use( value );
    }
}
"""
    if separate_multiline_declarations(untouched).text != untouched:
        print("SELF_TEST_FAIL: single-line declarations or control flow changed.", file=sys.stderr)
        return 1

    print("SELF_TEST_PASS: multiline declaration spacing is stable and token-preserving.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="Repository root. Defaults to cwd.")
    parser.add_argument("--check", action="store_true", help="Fail if any implementation file would change.")
    parser.add_argument("--write", action="store_true", help="Rewrite implementation files in place.")
    parser.add_argument("--stdin", action="store_true", help="Format stdin to stdout without touching files.")
    parser.add_argument("--self-test", action="store_true", help="Run focused formatter regression cases.")
    parser.add_argument("paths", nargs="*", type=Path, help="C++ files or directories. Defaults to SkullbonezSource.")
    args = parser.parse_args()

    selected_modes = sum(1 for enabled in (args.check, args.write, args.stdin, args.self_test) if enabled)
    if selected_modes > 1:
        parser.error("--check, --write, --stdin, and --self-test are mutually exclusive")

    if args.self_test:
        return run_self_test()
    if args.stdin:
        sys.stdout.write(separate_multiline_declarations(sys.stdin.read()).text)
        return 0

    mode = "write" if args.write else "check"
    repo = args.repo.resolve()
    paths = args.paths or [repo / "SkullbonezSource"]
    implementations = iter_implementation_files(paths)
    changed_files: list[tuple[Path, int]] = []

    for implementation in implementations:
        original = read_text(implementation)
        result = separate_multiline_declarations(original)
        if result.text == original:
            continue

        changed_files.append((implementation, result.inserted_breaks))
        if mode == "write":
            write_text(implementation, result.text)

    if changed_files:
        action = "Updated" if mode == "write" else "Needs declaration spacing"
        print(f"{action}: {len(changed_files)} implementation file(s).")
        # Invariant: validation output stays bounded even during a repository-wide
        # style migration; the summary preserves the full affected-file count.
        for path, inserted_breaks in changed_files[:MAX_REPORTED_FILES]:
            print(f"  {path.relative_to(repo)} ({inserted_breaks} paragraph break(s))")
        omitted_files = len(changed_files) - MAX_REPORTED_FILES
        if omitted_files > 0:
            print(f"  ... {omitted_files} additional file(s) omitted")
        return 0 if mode == "write" else 1

    print(f"PASS: {len(implementations)} implementation file(s) already separate multiline declarations.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
