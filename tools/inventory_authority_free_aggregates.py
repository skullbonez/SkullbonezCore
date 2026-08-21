#!/usr/bin/env python3
"""
File: inventory_authority_free_aggregates.py
Purpose:
  Report aggregate types that carry data for one operation without owning an
  invariant, so a reviewer can rule each one instead of pattern-matching a name.
  Strict mode gates the bounded legacy-suffix family when the type states no
  invariant of its own. Suffix-free discovery remains wider review context.

Summary:
  The repository historically described context bags, service bags, and
  parameter bags by name. Naming is the one property a type can change for free,
  so this inventory discovers every data-bearing struct or class before it uses
  structural signals and the complete historical suffix family to select the
  owner-review surface. It reports member count, whether the type names an
  invariant, and lexical construction/consumer sites.

Mental model:
  A legitimate aggregate answers "what rule do I enforce?" — a phase order, a
  lifetime, an arbitration policy — and its header says so in an `Invariant:`
  block. An authority-free bag answers only "what does this call need?", so it is
  built at one site, unpacked at one site, and its header describes its fields.
  The strongest mechanical signal is a behavior-free type whose sole member is
  a borrowed pointer/reference. Strong scalar values and one-field behavior
  owners remain visible as review context without being mistaken for couriers.

Glossary:
  Aggregate: A data-bearing `struct` or `class`.
  Borrowed-member courier: A behavior-free aggregate whose sole member is a
    borrowed pointer/reference forwarded to another operation.
  Stated invariant: An `Invariant:` line in the type's own doc comment, or the
    file header, naming the rule the type enforces.
  Destructured at entry: Consumers read members straight into locals or
    parameters without retaining or forwarding the aggregate; `mixed` records a
    family whose consumers differ.
  Signal: A structural observation about one aggregate. Signals rank rows for
    review; they are not a verdict and never a score.
  Ruling: An owner verdict in tools/aggregate_ownership_rulings.json — `remove`,
    `retain`, `retain-prior`, or `repair`, each with an owner and reason.

Invariants:
  - Comments, literals, and preprocessor lines cannot create a row or a member.
  - The inventory reports and ranks; it never decides. Only the ruling file
    decides, and only an owner edits the ruling file.
  - In strict mode, an unruled aggregate with a legacy review suffix and no
    stated invariant fails the gate. A ruled row passes only as evidence that
    an owner has judged it.
  - Discovery never depends on a type suffix or on choosing `struct` over
    `class`; renaming cannot remove a shape from the inventory.
  - The bounded gate is name-scoped and therefore evadable by renaming a bag
    `FooFrameData`. This is a visibility measure, not proof of ownership; the
    AGENTS.md review question remains authoritative for deliberately renamed
    bags.
  - The migration verdict `pre-existing-unreviewed` is retired and rejected.
  - Recorded site/member counts are source-drift checks, never an allowance or
    budget.
  - The source scan is read-only; an explicit `--output` path may receive the
    deterministic JSON or Markdown report.

Related:
  - tools/aggregate_ownership_rulings.json
  - tools/inventory_extraction_scars.py
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
RETIRED_TRANSITIONAL_VERDICT = "pre-existing-unreviewed"
ACCEPTED_VERDICTS = ("remove", "retain", "retain-prior", "repair")

# Suffix families the repository has historically used for per-operation data.
# Discovery remains suffix-free; strict gating is deliberately bounded to these
# families when the type states no invariant of its own.
LEGACY_SUFFIXES = (
    "Context",
    "Inputs",
    "Input",
    "Params",
    "Args",
    "Operands",
    "Services",
    "Bindings",
    "Bag",
    "Request",
    "Facts",
)

BALANCED_ONE_LEVEL_PARENS = r"\((?:[^()]|\([^()]*\))*\)"
TYPE_DECORATOR = (
    rf"(?:\[\[[^\]]+\]\]|alignas\s*{BALANCED_ONE_LEVEL_PARENS}|"
    rf"__declspec\s*{BALANCED_ONE_LEVEL_PARENS}|[A-Z][A-Z0-9_]*(?:_API|_EXPORT))"
)
TYPE_PATTERN = r"""
    \b(?P<kind>struct|class)\s+
    (?:__TYPE_DECORATOR__\s+)*
    (?:(?:[A-Za-z_]\w*)::)*
    (?P<name>[A-Za-z_]\w*)\s*
    (?:<[^;{}]*>)?\s*
    (?:final\s*)?
    (?::(?P<bases>[^{;]*))?
    \{
    """
TYPE_RE = re.compile(TYPE_PATTERN.replace("__TYPE_DECORATOR__", TYPE_DECORATOR), re.M | re.X)
MEMBER_RE = re.compile(
    r"""
    ^[ \t]*
    (?:\[\[[^\]]+\]\]\s*)*
    (?!(?:public|private|protected|using|typedef|static_assert|friend|template|return|class|struct|union|enum)\b)
    (?P<decl>[A-Za-z_][\w:<>,\s\*&\[\]]*?)
    \s(?P<name>[A-Za-z_]\w*)
    \s*(?:\[[^\]]*\])?
    \s*(?:=[^;]*)?;
    """,
    re.X | re.M,
)
FUNCTION_POINTER_MEMBER_RE = re.compile(
    r"""
    ^[ \t]*
    (?:\[\[[^\]]+\]\]\s*)*
    (?P<decl>[A-Za-z_][\w:<>,\s\*&\[\]]*?)
    \(\s*(?:[A-Za-z_]\w*::)?[*&]\s*(?P<name>[A-Za-z_]\w*)\s*\)
    \s*\([^;]*\)\s*(?:const\s*)?(?:noexcept\s*)?;
    """,
    re.X | re.M,
)
BITFIELD_MEMBER_RE = re.compile(
    r"""
    ^[ \t]*
    (?:\[\[[^\]]+\]\]\s*)*
    (?P<decl>[A-Za-z_][\w:<>,\s\*&\[\]]*?)
    \s(?P<name>[A-Za-z_]\w*)\s*:(?!:)\s*[^;]+;
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
    member_declarations: list[str]
    has_behavior: bool
    has_stated_invariant: bool
    construction_sites: list[str] = field(default_factory=list)
    parameter_sites: list[str] = field(default_factory=list)
    duplicate_definition_sites: list[str] = field(default_factory=list)

    def key(self) -> str:
        return self.name

    def structural_signals(self) -> list[str]:
        """Exact shape facts computed from the type body alone.

        Only facts that need no cross-file resolution appear here. A wrapper
        whose sole field is a borrowed pointer/reference carries another owner
        without narrowing its representation or identity, so it is safe to gate.
        A one-scalar strong value remains review context rather than being
        mistaken for a parameter bag.
        """
        found: list[str] = []
        if (
            not self.has_behavior
            and self.member_count == 1
            and any(token in self.member_declarations[0] for token in ("&", "*"))
        ):
            found.append("single-borrow-member")
        return found

    def signals(self) -> list[str]:
        """Gating signals.

        A stated invariant is review evidence, not a mechanical exemption. The
        borrowed-member rule asks whether the wrapper enforces anything; adding
        an `Invariant:` sentence cannot make an unchanged courier disappear.

        Why "sole consumer destructures at entry" is not gated here: deciding it
        requires distinguishing a construction from a same-named local, which is
        not decidable lexically without a compiler database. Site counts below
        are review context, and the destructuring test stays an AGENTS.md review
        question. Gating on an unreliable count would reproduce the frozen-metric
        failure this inventory exists to replace.
        """
        found = self.structural_signals()
        if found:
            found.append("stated-invariant" if self.has_stated_invariant else "no-stated-invariant")
        return found

    def is_review_candidate(self) -> bool:
        return bool(self.signals()) or self.name.endswith(LEGACY_SUFFIXES)

    def is_gated_candidate(self) -> bool:
        """Bounded strict-mode surface; discovery and review remain wider.

        The suffix family is intentionally centralized here. Expanding this to
        every discovered data record would turn the ruling file into a second
        copy of the type system and bury the ownership signal.
        """
        return self.name.endswith(LEGACY_SUFFIXES) and not self.has_stated_invariant


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


def _is_enum_class(masked: str, class_offset: int) -> bool:
    prefix = masked[max(0, class_offset - 32) : class_offset]
    return bool(re.search(r"\benum\s*$", prefix))


def _doc_comment_above(text: str, offset: int) -> str:
    """Contiguous comment block immediately preceding the declaration."""
    start = text.rfind("\n\n", 0, offset)
    return text[start if start >= 0 else 0 : offset]


def _top_level_type_body(body: str) -> str:
    # Why: inline methods and nested types contain declarations that are not
    # fields of the outer type. Blank nested brace bodies while preserving line
    # shape; a field's default initializer remains countable from its head.
    chars = list(body)
    depth = 0
    for index, char in enumerate(body):
        if char == "{":
            depth += 1
            chars[index] = " "
        elif char == "}":
            chars[index] = " "
            depth = max(0, depth - 1)
        elif depth > 0 and char != "\n":
            chars[index] = " "
    return "".join(chars)


def _count_members(body: str) -> tuple[int, list[str], list[str]]:
    top_level = _top_level_type_body(body)

    names: list[str] = []
    declarations: list[str] = []
    matches = sorted(
        [
            *MEMBER_RE.finditer(top_level),
            *FUNCTION_POINTER_MEMBER_RE.finditer(top_level),
            *BITFIELD_MEMBER_RE.finditer(top_level),
        ],
        key=lambda match: match.start(),
    )
    for match in matches:
        decl = match.group("decl")
        # A method declaration has a parameter list; a member does not.
        if "(" in decl or ")" in decl:
            continue
        names.append(match.group("name"))
        declarations.append(decl.strip())

    # Anonymous unions carry data even though the outer-body brace masker hides
    # their fields. Count their alternatives so a `*Services` owner union cannot
    # disappear from the bounded gate.
    for union_match in re.finditer(r"\bunion\s*\{", body):
        open_index = body.find("{", union_match.start())
        union_body = _matched_brace_body(body, open_index)
        if union_body is None or not re.match(r"\s*;", body[union_body[1] :]):
            continue
        _, union_names, union_declarations = _count_members(union_body[0])
        names.extend(union_names)
        declarations.extend(f"anonymous union {decl}" for decl in union_declarations)
    return (len(names), names, declarations)


def _has_behavior(body: str) -> bool:
    top_level = _top_level_type_body(body)
    return bool(re.search(r"^[^;\n{}]*\([^;{}\n]*\)", top_level, re.M))


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
        for match in TYPE_RE.finditer(masked):
            if _is_enum_class(masked, match.start()):
                continue
            name = match.group("name")
            open_index = masked.find("{", match.start())
            body = _matched_brace_body(masked, open_index)
            if body is None:
                continue
            member_count, member_names, member_declarations = _count_members(body[0])
            if member_count == 0 and not name.endswith(LEGACY_SUFFIXES):
                continue
            line = line_of_offset(masked, match.start("name"))
            definition_site = f"{relative}:{line}"
            existing = aggregates.get(name)
            if existing is not None:
                existing.duplicate_definition_sites.append(definition_site)
                continue
            # A forward declaration has no body content worth reading; skip it in
            # favour of the definition, wherever that lives.
            aggregates[name] = Aggregate(
                name=name,
                path=relative,
                line=line,
                member_count=member_count,
                member_names=member_names,
                member_declarations=member_declarations,
                has_behavior=_has_behavior(body[0]),
                has_stated_invariant=bool(INVARIANT_RE.search(_doc_comment_above(text, match.start()))),
            )

    # Second pass: construction and parameter sites for the collected names.
    # Why: one combined alternation per file is the difference between a
    # sub-second scan and a per-identifier regex storm across ~180K lines.
    usage_names = {name for name, item in aggregates.items() if item.is_review_candidate()}
    if not usage_names:
        return aggregates
    usage_re = re.compile(
        r"\b(?:" + "|".join(sorted(map(re.escape, usage_names), key=len, reverse=True)) + r")\b"
    )
    # Covers `Type { ... }` temporaries and unambiguous named declarations such
    # as `Type value { ... }`, `Type value = ...`, and `Type value;`. Deliberately
    # excludes `Type Function(...)`, which is indistinguishable from a
    # return-type declaration without compiler resolution.
    construction_re = re.compile(
        r"(?:\s*[{(]|\s+[A-Za-z_]\w*\s*(?:[;={]))"
    )
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


def report(
    aggregates: dict[str, Aggregate],
    rulings: dict[str, dict],
    *,
    strict: bool,
    verbose: bool = True,
) -> int:
    flagged = {name: item for name, item in aggregates.items() if item.signals()}
    review = {name: item for name, item in aggregates.items() if item.is_review_candidate()}
    gated = {name: item for name, item in aggregates.items() if item.is_gated_candidate()}
    ambiguous = sorted(
        name
        for name, item in aggregates.items()
        if name.endswith(LEGACY_SUFFIXES) and item.duplicate_definition_sites
    )
    unruled = sorted(name for name in gated if name not in rulings)
    stale = sorted(name for name in rulings if name not in gated)
    invalid: list[str] = []
    transitional_count = 0
    for name, item in gated.items():
        ruling = rulings.get(name)
        if ruling is None:
            continue
        expected_site = f"{item.path}:{item.line}"
        if ruling.get("site") != expected_site:
            invalid.append(f"{name}: site is {ruling.get('site')!r}, expected {expected_site!r}")
        if ruling.get("members") != item.member_count:
            invalid.append(
                f"{name}: members is {ruling.get('members')!r}, expected {item.member_count}"
            )
        if ruling.get("verdict") not in ACCEPTED_VERDICTS:
            invalid.append(f"{name}: invalid verdict {ruling.get('verdict')!r}")
        if ruling.get("verdict") == RETIRED_TRANSITIONAL_VERDICT:
            transitional_count += 1
        if not isinstance(ruling.get("owner"), str) or not ruling["owner"].strip():
            invalid.append(f"{name}: owner is missing")
        if not isinstance(ruling.get("reason"), str) or not ruling["reason"].strip():
            invalid.append(f"{name}: reason is missing")
        if ruling.get("destructures_at_entry") not in ("yes", "no", "mixed"):
            invalid.append(f"{name}: destructures_at_entry must be yes, no, or mixed")
        if ruling.get("verdict") == "remove" and (
            not isinstance(ruling.get("post_removal_signature"), str)
            or not ruling["post_removal_signature"].strip()
        ):
            invalid.append(f"{name}: remove ruling lacks post_removal_signature")

    if verbose:
        stated = sum(1 for item in aggregates.values() if item.has_stated_invariant)
        print(
            f"Authority-free aggregate inventory: candidates={len(aggregates)} "
            f"state_own_invariant={stated} signalled={len(flagged)} "
            f"review={len(review)} gated={len(gated)} "
            f"ruled={len(gated) - len(unruled)} unruled={len(unruled)} "
            f"ambiguous_gated_names={len(ambiguous)} "
            f"pre_existing_unreviewed={transitional_count}"
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
        widest_review = sorted(
            (
                item
                for item in aggregates.values()
                if item.name.endswith(LEGACY_SUFFIXES)
                and not item.has_stated_invariant
                and item.member_count >= 4
            ),
            key=lambda entry: (-entry.member_count, entry.name),
        )
        if widest_review:
            print(
                f"Review context ({len(widest_review)} multi-member candidates "
                "without a stated invariant):"
            )
            for item in widest_review[:20]:
                print(
                    f"  - {item.path}:{item.line} {item.name} members={item.member_count} "
                    f"lexical_constructions={len(set(item.construction_sites))} "
                    f"lexical_parameter_uses={len(set(item.parameter_sites))}"
                )
            if len(widest_review) > 20:
                print(
                    f"  ... {len(widest_review) - 20} more; "
                    "use --format json or markdown for the complete list"
                )

    if (strict and (unruled or ambiguous)) or stale or invalid:
        if verbose:
            if strict and unruled:
                print(
                    f"FAIL: {len(unruled)} gated aggregate(s) require an owner "
                    f"ruling in {RULINGS_RELATIVE}: {', '.join(unruled)}"
                )
            if strict and ambiguous:
                for name in ambiguous:
                    item = aggregates[name]
                    sites = [f"{item.path}:{item.line}", *item.duplicate_definition_sites]
                    print(
                        f"FAIL: gated aggregate name {name} has multiple definitions; "
                        f"rulings require an unambiguous type identity: {', '.join(sites)}"
                    )
            if stale:
                print(f"FAIL: {len(stale)} stale aggregate ruling(s): {', '.join(stale)}")
            for error in invalid:
                print(f"FAIL: {error}")
        return 1
    if verbose:
        if strict:
            print("PASS: every bounded gated aggregate carries an owner ruling.")
        else:
            print("PASS: aggregate inventory report completed (strict ruling gate disabled).")
    return 0


def markdown_table(aggregates: dict[str, Aggregate], rulings: dict[str, dict]) -> str:
    review = sorted(
        (item for item in aggregates.values() if item.is_review_candidate()),
        key=lambda entry: entry.name,
    )
    lines = [
        "# Ceremonial Aggregate Elimination CA0 Machine Census",
        "",
        "Generated from the current source tree by",
        "`python tools/inventory_authority_free_aggregates.py --repo . --format markdown`.",
        "Construction and consumer columns are lexical source sites; the owner",
        "judgement columns come from `tools/aggregate_ownership_rulings.json`.",
        "",
        f"Rows: {len(review)}. Unruled: {sum(1 for item in review if item.name not in rulings)}.",
        "",
        "| Type | Definition | Members | Construction sites | Consumer sites | "
        "Destructures at entry | Verdict | Owner / invariant or endpoint |",
        "|---|---|---:|---|---|---|---|---|",
    ]
    for item in review:
        ruling = rulings.get(item.name, {})
        constructions = "<br>".join(f"`{site}`" for site in sorted(set(item.construction_sites)))
        consumers = "<br>".join(f"`{site}`" for site in sorted(set(item.parameter_sites)))
        member_text = ", ".join(
            f"`{declaration} {name}`"
            for declaration, name in zip(item.member_declarations, item.member_names)
        )
        verdict = ruling.get("verdict", "**UNRULED**")
        owner = ruling.get("owner", "")
        reason = ruling.get("reason", "")
        endpoint = ruling.get("post_removal_signature", "")
        details = f"`{owner}` — {reason}" if owner or reason else ""
        if endpoint:
            details += f"<br>Endpoint: `{endpoint}`"
        lines.append(
            f"| `{item.name}` | `{item.path}:{item.line}` | {item.member_count}: "
            f"{member_text} | {constructions or '—'} | {consumers or '—'} | "
            f"{ruling.get('destructures_at_entry', 'unruled')} | `{verdict}` | {details} |"
        )
    lines.append("")
    return "\n".join(lines)


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
//   an out-of-order call is fatal-invariant fatal.
struct SceneLoadTransactionInput
{
    int phaseCursor;
    int requestCount;
    bool arbitrationResolved;
};
"""

FIXTURE_RENAMED_SINGLE_MEMBER = """
struct FooPayload
{
    SceneWorld& world;
};
"""

FIXTURE_CLASS_SINGLE_MEMBER = """
class FooContext
{
  public:
    SceneWorld& world;
};
"""

FIXTURE_CLAIM_ONLY_SINGLE_MEMBER = """
// Invariant: this sentence must not exempt an unchanged courier.
struct ClaimedContext
{
    SceneWorld& world;
};
"""

FIXTURE_ONE_FIELD_BEHAVIOR_OWNER = """
class WakeAccess
{
  public:
    explicit WakeAccess( Store& store ) : m_store( store ) {}
    void Forget( int body );
  private:
    Store& m_store;
};
"""

FIXTURE_STRONG_SCALAR = """
struct BodyId
{
    int value;
};
"""

FIXTURE_UNRELATED_STRUCT = """
struct PhysicsBodyRecord
{
    int handle;
    float mass;
};
"""

FIXTURE_LEGACY_REQUEST = """
struct WidgetRequest
{
    int row;
    int mode;
};
"""

FIXTURE_DECORATED_CLASS_HEADS = """
class FinalContext final
{
    int first;
    int second;
};

struct alignas( alignof( int ) ) AlignedInput
{
    int first;
    int second;
};

struct [[nodiscard]] AttributedRequest
{
    int first;
    int second;
};

struct Outer::QualifiedContext
{
    int first;
    int second;
};

struct SKORE_API ExportedContext
{
    int first;
    int second;
};

class __declspec(dllexport) MsvcInput
{
    int first;
    int second;
};

template <typename T>
struct PartialRequest<T*>
{
    int first;
    int second;
};

struct CallbackServices
{
    void (*run)(int);
};

struct AttributedFacts
{
    [[maybe_unused]] int value;
};

struct PackedContext
{
    unsigned first : 1;
    unsigned second : 1;
};

struct OwnerServices
{
    union
    {
        World* world;
        Scene* scene;
    };
};

struct ServiceBase
{
    int owner;
};

struct InheritedServices : ServiceBase
{
};

struct NestedContext
{
    struct Storage
    {
        World* world;
        Scene* scene;
    } storage;
};

struct MacroContext
{
    SKORE_FIELD(World*, world)
};

enum class IgnoredRequest
{
    First,
    Second,
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
    for match in TYPE_RE.finditer(masked):
        if _is_enum_class(masked, match.start()):
            continue
        name = match.group("name")
        body = _matched_brace_body(masked, masked.find("{", match.start()))
        if body is None:
            continue
        count, names, declarations = _count_members(body[0])
        if count == 0 and not name.endswith(LEGACY_SUFFIXES):
            continue
        line = line_of_offset(masked, match.start("name"))
        existing = found.get(name)
        if existing is not None:
            existing.duplicate_definition_sites.append(f"fixture.h:{line}")
            continue
        found[name] = Aggregate(
            name=name,
            path="fixture.h",
            line=line,
            member_count=count,
            member_names=names,
            member_declarations=declarations,
            has_behavior=_has_behavior(body[0]),
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
    elif "single-borrow-member" not in single["TornadoUICommandContext"].signals():
        failures.append("single borrowed-member signal was not raised")

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
        owner[key].construction_sites.append("fixture.cpp:10")
        if owner[key].signals():
            failures.append("a multi-member invariant owner must not gain a shape signal")
        if report(owner, {}, strict=True, verbose=False) != 0:
            failures.append("a suffix-family aggregate with its own invariant must stay outside the gate")

    renamed = _parse(FIXTURE_RENAMED_SINGLE_MEMBER)
    if "FooPayload" not in renamed or "single-borrow-member" not in renamed["FooPayload"].signals():
        failures.append("renaming a single-member aggregate outside legacy suffixes must not hide it")
    elif report(renamed, {}, strict=True, verbose=False) != 0:
        failures.append("a non-suffix signal must remain review context rather than widen the gate")

    class_shape = _parse(FIXTURE_CLASS_SINGLE_MEMBER)
    if "FooContext" not in class_shape or "single-borrow-member" not in class_shape["FooContext"].signals():
        failures.append("changing a single-member struct to class must not hide it")

    claim_only = _parse(FIXTURE_CLAIM_ONLY_SINGLE_MEMBER)
    if "ClaimedContext" not in claim_only or "single-borrow-member" not in claim_only["ClaimedContext"].signals():
        failures.append("an Invariant comment alone must not exempt a single-member courier")

    behavior_owner = _parse(FIXTURE_ONE_FIELD_BEHAVIOR_OWNER)
    if "WakeAccess" not in behavior_owner:
        failures.append("a one-field behavior owner must remain visible as review context")
    elif behavior_owner["WakeAccess"].signals():
        failures.append("a one-field behavior owner must not be classified as a courier")

    strong_scalar = _parse(FIXTURE_STRONG_SCALAR)
    if "BodyId" not in strong_scalar:
        failures.append("a strong scalar wrapper must remain visible as review context")
    elif strong_scalar["BodyId"].signals():
        failures.append("a strong scalar wrapper must not be classified as a borrowed courier")

    unrelated = _parse(FIXTURE_UNRELATED_STRUCT)
    if "PhysicsBodyRecord" not in unrelated:
        failures.append("a data-bearing struct outside legacy suffixes must still be collected")
    elif unrelated["PhysicsBodyRecord"].signals():
        failures.append("a multi-member domain record must not gain a shape signal")
    if _parse(FIXTURE_COMMENTED_STRUCT):
        failures.append("a commented-out struct must not be collected")

    legacy_request = _parse(FIXTURE_LEGACY_REQUEST)
    if "WidgetRequest" not in legacy_request or not legacy_request["WidgetRequest"].is_review_candidate():
        failures.append("a legacy Request suffix must remain in the complete review census")

    gate = _parse(FIXTURE_SINGLE_MEMBER)
    gate_item = gate["TornadoUICommandContext"]
    valid_ruling = {
        "site": "fixture.h:2",
        "members": gate_item.member_count,
        "verdict": "remove",
        "owner": "fixture owner",
        "reason": "fixture reason",
        "destructures_at_entry": "yes",
        "post_removal_signature": "Take(SceneWorld&) — 1 parameter",
    }
    if report(gate, {}, strict=True, verbose=False) == 0:
        failures.append("an unruled signalled aggregate must fail the gate")
    if report(
        gate,
        {"TornadoUICommandContext": valid_ruling},
        strict=True,
        verbose=False,
    ) != 0:
        failures.append("a ruled aggregate must pass the gate")
    legacy_item = legacy_request["WidgetRequest"]
    valid_legacy_ruling = {
        "site": "fixture.h:2",
        "members": legacy_item.member_count,
        "verdict": "retain",
        "owner": "fixture owner",
        "reason": "fixture invariant",
        "destructures_at_entry": "no",
    }
    if report(legacy_request, {}, strict=True, verbose=False) == 0:
        failures.append("an unruled legacy-suffix aggregate must fail the gate")
    if report(
        legacy_request,
        {"WidgetRequest": valid_legacy_ruling},
        strict=True,
        verbose=False,
    ) != 0:
        failures.append("a ruled legacy-suffix aggregate must pass the gate")
    duplicate_legacy = _parse(FIXTURE_LEGACY_REQUEST + "\n" + FIXTURE_LEGACY_REQUEST)
    if report(
        duplicate_legacy,
        {"WidgetRequest": valid_legacy_ruling},
        strict=True,
        verbose=False,
    ) == 0:
        failures.append("a duplicate gated type name must not reuse one ruling")
    decorated = _parse(FIXTURE_DECORATED_CLASS_HEADS)
    expected_decorated = {
        "FinalContext",
        "AlignedInput",
        "AttributedRequest",
        "QualifiedContext",
        "ExportedContext",
        "MsvcInput",
        "PartialRequest",
        "CallbackServices",
        "AttributedFacts",
        "PackedContext",
        "OwnerServices",
        "ServiceBase",
        "InheritedServices",
        "NestedContext",
        "MacroContext",
        "Storage",
    }
    if set(decorated) != expected_decorated:
        failures.append(
            "decorated, qualified, specialized, and function-pointer-bearing class heads "
            f"must be discovered (got {sorted(decorated)})"
        )
    elif report(decorated, {}, strict=True, verbose=False) == 0:
        failures.append("decorated bounded class heads must not evade strict rulings")
    prior_ruling = dict(valid_legacy_ruling, verdict="retain-prior")
    if report(
        legacy_request,
        {"WidgetRequest": prior_ruling},
        strict=True,
        verbose=False,
    ) != 0:
        failures.append("a retain-prior legacy-suffix ruling must pass the gate")
    transitional_ruling = dict(
        valid_legacy_ruling,
        verdict=RETIRED_TRANSITIONAL_VERDICT,
        owner="ceremonial-aggregate-elimination CA0",
        reason="fixture transition",
    )
    if report(
        legacy_request,
        {"WidgetRequest": transitional_ruling},
        strict=True,
        verbose=False,
    ) == 0:
        failures.append("the retired transitional verdict must be unusable")
    wrong_site_ruling = dict(transitional_ruling, site="fixture.h:999")
    if report(
        legacy_request,
        {"WidgetRequest": wrong_site_ruling},
        strict=True,
        verbose=False,
    ) == 0:
        failures.append("a retired transitional ruling whose declaration site moved must fail")
    if report(legacy_request, {}, strict=False, verbose=False) != 0:
        failures.append("report-only mode must not enforce missing bounded rulings")

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
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail when a bounded gated aggregate has no owner ruling",
    )
    parser.add_argument("--format", choices=("text", "json", "markdown"), default="text")
    parser.add_argument("--output", type=Path, help="Optional output path for JSON or Markdown")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    repo = args.repo.resolve()
    aggregates = collect_aggregates(repo, args.roots)
    rulings = load_rulings(repo)
    if args.format == "json":
        payload = [
            {
                "name": item.name,
                "path": item.path,
                "line": item.line,
                "members": item.member_count,
                "member_names": item.member_names,
                "member_declarations": item.member_declarations,
                "has_behavior": item.has_behavior,
                "stated_invariant": item.has_stated_invariant,
                "construction_sites": sorted(set(item.construction_sites)),
                "parameter_sites": sorted(set(item.parameter_sites)),
                "duplicate_definition_sites": item.duplicate_definition_sites,
                "signals": item.signals(),
                "review_candidate": item.is_review_candidate(),
                "ruling": rulings.get(item.name),
            }
            for item in sorted(aggregates.values(), key=lambda entry: entry.name)
        ]
        rendered = json.dumps(payload, indent=2)
        if args.output:
            args.output.write_text(rendered + "\n", encoding="utf-8")
        else:
            print(rendered)
        return 0
    if args.format == "markdown":
        rendered = markdown_table(aggregates, rulings)
        if args.output:
            args.output.write_text(rendered, encoding="utf-8")
        else:
            print(rendered, end="")
        return 0
    return report(aggregates, rulings, strict=args.strict)


if __name__ == "__main__":
    sys.exit(main())
