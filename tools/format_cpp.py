"""Apply clang-format, then the repository's argument-count wrapping rule.

Only whitespace between C++ tokens is changed by the second pass. Comments,
directives, raw strings, and multiline brace bodies retain their own layout.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys


TOKEN = re.compile(
    r'(?P<directive>^[ \t]*\#(?:[^\n\\]|\\[^\n]|\\\n)*)'
    r'|(?P<comment>//[^\n]*(?:\\\n[^\n]*)*|/\*[\s\S]*?\*/)'
    r'|(?P<raw>(?:u8|u|U|L)?R"(?P<delimiter>[^\s()\\]{0,16})\([\s\S]*?\)(?P=delimiter)")'
    r'|(?P<string>(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*"'
    r"|(?:u8|u|U|L)?'(?:\\[\s\S]|[^'\\])*')"
    r"|(?P<number>\b[0-9][\w.']*)"
    r'|(?P<identifier>[A-Za-z_][A-Za-z_0-9]*)'
    r'|(?P<operator>::|->|<=|>=|<<|==|!=|&&|\|\||\+\+|--)'
    r'|(?P<punctuation>[^\s])',
    re.MULTILINE,
)
NON_CALLS = {
    "if", "for", "while", "switch", "catch", "sizeof", "alignof",
    "decltype", "noexcept", "requires", "static_assert", "__declspec",
    "__attribute__", "typeid", "return", "throw",
}


def delimiter_pairs(tokens: list[re.Match[str]]) -> dict[int, int]:
    pairs: dict[int, int] = {}
    stack: list[tuple[str, int]] = []
    for index, token in enumerate(tokens):
        value = token.group()
        if value in ("(", "[", "{"):
            stack.append((value, index))
        elif value == "<" and index:
            previous = tokens[index - 1]
            # Clang-format puts spaces around comparisons, but none before
            # template arguments. Do not interpret operator< as a template.
            if (previous.end() == token.start()
                    and previous.group() != "operator"
                    and (previous.lastgroup == "identifier" or previous.group() == ">")):
                stack.append((value, index))
        elif value in (")", "]", "}", ">"):
            opening = {")": "(", "]": "[", "}": "{", ">": "<"}[value]
            if stack and stack[-1][0] == opening:
                _, start = stack.pop()
                pairs[start] = index
            elif value != ">":
                # An ambiguous angle expression must not prevent later,
                # independent declarations from being formatted.
                while stack and stack[-1][0] == "<":
                    stack.pop()
                if stack and stack[-1][0] == opening:
                    _, start = stack.pop()
                    pairs[start] = index
    return pairs


def call_opening(tokens: list[re.Match[str]], index: int) -> bool:
    if not index or tokens[index].group() != "(":
        return False
    previous = tokens[index - 1]
    return ((previous.lastgroup == "identifier" and previous.group() not in NON_CALLS)
            or previous.group() in (")", "]", ">"))


def argument_starts(tokens: list[re.Match[str]], pairs: dict[int, int], start: int, end: int) -> list[int]:
    arguments = [start + 1]
    cursor = start + 1
    while cursor < end:
        if cursor in pairs:
            cursor = pairs[cursor] + 1
            continue
        if tokens[cursor].group() == ",":
            arguments.append(cursor + 1)
        cursor += 1
    return arguments


def initializer_openings(tokens: list[re.Match[str]], pairs: dict[int, int]) -> set[int]:
    """Separate value braces from function, class, namespace, and lambda bodies."""
    initializers: set[int] = set()
    for start, end in sorted(pairs.items()):
        if tokens[start].group() != "{" or not start:
            continue
        previous = tokens[start - 1]
        if previous.group() in ("=", "return"):
            initializers.add(start)
            continue
        if previous.group() in ("(", ",", "{"):
            if previous.group() != "{" or start - 1 in initializers:
                initializers.add(start)
            continue
        if previous.lastgroup != "identifier" and previous.group() != ">":
            continue
        if previous.group() in ("else", "try", "do", "noexcept", "const", "override", "final"):
            continue
        cursor = start - 1
        declaration = []
        while cursor >= 0 and tokens[cursor].group() not in (";", "{", "}"):
            declaration.append(tokens[cursor].group())
            cursor -= 1
        if any(word in declaration for word in ("class", "struct", "union", "enum", "namespace", ")")):
            continue
        cursor = start + 1
        while cursor < end:
            if tokens[cursor].group() == ";":
                break
            cursor = pairs.get(cursor, cursor) + 1
        else:
            initializers.add(start)
    return initializers


def format_argument_lists(source: str, column_limit: int = 200) -> str:
    tokens = list(TOKEN.finditer(source))
    if not tokens:
        return source
    values = [token.group() for token in tokens]
    gaps = [source[:tokens[0].start()]]
    gaps.extend(source[left.end():right.start()] for left, right in zip(tokens, tokens[1:]))
    pairs = delimiter_pairs(tokens)
    initializers = initializer_openings(tokens, pairs)
    protected: set[int] = set()
    disabled = False
    for index, token in enumerate(tokens):
        if token.lastgroup == "comment" and "clang-format off" in token.group():
            disabled = True
        if disabled or token.lastgroup in ("comment", "directive"):
            protected.add(index)
        if token.lastgroup == "comment" and "clang-format on" in token.group():
            disabled = False

    # A nested expanded call or brace body keeps its internal line breaks;
    # an enclosing short call only joins its own argument boundaries.
    retained_gaps: set[int] = set()
    brace_bodies = [(start, end) for start, end in pairs.items()
                    if values[start] == "{" and start not in initializers
                    and "\n" in source[tokens[start].end():tokens[end].start()]]

    for start, end in sorted(pairs.items(), key=lambda pair: pair[1]):
        if (not call_opening(tokens, start) and start not in initializers) or end == start + 1:
            continue
        if any(index in protected for index in range(start, end + 1)):
            continue
        arguments = argument_starts(tokens, pairs, start, end)
        if start in initializers and arguments[-1] == end:
            arguments.pop()  # A trailing comma is not another initializer value.
        if arguments[-1] >= end:
            continue
        if start in initializers:
            # Keep `= { first` on the declaration line even when clang-format
            # would move the whole initializer below the assignment operator.
            gaps[start] = " "
        body_gaps = set()
        for body_start, body_end in brace_bodies:
            if start < body_start < body_end < end:
                body_gaps.update(range(body_start, body_end + 1))
        compact_gaps = {
            index: (" " if gaps[index] else "")
            for index in range(start + 1, end + 1)
            if index not in retained_gaps and index not in body_gaps
        }
        compact_gaps[start + 1] = " "
        compact_gaps[end] = " "
        compact = "".join(compact_gaps.get(index, gaps[index]) + values[index]
                          for index in range(start + 1, end + 1))
        prefix_parts = []
        for index in range(start, -1, -1):
            prefix_parts.append(gaps[index].rsplit("\n", 1)[-1] + values[index])
            if "\n" in gaps[index]:
                break
        prefix = "".join(reversed(prefix_parts))
        suffix = source[tokens[end].end():].split("\n", 1)[0]
        expand = len(arguments) >= 4 and ("\n" in compact or len(prefix + compact + suffix) > column_limit)
        for index, gap in compact_gaps.items():
            gaps[index] = gap
        if expand:
            indent = " " * (len(prefix) + 1)
            for index in arguments[1:]:
                gaps[index] = "\n" + indent
            retained_gaps.update(index for index in range(start + 1, end + 1) if "\n" in gaps[index])

    result = "".join(gap + value for gap, value in zip(gaps, values)) + source[tokens[-1].end():]
    # Hazard: whitespace can affect comments and preprocessing. Refuse to emit
    # output if a rewrite changes the token sequence or any token's contents.
    if [token.group() for token in TOKEN.finditer(result)] != values:
        raise ValueError("argument layout changed C++ tokens")
    return result


def format_once(source: bytes, path: Path, clang_format: str) -> bytes:
    result = subprocess.run(
        [clang_format, "--style=file", f"--assume-filename={path}"],
        input=source, capture_output=True, check=False,
    )
    if result.returncode:
        raise RuntimeError(result.stderr.decode("utf-8", errors="replace"))
    return format_argument_lists(result.stdout.decode("utf-8")).encode("utf-8")


def format_source(source: bytes, path: Path, clang_format: str) -> bytes:
    # Clang-format may realign adjacent trailing comments after our call layout
    # changes. Reach a fixed point in memory before either checking or writing.
    seen = {source}
    for _ in range(8):
        formatted = format_once(source, path, clang_format)
        if formatted == source:
            return formatted
        if formatted in seen:
            raise ValueError("formatting oscillated; file left unchanged")
        seen.add(formatted)
        source = formatted
    raise ValueError("formatting did not stabilize after eight passes; file left unchanged")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    parser.add_argument("--clang-format", default=shutil.which("clang-format"))
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()
    if not args.clang_format:
        parser.error("clang-format was not found; provide --clang-format")
    failed = False
    for path in args.files:
        try:
            path = path.resolve()
            original = path.read_bytes()
            formatted = format_source(original, path, args.clang_format)
            if original != formatted:
                if args.write:
                    path.write_bytes(formatted)
                else:
                    print(f"Formatting differs: {path}", file=sys.stderr)
                    failed = True
        except (OSError, UnicodeError, ValueError, RuntimeError) as error:
            print(f"{path}: {error}", file=sys.stderr)
            failed = True
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
