"""
File: tools/check_coverage.py
Purpose:
  Summarize OpenCppCoverage Cobertura XML and enforce versioned subsystem floors.

Mental model:
  Cobertura records each instrumented product line and its hit count. This tool
  normalizes those paths, applies the repository-owned tier/exclusion map, then
  aggregates unique source lines per subsystem. The whole-engine percentage is
  reported as context and is never a gate.

Glossary:
  Cobertura: XML interchange format containing executable line hit counts.
  Floor: Minimum covered-line percentage required for one named subsystem.
  Report-only: Bring-up mode that prints measurements without failing on floors.
  Instrumented line: Product line emitted by the compiler's debug information
    and observed by OpenCppCoverage, whether its hit count is zero or nonzero.
  Required source: Product translation unit that must appear in Cobertura so a
    link or project-file omission cannot silently shrink a subsystem denominator.

Invariants:
  - Duplicate inline/header rows merge by normalized path and line number.
  - One product line may belong to at most one configured subsystem.
  - Every required product translation unit must appear in the Cobertura input.
  - Tier-4 exclusions are versioned data, never hidden checker constants.
  - Enforced subsystems with no instrumented lines fail closed.

Related:
  - tools/coverage_floors.json
  - tools/validate_coverage.bat
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import sys
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


PRODUCT_ROOT = "SkullbonezSource/"


@dataclass(frozen=True)
class CoverageLine:
    path: str
    number: int
    hits: int


@dataclass(frozen=True)
class SubsystemResult:
    name: str
    tier: int
    covered: int
    total: int
    floor_percent: float
    ratified_floor_percent: float

    @property
    def percent(self) -> float | None:
        return (100.0 * self.covered / self.total) if self.total else None


def normalize_product_path(raw_path: str) -> str | None:
    normalized = raw_path.replace("\\", "/")
    lower = normalized.lower()
    marker = PRODUCT_ROOT.lower()
    index = lower.find(marker)
    if index < 0:
        return None
    return PRODUCT_ROOT + normalized[index + len(PRODUCT_ROOT) :]


def matches_any(path: str, globs: Iterable[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern.replace("\\", "/")) for pattern in globs)


def load_lines(xml_path: Path) -> dict[tuple[str, int], CoverageLine]:
    root = ET.parse(xml_path).getroot()
    merged: dict[tuple[str, int], CoverageLine] = {}
    for class_node in root.findall("./packages/package/classes/class"):
        path = normalize_product_path(class_node.get("filename", ""))
        if path is None:
            continue
        for line_node in class_node.findall("./lines/line"):
            number = int(line_node.get("number", "0"))
            hits = int(float(line_node.get("hits", "0")))
            if number <= 0:
                continue
            key = (path, number)
            previous = merged.get(key)
            if previous is None or hits > previous.hits:
                merged[key] = CoverageLine(path, number, hits)
    return merged


def load_config(config_path: Path) -> dict[str, object]:
    data = json.loads(config_path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1:
        raise ValueError("coverage config schema_version must be 1")
    if data.get("mode") not in ("report_only", "enforce"):
        raise ValueError("coverage config mode must be report_only or enforce")
    subsystems = data.get("subsystems")
    if not isinstance(subsystems, list) or not subsystems:
        raise ValueError("coverage config must define at least one subsystem")
    return data


def aggregate(
    lines: dict[tuple[str, int], CoverageLine], config: dict[str, object]
) -> tuple[list[SubsystemResult], tuple[int, int], list[str]]:
    excluded_globs = config.get("excluded_product_globs", [])
    if not isinstance(excluded_globs, list) or not all(isinstance(item, str) for item in excluded_globs):
        raise ValueError("excluded_product_globs must be a list of strings")

    subsystem_rows = config["subsystems"]
    assert isinstance(subsystem_rows, list)
    ownership: dict[tuple[str, int], str] = {}
    results: list[SubsystemResult] = []
    errors: list[str] = []

    for row in subsystem_rows:
        if not isinstance(row, dict):
            raise ValueError("each subsystem row must be an object")
        name = row.get("name")
        tier = row.get("tier")
        floor = row.get("floor_percent")
        ratified = row.get("ratified_floor_percent")
        include_globs = row.get("include_globs")
        required_sources = row.get("required_instrumented_sources", [])
        if not isinstance(name, str) or not name:
            raise ValueError("each subsystem needs a non-empty name")
        if not isinstance(tier, int) or tier not in (1, 2, 3):
            raise ValueError(f"subsystem {name} tier must be 1, 2, or 3")
        if not isinstance(floor, (int, float)) or not 0.0 <= float(floor) <= 100.0:
            raise ValueError(f"subsystem {name} floor_percent must be in [0, 100]")
        if not isinstance(ratified, (int, float)) or not 0.0 <= float(ratified) <= 100.0:
            raise ValueError(f"subsystem {name} ratified_floor_percent must be in [0, 100]")
        if not isinstance(include_globs, list) or not include_globs or not all(
            isinstance(item, str) and item for item in include_globs
        ):
            raise ValueError(f"subsystem {name} include_globs must be non-empty strings")
        if not isinstance(required_sources, list) or not all(
            isinstance(item, str) and item.startswith(PRODUCT_ROOT) for item in required_sources
        ):
            raise ValueError(
                f"subsystem {name} required_instrumented_sources must be product source paths"
            )

        instrumented_paths = {line.path for line in lines.values()}
        # Invariant: configured positive scope fails closed before floor math;
        # a missing translation unit is not equivalent to zero uncovered lines.
        for required_source in required_sources:
            normalized_required = required_source.replace("\\", "/")
            if matches_any(normalized_required, excluded_globs):
                raise ValueError(f"subsystem {name} required source is globally excluded: {normalized_required}")
            if not matches_any(normalized_required, include_globs):
                raise ValueError(f"subsystem {name} required source is outside its include globs: {normalized_required}")
            if normalized_required not in instrumented_paths:
                errors.append(f"{name}: required source was not instrumented: {normalized_required}")

        selected: list[CoverageLine] = []
        for key, line in lines.items():
            if matches_any(line.path, excluded_globs) or not matches_any(line.path, include_globs):
                continue
            previous_owner = ownership.get(key)
            if previous_owner is not None and previous_owner != name:
                errors.append(f"{line.path}:{line.number} belongs to both {previous_owner} and {name}")
                continue
            ownership[key] = name
            selected.append(line)

        covered = sum(1 for line in selected if line.hits > 0)
        results.append(
            SubsystemResult(name, tier, covered, len(selected), float(floor), float(ratified))
        )

    global_lines = [line for line in lines.values() if not matches_any(line.path, excluded_globs)]
    global_covered = sum(1 for line in global_lines if line.hits > 0)
    return results, (global_covered, len(global_lines)), errors


def format_percent(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2f}%"


def markdown_table(results: list[SubsystemResult], mode: str, global_totals: tuple[int, int]) -> str:
    lines = [
        "| Subsystem | Tier | Covered / instrumented | Line coverage | Active floor | Ratified floor | Status |",
        "|---|---:|---:|---:|---:|---:|---|",
    ]
    for result in results:
        percent = result.percent
        meets = percent is not None and percent + 1.0e-9 >= result.floor_percent
        status = "report-only" if mode == "report_only" else ("pass" if meets else "FAIL")
        lines.append(
            f"| `{result.name}` | {result.tier} | {result.covered} / {result.total} | "
            f"{format_percent(percent)} | {result.floor_percent:.2f}% | "
            f"{result.ratified_floor_percent:.2f}% | {status} |"
        )
    covered, total = global_totals
    global_percent = (100.0 * covered / total) if total else None
    lines.extend(
        [
            "",
            f"Whole instrumented product output (never gated): {covered} / {total} lines, "
            f"{format_percent(global_percent)}.",
        ]
    )
    return "\n".join(lines)


def evaluate(xml_path: Path, config_path: Path) -> tuple[int, str]:
    config = load_config(config_path)
    lines = load_lines(xml_path)
    results, global_totals, errors = aggregate(lines, config)
    mode = str(config["mode"])
    if mode == "enforce":
        for result in results:
            percent = result.percent
            if percent is None:
                errors.append(f"{result.name}: no instrumented product lines matched")
            elif percent + 1.0e-9 < result.floor_percent:
                errors.append(
                    f"{result.name}: {percent:.2f}% is below the {result.floor_percent:.2f}% floor"
                )
    table = markdown_table(results, mode, global_totals)
    if errors:
        table += "\n\nFailures:\n" + "\n".join(f"- {error}" for error in errors)
        return 1, table
    return 0, table


def run_self_tests() -> int:
    xml = """<?xml version=\"1.0\"?><coverage><packages><package><classes>
    <class filename=\"Repo\\SkullbonezSource\\Maths\\Vector3.cpp\"><lines>
      <line number=\"10\" hits=\"1\"/><line number=\"11\" hits=\"0\"/>
    </lines></class>
    <class filename=\"Repo/SkullbonezSource/Maths/Vector3.cpp\"><lines>
      <line number=\"10\" hits=\"3\"/>
    </lines></class>
    <class filename=\"Repo/SkullbonezSource/UI/UI.cpp\"><lines><line number=\"1\" hits=\"1\"/></lines></class>
    </classes></package></packages></coverage>"""
    config = {
        "schema_version": 1,
        "mode": "enforce",
        "excluded_product_globs": ["SkullbonezSource/UI/**"],
        "subsystems": [
            {
                "name": "maths",
                "tier": 1,
                "floor_percent": 50.0,
                "ratified_floor_percent": 85.0,
                "include_globs": ["SkullbonezSource/Maths/**"],
                "required_instrumented_sources": ["SkullbonezSource/Maths/Vector3.cpp"],
            }
        ],
    }
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        xml_path = root / "coverage.xml"
        config_path = root / "floors.json"
        xml_path.write_text(xml, encoding="utf-8")
        config_path.write_text(json.dumps(config), encoding="utf-8")
        code, output = evaluate(xml_path, config_path)
        if code != 0 or "1 / 2" not in output or "50.00%" not in output:
            print("SELF_TEST_FAIL: merge/exclusion/floor pass case", file=sys.stderr)
            return 1
        config["subsystems"][0]["floor_percent"] = 51.0
        config_path.write_text(json.dumps(config), encoding="utf-8")
        code, output = evaluate(xml_path, config_path)
        if code == 0 or "below the 51.00% floor" not in output:
            print("SELF_TEST_FAIL: enforced floor breach was not rejected", file=sys.stderr)
            return 1
        config["subsystems"][0]["floor_percent"] = 50.0
        config["subsystems"][0]["required_instrumented_sources"] = [
            "SkullbonezSource/Maths/Matrix4.cpp"
        ]
        config_path.write_text(json.dumps(config), encoding="utf-8")
        code, output = evaluate(xml_path, config_path)
        if code == 0 or "required source was not instrumented" not in output:
            print("SELF_TEST_FAIL: missing required translation unit was not rejected", file=sys.stderr)
            return 1
    print("SELF_TEST_PASS: coverage path, merge, exclusion, required-source, and floor cases passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize Cobertura product-line coverage by subsystem.")
    parser.add_argument("--xml", default="TestOutput/coverage/coverage.xml", help="Cobertura XML path")
    parser.add_argument("--config", default="tools/coverage_floors.json", help="Versioned floor config")
    parser.add_argument("--markdown-out", help="Optional Markdown summary output path")
    parser.add_argument("--self-test", action="store_true", help="Run synthetic checker tests")
    args = parser.parse_args()

    if args.self_test:
        return run_self_tests()

    xml_path = Path(args.xml)
    config_path = Path(args.config)
    if not xml_path.exists():
        print(f"FAIL: coverage XML not found: {xml_path}", file=sys.stderr)
        return 2
    if not config_path.exists():
        print(f"FAIL: coverage config not found: {config_path}", file=sys.stderr)
        return 2
    try:
        code, output = evaluate(xml_path, config_path)
    except (ET.ParseError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"FAIL: coverage input error: {error}", file=sys.stderr)
        return 2
    print(output)
    if args.markdown_out:
        output_path = Path(args.markdown_out)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(output + "\n", encoding="utf-8")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
