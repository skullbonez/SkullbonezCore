#!/usr/bin/env python3
"""
File: inventory_function_complexity.py
Purpose:
  Report the extent and brace nesting of every lexically recognized first-party
  C++ function definition without turning either measurement into an allowance.

Summary:
  Reuses the repository's tracked-source mask and wide-signature definition
  identities, pairs each definition with its balanced body, then matches every
  triggered row to an exact current-body owner ruling. Markdown, JSON, and CSV
  remain report formats; strict mode turns currentness drift into a gate.

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
  Current-body ruling: Owner judgement keyed by file, normalized signature, and
    a digest of the complete body text.

Invariants:
  - Tracked-file enumeration and non-code masking come from cpp_source_scan.
  - Function identity comes from inventory_wide_signatures; this tool does not
    maintain a competing name or declaration parser.
  - Body length and nesting remain independent measurements.
  - Strict mode fails on unruled, edited-body, or stale current-source evidence.
  - A repair-plan ruling names an existing repository plan.
  - Any recognized definition whose body cannot be paired is reported, never
    silently omitted.

Related:
  - tools/cpp_source_scan.py
  - tools/inventory_wide_signatures.py
  - Agentic/Reports/2026-07-29/function-complexity-cx0-distribution.md
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import re
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cpp_source_scan import line_of_offset, mask_cpp, tracked_files  # noqa: E402
from inventory_wide_signatures import Candidate, matching_pairs, scan_file  # noqa: E402


DEFAULT_BODY_TRIGGER = 400
DEFAULT_DEPTH_TRIGGER = 6
DEFAULT_RULINGS_PATH = Path("tools/function_complexity_rulings.json")
RULING_DISPOSITIONS = {"retain-owner", "repair-plan"}
SHA256_RE = re.compile(r"[0-9a-f]{64}")
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
    body_sha256: str
    body_triggered: bool
    depth_triggered: bool

    @property
    def review_triggered(self) -> bool:
        return self.body_triggered or self.depth_triggered


@dataclass(frozen=True)
class OwnerRuling:
    file: str
    signature: str
    body_sha256: str
    owner: str
    disposition: str
    reason: str
    evidence: str
    plan: str

    @property
    def key(self) -> tuple[str, str]:
        return self.file, self.signature


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
            body_sha256 = hashlib.sha256(text[opening : closing + 1].encode("utf-8")).hexdigest()
            rows.append(
                FunctionExtent(
                    file=candidate.file,
                    identity=candidate.signature,
                    name=candidate.qualified_name,
                    start_line=start_line,
                    body_lines=end_line - start_line + 1,
                    max_brace_depth=max_depth,
                    closure_count=closures,
                    body_sha256=body_sha256,
                    body_triggered=end_line - start_line + 1 >= body_trigger,
                    depth_triggered=max_depth >= depth_trigger,
                )
            )

    rows.sort(key=lambda row: (-row.body_lines, -row.max_brace_depth, row.file, row.start_line))
    return rows, diagnostics


def load_owner_rulings(
    path: Path,
) -> tuple[int, int, dict[tuple[str, str], OwnerRuling]]:
    """Load exact current-body rulings and reject ambiguous policy data."""
    payload = json.loads(path.read_text(encoding="utf-8", errors="strict"))
    body_trigger = payload.get("review_trigger_body_lines")
    depth_trigger = payload.get("review_trigger_brace_depth")
    if not isinstance(body_trigger, int) or isinstance(body_trigger, bool) or body_trigger < 1:
        raise ValueError("review_trigger_body_lines must be a positive integer")
    if not isinstance(depth_trigger, int) or isinstance(depth_trigger, bool) or depth_trigger < 1:
        raise ValueError("review_trigger_brace_depth must be a positive integer")
    raw_rulings = payload.get("rulings")
    if not isinstance(raw_rulings, list):
        raise ValueError("rulings must be an array")

    rulings: dict[tuple[str, str], OwnerRuling] = {}
    required_fields = (
        "file",
        "signature",
        "body_sha256",
        "owner",
        "disposition",
        "reason",
        "evidence",
    )
    for index, raw in enumerate(raw_rulings):
        if not isinstance(raw, dict):
            raise ValueError(f"rulings[{index}] must be an object")
        values: dict[str, str] = {}
        for field in required_fields:
            value = raw.get(field)
            if not isinstance(value, str) or not value.strip():
                raise ValueError(f"rulings[{index}].{field} must be a non-empty string")
            values[field] = value.strip()
        if not SHA256_RE.fullmatch(values["body_sha256"]):
            raise ValueError(f"rulings[{index}].body_sha256 must be 64 lowercase hexadecimal digits")
        if values["disposition"] not in RULING_DISPOSITIONS:
            allowed = ", ".join(sorted(RULING_DISPOSITIONS))
            raise ValueError(f"rulings[{index}].disposition must be one of: {allowed}")
        plan = raw.get("plan", "")
        if not isinstance(plan, str):
            raise ValueError(f"rulings[{index}].plan must be a string when present")
        plan = plan.strip()
        if values["disposition"] == "repair-plan" and not plan:
            raise ValueError(f"rulings[{index}] repair-plan disposition requires plan")
        ruling = OwnerRuling(**values, plan=plan)
        if ruling.key in rulings:
            raise ValueError(f"duplicate ruling for {ruling.file}: {ruling.signature}")
        rulings[ruling.key] = ruling
    return body_trigger, depth_trigger, rulings


def _ruling_status(
    row: FunctionExtent,
    rulings: dict[tuple[str, str], OwnerRuling],
) -> tuple[str, OwnerRuling | None]:
    if not row.review_triggered:
        return "NOT-TRIGGERED", None
    ruling = rulings.get((row.file, row.identity))
    if ruling is None:
        return "UNRULED", None
    if ruling.body_sha256 != row.body_sha256:
        return "EDITED-BODY", ruling
    return "RULED", ruling


def apply_owner_rulings(
    rows: list[FunctionExtent],
    rulings: dict[tuple[str, str], OwnerRuling],
) -> list[str]:
    """Return blocking diagnostics for every unruled, edited, or stale row."""
    current_keys: set[tuple[str, str]] = set()
    diagnostics: list[str] = []
    for row in rows:
        if not row.review_triggered:
            continue
        key = (row.file, row.identity)
        current_keys.add(key)
        status, ruling = _ruling_status(row, rulings)
        if status == "UNRULED":
            diagnostics.append(f"UNRULED {row.file}: {row.identity}")
        elif status == "EDITED-BODY":
            assert ruling is not None
            diagnostics.append(
                f"EDITED-BODY {row.file}: {row.identity} "
                f"expected={ruling.body_sha256} actual={row.body_sha256}"
            )

    # Hazard: a ruling may outlive a deleted, renamed, moved, or narrowed
    # function. Such an entry is stale evidence, not harmless documentation.
    for file_name, signature in sorted(set(rulings) - current_keys):
        diagnostics.append(f"STALE-RULING {file_name}: {signature}")
    return diagnostics


def validate_ruling_references(
    repo: Path,
    rulings: dict[tuple[str, str], OwnerRuling],
) -> list[str]:
    """Require every repair disposition to resolve to a live repository plan."""
    diagnostics: list[str] = []
    plan_root_relative = Path("Agentic") / "Plans" / "TODO"
    plan_root = (repo / plan_root_relative).resolve()
    for ruling in rulings.values():
        if ruling.disposition != "repair-plan":
            continue
        plan_relative = Path(ruling.plan)
        canonical_text = plan_relative.as_posix()
        valid_shape = (
            not plan_relative.is_absolute()
            and canonical_text == ruling.plan
            and plan_relative.suffix.lower() == ".md"
            and plan_relative.parts[:3] == plan_root_relative.parts
            and all(part not in {".", ".."} for part in plan_relative.parts)
        )
        if not valid_shape:
            diagnostics.append(
                f"INVALID-REPAIR-PLAN {ruling.file}: {ruling.signature} plan={ruling.plan}"
            )
            continue
        plan_path = (repo / plan_relative).resolve()
        if not plan_path.is_relative_to(plan_root):
            diagnostics.append(
                f"INVALID-REPAIR-PLAN {ruling.file}: {ruling.signature} plan={ruling.plan}"
            )
        elif not plan_path.is_file():
            diagnostics.append(
                f"MISSING-REPAIR-PLAN {ruling.file}: {ruling.signature} plan={ruling.plan}"
            )
    return diagnostics


def _markdown(
    rows: list[FunctionExtent],
    scan_diagnostics: list[str],
    ruling_diagnostics: list[str],
    body_trigger: int,
    depth_trigger: int,
    rulings: dict[tuple[str, str], OwnerRuling],
) -> str:
    triggered = [row for row in rows if row.review_triggered]
    ruled = sum(_ruling_status(row, rulings)[0] == "RULED" for row in triggered)
    body_selected = sum(row.body_triggered for row in rows)
    depth_selected = sum(row.depth_triggered for row in rows)
    both_selected = sum(row.body_triggered and row.depth_triggered for row in rows)
    defaults_selected = body_trigger == DEFAULT_BODY_TRIGGER and depth_trigger == DEFAULT_DEPTH_TRIGGER
    trigger_heading = "Ratified Review Triggers" if defaults_selected else "Comparison Trigger Inputs"
    output = [
        "# Function Complexity Inventory",
        "",
        "The repository defaults of 400 body lines and brace depth 6 were",
        "owner-ratified on 2026-07-29. Strict mode requires an exact current-body",
        "ruling for every selected row.",
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
        f"- Current ruled trigger rows: {ruled}",
        f"- Body signal: {body_selected}; depth signal: {depth_selected}; both signals: {both_selected}",
        f"- Unpaired recognized definitions: {len(scan_diagnostics)}",
        f"- Ruling currentness diagnostics: {len(ruling_diagnostics)}",
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
            "| Function | Location | Body lines | Max brace depth | Closures | Signals | Current ruling |",
            "|---|---|---:|---:|---:|---|---|",
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
        status, ruling = _ruling_status(row, rulings)
        ruling_text = (
            f"`{ruling.disposition}` — {ruling.owner}" if status == "RULED" and ruling else f"`{status}`"
        )
        output.append(
            f"| `{row.name}` | `{row.file}:{row.start_line}` | {row.body_lines} | "
            f"{row.max_brace_depth} | {row.closure_count} | {signals} | {ruling_text} |"
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
    if scan_diagnostics:
        output.extend(f"- `{diagnostic}`" for diagnostic in scan_diagnostics)
    else:
        output.append("None.")
    output.extend(["", "## Ruling Currentness Diagnostics", ""])
    if ruling_diagnostics:
        output.extend(f"- `{diagnostic}`" for diagnostic in ruling_diagnostics)
    else:
        output.append("None.")
    return "\n".join(output) + "\n"


def _row_payload(
    row: FunctionExtent,
    rulings: dict[tuple[str, str], OwnerRuling],
) -> dict[str, object]:
    status, ruling = _ruling_status(row, rulings)
    payload: dict[str, object] = {
        **asdict(row),
        "review_triggered": row.review_triggered,
        "ruling_status": status,
        "ruling_disposition": ruling.disposition if ruling else "",
        "ruling_owner": ruling.owner if ruling else "",
        "ruling_reason": ruling.reason if ruling else "",
        "ruling_evidence": ruling.evidence if ruling else "",
        "ruling_plan": ruling.plan if ruling else "",
    }
    return payload


def _json(
    rows: list[FunctionExtent],
    scan_diagnostics: list[str],
    ruling_diagnostics: list[str],
    rulings: dict[tuple[str, str], OwnerRuling],
) -> str:
    payload = {
        "functions": [_row_payload(row, rulings) for row in rows],
        "scan_diagnostics": scan_diagnostics,
        "ruling_diagnostics": ruling_diagnostics,
    }
    return json.dumps(payload, indent=2) + "\n"


def _csv(
    rows: list[FunctionExtent],
    rulings: dict[tuple[str, str], OwnerRuling],
) -> str:
    stream = io.StringIO(newline="")
    fields = [
        "file",
        "identity",
        "name",
        "start_line",
        "body_lines",
        "max_brace_depth",
        "closure_count",
        "body_sha256",
        "body_triggered",
        "depth_triggered",
        "review_triggered",
        "ruling_status",
        "ruling_disposition",
        "ruling_owner",
        "ruling_reason",
        "ruling_evidence",
        "ruling_plan",
    ]
    writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow(_row_payload(row, rulings))
    return stream.getvalue()


def self_test() -> None:
    """Prove new, edited, stale, and deleted current-source drift fail closed."""
    flat = """\
int Flat(int value)
{
    int result = value;
    return result;
}
"""
    nested = """\
int Nested(int value)
{
    if (value)
    {
        return value;
    }
    return 0;
}
"""
    added = """\
int Added(int value)
{
    int result = value * 2;
    return result;
}
"""
    constructor = """\
class FixtureOwner
{
public:
    FixtureOwner(int value)
        : m_value{value}
    {
        if (value)
        {
            ++m_value;
        }
    }
private:
    int m_value = 0;
};
"""
    with tempfile.TemporaryDirectory(prefix="function-complexity-") as temp_name:
        repo = Path(temp_name)
        source = repo / "SkullbonezSource" / "Fixture.cpp"
        source.parent.mkdir(parents=True)
        source.write_text(flat + "\n" + nested + "\n" + constructor, encoding="utf-8", newline="\n")
        subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
        subprocess.run(["git", "config", "core.autocrlf", "false"], cwd=repo, check=True)
        subprocess.run(["git", "add", "--", "SkullbonezSource/Fixture.cpp"], cwd=repo, check=True)

        rows, scan_diagnostics = scan_repository(repo, body_trigger=4, depth_trigger=2)
        assert not scan_diagnostics
        assert len(rows) == 3
        constructor_row = next(row for row in rows if row.name == "FixtureOwner::FixtureOwner")
        assert constructor_row.body_lines == 6
        assert constructor_row.max_brace_depth == 2
        assert constructor_row.closure_count == 0
        complete_rulings = {
            (row.file, row.identity): OwnerRuling(
                file=row.file,
                signature=row.identity,
                body_sha256=row.body_sha256,
                owner=f"{row.name} fixture owner",
                disposition="retain-owner",
                reason="One bounded fixture operation with one synchronous value lifetime.",
                evidence="self-test fixture",
                plan="",
            )
            for row in rows
        }
        assert not apply_owner_rulings(rows, complete_rulings)

        # Planted drift: a new trigger row has no inherited judgement.
        source.write_text(
            flat + "\n" + nested + "\n" + constructor + "\n" + added,
            encoding="utf-8",
            newline="\n",
        )
        added_rows, _ = scan_repository(repo, body_trigger=4, depth_trigger=2)
        added_diagnostics = apply_owner_rulings(added_rows, complete_rulings)
        assert any(item.startswith("UNRULED") and "Added" in item for item in added_diagnostics)

        # Edited body: stable file and signature still cannot inherit a digest.
        edited_flat = flat.replace("return result;", "return result + 1;")
        source.write_text(
            edited_flat + "\n" + nested + "\n" + constructor,
            encoding="utf-8",
            newline="\n",
        )
        edited_rows, _ = scan_repository(repo, body_trigger=4, depth_trigger=2)
        edited_diagnostics = apply_owner_rulings(edited_rows, complete_rulings)
        assert any(item.startswith("EDITED-BODY") and "Flat" in item for item in edited_diagnostics)

        # Stale ruling: fabricated evidence that has no current function fails.
        fabricated = OwnerRuling(
            file="SkullbonezSource/Fixture.cpp",
            signature="int Missing(int value)",
            body_sha256="0" * 64,
            owner="Missing fixture owner",
            disposition="retain-owner",
            reason="Planted stale fixture.",
            evidence="self-test fixture",
            plan="",
        )
        stale_diagnostics = apply_owner_rulings(
            rows,
            {**complete_rulings, fabricated.key: fabricated},
        )
        assert any(item.startswith("STALE-RULING") and "Missing" in item for item in stale_diagnostics)

        # Deleted function: a real prior ruling becomes stale after deletion.
        source.write_text(flat + "\n" + constructor, encoding="utf-8", newline="\n")
        deleted_rows, _ = scan_repository(repo, body_trigger=4, depth_trigger=2)
        deleted_diagnostics = apply_owner_rulings(deleted_rows, complete_rulings)
        assert any(item.startswith("STALE-RULING") and "Nested" in item for item in deleted_diagnostics)

        # Repair plans are canonical repository-relative TODO Markdown paths,
        # never arbitrary existing files or traversal/absolute aliases.
        plans = repo / "Agentic" / "Plans" / "TODO"
        plans.mkdir(parents=True)
        live_plan = plans / "fixture-repair.md"
        live_plan.write_text("# Fixture repair\n", encoding="utf-8", newline="\n")
        readme = repo / "README.md"
        readme.write_text("# Not a repair plan\n", encoding="utf-8", newline="\n")
        sample = next(iter(complete_rulings.values()))

        def repair_ruling(plan: str) -> OwnerRuling:
            return OwnerRuling(
                file=sample.file,
                signature=sample.signature,
                body_sha256=sample.body_sha256,
                owner="Fixture repair owner",
                disposition="repair-plan",
                reason="Fixture repair reason.",
                evidence="self-test fixture",
                plan=plan,
            )

        valid_repair = repair_ruling("Agentic/Plans/TODO/fixture-repair.md")
        assert not validate_ruling_references(repo, {valid_repair.key: valid_repair})
        for invalid_plan in (
            "README.md",
            "../README.md",
            str(readme.resolve()),
            "Agentic/Plans/TODO/../fixture-repair.md",
        ):
            invalid = repair_ruling(invalid_plan)
            diagnostics = validate_ruling_references(repo, {invalid.key: invalid})
            assert any(item.startswith("INVALID-REPAIR-PLAN") for item in diagnostics)
        missing = repair_ruling("Agentic/Plans/TODO/missing.md")
        assert any(
            item.startswith("MISSING-REPAIR-PLAN")
            for item in validate_ruling_references(repo, {missing.key: missing})
        )

        # A recognized definition with an unmatched brace must surface as an
        # omission diagnostic instead of disappearing from the distribution.
        broken = repo / "SkullbonezSource" / "Broken.cpp"
        broken.write_text("int Broken(int value)\n{\n", encoding="utf-8", newline="\n")
        subprocess.run(["git", "add", "--", "SkullbonezSource/Broken.cpp"], cwd=repo, check=True)
        _, broken_diagnostics = scan_repository(repo, body_trigger=4, depth_trigger=2)
        assert any("could not pair body for int Broken(int value)" in item for item in broken_diagnostics)

    # CLI modes are mutually exclusive. Otherwise `--self-test --strict` could
    # return before loading the requested rulings or scanning the repository.
    for conflicting_args in (
        ("--strict", "--rulings", "missing.json"),
        ("--repo", "."),
        ("--rulings", str(DEFAULT_RULINGS_PATH)),
        ("--format", "markdown"),
    ):
        conflict = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "--self-test", *conflicting_args],
            capture_output=True,
            text=True,
            check=False,
        )
        assert conflict.returncode != 0
        assert "--self-test cannot be combined" in conflict.stderr
    print("PASS: function-complexity inventory self-test")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path)
    parser.add_argument("--body-trigger", type=int)
    parser.add_argument("--depth-trigger", type=int)
    parser.add_argument("--format", choices=("markdown", "json", "csv"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--rulings", type=Path)
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        if (
            args.strict
            or args.output is not None
            or args.rulings is not None
            or args.repo is not None
            or args.body_trigger is not None
            or args.depth_trigger is not None
            or args.format is not None
        ):
            parser.error("--self-test cannot be combined with repository, ruling, output, or strict modes")
        self_test()
        return 0

    repo = (args.repo or Path(".")).resolve()
    rulings_argument = args.rulings or DEFAULT_RULINGS_PATH
    rulings_path = rulings_argument if rulings_argument.is_absolute() else repo / rulings_argument
    try:
        ratified_body_trigger, ratified_depth_trigger, rulings = load_owner_rulings(rulings_path)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"ERROR: invalid function-complexity rulings: {error}", file=sys.stderr)
        return 2

    body_trigger = ratified_body_trigger if args.body_trigger is None else args.body_trigger
    depth_trigger = ratified_depth_trigger if args.depth_trigger is None else args.depth_trigger
    if body_trigger < 1 or depth_trigger < 1:
        parser.error("trigger values must be positive integers")
    if args.strict and (
        body_trigger != ratified_body_trigger or depth_trigger != ratified_depth_trigger
    ):
        print("ERROR: strict mode must use the ratified ruling-file triggers", file=sys.stderr)
        return 2

    rows, scan_diagnostics = scan_repository(repo, body_trigger, depth_trigger)
    ruling_diagnostics = [
        *apply_owner_rulings(rows, rulings),
        *validate_ruling_references(repo, rulings),
    ]
    output_format = args.format or "markdown"
    if output_format == "markdown":
        rendered = _markdown(
            rows,
            scan_diagnostics,
            ruling_diagnostics,
            body_trigger,
            depth_trigger,
            rulings,
        )
    elif output_format == "json":
        rendered = _json(rows, scan_diagnostics, ruling_diagnostics, rulings)
    else:
        rendered = _csv(rows, rulings)

    if args.output:
        output = args.output if args.output.is_absolute() else repo / args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8", newline="\n")
    elif not args.strict:
        sys.stdout.write(rendered)
    if args.strict and (scan_diagnostics or ruling_diagnostics):
        for diagnostic in [*scan_diagnostics, *ruling_diagnostics]:
            print(f"ERROR: {diagnostic}", file=sys.stderr)
        return 1
    if args.strict:
        print(
            "PASS: function-complexity "
            f"functions={len(rows)} triggered={sum(row.review_triggered for row in rows)} "
            f"ruled={len(rulings)} body-trigger={body_trigger} depth-trigger={depth_trigger}",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
