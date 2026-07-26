"""File: tools/separate_multiline_cpp_declarations.py

Purpose:
  Adds paragraph breaks after wrapped local statements and completed
  condition/loop blocks once clang-format has made their boundaries clear.
  It also rejects assignment lines that strand `=` without the expression.
  One-to-three short parenthesized arguments remain on one line. Wider
  expressions keep the first argument beside the opening parenthesis and align
  continuations beneath it. Standalone comments and control-flow blocks receive
  the owner-requested paragraph space above them.

Summary:
  clang-format owns token layout but intentionally does not create semantic
  paragraph breaks. This post-pass recognizes indented multiline statements
  with an unambiguous declaration, assignment, or call start. It also matches
  braces to separate completed if/else, switch, for, while, and do/while blocks
  from the next statement without splitting a control-flow chain.

Glossary:
  Paragraph break: One empty line between adjacent local statements.
  Brace-line initializer: Aggregate layout whose first value follows `{` and
    whose continuation values align beneath that first value.
  Compact call: A parenthesized expression with one to three simple arguments
    whose complete line stays within the repository limit.
  Control-flow chain: One if/else-if/else or do/while unit whose internal
    clauses must stay adjacent.

Invariants:
  - The pass changes whitespace only; it never changes or reorders code tokens.
  - Global statements, single-line statements, functions, and ordinary scopes
    are outside the rule.
  - A break is never inserted between `}` and `else`, before an enclosing `}`,
    or between a `do` body and its trailing `while`.
  - An assignment line never ends at `=`; the first expression token stays on
    the same line.
  - A simple single argument stays beside its opening parenthesis even when the
    complete declaration exceeds the ordinary column preference.
  - One to three simple arguments stay together when the complete line is at
    most 125 columns.
  - A wrapped expression never leaves its opening parenthesis on an otherwise
    parameter-free line.
  - Standalone comment groups and condition/loop blocks have one blank line
    above them.
  - Running the pass repeatedly produces byte-identical output.

Related:
  - .clang-format owns the primary C++ layout.
  - tools/format_fix.bat applies this pass after clang-format.
  - tools/validate_format.bat checks both formatting stages.
"""

from __future__ import annotations

import argparse
import bisect
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


MULTILINE_STATEMENT_END = re.compile(
    r"^(?P<indent>[ \t]*)(?P<body>.+);(?:\s*//.*)?$"
)
DECLARATION_START = re.compile(
    r"^(?:(?:const|constexpr|consteval|constinit|static|thread_local|volatile)\s+)*"
    r"(?:[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s*<.*>)?)"
    r"(?:\s*[*&]+\s*|\s+)"
    r"[A-Za-z_]\w*(?:\s*(?:=|\{|\[))"
)
ASSIGNMENT_START = re.compile(
    r"^[A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*|\[[^\]]+\])*\s*=\s*.+$"
)
CALL_START = re.compile(
    r"^(?:[A-Za-z_]\w*::)*[A-Za-z_]\w*"
    r"(?:(?:\.|->)[A-Za-z_]\w*)*\s*\(.+$"
)
FOLLOWING_CLOSERS = ("}", "else", "catch", ")", "]", ",", "#else", "#elif", "#endif")
CONTROL_FLOW_HEADER = re.compile(r"^(?P<kind>if|else(?:\s+if)?|for|while|switch|do)\b")
STRANDED_ASSIGNMENT = re.compile(r"(?<![=!<>])=(?!=)\s*(?://.*)?$")
MAX_REPORTED_FILES = 40
COMPACT_ARGUMENT_COLUMN_LIMIT = 125
MAX_COMPACT_ARGUMENTS = 3


@dataclass(frozen=True)
class SpacingResult:
    text: str
    inserted_breaks: int = 0
    joined_assignments: int = 0
    joined_compact_calls: int = 0


def indentation(line: str) -> str:
    return line[: len(line) - len(line.lstrip(" \t"))]


def first_parameter_boundary(expression: str) -> int | None:
    opening_parenthesis = expression.find("(")
    if opening_parenthesis < 0:
        return None

    parenthesis_depth = 0
    bracket_depth = 0
    brace_depth = 0
    angle_depth = 0
    for index in range(opening_parenthesis, len(expression)):
        char = expression[index]
        if char == "(":
            parenthesis_depth += 1
        elif char == ")":
            parenthesis_depth -= 1
        elif char == "[":
            bracket_depth += 1
        elif char == "]":
            bracket_depth -= 1
        elif char == "{":
            brace_depth += 1
        elif char == "}":
            brace_depth -= 1
        elif char == "<":
            angle_depth += 1
        elif char == ">" and angle_depth > 0:
            angle_depth -= 1
        elif (
            char == ","
            and parenthesis_depth == 1
            and bracket_depth == 0
            and brace_depth == 0
            and angle_depth == 0
        ):
            return index + 1

    return None


def normalize_assignment_heads(text: str) -> tuple[str, int]:
    """Keep the first assigned expression token on the assignment line."""
    trailing_newline = text.endswith("\n")
    lines = text.splitlines()
    output: list[str] = []
    joined_assignments = 0
    index = 0

    while index < len(lines):
        line = lines[index]
        if (
            STRANDED_ASSIGNMENT.search(line) is None
            or index + 1 >= len(lines)
            or not lines[index + 1].strip()
        ):
            output.append(line)
            index += 1
            continue

        expression = lines[index + 1].lstrip()
        boundary = first_parameter_boundary(expression)
        if boundary is None:
            output.append(f"{line.rstrip()} {expression}")
        else:
            joined_head = f"{line.rstrip()} {expression[:boundary]}"
            opening_parenthesis = joined_head.find("(", len(line.rstrip()))
            output.append(joined_head)
            remainder = expression[boundary:].lstrip()
            if remainder:
                output.append(" " * (opening_parenthesis + 1) + remainder)
            else:
                original_opening_parenthesis = len(indentation(lines[index + 1])) + expression.find("(")
                indentation_delta = opening_parenthesis - original_opening_parenthesis
                continuation_index = index + 2
                parenthesis_depth = expression.count("(") - expression.count(")")
                while continuation_index < len(lines) and parenthesis_depth > 0:
                    continuation = lines[continuation_index]
                    if not continuation.strip():
                        continuation_index += 1
                        continue
                    continuation_indent = indentation(continuation)
                    shifted_indent = " " * max(0, len(continuation_indent) + indentation_delta)
                    output.append(shifted_indent + continuation.lstrip())
                    parenthesis_depth += continuation.count("(") - continuation.count(")")
                    continuation_index += 1
                index = continuation_index - 2

        joined_assignments += 1
        index += 2

    formatted = "\n".join(output)
    if trailing_newline:
        formatted += "\n"
    return formatted, joined_assignments


def _simple_argument_with_close(line: str) -> str | None:
    stripped = line.strip()
    if not stripped or stripped.count("(") != 0 or stripped.count(")") != 1:
        return None
    if "//" in stripped or "/*" in stripped or "*/" in stripped:
        return None

    close_index = stripped.rfind(")")
    argument = stripped[:close_index].strip()
    suffix = stripped[close_index:]
    if not argument or "," in argument or any(char in argument for char in "{};"):
        return None
    if not re.fullmatch(r"\)\s*(?:[,;{]|\b(?:const|noexcept|override|final)\b.*)?", suffix):
        return None
    return stripped


def _simple_argument_with_comma(line: str) -> str | None:
    stripped = line.strip()
    if not stripped.endswith(",") or "(" in stripped or ")" in stripped:
        return None
    if "//" in stripped or "/*" in stripped or "*/" in stripped:
        return None

    argument = stripped[:-1].strip()
    if not argument or "," in argument or any(char in argument for char in "{};"):
        return None
    return stripped


def normalize_compact_parenthesized_expressions(text: str) -> tuple[str, int]:
    """Keep up to three short arguments together and never strand an opening parenthesis."""
    trailing_newline = text.endswith("\n")
    lines = text.splitlines()
    output: list[str] = []
    joined_compact_calls = 0
    index = 0

    while index < len(lines):
        line = lines[index]
        stripped = line.strip()
        if (
            not line.rstrip().endswith("(")
            or re.fullmatch(r"(?:if|for|while|switch|catch)\s*\(", stripped) is not None
            or index + 1 >= len(lines)
            or not lines[index + 1].strip()
        ):
            output.append(line)
            index += 1
            continue

        arguments: list[str] = []
        argument_index = index + 1
        expression_closed = False
        while argument_index < len(lines) and len(arguments) < MAX_COMPACT_ARGUMENTS:
            closing_argument = _simple_argument_with_close(lines[argument_index])
            if closing_argument is not None:
                arguments.append(closing_argument)
                expression_closed = True
                break

            comma_argument = _simple_argument_with_comma(lines[argument_index])
            if comma_argument is None:
                arguments.clear()
                break

            arguments.append(comma_argument)
            argument_index += 1

        if arguments and expression_closed:
            joined = f"{line.rstrip()} {' '.join(arguments)}"
            if len(arguments) == 1 or len(joined) <= COMPACT_ARGUMENT_COLUMN_LIMIT:
                output.append(joined)
                joined_compact_calls += 1
                index = argument_index + 1
                continue

        first_argument = _simple_argument_with_comma(lines[index + 1])
        if first_argument is not None:
            opening_column = len(line.rstrip())
            output.append(f"{line.rstrip()} {first_argument}")
            index += 2
            while index < len(lines):
                continuation = lines[index]
                if not continuation.strip():
                    output.append(continuation)
                    index += 1
                    continue

                output.append(" " * (opening_column + 1) + continuation.lstrip())
                if _simple_argument_with_close(continuation) is not None:
                    break
                index += 1

            joined_compact_calls += 1
            index += 1
            continue

        output.append(line)
        index += 1

    formatted = "\n".join(output)
    if trailing_newline:
        formatted += "\n"
    return formatted, joined_compact_calls


def normalize_first_argument_heads(text: str) -> tuple[str, int]:
    """Move the first argument token line beside any otherwise empty call parenthesis."""
    trailing_newline = text.endswith("\n")
    lines = text.splitlines()
    output: list[str] = []
    joined_heads = 0
    index = 0

    while index < len(lines):
        line = lines[index]
        stripped = line.strip()
        if (
            not line.rstrip().endswith("(")
            or re.fullmatch(r"(?:if|for|while|switch|catch)\s*\(", stripped) is not None
            or index + 1 >= len(lines)
            or not lines[index + 1].strip()
            or standalone_comment_start(lines[index + 1])
            or lines[index + 1].lstrip().startswith("#")
        ):
            output.append(line)
            index += 1
            continue

        opening_column = len(line.rstrip()) - 1
        first_line = lines[index + 1]
        first_indent = len(indentation(first_line))
        indentation_delta = opening_column + 2 - first_indent
        output.append(f"{line.rstrip()} {first_line.lstrip()}")
        parenthesis_depth = 1
        first_masked = mask_cpp_non_code(first_line)
        parenthesis_depth += first_masked.count("(") - first_masked.count(")")
        index += 2

        while index < len(lines) and parenthesis_depth > 0:
            continuation = lines[index]
            if re.fullmatch(r"\s*\)\s*(?:[,;{]|\b(?:const|noexcept|override|final)\b.*)?", continuation):
                if output[-1].lstrip().startswith("#"):
                    output.append(" " * (opening_column + 2) + continuation.strip())
                else:
                    output[-1] = f"{output[-1].rstrip()} {continuation.strip()}"
                masked = mask_cpp_non_code(continuation)
                parenthesis_depth += masked.count("(") - masked.count(")")
                index += 1
                continue

            if continuation.strip():
                continuation_indent = len(indentation(continuation))
                shifted_indent = " " * max(0, continuation_indent + indentation_delta)
                output.append(shifted_indent + continuation.lstrip())
                masked = mask_cpp_non_code(continuation)
                parenthesis_depth += masked.count("(") - masked.count(")")
            else:
                output.append(continuation)
            index += 1

        joined_heads += 1

    formatted = "\n".join(output)
    if trailing_newline:
        formatted += "\n"
    return formatted, joined_heads


def standalone_comment_start(line: str) -> bool:
    stripped = line.lstrip()
    return stripped.startswith("//") or stripped.startswith("/*")


def ensure_comment_spacing(text: str) -> tuple[str, int]:
    """Insert one blank line above each standalone comment group."""
    trailing_newline = text.endswith("\n")
    lines = text.splitlines()
    output: list[str] = []
    inserted_breaks = 0

    for line in lines:
        if (
            standalone_comment_start(line)
            and output
            and output[-1].strip()
            and not standalone_comment_start(output[-1])
            and not output[-1].rstrip().endswith("\\")
            and not line.rstrip().endswith("\\")
        ):
            output.append("")
            inserted_breaks += 1

        output.append(line)

    formatted = "\n".join(output)
    if trailing_newline:
        formatted += "\n"
    return formatted, inserted_breaks


def remove_macro_continuation_breaks(text: str) -> str:
    """Never let a paragraph break terminate a continued preprocessor line."""
    trailing_newline = text.endswith("\n")
    output: list[str] = []

    for line in text.splitlines():
        if not line.strip() and output and output[-1].rstrip().endswith("\\"):
            continue

        output.append(line)

    formatted = "\n".join(output)
    if trailing_newline:
        formatted += "\n"
    return formatted


def _blank_non_newlines(chars: list[str], start: int, end: int) -> None:
    for index in range(start, end):
        if chars[index] not in "\r\n":
            chars[index] = " "


def mask_cpp_non_code(text: str) -> str:
    """Blank comments and literals while preserving brace offsets and lines."""
    chars = list(text)
    length = len(text)
    index = 0
    while index < length:
        if text.startswith("//", index):
            end = text.find("\n", index + 2)
            end = length if end < 0 else end
            _blank_non_newlines(chars, index, end)
            index = end
            continue
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = length if end < 0 else end + 2
            _blank_non_newlines(chars, index, end)
            index = end
            continue

        raw_match = re.match(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(', text[index:])
        if raw_match:
            delimiter = raw_match.group(1)
            close_marker = ")" + delimiter + '"'
            end = text.find(close_marker, index + raw_match.end())
            end = length if end < 0 else end + len(close_marker)
            _blank_non_newlines(chars, index, end)
            index = end
            continue

        quote_start = re.match(r"(?:u8|u|U|L)?(['\"])", text[index:])
        if quote_start:
            quote = quote_start.group(1)
            end = index + quote_start.end()
            while end < length:
                if text[end] == "\\":
                    end += 2
                    continue
                if text[end] == quote:
                    end += 1
                    break
                end += 1
            _blank_non_newlines(chars, index, min(end, length))
            index = min(end, length)
            continue

        index += 1
    return "".join(chars)


def matching_braces(masked: str) -> dict[int, int]:
    stack: list[int] = []
    pairs: dict[int, int] = {}
    for index, char in enumerate(masked):
        if char == "{":
            stack.append(index)
        elif char == "}" and stack:
            pairs[stack.pop()] = index
    return pairs


def control_flow_header_kind(masked_lines: list[str], opening_line: int, opening_column: int) -> str | None:
    fragments: list[str] = []
    same_line_prefix = masked_lines[opening_line][:opening_column].strip()
    if same_line_prefix:
        fragments.append(same_line_prefix)

    for line_index in range(opening_line - 1, max(-1, opening_line - 64), -1):
        stripped = masked_lines[line_index].strip()
        if not stripped:
            continue

        fragments.insert(0, stripped)
        candidate = " ".join(fragments)
        match = CONTROL_FLOW_HEADER.match(candidate)
        if match is not None:
            return match.group("kind").split()[0]

        # A control header cannot cross an earlier completed statement or block.
        if stripped.endswith(";") or stripped in ("{", "}"):
            return None

    return None


def next_nonempty_line(lines: list[str], start_index: int) -> int | None:
    for index in range(start_index, len(lines)):
        if lines[index].strip():
            return index
    return None


def do_while_end_line(lines: list[str], closing_line: int) -> int:
    while_line = next_nonempty_line(lines, closing_line + 1)
    if while_line is None or not lines[while_line].lstrip().startswith(("while (", "while(")):
        return closing_line

    for index in range(while_line, min(len(lines), while_line + 64)):
        if lines[index].rstrip().endswith(";"):
            return index
    return closing_line


def control_flow_end_lines(text: str, lines: list[str]) -> set[int]:
    masked = mask_cpp_non_code(text)
    masked_lines = masked.splitlines()
    line_starts = [0]
    line_starts.extend(match.end() for match in re.finditer("\n", masked))
    result: set[int] = set()

    for opening, closing in matching_braces(masked).items():
        opening_line = bisect.bisect_right(line_starts, opening) - 1
        closing_line = bisect.bisect_right(line_starts, closing) - 1
        if closing_line >= len(masked_lines) or masked_lines[closing_line].strip() != "}":
            continue

        opening_column = opening - line_starts[opening_line]
        kind = control_flow_header_kind(masked_lines, opening_line, opening_column)
        if kind is None:
            continue
        result.add(do_while_end_line(lines, closing_line) if kind == "do" else closing_line)

    return result


def is_local_statement_start(line: str, expected_indent: str | None, maximum_indent: int) -> bool:
    line_indent = indentation(line)
    if not line_indent:
        return False
    if expected_indent is not None and line_indent != expected_indent:
        return False
    if len(line_indent) > maximum_indent:
        return False

    stripped = line.strip()
    if CONTROL_FLOW_HEADER.match(stripped) is not None:
        return False
    return (
        DECLARATION_START.match(stripped) is not None
        or ASSIGNMENT_START.match(stripped) is not None
        or CALL_START.match(stripped) is not None
    )


def find_statement_start(lines: list[str], end_index: int, expected_indent: str | None) -> int | None:
    maximum_indent = len(indentation(lines[end_index]))
    for index in range(end_index - 1, -1, -1):
        line = lines[index]
        if not line.strip():
            return None

        line_indent = indentation(line)
        if expected_indent is not None and len(line_indent) < len(expected_indent):
            return None
        if line.lstrip().startswith("#"):
            return None
        if line.rstrip().endswith(";"):
            return None
        if is_local_statement_start(line, expected_indent, maximum_indent):
            return index

    return None


def following_line_allows_break(lines: list[str], end_index: int) -> bool:
    next_index = end_index + 1
    if next_index >= len(lines) or not lines[next_index].strip():
        return False

    next_line = lines[next_index].lstrip()
    return not next_line.startswith(FOLLOWING_CLOSERS)


def should_separate_after(lines: list[str], end_index: int) -> bool:
    match = MULTILINE_STATEMENT_END.match(lines[end_index])
    if match is None:
        return False

    body = match.group("body").strip()
    expected_indent = match.group("indent") if body in (")", "}") else None
    return find_statement_start(lines, end_index, expected_indent) is not None and following_line_allows_break(
        lines,
        end_index,
    )


def ensure_control_flow_spacing(text: str) -> tuple[str, int]:
    """Insert paragraph space above and below condition/loop blocks."""
    trailing_newline = text.endswith("\n")
    lines = text.splitlines()
    control_ends = control_flow_end_lines(text, lines)
    output: list[str] = []
    inserted_breaks = 0

    for index, line in enumerate(lines):
        stripped = line.strip()
        previous_nonempty = next(
            (candidate for candidate in reversed(output) if candidate.strip()),
            "",
        )
        starts_condition_or_loop = (
            CONTROL_FLOW_HEADER.match(stripped) is not None
            and not stripped.startswith(("else", "catch"))
            and not (stripped.startswith("while") and previous_nonempty.strip() == "}")
        )
        if starts_condition_or_loop and output and output[-1].strip():
            output.append("")
            inserted_breaks += 1

        output.append(line)
        if index in control_ends and following_line_allows_break(lines, index):
            output.append("")
            inserted_breaks += 1

    formatted = "\n".join(output)
    if trailing_newline:
        formatted += "\n"
    return remove_macro_continuation_breaks(formatted), inserted_breaks


def separate_multiline_statements(text: str) -> SpacingResult:
    text, joined_assignments = normalize_assignment_heads(text)
    text, joined_compact_calls = normalize_compact_parenthesized_expressions(text)
    text, joined_first_argument_heads = normalize_first_argument_heads(text)
    joined_compact_calls += joined_first_argument_heads
    text, comment_breaks = ensure_comment_spacing(text)
    text, control_breaks = ensure_control_flow_spacing(text)
    trailing_newline = text.endswith("\n")
    lines = text.splitlines()
    output: list[str] = []
    inserted_breaks = 0

    for index, line in enumerate(lines):
        output.append(line)
        separates_multiline_statement = should_separate_after(lines, index)
        if separates_multiline_statement:
            output.append("")
            inserted_breaks += 1

    formatted = "\n".join(output)
    if trailing_newline:
        formatted += "\n"

    return SpacingResult(
        formatted,
        inserted_breaks + comment_breaks + control_breaks,
        joined_assignments,
        joined_compact_calls,
    )


def iter_implementation_files(paths: Iterable[Path]) -> list[Path]:
    implementations: set[Path] = set()

    for path in paths:
        resolved = path.resolve()
        if resolved.is_file() and resolved.suffix.lower() == ".cpp":
            implementations.add(resolved)
        elif resolved.is_dir():
            implementations.update(file.resolve() for file in resolved.rglob("*.cpp") if file.is_file())

    return sorted(implementations)


def iter_source_files(paths: Iterable[Path]) -> list[Path]:
    sources: set[Path] = set()

    for path in paths:
        resolved = path.resolve()
        if resolved.is_file() and resolved.suffix.lower() in (".cpp", ".h"):
            sources.add(resolved)
        elif resolved.is_dir():
            sources.update(
                file.resolve()
                for pattern in ("*.cpp", "*.h")
                for file in resolved.rglob(pattern)
                if file.is_file()
            )

    return sorted(sources)


def stranded_assignment_lines(text: str) -> list[int]:
    return [
        line_number
        for line_number, line in enumerate(text.splitlines(), start=1)
        if STRANDED_ASSIGNMENT.search(line) is not None
    ]


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


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


def write_text(path: Path, text: str) -> None:
    # Windows compilers and IDE indexers can briefly retain a source handle
    # after reading it. A bounded retry keeps the explicit formatter run
    # deterministic without hiding a persistent permission or path failure.
    attempts = 400
    for attempt in range(attempts):
        try:
            path.write_text(text, encoding="utf-8", newline="\n")
            return
        except OSError:
            if attempt + 1 >= attempts:
                raise
            time.sleep(0.05)


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
    m_lastResult = loader.Load( first,
                                second );
    ApplyFirst( m_lastResult,
                second );
    ApplySecond( m_lastResult,
                 first );
    if (
        m_lastResult.ok
    )
    {
        Observe();
    }
#ifdef ENABLE_PROBE
    Probe();
#endif
}
"""
    expected = """void Example()
{
    const Result first = Build( input );
    Owners owners{
        first
    };

    const Input second{
        owners
    };

    m_lastResult = loader.Load( first,
                                second );

    ApplyFirst( m_lastResult,
                second );

    ApplySecond( m_lastResult,
                 first );

    if (
        m_lastResult.ok
    )
    {
        Observe();
    }

#ifdef ENABLE_PROBE
    Probe();
#endif
}
"""
    first = separate_multiline_statements(compact)
    second = separate_multiline_statements(first.text)
    if first.text != expected or first.inserted_breaks != 6:
        print("SELF_TEST_FAIL: multiline statements were not separated as expected.", file=sys.stderr)
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
    if separate_multiline_statements(brace_line).text != brace_line_expected:
        print("SELF_TEST_FAIL: brace-line initializer was not separated.", file=sys.stderr)
        return 1

    compact_control = """void Example()
{
    const Result value = Build();
    const Owners owners { value, value };
    if ( value )
    {
        Use( value );
    }
}
"""
    compact_control_expected = """void Example()
{
    const Result value = Build();
    const Owners owners { value, value };

    if ( value )
    {
        Use( value );
    }
}
"""
    if separate_multiline_statements(compact_control).text != compact_control_expected:
        print("SELF_TEST_FAIL: compact control flow did not receive space above it.", file=sys.stderr)
        return 1

    macro_body = """void Example()
{
    const auto apply = [&]()
    {
#define APPLY_FIELD( field ) Use( field );
        APPLY_FIELDS( APPLY_FIELD )
#undef APPLY_FIELD
    };
}
"""
    if separate_multiline_statements(macro_body).text != macro_body:
        print("SELF_TEST_FAIL: a macro body was split from its expansion.", file=sys.stderr)
        return 1

    continued_macro = """#define APPLY_WHEN_READY( value ) \\
    do                              \\
    {                               \\
        if ( value )                \\
        {                           \\
            Apply( value );         \\
        }                           \\
    } while ( false )
"""
    if "\n\n" in separate_multiline_statements(continued_macro).text:
        print("SELF_TEST_FAIL: paragraph spacing terminated a continued macro.", file=sys.stderr)
        return 1

    conditional_signature = """Result Build(
    const Input& input
#ifdef _DEBUG
    ,
    DebugOwner& debug
#endif
)
{
    return {};
}
"""
    normalized_conditional_signature = separate_multiline_statements(conditional_signature).text
    if "#endif )" in normalized_conditional_signature or re.search(r"\n\s*#endif\n", normalized_conditional_signature) is None:
        print("SELF_TEST_FAIL: a conditional signature lost its closing parenthesis.", file=sys.stderr)
        return 1

    control_flow = """void Example()
{
    if ( ready )
    {
        Use();
    }
    else
    {
        Recover();
    }
    for ( int index = 0; index < count; ++index )
    {
        Step( index );
    }
    while ( pending )
    {
        Poll();
    }
    do
    {
        Retry();
    }
    while ( retry );
    switch ( mode )
    {
    case 1:
        Select();
        break;
    default:
        break;
    }
    if ( outer )
    {
        if ( inner )
        {
            Nested();
        }
    }
    Finish();
}
"""
    control_flow_expected = """void Example()
{

    if ( ready )
    {
        Use();
    }
    else
    {
        Recover();
    }

    for ( int index = 0; index < count; ++index )
    {
        Step( index );
    }

    while ( pending )
    {
        Poll();
    }

    do
    {
        Retry();
    }
    while ( retry );

    switch ( mode )
    {
    case 1:
        Select();
        break;
    default:
        break;
    }

    if ( outer )
    {

        if ( inner )
        {
            Nested();
        }
    }

    Finish();
}
"""
    control_first = separate_multiline_statements(control_flow)
    control_second = separate_multiline_statements(control_first.text)
    if control_first.text != control_flow_expected or control_first.inserted_breaks != 8:
        print("SELF_TEST_FAIL: control-flow blocks were not separated as expected.", file=sys.stderr)
        return 1
    if control_second.text != control_flow_expected or control_second.inserted_breaks != 0:
        print("SELF_TEST_FAIL: control-flow spacing is not idempotent.", file=sys.stderr)
        return 1

    plain_scope = """void Example()
{
    {
        Scoped();
    }
    Continue();
}
"""
    if separate_multiline_statements(plain_scope).text != plain_scope:
        print("SELF_TEST_FAIL: an ordinary scope was treated as control flow.", file=sys.stderr)
        return 1

    stranded_assignment = """void Example()
{
    const Result result =
        Build();
}
"""
    joined_assignment = """void Example()
{
    const Result result = Build(
        input );
}
"""
    if stranded_assignment_lines(stranded_assignment) != [3] or stranded_assignment_lines(joined_assignment):
        print("SELF_TEST_FAIL: stranded assignment detection is incorrect.", file=sys.stderr)
        return 1

    if separate_multiline_statements(stranded_assignment).text != """void Example()
{
    const Result result = Build();
}
""":
        print("SELF_TEST_FAIL: a stranded call assignment was not joined.", file=sys.stderr)
        return 1

    stranded_multiline_call = """void Example()
{
    const Result result =
        Build( first,
               second );
}
"""
    normalized_multiline_call = separate_multiline_statements(stranded_multiline_call).text
    if (
        "const Result result = Build( first,\n" not in normalized_multiline_call
        or "\n                                 second );" not in normalized_multiline_call
        or stranded_assignment_lines(normalized_multiline_call)
    ):
        print("SELF_TEST_FAIL: a multiline call assignment was not joined and aligned.", file=sys.stderr)
        return 1

    replay_timeline_assignment = """void Example()
{
    const ReplaySceneTimelineResetInput timelineReset =
        DescribeReplaySceneTimeline( m_sceneController,
                                     sceneOverrides,
                                     sceneState,
                                     sceneObjectCapacity,
                                     generatedObjectTypeOverride );
}
"""
    normalized_replay_timeline_assignment = separate_multiline_statements(replay_timeline_assignment).text
    if (
        "const ReplaySceneTimelineResetInput timelineReset = DescribeReplaySceneTimeline( m_sceneController,\n"
        not in normalized_replay_timeline_assignment
        or stranded_assignment_lines(normalized_replay_timeline_assignment)
    ):
        print("SELF_TEST_FAIL: a replay timeline declaration stranded its callee after `=`.", file=sys.stderr)
        return 1

    compact_calls = """void Example()
{
    inline constexpr std::size_t MAIN_MEMORY_REPLAY_BUDGET_PASS_COUNT = static_cast<std::size_t>(
        MainMemoryReplayBudgetPass::Count );
    const Result pair = BuildPair(
        first,
        second );
    const Result triple = BuildTriple(
        first,
        second,
        third );
    const Result longPair = BuildPairWithAnIntentionallyLongOwnerSpecificName(
        firstOwnerSpecificArgument,
        secondOwnerSpecificArgument );
}
"""
    compact_calls_expected = """void Example()
{
    inline constexpr std::size_t MAIN_MEMORY_REPLAY_BUDGET_PASS_COUNT = static_cast<std::size_t>( MainMemoryReplayBudgetPass::Count );
    const Result pair = BuildPair( first, second );
    const Result triple = BuildTriple( first, second, third );
    const Result longPair = BuildPairWithAnIntentionallyLongOwnerSpecificName( firstOwnerSpecificArgument,
                                                                               secondOwnerSpecificArgument );
}
"""
    compact_first = separate_multiline_statements(compact_calls)
    compact_second = separate_multiline_statements(compact_first.text)
    if compact_first.text != compact_calls_expected or compact_first.joined_compact_calls != 4:
        print("SELF_TEST_FAIL: compact one-to-three-argument expressions were not joined.", file=sys.stderr)
        return 1
    if compact_second.text != compact_calls_expected or compact_second.joined_compact_calls != 0:
        print("SELF_TEST_FAIL: compact-call joining is not idempotent.", file=sys.stderr)
        return 1

    stranded_lambda = """void Example()
{
    const auto compare =
        [&]( const First& first, const Second& second, int mode )
    {
        return first == second;
    };
}
"""
    normalized_lambda = separate_multiline_statements(stranded_lambda).text
    if (
        "const auto compare = [&]( const First& first,\n" not in normalized_lambda
        or "\n                             const Second& second, int mode )" not in normalized_lambda
        or stranded_assignment_lines(normalized_lambda)
        or any(line and not line.strip() for line in normalized_lambda.splitlines())
    ):
        print("SELF_TEST_FAIL: a stranded lambda assignment was not joined and aligned.", file=sys.stderr)
        return 1

    comment_spacing = """void Example()
{
    Prepare();
    // Why: this comment introduces the next operation.
    Apply();
    // First line.
    // Second line.
    Finish();
}
"""
    comment_spacing_expected = """void Example()
{
    Prepare();

    // Why: this comment introduces the next operation.
    Apply();

    // First line.
    // Second line.
    Finish();
}
"""
    if separate_multiline_statements(comment_spacing).text != comment_spacing_expected:
        print("SELF_TEST_FAIL: standalone comment groups did not receive space above them.", file=sys.stderr)
        return 1

    print(
        "SELF_TEST_PASS: multiline spacing, assignment heads, and compact calls are stable."
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="Repository root. Defaults to cwd.")
    parser.add_argument("--check", action="store_true", help="Fail if any C++ source file would change.")
    parser.add_argument(
        "--check-pipeline",
        action="store_true",
        help="Fail if clang-format plus this source pass would change any implementation.",
    )
    parser.add_argument("--clang-format", type=Path, help="clang-format executable for --check-pipeline.")
    parser.add_argument("--write", action="store_true", help="Rewrite C++ source files in place.")
    parser.add_argument("--stdin", action="store_true", help="Format stdin to stdout without touching files.")
    parser.add_argument("--self-test", action="store_true", help="Run focused formatter regression cases.")
    parser.add_argument("paths", nargs="*", type=Path, help="C++ files or directories. Defaults to SkullbonezSource.")
    args = parser.parse_args()

    selected_modes = sum(
        1
        for enabled in (args.check, args.check_pipeline, args.write, args.stdin, args.self_test)
        if enabled
    )
    if selected_modes > 1:
        parser.error("--check, --check-pipeline, --write, --stdin, and --self-test are mutually exclusive")
    if args.check_pipeline and args.clang_format is None:
        parser.error("--check-pipeline requires --clang-format")

    if args.self_test:
        return run_self_test()
    if args.stdin:
        sys.stdout.write(separate_multiline_statements(sys.stdin.read()).text)
        return 0

    mode = "write" if args.write else "pipeline" if args.check_pipeline else "check"
    repo = args.repo.resolve()
    paths = args.paths or [repo / "SkullbonezSource"]
    sources = iter_source_files(paths)
    changed_files: list[tuple[Path, int, int, int]] = []

    for source in sources:
        original = read_text(source)
        candidate = original
        if mode == "pipeline" and source.suffix.lower() == ".cpp":
            try:
                candidate = clang_format_text(args.clang_format, source, original)
            except RuntimeError as error:
                print(f"FAIL: {error}")
                return 1

        if source.suffix.lower() == ".cpp":
            result = separate_multiline_statements(candidate)
        else:
            normalized, joined_assignments = normalize_assignment_heads(candidate)
            normalized, joined_compact_calls = normalize_compact_parenthesized_expressions(normalized)
            normalized, joined_first_argument_heads = normalize_first_argument_heads(normalized)
            joined_compact_calls += joined_first_argument_heads
            normalized, comment_breaks = ensure_comment_spacing(normalized)
            normalized, control_breaks = ensure_control_flow_spacing(normalized)
            result = SpacingResult(
                normalized,
                inserted_breaks=comment_breaks + control_breaks,
                joined_assignments=joined_assignments,
                joined_compact_calls=joined_compact_calls,
            )
        if result.text == original:
            continue

        changed_files.append(
            (
                source,
                result.inserted_breaks,
                result.joined_assignments,
                result.joined_compact_calls,
            )
        )
        if mode == "write":
            write_text(source, result.text)

    if changed_files:
        action = "Updated" if mode == "write" else "Needs source formatting"
        print(f"{action}: {len(changed_files)} source file(s).")
        # Invariant: validation output stays bounded even during a repository-wide
        # style migration; the summary preserves the full affected-file count.
        for path, inserted_breaks, joined_assignments, joined_compact_calls in changed_files[:MAX_REPORTED_FILES]:
            print(
                f"  {path.relative_to(repo)} "
                f"({inserted_breaks} paragraph break(s), {joined_assignments} assignment head(s), "
                f"{joined_compact_calls} compact call(s))"
            )
        omitted_files = len(changed_files) - MAX_REPORTED_FILES
        if omitted_files > 0:
            print(f"  ... {omitted_files} additional file(s) omitted")
        return 0 if mode == "write" else 1

    print(
        f"PASS: {len(sources)} source file(s) already separate multiline statements/control blocks "
        "and keep assignment heads/compact calls together."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
