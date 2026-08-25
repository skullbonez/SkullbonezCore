#!/usr/bin/env python3
"""
File: tools/check_determinism_math_policy.py
Purpose:
  Inventory implementation-defined math entry-point uses in Physics and Maths.

Summary:
  The checker strips comments and literals, identifies C/C++ math entry-point
  references, and reconciles every non-certified use against an exact
  current-source review decision. It reports current structure; review records
  document review rather than creating a numerical allowance or count budget.

Glossary:
  Certified call: Exact or correctly-rounded operation admitted directly by the
    tier-2 determinism envelope.
  Source fingerprint: SHA-256 of the normalized statement containing a call;
    comment, whitespace, and physical line movement do not change it.

Invariants:
  - Scan roots are both SkullbonezSource/Physics and SkullbonezSource/Maths.
  - Unruled findings, stale rulings, malformed rulings, and missing repair plans
    fail the command.
  - Explicit fused multiply-add calls always require a ruling even though the
    operation itself is correctly rounded.
  - The tool never mutates the repository.

Related:
  - tools/determinism_math_rulings.json
  - Agentic/Reference/physics-overview.md
  - tools/validate_fast.bat
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


SOURCE_ROOTS = ("SkullbonezSource/Physics", "SkullbonezSource/Maths")
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl"}
RULING_DISPOSITIONS = {"retain-owner", "repair-plan"}
REQUIRED_RULING_FIELDS = (
    "path",
    "call",
    "source_fingerprint",
    "occurrence",
    "disposition",
    "owner",
    "reason",
)

# IEEE/basic operations and exact classification are certified directly. Both
# suffixed C spellings and overloaded std spellings represent the same operation
# family; the checker is about arithmetic semantics rather than namespace style.
CERTIFIED_FAMILIES = {
    "ceil",
    "copysign",
    "fabs",
    "floor",
    "fmod",
    "frexp",
    "ldexp",
    "round",
    "sqrt",
    "trunc",
}
CLASSIFICATION_CALLS = {
    "fpclassify",
    "isfinite",
    "isgreater",
    "isgreaterequal",
    "isinf",
    "isless",
    "islessequal",
    "islessgreater",
    "isnan",
    "isnormal",
    "isunordered",
    "signbit",
}
IMPLEMENTATION_DEFINED_FAMILIES = {
    "acos",
    "acosh",
    "asin",
    "asinh",
    "atan",
    "atan2",
    "atanh",
    "cbrt",
    "cos",
    "cosh",
    "erf",
    "erfc",
    "exp",
    "exp2",
    "expm1",
    "fdim",
    "fmax",
    "fmin",
    "hypot",
    "ilogb",
    "lgamma",
    "llrint",
    "llround",
    "log",
    "log10",
    "log1p",
    "log2",
    "logb",
    "lrint",
    "lround",
    "modf",
    "nan",
    "nearbyint",
    "nextafter",
    "nexttoward",
    "pow",
    "remainder",
    "remquo",
    "rint",
    "scalbln",
    "scalbn",
    "sin",
    "sinh",
    "tan",
    "tanh",
    "tgamma",
}
SPECIAL_MATH_FAMILIES = {
    "assoc_laguerre",
    "assoc_legendre",
    "beta",
    "comp_ellint_1",
    "comp_ellint_2",
    "comp_ellint_3",
    "cyl_bessel_i",
    "cyl_bessel_j",
    "cyl_bessel_k",
    "cyl_neumann",
    "ellint_1",
    "ellint_2",
    "ellint_3",
    "expint",
    "hermite",
    "laguerre",
    "legendre",
    "riemann_zeta",
    "sph_bessel",
    "sph_legendre",
    "sph_neumann",
}


def family_spellings(families: set[str]) -> set[str]:
    return {f"{family}{suffix}" for family in families for suffix in ("", "f", "l")}


CERTIFIED_CALLS = (
    family_spellings(CERTIFIED_FAMILIES)
    | CLASSIFICATION_CALLS
    | {"abs", "labs", "llabs"}
)
IMPLEMENTATION_DEFINED_CALLS = (
    family_spellings(IMPLEMENTATION_DEFINED_FAMILIES | SPECIAL_MATH_FAMILIES)
    | {"lerp"}
)
EXPLICIT_FMA_CALLS = {"fma", "fmaf", "fmal"}
KNOWN_MATH_CALLS = CERTIFIED_CALLS | IMPLEMENTATION_DEFINED_CALLS | EXPLICIT_FMA_CALLS
# Hazard: requiring an immediately following '(' on the same line would let
# line breaks, function pointers, using declarations, and aliases bypass the
# policy. Qualified references are always evidence; unqualified identifiers
# need call/address/using/macro context so an ordinary variable named
# `remainder` is not mistaken for std::remainder.
QUALIFIED_REFERENCE_PATTERN = re.compile(r"(?<![A-Za-z0-9_])std\s*::\s*(?P<call>[A-Za-z_]\w*)\b")
GLOBAL_REFERENCE_PATTERN = re.compile(r"(?<![A-Za-z0-9_:])::\s*(?P<call>[A-Za-z_]\w*)\b")
UNQUALIFIED_CALL_PATTERN = re.compile(r"(?<![A-Za-z0-9_:.>])(?P<call>[A-Za-z_]\w*)\s*\(")
UNQUALIFIED_ALIAS_PATTERN = re.compile(r"(?:&\s*|\busing\s+(?:::)?\s*)(?P<call>[A-Za-z_]\w*)\b")
STD_NAMESPACE_ALIAS_PATTERN = re.compile(
    r"\bnamespace\s+(?P<alias>[A-Za-z_]\w*)\s*=\s*(?:::)?\s*std\s*;"
)
MACRO_DEFINITION_PATTERN = re.compile(r"(?m)^[ \t]*#\s*define\b(?:[^\n]*\\[ \t]*\n)*[^\n]*$")
TOKEN_PATTERN = re.compile(r"\b(?P<call>[A-Za-z_]\w*)\b")
FINGERPRINT_PATTERN = re.compile(r"[0-9a-f]{64}")


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    column: int
    call: str
    kind: str
    source: str
    source_fingerprint: str
    occurrence: int

    @property
    def identity(self) -> tuple[str, str, str, int]:
        return (self.path, self.call, self.source_fingerprint, self.occurrence)


def normalize_path(path: Path | str) -> str:
    return str(path).replace("\\", "/")


# Invariant: replacing ignored bytes one-for-one preserves line and column
# coordinates, which are part of each ruling's exact current-source identity.
def strip_comments_and_literals(source: str) -> str:
    result: list[str] = []
    index = 0
    state = "code"
    while index < len(source):
        character = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""

        if state == "code":
            if character == "/" and following == "/":
                result.extend((" ", " "))
                index += 2
                state = "line-comment"
                continue
            if character == "/" and following == "*":
                result.extend((" ", " "))
                index += 2
                state = "block-comment"
                continue
            if character == '"':
                result.append(" ")
                index += 1
                state = "string"
                continue
            if character == "'":
                result.append(" ")
                index += 1
                state = "character"
                continue
            result.append(character)
            index += 1
            continue

        if state == "line-comment":
            result.append("\n" if character == "\n" else " ")
            if character == "\n":
                state = "code"
            index += 1
            continue

        if state == "block-comment":
            if character == "*" and following == "/":
                result.extend((" ", " "))
                index += 2
                state = "code"
                continue
            result.append("\n" if character == "\n" else " ")
            index += 1
            continue

        if character == "\\" and following:
            result.extend((" ", " "))
            index += 2
            continue
        result.append("\n" if character == "\n" else " ")
        terminator = '"' if state == "string" else "'"
        if character == terminator:
            state = "code"
        index += 1

    return "".join(result)


def source_fingerprint(stripped_source: str, offset: int) -> str:
    # Why: source coordinates are useful diagnostics but unstable policy keys.
    # The nearest statement/block delimiters retain enough semantic context to
    # invalidate edited arithmetic while ignoring comments and physical layout.
    previous_delimiters = [stripped_source.rfind(delimiter, 0, offset) for delimiter in ";{}"]
    start = max(previous_delimiters) + 1
    following_delimiters = [
        position
        for delimiter in ";{}"
        if (position := stripped_source.find(delimiter, offset)) >= 0
    ]
    end = min(following_delimiters) + 1 if following_delimiters else len(stripped_source)
    normalized = " ".join(stripped_source[start:end].split())
    return hashlib.sha256(normalized.encode("utf-8")).hexdigest()


def findings_in_text(relative_path: str, source: str) -> list[Finding]:
    stripped = strip_comments_and_literals(source)
    original_lines = source.splitlines()
    stripped_lines = stripped.splitlines()
    candidates: set[tuple[int, str]] = set()
    for pattern in (
        QUALIFIED_REFERENCE_PATTERN,
        GLOBAL_REFERENCE_PATTERN,
        UNQUALIFIED_CALL_PATTERN,
        UNQUALIFIED_ALIAS_PATTERN,
    ):
        for match in pattern.finditer(stripped):
            candidates.add((match.start("call"), match.group("call")))
    for alias_match in STD_NAMESPACE_ALIAS_PATTERN.finditer(stripped):
        alias_pattern = re.compile(
            rf"(?<![A-Za-z0-9_]){re.escape(alias_match.group('alias'))}\s*::\s*(?P<call>[A-Za-z_]\w*)\b"
        )
        for match in alias_pattern.finditer(stripped):
            candidates.add((match.start("call"), match.group("call")))
    for macro_match in MACRO_DEFINITION_PATTERN.finditer(stripped):
        for token_match in TOKEN_PATTERN.finditer(macro_match.group()):
            candidates.add((macro_match.start() + token_match.start("call"), token_match.group("call")))

    findings: list[Finding] = []
    occurrence_counts: dict[tuple[str, str], int] = {}
    for offset, call in sorted(candidates):
        if call not in KNOWN_MATH_CALLS or call in CERTIFIED_CALLS:
            continue
        line_number = stripped.count("\n", 0, offset) + 1
        line_start = stripped.rfind("\n", 0, offset) + 1
        column = offset - line_start
        stripped_line = stripped_lines[line_number - 1]
        original = original_lines[line_number - 1] if line_number <= len(original_lines) else stripped_line
        kind = "explicit-fma" if call in EXPLICIT_FMA_CALLS else "implementation-defined"
        fingerprint = source_fingerprint(stripped, offset)
        occurrence_key = (call, fingerprint)
        occurrence = occurrence_counts.get(occurrence_key, 0) + 1
        occurrence_counts[occurrence_key] = occurrence
        findings.append(
            Finding(
                relative_path,
                line_number,
                column,
                call,
                kind,
                original.strip(),
                fingerprint,
                occurrence,
            )
        )
    return findings


def iter_source_files(repo: Path) -> Iterable[Path]:
    for root in SOURCE_ROOTS:
        base = repo / root
        if not base.exists():
            continue
        for path in sorted(base.rglob("*")):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
                yield path


def scan_repository(repo: Path) -> tuple[list[Finding], int]:
    findings: list[Finding] = []
    scanned = 0
    for path in iter_source_files(repo):
        scanned += 1
        relative_path = normalize_path(path.relative_to(repo))
        findings.extend(findings_in_text(relative_path, path.read_text(encoding="utf-8", errors="replace")))
    findings.sort(key=lambda item: (item.path.lower(), item.line, item.column, item.call))
    return findings, scanned


def load_rulings(path: Path) -> tuple[list[dict[str, object]], list[str]]:
    if not path.exists():
        return [], [f"rulings file not found: {path}"]
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return [], [f"failed to read rulings: {error}"]
    if not isinstance(data, dict) or data.get("version") != 2 or not isinstance(data.get("rulings"), list):
        return [], ["rulings file must contain version 2 and a rulings array"]
    return data["rulings"], []


def validate_ruling(row: object, index: int, repo: Path) -> list[str]:
    if not isinstance(row, dict):
        return [f"ruling {index} must be an object"]
    errors: list[str] = []
    for retired_field in ("line", "column"):
        if retired_field in row:
            errors.append(f"ruling {index} must not pin physical source {retired_field}")
    for field in REQUIRED_RULING_FIELDS:
        value = row.get(field)
        if field == "occurrence":
            if not isinstance(value, int) or isinstance(value, bool) or value < 1:
                errors.append(f"ruling {index} has invalid {field}")
        elif not isinstance(value, str) or not value.strip():
            errors.append(f"ruling {index} missing {field}")
    disposition = row.get("disposition")
    if disposition not in RULING_DISPOSITIONS:
        errors.append(f"ruling {index} has invalid disposition: {disposition}")
    fingerprint = row.get("source_fingerprint")
    if isinstance(fingerprint, str) and FINGERPRINT_PATTERN.fullmatch(fingerprint) is None:
        errors.append(f"ruling {index} has invalid source_fingerprint")
    path = row.get("path")
    if isinstance(path, str) and normalize_path(path) != path:
        errors.append(f"ruling {index} path must use forward slashes: {path}")
    if disposition == "repair-plan":
        plan = row.get("plan")
        if not isinstance(plan, str) or not plan.startswith("Agentic/Plans/TODO/") or not plan.endswith(".md"):
            errors.append(f"ruling {index} repair-plan must name a canonical TODO Markdown plan")
        elif not (repo / plan).is_file():
            errors.append(f"ruling {index} repair plan does not exist: {plan}")
    return errors


def ruling_identity(row: dict[str, object]) -> tuple[object, object, object, object]:
    return (row.get("path"), row.get("call"), row.get("source_fingerprint"), row.get("occurrence"))


def reconcile(
    findings: list[Finding], rulings: list[dict[str, object]], repo: Path
) -> tuple[list[str], list[Finding], list[dict[str, object]]]:
    # Invariant: passing requires a one-to-one join between live findings and
    # structurally valid rulings. Historical rows cannot satisfy current code.
    errors: list[str] = []
    valid_rulings: list[dict[str, object]] = []
    seen: set[tuple[object, object, object, object, object]] = set()
    for index, row in enumerate(rulings):
        row_errors = validate_ruling(row, index, repo)
        errors.extend(row_errors)
        if row_errors:
            continue
        identity = ruling_identity(row)
        if identity in seen:
            errors.append(f"duplicate ruling identity: {identity}")
            continue
        seen.add(identity)
        valid_rulings.append(row)

    findings_by_identity = {finding.identity: finding for finding in findings}
    rulings_by_identity = {ruling_identity(row): row for row in valid_rulings}
    unruled = [finding for finding in findings if finding.identity not in rulings_by_identity]
    stale = [row for row in valid_rulings if ruling_identity(row) not in findings_by_identity]
    for finding in unruled:
        errors.append(f"{finding.path}:{finding.line}:{finding.column + 1}: unruled {finding.kind} call {finding.call}")
    for row in stale:
        errors.append(
            f"{row['path']}: stale ruling for {row['call']} occurrence={row['occurrence']}"
        )
    return errors, unruled, stale


def run_self_tests() -> int:
    synthetic = """
float ok(float x) { return sqrtf(x) + std::fabs(x) + std::isgreater(x, 0.0f); }
float bad(float x) { return std::sin(x) + acosf(x) + std::riemann_zeta(x) + std::lerp(x, 1.0f, 0.5f); }
float fused(float x) { return fmaf(x, x, 1.0f); }
float split(float x) { return std::sin
(x); }
auto pointer = &std::cos;
#define SYNTHETIC_TAN std::tan
#define SYNTHETIC_ASIN asinf
float global_call(float x) { return ::sinf(x); }
auto global_pointer = &::cosf;
namespace cm = std;
float namespace_alias(float x) { return cm::sin(x); }
#define SYNTHETIC_CONTINUED \
    cosf
float member_call(auto& value) { return value.remainder(); }
// cosf(x) and "atan2f(" are evidence text, not calls.
"""
    findings = findings_in_text("SkullbonezSource/Maths/Synthetic.cpp", synthetic)
    if [(finding.call, finding.kind) for finding in findings] != [
        ("sin", "implementation-defined"),
        ("acosf", "implementation-defined"),
        ("riemann_zeta", "implementation-defined"),
        ("lerp", "implementation-defined"),
        ("fmaf", "explicit-fma"),
        ("sin", "implementation-defined"),
        ("cos", "implementation-defined"),
        ("tan", "implementation-defined"),
        ("asinf", "implementation-defined"),
        ("sinf", "implementation-defined"),
        ("cosf", "implementation-defined"),
        ("sin", "implementation-defined"),
        ("cosf", "implementation-defined"),
    ]:
        print("SELF_TEST_FAIL: call classification or comment stripping changed", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as directory:
        repo = Path(directory)
        plan = repo / "Agentic/Plans/TODO/synthetic.md"
        plan.parent.mkdir(parents=True)
        plan.write_text("# Synthetic\n", encoding="utf-8")
        rulings: list[dict[str, object]] = []
        for finding in findings:
            disposition = "repair-plan" if finding.call == "acosf" else "retain-owner"
            row: dict[str, object] = {
                "path": finding.path,
                "call": finding.call,
                "source_fingerprint": finding.source_fingerprint,
                "occurrence": finding.occurrence,
                "disposition": disposition,
                "owner": "synthetic owner",
                "reason": "synthetic current-source review",
            }
            if disposition == "repair-plan":
                row["plan"] = "Agentic/Plans/TODO/synthetic.md"
            rulings.append(row)

        errors, unruled, stale = reconcile(findings, rulings, repo)
        if errors or unruled or stale:
            print("SELF_TEST_FAIL: exact current rulings did not pass", file=sys.stderr)
            return 1

        shifted_findings = findings_in_text(
            "SkullbonezSource/Maths/Synthetic.cpp",
            "\n// A location-only edit must not invalidate reviewed calls.\n" + synthetic,
        )
        errors, unruled, stale = reconcile(shifted_findings, rulings, repo)
        if errors or unruled or stale:
            print("SELF_TEST_FAIL: comment and line movement invalidated content rulings", file=sys.stderr)
            return 1

        pinned = list(rulings)
        pinned[0] = dict(pinned[0], line=findings[0].line)
        errors, _, _ = reconcile(findings, pinned, repo)
        if not any("must not pin physical source line" in error for error in errors):
            print("SELF_TEST_FAIL: a physical source line was accepted as ruling data", file=sys.stderr)
            return 1

        errors, unruled, _ = reconcile(findings, rulings[:-1], repo)
        if len(unruled) != 1 or not errors:
            print("SELF_TEST_FAIL: an unruled call did not fail", file=sys.stderr)
            return 1

        missing_plan = list(rulings)
        missing_plan[1] = dict(missing_plan[1])
        missing_plan[1]["plan"] = "Agentic/Plans/TODO/missing.md"
        errors, _, _ = reconcile(findings, missing_plan, repo)
        if not any("repair plan does not exist" in error for error in errors):
            print("SELF_TEST_FAIL: a missing repair plan did not fail", file=sys.stderr)
            return 1

        edited = list(rulings)
        edited[0] = dict(edited[0])
        edited[0]["source_fingerprint"] = "0" * 64
        errors, unruled, stale = reconcile(findings, edited, repo)
        if len(unruled) != 1 or len(stale) != 1 or not errors:
            print("SELF_TEST_FAIL: an edited source fingerprint did not fail currentness", file=sys.stderr)
            return 1

        maths_source = repo / "SkullbonezSource/Maths/Root.h"
        physics_source = repo / "SkullbonezSource/Physics/Root.cpp"
        maths_source.parent.mkdir(parents=True)
        physics_source.parent.mkdir(parents=True)
        maths_source.write_text("#define ROOT_COS std::cos\n", encoding="utf-8")
        physics_source.write_text("float Root(float x) { return std::sin\n(x); }\n", encoding="utf-8")
        root_findings, scanned = scan_repository(repo)
        root_projection = [(finding.path, finding.call) for finding in root_findings]
        if scanned != 2 or root_projection != [
            ("SkullbonezSource/Maths/Root.h", "cos"),
            ("SkullbonezSource/Physics/Root.cpp", "sin"),
        ]:
            print("SELF_TEST_FAIL: both required roots or bypass forms were not scanned", file=sys.stderr)
            return 1

    print("SELF_TEST_PASS: math references, both roots, exact rulings, and currentness are enforced.")
    return 0


def render_text(
    findings: list[Finding], scanned: int, rulings: list[dict[str, object]], errors: list[str], unruled: list[Finding], stale: list[dict[str, object]]
) -> str:
    lines = [
        "Determinism math policy inventory",
        f"source files: {scanned}",
        f"policy findings: {len(findings)}",
        f"current rulings: {len(findings) - len(unruled)}",
        f"unruled findings: {len(unruled)}",
        f"stale rulings: {len(stale)}",
        f"blocking diagnostics: {len(errors)}",
    ]
    for finding in findings:
        lines.append(f"  {finding.path}:{finding.line}:{finding.column + 1} {finding.kind} {finding.call}: {finding.source}")
    for error in errors:
        lines.append(f"ERROR: {error}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Inventory non-certified math calls in Physics and Maths.")
    parser.add_argument("--repo", default=".", help="repository root")
    parser.add_argument("--rulings", help="rulings JSON; defaults to tools/determinism_math_rulings.json")
    parser.add_argument("--self-test", action="store_true", help="run bounded parser/currentness fixtures")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    args = parser.parse_args()

    if args.self_test:
        return run_self_tests()

    repo = Path(args.repo).resolve()
    rulings_path = Path(args.rulings).resolve() if args.rulings else repo / "tools/determinism_math_rulings.json"
    findings, scanned = scan_repository(repo)
    rulings, load_errors = load_rulings(rulings_path)
    errors, unruled, stale = reconcile(findings, rulings, repo)
    errors = load_errors + errors

    if args.format == "json":
        payload = {
            "source_files": scanned,
            "findings": [asdict(finding) for finding in findings],
            "rulings": len(rulings),
            "unruled": len(unruled),
            "stale": len(stale),
            "errors": errors,
        }
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print(render_text(findings, scanned, rulings, errors, unruled, stale))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
