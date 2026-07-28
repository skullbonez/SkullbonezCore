#!/usr/bin/env python3
"""
File: inventory_function_complexity.py
Purpose:
  Report the extent and brace nesting of every lexically recognized first-party
  C++ function definition without turning either measurement into an allowance.

Summary:
  Reuses the repository's tracked-source mask and wide-signature definition
  identities, pairs each definition with its balanced body, then renders
  repeatable Markdown, JSON, or CSV evidence for qualitative owner review.

Mental model:
  The wide-signature inventory already decides which parenthesized forms are
  declarations and supplies their normalized identities. This tool follows each
  recognized definition to its body, measures that body, and prints the complete
  distribution plus the rows selected by proposed review triggers.

Glossary:
  Body line: An inclusive source line from a function's opening brace through
    its matching closing brace.
  Brace depth: Maximum simultaneously open curly braces inside the body,
    including the function body's outer brace.
  Closure: A lexically recognized C++ lambda expression inside the body.
  Review trigger: A signal that requires owner judgement; it is neither a
    maximum nor evidence that a lower measurement is automatically acceptable.

Invariants:
  - Tracked-file enumeration and non-code masking come from cpp_source_scan.
  - Function identity comes from inventory_wide_signatures; this tool does not
    maintain a competing name or declaration parser.
  - Body length and nesting remain independent measurements.
  - CX0 is report-only: trigger matches never change the process exit code.
  - Any recognized definition whose body cannot be paired is reported, never
    silently omitted.

Related:
  - tools/cpp_source_scan.py
  - tools/inventory_wide_signatures.py
  - Agentic/Plans/TODO/function-complexity-review-trigger.md
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cpp_source_scan import line_of_offset, mask_cpp, tracked_files  # noqa: E402
from inventory_wide_signatures import Candidate, matching_pairs, scan_file  # noqa: E402


DEFAULT_BODY_TRIGGER = 400
DEFAULT_DEPTH_TRIGGER = 6
LAMBDA_RE = re.compile(
    r"""
    (?<![\w)\]\[])\[(?!\[)[^\]]*\]
    \s*(?:<[^;{}]*>\s*)?
    (?:\([^;{}]*\)\s*)?
    (?:mutable\s*)?
    (?:constexpr\s*)?
    (?:noexcept(?:\([^;{}]*\))?\s*)?
    (?:->\s*[^;{}]+)?
    \{
    """,
    re.X,
)


@dataclass(frozen=True)
class FunctionExtent:
    file: str
    identity: str
    name: str
    start_line: int
    body_lines: int
    max_brace_depth: int
    closure_count: int
    body_triggered: bool
    depth_triggered: bool

    @property
    def review_triggered(self) -> bool:
        return self.body_triggered or self.depth_triggered


def _next_code(masked: str, start: int) -> int:
    index = start
    while index < len(masked) and masked[index].isspace():
        index += 1
    return index


def _definition_body(
    masked: str,
    candidate: Candidate,
    brace_pairs: dict[int, int],
) -> tuple[int, int] | None:
    """Return the body brace pair following a recognized definition."""
    index = candidate.closing_paren + 1
    paren_depth = 0
    bracket_depth = 0
    initializer_list = False

    while index < len(masked):
        char = masked[index]
        if char == "(":
            paren_depth += 1
        elif char == ")":
            paren_depth = max(0, paren_depth - 1)
        elif char == "[":
            bracket_depth += 1
        elif char == "]":
            bracket_depth = max(0, bracket_depth - 1)
        elif (
            char == ":"
            and paren_depth == 0
            and bracket_depth == 0
            and not masked.startswith("::", index)
            and (index == 0 or masked[index - 1] != ":")
        ):
            initializer_list = True
        elif char == ";" and paren_depth == 0 and bracket_depth == 0:
            return None
        elif char == "{" and paren_depth == 0 and bracket_depth == 0:
            close = brace_pairs.get(index)
            if close is None:
                return None
            if not initializer_list:
                return index, close

            # Hazard: constructor member initializers may themselves use braces.
            # Their closing brace is followed by a comma or the real body.
            # The function body is the first top-level brace whose close does
            # not continue that initializer sequence.
            tail = _next_code(masked, close + 1)
            if tail < len(masked) and masked[tail] in ",{)]":
                index = close
            else:
                return index, close
        index += 1
    return None


def _body_metrics(masked: str, opening: int, closing: int) -> tuple[int, int]:
    depth = 0
    maximum = 0
    for char in masked[opening : closing + 1]:
        if char == "{":
            depth += 1
            maximum = max(maximum, depth)
        elif char == "}":
            depth -= 1

    # Lexical lambda recognition deliberately runs on masked text. Comments,
    # strings, attributes, and array subscripts therefore cannot manufacture
    # closure rows through ordinary prose or data access.
    closures = len(LAMBDA_RE.findall(masked[opening : closing + 1]))
    return maximum, closures


def scan_repository(
    repo: Path,
    body_trigger: int,
    depth_trigger: int,
) -> tuple[list[FunctionExtent], list[str]]:
    rows: list[FunctionExtent] = []
    diagnostics: list[str] = []
    for path in tracked_files(repo, ["SkullbonezSource"]):
        text = path.read_text(encoding="utf-8", errors="strict")
        masked = mask_cpp(text)
        brace_pairs = matching_pairs(masked, "{", "}")
        candidates, _ = scan_file(path, repo, text=text, masked=masked)
        for candidate in candidates:
            if not candidate.is_definition:
                continue
            body = _definition_body(masked, candidate, brace_pairs)
            if body is None:
                diagnostics.append(
                    f"{candidate.file}:{candidate.line}: could not pair body for {candidate.signature}"
                )
                continue
            opening, closing = body
            start_line = line_of_offset(text, opening)
            end_line = line_of_offset(text, closing)
            max_depth, closures = _body_metrics(masked, opening, closing)
            rows.append(
                FunctionExtent(
                    file=candidate.file,
                    identity=candidate.signature,
                    name=candidate.qualified_name,
                    start_line=start_line,
                    body_lines=end_line - start_line + 1,
                    max_brace_depth=max_depth,
                    closure_count=closures,
                    body_triggered=end_line - start_line + 1 >= body_trigger,
                    depth_triggered=max_depth >= depth_trigger,
                )
            )

    rows.sort(key=lambda row: (-row.body_lines, -row.max_brace_depth, row.file, row.start_line))
    return rows, diagnostics


def _markdown(
    rows: list[FunctionExtent],
    diagnostics: list[str],
    body_trigger: int,
    depth_trigger: int,
) -> str:
    triggered = [row for row in rows if row.review_triggered]
    body_selected = sum(row.body_triggered for row in rows)
    depth_selected = sum(row.depth_triggered for row in rows)
    both_selected = sum(row.body_triggered and row.depth_triggered for row in rows)
    defaults_selected = body_trigger == DEFAULT_BODY_TRIGGER and depth_trigger == DEFAULT_DEPTH_TRIGGER
    trigger_heading = "Ratified Review Triggers" if defaults_selected else "Comparison Trigger Inputs"
    output = [
        "# Function Complexity CX0 Distribution",
        "",
        "State: report-only measurement. The repository defaults of 400 body",
        "lines and brace depth 6 were owner-ratified on 2026-07-29.",
        "",
        f"## {trigger_heading}",
        "",
        f"- Body length: {body_trigger} inclusive body lines.",
        f"- Maximum brace depth: {depth_trigger}.",
        "- Either independent signal selects a row for qualitative owner review.",
        "- These values are review triggers, not maxima, budgets, or allowances.",
        "",
        "## Summary",
        "",
        f"- Recognized function definitions: {len(rows)}",
        f"- Selected trigger rows: {len(triggered)}",
        f"- Body signal: {body_selected}; depth signal: {depth_selected}; both signals: {both_selected}",
        f"- Unpaired recognized definitions: {len(diagnostics)}",
        "",
        "## Observed Distribution Tails",
        "",
        "| Minimum body lines | Functions at or above |",
        "|---:|---:|",
    ]
    for minimum in (100, 200, 300, 400, 500, 750, 1000, 1500):
        output.append(f"| {minimum} | {sum(row.body_lines >= minimum for row in rows)} |")
    output.extend(
        [
            "",
            "| Minimum max brace depth | Functions at or above |",
            "|---:|---:|",
        ]
    )
    for minimum in range(4, 10):
        output.append(f"| {minimum} | {sum(row.max_brace_depth >= minimum for row in rows)} |")
    output.extend(
        [
            "",
            "## Nearby Trigger Comparison",
            "",
            "Each cell is the union selected when either the row's body value or",
            "the column's depth value is reached. This is decision evidence,",
            "not a menu of allowances.",
            "",
            "| Body lines \\ brace depth | 6 | 7 | 8 |",
            "|---:|---:|---:|---:|",
        ]
    )
    for body_minimum in (300, 400, 500):
        counts = [
            sum(
                row.body_lines >= body_minimum or row.max_brace_depth >= depth_minimum
                for row in rows
            )
            for depth_minimum in (6, 7, 8)
        ]
        output.append(
            f"| {body_minimum} | {counts[0]} | {counts[1]} | {counts[2]} |"
        )
    output.extend(
        [
            "",
        "## Selected Trigger Set",
        "",
        "| Function | Location | Body lines | Max brace depth | Closures | Signals |",
        "|---|---|---:|---:|---:|---|",
        ]
    )
    for row in triggered:
        signals = ", ".join(
            signal
            for signal, selected in (
                ("body", row.body_triggered),
                ("depth", row.depth_triggered),
            )
            if selected
        )
        output.append(
            f"| `{row.name}` | `{row.file}:{row.start_line}` | {row.body_lines} | "
            f"{row.max_brace_depth} | {row.closure_count} | {signals} |"
        )

    output.extend(
        [
            "",
            "## Complete Distribution",
            "",
            "| Function identity | Location | Body lines | Max brace depth | Closures |",
            "|---|---|---:|---:|---:|",
        ]
    )
    for row in rows:
        identity = row.identity.replace("|", "\\|")
        output.append(
            f"| `{identity}` | `{row.file}:{row.start_line}` | {row.body_lines} | "
            f"{row.max_brace_depth} | {row.closure_count} |"
        )

    output.extend(["", "## Unpaired Recognized Definitions", ""])
    if diagnostics:
        output.extend(f"- `{diagnostic}`" for diagnostic in diagnostics)
    else:
        output.append("None.")
    return "\n".join(output) + "\n"


def _json(rows: list[FunctionExtent], diagnostics: list[str]) -> str:
    payload = {
        "functions": [{**asdict(row), "review_triggered": row.review_triggered} for row in rows],
        "diagnostics": diagnostics,
    }
    return json.dumps(payload, indent=2) + "\n"


def _csv(rows: list[FunctionExtent]) -> str:
    stream = io.StringIO(newline="")
    fields = [
        "file",
        "identity",
        "name",
        "start_line",
        "body_lines",
        "max_brace_depth",
        "closure_count",
        "body_triggered",
        "depth_triggered",
        "review_triggered",
    ]
    writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow({**asdict(row), "review_triggered": row.review_triggered})
    return stream.getvalue()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--body-trigger", type=int, default=DEFAULT_BODY_TRIGGER)
    parser.add_argument("--depth-trigger", type=int, default=DEFAULT_DEPTH_TRIGGER)
    parser.add_argument("--format", choices=("markdown", "json", "csv"), default="markdown")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    if args.body_trigger < 1 or args.depth_trigger < 1:
        parser.error("trigger values must be positive integers")

    repo = args.repo.resolve()
    rows, diagnostics = scan_repository(repo, args.body_trigger, args.depth_trigger)
    if args.format == "markdown":
        rendered = _markdown(rows, diagnostics, args.body_trigger, args.depth_trigger)
    elif args.format == "json":
        rendered = _json(rows, diagnostics)
    else:
        rendered = _csv(rows)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8", newline="\n")
    else:
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
