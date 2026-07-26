#!/usr/bin/env python3
"""
File: inventory_extraction_scars.py
Purpose:
  Report locals that claim membership they do not have, or that alias a
  parameter without transforming it.

Summary:
  When a function body is lifted out of a god class, the cheapest way to avoid
  editing it is to rebind the new parameters to the old member names. The result
  compiles, behaves identically, and hides the fact that the extraction moved
  code rather than design. No existing repository checker can see it, because the
  evidence is a local variable. This inventory reports two shapes: a block-scope
  declaration whose declarator uses the `m_` member convention, and a declaration
  that is nothing but a second name for a parameter.

Mental model:
  Both rules need the same primitive: "is this statement a *declaration*, and
  what is its declarator name?" `return m_dataRoot;` and `m_assets = assets;`
  are not declarations, so a loose pattern that keys on `m_` or on `name =`
  reports member reads and member writes as scars. This script therefore scans
  block-scope statements for a declaration head — qualifiers, one plausible type
  expression, optional pointer/reference, then a declarator — and applies both
  rules only to what that scanner accepts.

Glossary:
  Extraction scar: A local declaration that preserves a pre-extraction spelling
    so a moved function body needed no internal edits.
  Member-prefixed local: A block-scope declaration named `m_*`, which readers
    correctly interpret as owner state rather than a call-scoped borrow.
  Pure alias: A *reference* declaration initialized from a bare parameter name.
    A value declaration is a copy the body then mutates, which is a real
    semantic difference from the parameter and is deliberately not reported.
  Plausible type expression: A qualified identifier with optional template
    arguments that is not a control keyword. This is what separates a
    declaration from an expression statement.
  Ruling: An owner verdict in tools/aggregate_ownership_rulings.json. An unruled
    row fails the gate; a ruled row reports and passes.

Invariants:
  - Comments, literals, and preprocessor lines cannot create a row.
  - Only block scope is inspected. A declaration directly inside a class body is
    a real member and must never be reported.
  - A statement that is not a declaration cannot produce a row, so member reads
    (`return m_x;`) and member writes (`m_x = p;`) are structurally excluded.
  - The script is read-only and never edits the ruling file.
  - An unruled finding is a gate failure; a ruled finding is evidence, not a
    budget. No count is frozen.

Related:
  - tools/aggregate_ownership_rulings.json
  - tools/inventory_authority_free_aggregates.py
  - Agentic/Plans/TODO/extraction-scar-remediation.md
  - Agentic/Plans/TODO/governance-shape-to-judgment-conversion.md
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

from cpp_source_scan import line_of_offset, mask_cpp, tracked_files

DEFAULT_ROOTS = ["SkullbonezSource"]
RULINGS_RELATIVE = "tools/aggregate_ownership_rulings.json"

DECL_QUALIFIERS = r"(?:(?:const|constexpr|static|volatile|mutable|thread_local|inline|register)\s+)*"
TYPE_EXPRESSION = r"(?:[A-Za-z_]\w*\s*::\s*)*[A-Za-z_]\w*(?:\s*<[^;{}()]*?>)?"

# Statement boundary, qualifiers, one type expression, pointer/reference, then a
# declarator. `[*&\s]+` after the type is what forces a declaration shape: an
# expression statement has an operator there instead.
DECLARATION_RE = re.compile(
    rf"""
    (?:^|[;{{}}])                    # statement boundary
    \s*
    (?P<quals>{DECL_QUALIFIERS})
    (?P<type>{TYPE_EXPRESSION})
    (?P<ptr>[\s]*[*&]*[\s]*|[\s]+)
    (?P<name>[A-Za-z_]\w*)\b
    \s*
    (?P<init>=\s*(?P<rhs>[^;{{}}]*);|;|\{{)
    """,
    re.X | re.M,
)

# Words that can lead a statement and look like a type to a regex.
NON_TYPE_LEADERS = {
    "return",
    "if",
    "else",
    "while",
    "for",
    "do",
    "switch",
    "case",
    "default",
    "break",
    "continue",
    "goto",
    "throw",
    "delete",
    "new",
    "sizeof",
    "alignof",
    "decltype",
    "typeid",
    "static_assert",
    "using",
    "typedef",
    "namespace",
    "template",
    "class",
    "struct",
    "union",
    "enum",
    "public",
    "private",
    "protected",
    "virtual",
    "friend",
    "operator",
    "explicit",
    "noexcept",
    "co_await",
    "co_return",
    "co_yield",
    "assert",
}

CLASS_BODY_RE = re.compile(r"\b(?:class|struct|union)\b[^;{}()]*\{")
IDENTIFIER_RE = re.compile(r"[A-Za-z_]\w*")


@dataclass(frozen=True)
class Scar:
    path: str
    line: int
    kind: str
    name: str
    detail: str

    def key(self) -> str:
        return f"{self.path}:{self.name}"


def _matched_brace_span(masked: str, open_index: int) -> tuple[int, int] | None:
    depth = 0
    index = open_index
    while index < len(masked):
        char = masked[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return (open_index, index + 1)
        index += 1
    return None


def _class_body_spans(masked: str) -> list[tuple[int, int]]:
    """Half-open offset ranges covering class/struct/union bodies."""
    spans: list[tuple[int, int]] = []
    for match in CLASS_BODY_RE.finditer(masked):
        open_index = masked.find("{", match.start())
        if open_index < 0:
            continue
        span = _matched_brace_span(masked, open_index)
        if span:
            spans.append(span)
    return spans


def _innermost_span(spans: list[tuple[int, int]], offset: int) -> tuple[int, int] | None:
    best: tuple[int, int] | None = None
    for start, end in spans:
        if start <= offset < end and (best is None or start > best[0]):
            best = (start, end)
    return best


def _directly_in_class_body(class_spans: list[tuple[int, int]], masked: str, offset: int) -> bool:
    """True when the offset sits in a class body but not inside a nested function body."""
    span = _innermost_span(class_spans, offset)
    if span is None:
        return False
    # A member function body inside the class introduces another brace level.
    depth = masked.count("{", span[0] + 1, offset) - masked.count("}", span[0] + 1, offset)
    return depth == 0


def _split_top_level(text: str, separator: str = ",") -> list[str]:
    parts: list[str] = []
    depth = 0
    current: list[str] = []
    for char in text:
        if char in "<([{":
            depth += 1
        elif char in ">)]}":
            depth -= 1
        if char == separator and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(char)
    parts.append("".join(current))
    return parts


def _enclosing_parameter_names(masked: str, offset: int) -> set[str]:
    """Parameter identifiers of the function body containing the offset."""
    # Walk outward through brace levels to find the nearest enclosing body whose
    # head is a parameter list.
    depth = 0
    index = offset
    while index > 0:
        char = masked[index]
        if char == "}":
            depth += 1
        elif char == "{":
            if depth == 0:
                close = masked.rfind(")", 0, index)
                if close > 0:
                    inner = 0
                    probe = close
                    while probe >= 0:
                        if masked[probe] == ")":
                            inner += 1
                        elif masked[probe] == "(":
                            inner -= 1
                            if inner == 0:
                                break
                        probe -= 1
                    if probe >= 0:
                        between = masked[close + 1 : index]
                        # A function head allows only qualifiers/initializers here.
                        if not any(token in between for token in (";", "}")):
                            names: set[str] = set()
                            for raw in _split_top_level(masked[probe + 1 : close]):
                                words = IDENTIFIER_RE.findall(raw.split("=")[0])
                                if words:
                                    names.add(words[-1])
                            return names
                return set()
            depth -= 1
        index -= 1
    return set()


def _iter_declarations(masked: str):
    for match in DECLARATION_RE.finditer(masked):
        type_text = match.group("type").strip()
        first_word = IDENTIFIER_RE.match(type_text)
        if first_word and first_word.group(0) in NON_TYPE_LEADERS:
            continue
        if match.group("name") in NON_TYPE_LEADERS:
            continue
        # `Type name;` with no pointer/reference and no qualifier is ambiguous
        # with a call expression only when the type is a single word followed by
        # whitespace; require real separation.
        if not match.group("ptr").strip() and not match.group("ptr"):
            continue
        yield match


def scan_text(path: str, text: str) -> list[Scar]:
    masked = mask_cpp(text)
    class_spans = _class_body_spans(masked)
    scars: list[Scar] = []
    seen: set[tuple[str, int, str]] = set()

    for match in _iter_declarations(masked):
        offset = match.start("name")
        if _directly_in_class_body(class_spans, masked, offset):
            continue
        name = match.group("name")
        line = line_of_offset(masked, offset)

        if name.startswith("m_"):
            signature = (path, line, name)
            if signature in seen:
                continue
            seen.add(signature)
            scars.append(
                Scar(
                    path=path,
                    line=line,
                    kind="member-prefixed-local",
                    name=name,
                    detail="Block-scope declaration uses the m_ member convention; readers "
                    "will treat a call-scoped borrow as owner state.",
                )
            )
            continue

        # Why: only a reference declaration is genuinely a second name. A value
        # declaration (`Vector3 u = up;`) is a copy the body then mutates, which
        # is a real semantic difference from the parameter, not a scar.
        if "&" not in match.group("ptr"):
            continue
        rhs = (match.group("rhs") or "").strip()
        if not rhs or not re.fullmatch(r"[A-Za-z_]\w*", rhs):
            continue
        if rhs in {"nullptr", "true", "false", "this"} or rhs == name:
            continue
        if rhs not in _enclosing_parameter_names(masked, offset):
            continue
        signature = (path, line, name)
        if signature in seen:
            continue
        seen.add(signature)
        scars.append(
            Scar(
                path=path,
                line=line,
                kind="pure-parameter-alias",
                name=name,
                detail=f"Declaration is only a second name for parameter '{rhs}'; "
                "use the parameter directly or record why the alias is required.",
            )
        )

    scars.sort(key=lambda scar: (scar.path, scar.line, scar.name))
    return scars


def load_rulings(repo: Path) -> dict[str, dict]:
    path = repo / RULINGS_RELATIVE
    if not path.exists():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    return {row["key"]: row for row in data.get("extraction_scars", [])}


def scan_repo(repo: Path, roots: list[str]) -> list[Scar]:
    scars: list[Scar] = []
    for file_path in tracked_files(repo, roots):
        relative = file_path.relative_to(repo).as_posix()
        text = file_path.read_text(encoding="utf-8", errors="replace")
        scars.extend(scan_text(relative, text))
    return scars


def report(scars: list[Scar], rulings: dict[str, dict], verbose: bool = True) -> int:
    unruled = [scar for scar in scars if scar.key() not in rulings]
    ruled = [scar for scar in scars if scar.key() in rulings]

    if verbose:
        print(f"Extraction-scar inventory: findings={len(scars)} ruled={len(ruled)} unruled={len(unruled)}")
        for scar in scars:
            ruling = rulings.get(scar.key())
            verdict = ruling["verdict"] if ruling else "UNRULED"
            print(f"  [{verdict}] {scar.path}:{scar.line} {scar.kind} {scar.name}")
            if not ruling:
                print(f"      {scar.detail}")

    if unruled:
        if verbose:
            print(
                f"FAIL: {len(unruled)} extraction scar(s) have no owner ruling in "
                f"{RULINGS_RELATIVE}. Repair the code or record a ruling with owner and reason."
            )
        return 1
    if verbose:
        print("PASS: every extraction-scar finding carries an owner ruling.")
    return 0


# --- Self-test -------------------------------------------------------------
# Why: planted fixtures pin the declaration-versus-expression boundary. Removing
# any guard below must make this self-test fail.

POSITIVE_MEMBER_LOCAL = """
void Stage::Solve( SpanType candidatePairs, SpanType sleepState )
{
    auto& m_candidatePairs = candidatePairs;
    int total = 0;
}
"""

POSITIVE_PURE_ALIAS = """
void Stage::Run( Store& bodyStore )
{
    Store& records = bodyStore;
    records.Touch();
}
"""

NEGATIVE_REAL_MEMBER = """
class Stage
{
  private:
    SpanType m_candidatePairs;
    int m_count = 0;
};
"""

NEGATIVE_MEMBER_READ = """
const std::string& AssetSystem::GetDataRoot() const
{
    return m_dataRoot;
}
"""

NEGATIVE_MEMBER_WRITE = """
void TextureCollection::BindAssetSystem( AssetSystem* assets )
{
    m_assets = assets;
}
"""

NEGATIVE_LOOP_COMPARISON = """
void Profiler::Reset()
{
    for ( int i = 0; i < m_counterCount; ++i )
    {
        m_counters[i] = 0;
    }
}
"""

NEGATIVE_CONSTANT_ASSIGNMENT = """
std::size_t Normalize( std::size_t alignment )
{
    if ( alignment < DEFAULT_ALIGNMENT )
    {
        alignment = DEFAULT_ALIGNMENT;
    }
    return alignment;
}
"""

NEGATIVE_TRANSFORMED_LOCAL = """
void Stage::Run( Store& bodyStore )
{
    auto records = bodyStore.MutableRecords();
    records.Touch();
}
"""

NEGATIVE_MUTATED_COPY = """
Matrix4 BuildView( const Vector3& up )
{
    Vector3 u = up;
    if ( !u.TryNormalise() )
    {
        return Matrix4();
    }
    return Matrix4( u );
}
"""

NEGATIVE_COMMENT_AND_STRING = """
void Stage::Run( Store& bodyStore )
{
    // auto& m_candidatePairs = bodyStore;
    const char* text = "auto& m_sleepState = bodyStore;";
}
"""

NEGATIVE_NON_PARAMETER_SOURCE = """
void Stage::Run()
{
    Store& records = s_globalStore;
    records.Touch();
}
"""


def self_test() -> int:
    failures: list[str] = []

    def kinds(source: str) -> list[str]:
        return [scar.kind for scar in scan_text("fixture.cpp", source)]

    positives = [
        ("member-prefixed local", POSITIVE_MEMBER_LOCAL, "member-prefixed-local"),
        ("pure parameter alias", POSITIVE_PURE_ALIAS, "pure-parameter-alias"),
    ]
    for label, source, expected in positives:
        if expected not in kinds(source):
            failures.append(f"{label} fixture was not reported")

    negatives = [
        ("real class member", NEGATIVE_REAL_MEMBER),
        ("member read via return", NEGATIVE_MEMBER_READ),
        ("member write from parameter", NEGATIVE_MEMBER_WRITE),
        ("loop comparison against a member", NEGATIVE_LOOP_COMPARISON),
        ("assignment from a constant", NEGATIVE_CONSTANT_ASSIGNMENT),
        ("transformed local", NEGATIVE_TRANSFORMED_LOCAL),
        ("mutated value copy of a parameter", NEGATIVE_MUTATED_COPY),
        ("comment and string literal", NEGATIVE_COMMENT_AND_STRING),
        ("alias of a non-parameter", NEGATIVE_NON_PARAMETER_SOURCE),
    ]
    for label, source in negatives:
        found = kinds(source)
        if found:
            failures.append(f"{label} must not be reported (got {found})")

    scars = scan_text("fixture.cpp", POSITIVE_MEMBER_LOCAL)
    if report(scars, {}, verbose=False) == 0:
        failures.append("an unruled finding must fail the gate")
    if report(scars, {scars[0].key(): {"verdict": "repair"}}, verbose=False) != 0:
        failures.append("a ruled finding must pass the gate")

    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1
    print("PASS: extraction-scar inventory self-test")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Inventory C++ extraction scars.")
    parser.add_argument("--repo", type=Path, default=Path("."), help="Repository root")
    parser.add_argument("--roots", nargs="*", default=DEFAULT_ROOTS, help="Repository-relative scan roots")
    parser.add_argument("--self-test", action="store_true", help="Run planted fixtures")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    repo = args.repo.resolve()
    scars = scan_repo(repo, args.roots)
    return report(scars, load_rulings(repo))


if __name__ == "__main__":
    sys.exit(main())
