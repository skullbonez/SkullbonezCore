#!/usr/bin/env python3
"""
File: inventory_authority_free_aggregates.py
Purpose:
  Report aggregate types that carry data for one operation without owning an
  invariant, so a reviewer can rule each one instead of pattern-matching a name.

Summary:
  The repository bans context bags, service bags, and parameter bags by name.
  Naming is the one property a type can change for free, so the bans produced
  more types rather than fewer. This inventory keys on structure instead: how
  many members an aggregate has, whether its header names an invariant it owns,
  how many sites construct it, and whether its consumers immediately destructure
  it. Those four facts are what separate a domain value from a bag.

Mental model:
  A legitimate aggregate answers "what rule do I enforce?" — a phase order, a
  lifetime, an arbitration policy — and its header says so in an `Invariant:`
  block. An authority-free bag answers only "what does this call need?", so it is
  built at one site, unpacked at one site, and its header describes its fields.
  The strongest mechanical signals are a single member (a type that shortens
  nothing) and one-construction/one-consumer with no stated invariant.

Glossary:
  Aggregate: A `struct` whose members are data, with no owned invariant claimed.
  Single-member aggregate: An aggregate with exactly one member. It cannot
    shorten a signature, so it exists only to add a name.
  Stated invariant: An `Invariant:` line in the type's own doc comment, or the
    file header, naming the rule the type enforces.
  Destructured at entry: The sole consumer reads members straight into locals or
    parameters without retaining the aggregate.
  Signal: A structural observation about one aggregate. Signals rank rows for
    review; they are not a verdict and never a score.
  Ruling: An owner verdict in tools/aggregate_ownership_rulings.json — `remove`,
    `retain`, `retain-prior`, or `repair`, each with an owner and reason.

Invariants:
  - Comments, literals, and preprocessor lines cannot create a row or a member.
  - The inventory reports and ranks; it never decides. Only the ruling file
    decides, and only an owner edits the ruling file.
  - An unruled aggregate carrying a signal fails the gate. A ruled row passes
    regardless of its signals, because the owner has judged it.
  - No count, ratio, or member budget is frozen anywhere in this script or its
    ruling file.
  - The script is read-only.

Related:
  - tools/aggregate_ownership_rulings.json
  - tools/inventory_extraction_scars.py
  - Agentic/Plans/TODO/ceremonial-aggregate-elimination.md
  - Agentic/Plans/TODO/governance-shape-to-judgment-conversion.md
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

from cpp_source_scan import line_of_offset, mask_cpp, tracked_files

DEFAULT_ROOTS = ["SkullbonezSource"]
RULINGS_RELATIVE = "tools/aggregate_ownership_rulings.json"

# Suffix families the repository has historically used for per-operation data.
# The suffix selects candidates for review; it is never the finding itself.
CANDIDATE_SUFFIXES = (
    "Context",
    "Inputs",
    "Input",
    "Params",
    "Args",
    "Operands",
    "Services",
    "Bindings",
    "Bag",
)

STRUCT_RE = re.compile(r"\bstruct\s+(?P<name>[A-Za-z_]\w*)\s*(?::[^{;]*)?\{", re.M)
MEMBER_RE = re.compile(
    r"""
    ^[ \t]*
    (?!(?:public|private|protected|using|typedef|static_assert|friend|template|return)\b)
    (?P<decl>[A-Za-z_][\w:<>,\s\*&\[\]]*?)
    \s(?P<name>[A-Za-z_]\w*)
    \s*(?:\[[^\]]*\])?
    \s*(?:=[^;]*)?;
    """,
    re.X | re.M,
)
INVARIANT_RE = re.compile(r"^\s*(?://|\*)?\s*Invariant:", re.M)
IDENTIFIER_RE = re.compile(r"[A-Za-z_]\w*")


@dataclass
class Aggregate:
    name: str
    path: str
    line: int
    member_count: int
    member_names: list[str]
    has_stated_invariant: bool
    construction_sites: list[str] = field(default_factory=list)
    parameter_sites: list[str] = field(default_factory=list)

    def key(self) -> str:
        return self.name

    def structural_signals(self) -> list[str]:
        """Exact shape facts computed from the type body alone.

        Only facts that need no cross-file resolution appear here. A single-member
        aggregate cannot shorten a signature, and an empty one carries nothing, so
        both are decidable from the declaration and safe to gate on.
        """
        found: list[str] = []
        if self.member_count == 0:
            found.append("empty")
        elif self.member_count == 1:
            found.append("single-member")
        return found

    def signals(self) -> list[str]:
        """Gating signals.

        Why a conjunction: a type that names the invariant it owns is never
        flagged, whatever its shape, because the owner has already answered the
        question this inventory asks. "No stated invariant" alone is not a defect
        either — it becomes one only when the shape also says the type carries
        data for one call.

        Why "sole consumer destructures at entry" is not gated here: deciding it
        requires distinguishing a construction from a same-named local, which is
        not decidable lexically without a compiler database. Site counts below
        are review context, and the destructuring test stays an AGENTS.md review
        question. Gating on an unreliable count would reproduce the frozen-metric
        failure this inventory exists to replace.
        """
        if self.has_stated_invariant:
            return []
        found = self.structural_signals()
        if found:
            found.append("no-stated-invariant")
        return found


def _matched_brace_body(masked: str, open_index: int) -> tuple[str, int] | None:
    depth = 0
    index = open_index
    while index < len(masked):
        char = masked[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return (masked[open_index + 1 : index], index + 1)
        index += 1
    return None


def _doc_comment_above(text: str, offset: int) -> str:
    """Contiguous comment block immediately preceding the declaration."""
    start = text.rfind("\n\n", 0, offset)
    return text[start if start >= 0 else 0 : offset]


def _count_members(body: str) -> tuple[int, list[str]]:
    names: list[str] = []
    for match in MEMBER_RE.finditer(body):
        decl = match.group("decl")
        # A method declaration has a parameter list; a member does not.
        if "(" in decl or ")" in decl:
            continue
        names.append(match.group("name"))
    return (len(names), names)


def collect_aggregates(repo: Path, roots: list[str]) -> dict[str, Aggregate]:
    aggregates: dict[str, Aggregate] = {}
    files = tracked_files(repo, roots)
    # Lifetime: masking is the expensive step, so each file is masked once and
    # both passes read the cache. It expires when this call returns.
    masked_cache: dict[Path, str] = {}

    for file_path in files:
        relative = file_path.relative_to(repo).as_posix()
        text = file_path.read_text(encoding="utf-8", errors="replace")
        masked = mask_cpp(text)
        masked_cache[file_path] = masked
        for match in STRUCT_RE.finditer(masked):
            name = match.group("name")
            if not name.endswith(CANDIDATE_SUFFIXES):
                continue
            open_index = masked.find("{", match.start())
            body = _matched_brace_body(masked, open_index)
            if body is None:
                continue
            member_count, member_names = _count_members(body[0])
            line = line_of_offset(masked, match.start("name"))
            # A forward declaration has no body content worth reading; skip it in
            # favour of the definition, wherever that lives.
            aggregates[name] = Aggregate(
                name=name,
                path=relative,
                line=line,
                member_count=member_count,
                member_names=member_names,
                has_stated_invariant=bool(INVARIANT_RE.search(_doc_comment_above(text, match.start()))),
            )

    # Second pass: construction and parameter sites for the collected names.
    # Why: one combined alternation per file is the difference between a
    # sub-second scan and a per-identifier regex storm across ~180K lines.
    if not aggregates:
        return aggregates
    usage_re = re.compile(r"\b(?:" + "|".join(sorted(map(re.escape, aggregates), key=len, reverse=True)) + r")\b")
    construction_re = re.compile(r"\s*[{(]")
    parameter_re = re.compile(r"\s*(?:const\s*)?[&*]?\s*[A-Za-z_]\w*\s*[,)]")
    declaration_re = re.compile(r"\b(?:struct|class)\s*$")

    for file_path in files:
        relative = file_path.relative_to(repo).as_posix()
        masked = masked_cache[file_path]
        for usage in usage_re.finditer(masked):
            aggregate = aggregates[usage.group(0)]
            after = masked[usage.end() : usage.end() + 120]
            before = masked[max(0, usage.start() - 16) : usage.start()]
            if declaration_re.search(before):
                continue
            location = f"{relative}:{line_of_offset(masked, usage.start())}"
            if construction_re.match(after):
                aggregate.construction_sites.append(location)
            elif parameter_re.match(after):
                aggregate.parameter_sites.append(location)

    return aggregates


def load_rulings(repo: Path) -> dict[str, dict]:
    path = repo / RULINGS_RELATIVE
    if not path.exists():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    return {row["key"]: row for row in data.get("aggregates", [])}


def report(aggregates: dict[str, Aggregate], rulings: dict[str, dict], verbose: bool = True) -> int:
    flagged = {name: item for name, item in aggregates.items() if item.signals()}
    unruled = sorted(name for name in flagged if name not in rulings)

    if verbose:
        stated = sum(1 for item in aggregates.values() if item.has_stated_invariant)
        print(
            f"Authority-free aggregate inventory: candidates={len(aggregates)} "
            f"state_own_invariant={stated} signalled={len(flagged)} "
            f"ruled={len(flagged) - len(unruled)} unruled={len(unruled)}"
        )
        for name in sorted(flagged):
            item = flagged[name]
            ruling = rulings.get(name)
            verdict = ruling["verdict"] if ruling else "UNRULED"
            print(
                f"  [{verdict}] {item.path}:{item.line} {name} "
                f"members={item.member_count} signals={','.join(item.signals())}"
            )
        # Review context: the widest candidates are where the destructuring
        # question matters most, but the counts are lexical and not gated.
        review = sorted(
            (item for item in aggregates.values() if not item.has_stated_invariant and item.member_count >= 4),
            key=lambda entry: (-entry.member_count, entry.name),
        )
        if review:
            print(f"Review context ({len(review)} multi-member candidates without a stated invariant):")
            for item in review[:20]:
                print(
                    f"  - {item.path}:{item.line} {item.name} members={item.member_count} "
                    f"lexical_constructions={len(set(item.construction_sites))} "
                    f"lexical_parameter_uses={len(set(item.parameter_sites))}"
                )
            if len(review) > 20:
                print(f"  ... {len(review) - 20} more; use --format json for the complete list")

    if unruled:
        if verbose:
            print(
                f"FAIL: {len(unruled)} aggregate(s) carry a structural signal and have no owner "
                f"ruling in {RULINGS_RELATIVE}: {', '.join(unruled)}"
            )
        return 1
    if verbose:
        print("PASS: every signalled aggregate carries an owner ruling.")
    return 0


# --- Self-test -------------------------------------------------------------
# Why: fixtures pin the difference between an invariant owner and a bag. Removing
# any guard below must make this self-test fail.

FIXTURE_SINGLE_MEMBER = """
struct TornadoUICommandContext
{
    SceneWorld& world;
};
"""

FIXTURE_NO_INVARIANT = """
// Lifetime: borrowed only while one packet is applied.
struct RuntimePresentationUICommandContext
{
    OverlayDebugState& debug;
    SceneSessionState& scene;
    EngineConfig& config;
};
"""

FIXTURE_INVARIANT_OWNER = """
// Concept: one phase-checked transaction owns the load walk.
// Invariant: phases run Load -> RuntimeReactions -> Presentation exactly once;
//   an out-of-order call is lane-F fatal.
struct SceneLoadTransactionInput
{
    int phaseCursor;
    int requestCount;
    bool arbitrationResolved;
};
"""

FIXTURE_UNRELATED_STRUCT = """
struct PhysicsBodyRecord
{
    int handle;
    float mass;
};
"""

FIXTURE_COMMENTED_STRUCT = """
/*
struct GhostContext
{
    int unused;
};
*/
"""


def _parse(source: str) -> dict[str, Aggregate]:
    masked = mask_cpp(source)
    found: dict[str, Aggregate] = {}
    for match in STRUCT_RE.finditer(masked):
        name = match.group("name")
        if not name.endswith(CANDIDATE_SUFFIXES):
            continue
        body = _matched_brace_body(masked, masked.find("{", match.start()))
        if body is None:
            continue
        count, names = _count_members(body[0])
        found[name] = Aggregate(
            name=name,
            path="fixture.h",
            line=line_of_offset(masked, match.start("name")),
            member_count=count,
            member_names=names,
            has_stated_invariant=bool(INVARIANT_RE.search(_doc_comment_above(source, match.start()))),
        )
    return found


def self_test() -> int:
    failures: list[str] = []

    single = _parse(FIXTURE_SINGLE_MEMBER)
    if "TornadoUICommandContext" not in single:
        failures.append("single-member fixture was not collected")
    elif single["TornadoUICommandContext"].member_count != 1:
        failures.append(
            f"single-member fixture counted {single['TornadoUICommandContext'].member_count} members"
        )
    elif "single-member" not in single["TornadoUICommandContext"].signals():
        failures.append("single-member signal was not raised")

    plain = _parse(FIXTURE_NO_INVARIANT)
    key = "RuntimePresentationUICommandContext"
    if key not in plain or plain[key].member_count != 3:
        failures.append("three-member fixture was miscounted")
    elif plain[key].signals():
        # Why: a multi-member aggregate must not be flagged on the missing-
        # invariant fact alone. Most types state their rules in the file header,
        # so that alone is not evidence of a bag, and site counting is not
        # reliable enough to gate. Such a type is review context, not a failure.
        failures.append("a multi-member aggregate with no shape signal must not be flagged")
    else:
        # Site counts must still be collected, because the review context list
        # ranks by them even though the gate ignores them.
        plain[key].construction_sites.append("fixture.cpp:10")
        plain[key].parameter_sites.append("fixture.h:20")
        if plain[key].signals():
            failures.append("lexical site counts must never raise a gating signal")

    owner = _parse(FIXTURE_INVARIANT_OWNER)
    key = "SceneLoadTransactionInput"
    if key not in owner:
        failures.append("invariant-owner fixture was not collected")
    elif not owner[key].has_stated_invariant:
        failures.append("a stated Invariant: block must be detected")
    else:
        # An invariant owner stays unflagged even with the strongest shape
        # signal, because the owner has already answered the question.
        owner[key].construction_sites.append("fixture.cpp:10")
        if owner[key].signals():
            failures.append("an aggregate that states its invariant must never be flagged")

    if _parse(FIXTURE_UNRELATED_STRUCT):
        failures.append("a struct outside the candidate suffixes must not be collected")
    if _parse(FIXTURE_COMMENTED_STRUCT):
        failures.append("a commented-out struct must not be collected")

    gate = _parse(FIXTURE_SINGLE_MEMBER)
    if report(gate, {}, verbose=False) == 0:
        failures.append("an unruled signalled aggregate must fail the gate")
    if report(gate, {"TornadoUICommandContext": {"verdict": "remove"}}, verbose=False) != 0:
        failures.append("a ruled aggregate must pass the gate")

    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1
    print("PASS: authority-free aggregate inventory self-test")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Inventory authority-free C++ aggregates.")
    parser.add_argument("--repo", type=Path, default=Path("."), help="Repository root")
    parser.add_argument("--roots", nargs="*", default=DEFAULT_ROOTS, help="Repository-relative scan roots")
    parser.add_argument("--self-test", action="store_true", help="Run planted fixtures")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    repo = args.repo.resolve()
    aggregates = collect_aggregates(repo, args.roots)
    if args.format == "json":
        payload = [
            {
                "name": item.name,
                "path": item.path,
                "line": item.line,
                "members": item.member_count,
                "member_names": item.member_names,
                "stated_invariant": item.has_stated_invariant,
                "construction_sites": sorted(set(item.construction_sites)),
                "parameter_sites": sorted(set(item.parameter_sites)),
                "signals": item.signals(),
            }
            for item in sorted(aggregates.values(), key=lambda entry: entry.name)
        ]
        print(json.dumps(payload, indent=2))
        return 0
    return report(aggregates, load_rulings(repo))


if __name__ == "__main__":
    sys.exit(main())
