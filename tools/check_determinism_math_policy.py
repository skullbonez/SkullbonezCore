#!/usr/bin/env python3
"""Reject nonportable math in Physics except in named presentation-only Maths owners."""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


SOURCE_ROOTS = ("SkullbonezSource/Physics", "SkullbonezSource/Maths")
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl"}
PRESENTATION_ONLY_FILES = {
    "SkullbonezSource/Maths/OrbitalMechanics.cpp": "Planning orbital mechanics",
}
PRESENTATION_ONLY_FUNCTIONS = {
    "SkullbonezSource/Maths/Matrix4.cpp": ("PerspectiveZeroToOne",),
    "SkullbonezSource/Maths/RotationMatrix.h": ("RotatePointAboutArbitrary",),
}
PHYSICS_FORBIDDEN_PRESENTATION_TOKENS = (
    "Orbital",
    "OrbitalMechanics",
    "PerspectiveZeroToOne",
    "RotatePointAboutArbitrary",
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
@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    column: int
    call: str
    kind: str
    source: str
    owner: str | None


def normalize_path(path: Path | str) -> str:
    return str(path).replace("\\", "/")


# Invariant: ignored bytes are replaced one-for-one so line and column remain
# useful diagnostics without becoming policy identity.
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


def offset_is_in_named_function(source: str, offset: int, function_name: str) -> bool:
    for match in re.finditer(rf"\b{re.escape(function_name)}\s*\(", source):
        parameter_opening = source.find("(", match.start(), match.end())
        depth = 0
        parameter_closing = -1
        for index in range(parameter_opening, len(source)):
            if source[index] == "(":
                depth += 1
            elif source[index] == ")":
                depth -= 1
                if depth == 0:
                    parameter_closing = index
                    break
        if parameter_closing < 0:
            continue
        opening = parameter_closing + 1
        while opening < len(source) and source[opening].isspace():
            opening += 1
        # A named function owner is granted only by its definition. A call in
        # another function must not borrow the name and widen that function.
        if opening >= len(source) or source[opening] != "{":
            continue
        depth = 0
        for index in range(opening, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    if opening <= offset <= index:
                        return True
                    break
    return False


def presentation_owner(relative_path: str, source: str, offset: int) -> str | None:
    if relative_path in PRESENTATION_ONLY_FILES:
        return PRESENTATION_ONLY_FILES[relative_path]
    for function_name in PRESENTATION_ONLY_FUNCTIONS.get(relative_path, ()):
        if offset_is_in_named_function(source, offset, function_name):
            return function_name
    return None


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
    for offset, call in sorted(candidates):
        if call not in KNOWN_MATH_CALLS or call in CERTIFIED_CALLS:
            continue
        line_number = stripped.count("\n", 0, offset) + 1
        line_start = stripped.rfind("\n", 0, offset) + 1
        column = offset - line_start
        stripped_line = stripped_lines[line_number - 1]
        original = original_lines[line_number - 1] if line_number <= len(original_lines) else stripped_line
        kind = "explicit-fma" if call in EXPLICIT_FMA_CALLS else "implementation-defined"
        findings.append(
            Finding(
                relative_path,
                line_number,
                column,
                call,
                kind,
                original.strip(),
                presentation_owner(relative_path, stripped, offset),
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


def policy_errors(repo: Path, findings: list[Finding]) -> list[str]:
    errors: list[str] = []
    for finding in findings:
        if finding.kind == "explicit-fma":
            reason = "explicit fused arithmetic is outside the repository floating-point contract"
        elif finding.path.startswith("SkullbonezSource/Physics/"):
            reason = "implementation-defined math is forbidden in Physics"
        elif finding.owner is None:
            reason = "call is outside a named presentation-only Maths owner"
        else:
            continue
        errors.append(
            f"{finding.path}:{finding.line}:{finding.column + 1}: {reason}: {finding.call}"
        )

    physics_root = repo / "SkullbonezSource/Physics"
    if physics_root.exists():
        for path in sorted(physics_root.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            raw_source = path.read_text(encoding="utf-8", errors="replace")
            source = strip_comments_and_literals(raw_source)
            for token in PHYSICS_FORBIDDEN_PRESENTATION_TOKENS:
                match = re.search(rf"\b{re.escape(token)}\b", source)
                if match is None:
                    match = re.search(
                        rf"(?m)^\s*#\s*include[^\n]*\b{re.escape(token)}\b", raw_source
                    )
                if match:
                    line = raw_source.count("\n", 0, match.start()) + 1
                    relative = normalize_path(path.relative_to(repo))
                    errors.append(
                        f"{relative}:{line}: Physics must not reference presentation-only Maths owner {token}"
                    )
    return errors


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

        if not any("forbidden in Physics" in error for error in policy_errors(repo, root_findings)):
            print("SELF_TEST_FAIL: Physics implementation-defined math was accepted", file=sys.stderr)
            return 1

        orbital_source = "double Angle(double x) { return std::atan2(x, 1.0); }\n"
        orbital_findings = findings_in_text(
            "SkullbonezSource/Maths/OrbitalMechanics.cpp", orbital_source
        )
        if policy_errors(repo, orbital_findings):
            print("SELF_TEST_FAIL: the named Planning orbital owner was rejected", file=sys.stderr)
            return 1

        matrix_source = """
float Matrix4::PerspectiveZeroToOne(float x) { return std::tan(x); }
float Matrix4::Unrelated(float x) { return std::tan(x); }
"""
        matrix_findings = findings_in_text("SkullbonezSource/Maths/Matrix4.cpp", matrix_source)
        matrix_errors = policy_errors(repo, matrix_findings)
        if len(matrix_errors) != 1 or "outside a named" not in matrix_errors[0]:
            print("SELF_TEST_FAIL: function-scoped presentation ownership widened", file=sys.stderr)
            return 1

        call_before_unrelated = """
float Call(float x) { return Matrix4::PerspectiveZeroToOne(x); }
float Unrelated(float x) { return std::tan(x); }
"""
        call_findings = findings_in_text(
            "SkullbonezSource/Maths/Matrix4.cpp", call_before_unrelated
        )
        call_errors = policy_errors(repo, call_findings)
        if len(call_errors) != 1 or "outside a named" not in call_errors[0]:
            print("SELF_TEST_FAIL: a function call widened presentation ownership", file=sys.stderr)
            return 1

        physics_source.write_text('#include "Maths/OrbitalMechanics.h"\n', encoding="utf-8")
        if not any("must not reference" in error for error in policy_errors(repo, [])):
            print("SELF_TEST_FAIL: Physics presentation-owner reference was accepted", file=sys.stderr)
            return 1
        physics_source.write_text(
            "float Use(auto x) { return Math::Orbital::PropagateToTime(x); }\n",
            encoding="utf-8",
        )
        if not any("owner Orbital" in error for error in policy_errors(repo, [])):
            print("SELF_TEST_FAIL: Physics orbital namespace reference was accepted", file=sys.stderr)
            return 1

    print("SELF_TEST_PASS: math references and function-scoped domain ownership are enforced.")
    return 0


def render_text(findings: list[Finding], scanned: int, errors: list[str]) -> str:
    lines = [
        "Determinism math policy inventory",
        f"source files: {scanned}",
        f"policy findings: {len(findings)}",
        f"named presentation-owner findings: {sum(finding.owner is not None for finding in findings)}",
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
    parser.add_argument("--self-test", action="store_true", help="run bounded parser and ownership fixtures")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    args = parser.parse_args()

    if args.self_test:
        return run_self_tests()

    repo = Path(args.repo).resolve()
    findings, scanned = scan_repository(repo)
    errors = policy_errors(repo, findings)

    if args.format == "json":
        payload = {
            "source_files": scanned,
            "findings": [asdict(finding) for finding in findings],
            "named_presentation_owner_findings": sum(
                finding.owner is not None for finding in findings
            ),
            "errors": errors,
        }
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print(render_text(findings, scanned, errors))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
