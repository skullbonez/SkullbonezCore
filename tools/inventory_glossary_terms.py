#!/usr/bin/env python3
"""
File: inventory_glossary_terms.py
Purpose:
  Inventory definitions in tracked source learning-header Glossary blocks and
  identify terms defined by more than one source file.

Summary:
  Parses the repository's comment-only glossary vocabulary, reports every
  multi-file term and its wording variants, and joins each finding to an exact
  current-source migration ruling. Strict mode turns missing, changed, stale,
  or malformed rulings into validation failures without imposing a count limit.

Mental model:
  A glossary term is local only while one tracked source file defines it. Once
  two files define the same term, the shared glossary owns the definition and
  the source copies form one review finding until the consolidation plan removes
  them.

Glossary:
  Definition site: One term/wording entry in a source learning-header Glossary
    block.
  Multi-file term: Term with definition sites in two or more tracked source
    files.
  Wording drift: A multi-file term whose normalized definitions are not all
    identical.
  Current-source ruling: Owner judgement keyed by term and a digest of every
    current definition site's file, line, and normalized wording.

Invariants:
  - The inventory scans tracked source-bearing files, never a directory walk.
  - Only comment text inside a Glossary block is parsed as a definition.
  - A finding's digest changes when a site, line, or wording changes.
  - Strict mode fails on unruled, changed, stale, or malformed evidence.
  - A repair-plan ruling names an existing canonical plan under
    Agentic/Plans/TODO.
  - Counts describe current structure and never become thresholds or budgets.

Related:
  - Agentic/Reference/engine-glossary.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reports/2026-07-31/engine-glossary-consolidation-gc0-inventory.md
  - Agentic/Reports/2026-07-31/engine-glossary-consolidation-gc1-standard.md
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from collections import defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable


DEFAULT_RULINGS_PATH = Path("tools/glossary_term_rulings.json")
SOURCE_ROOT = "SkullbonezSource"
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl", ".hlsl"}
RULING_DISPOSITIONS = {"repair-plan"}
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
FIELD_RE = re.compile(
    r"^(?:File|Purpose|Summary|Mental model|Glossary|Invariants|Related):\s*$",
    re.IGNORECASE,
)
ENTRY_RE = re.compile(r"^(?:-\s+)?([^:]+?):(?:\s+(.*)|\s*)$")


@dataclass(frozen=True)
class DefinitionSite:
    file: str
    line: int
    term: str
    definition: str


@dataclass(frozen=True)
class GlossaryFinding:
    term: str
    files: tuple[str, ...]
    sites: tuple[DefinitionSite, ...]
    definitions: tuple[str, ...]
    fingerprint: str

    @property
    def drifted(self) -> bool:
        return len(self.definitions) > 1


@dataclass(frozen=True)
class OwnerRuling:
    term: str
    fingerprint: str
    owner: str
    disposition: str
    reason: str
    evidence: str
    plan: str


@dataclass(frozen=True)
class RulingIssue:
    term: str
    message: str


def _tracked_source_files(repo: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "-C", str(repo), "ls-files", SOURCE_ROOT],
        check=True,
        capture_output=True,
        text=True,
    )
    paths: list[Path] = []
    for raw_path in result.stdout.splitlines():
        relative = Path(raw_path.strip())
        if relative.suffix.lower() in SOURCE_SUFFIXES:
            paths.append(relative)
    return sorted(paths, key=lambda path: path.as_posix().casefold())


def _comment_payloads(text: str) -> Iterable[tuple[int, str | None]]:
    """Yield normalized whole-line comment payloads and source-code markers."""
    in_block = False
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        stripped = raw_line.strip()
        if in_block:
            closes = "*/" in stripped
            payload = stripped.split("*/", 1)[0].rstrip() if closes else stripped
            if payload.startswith("*"):
                payload = payload[1:].lstrip()
            yield line_number, payload
            if closes:
                in_block = False
            continue

        if not stripped:
            yield line_number, ""
        elif stripped.startswith("/*"):
            payload = stripped[2:].lstrip()
            closes = "*/" in payload
            if closes:
                payload = payload.split("*/", 1)[0].rstrip()
            else:
                in_block = True
            yield line_number, payload
        elif stripped.startswith("//"):
            yield line_number, stripped[2:].lstrip()
        else:
            yield line_number, None


def _normalize_words(parts: Iterable[str]) -> str:
    return " ".join(" ".join(parts).split())


def scan_text(file: str, text: str) -> list[DefinitionSite]:
    sites: list[DefinitionSite] = []
    in_glossary = False
    current_term = ""
    current_line = 0
    current_definition: list[str] = []

    def flush() -> None:
        nonlocal current_term, current_line, current_definition
        if current_term:
            sites.append(
                DefinitionSite(
                    file=file,
                    line=current_line,
                    term=current_term,
                    definition=_normalize_words(current_definition),
                )
            )
        current_term = ""
        current_line = 0
        current_definition = []

    for line_number, payload in _comment_payloads(text):
        if payload is None:
            if in_glossary:
                flush()
            break

        if not in_glossary:
            if payload.casefold() == "glossary:":
                in_glossary = True
            continue

        if FIELD_RE.fullmatch(payload):
            flush()
            break

        entry = ENTRY_RE.fullmatch(payload)
        if entry:
            flush()
            current_term = _normalize_words([entry.group(1)])
            current_line = line_number
            if entry.group(2):
                current_definition.append(entry.group(2))
            continue

        if current_term and payload:
            current_definition.append(payload)

    if in_glossary:
        flush()
    return sites


def scan_paths(repo: Path, paths: Iterable[Path]) -> list[DefinitionSite]:
    sites: list[DefinitionSite] = []
    for relative in paths:
        text = (repo / relative).read_text(encoding="utf-8")
        sites.extend(scan_text(relative.as_posix(), text))
    return sites


def _finding_fingerprint(term: str, sites: Iterable[DefinitionSite]) -> str:
    # Why: include every occurrence rather than only distinct wording so moving,
    # adding, or deleting a copied definition invalidates the owner judgement.
    rows = [
        {
            "file": site.file,
            "line": site.line,
            "definition": site.definition,
        }
        for site in sorted(
            sites,
            key=lambda item: (
                item.file.casefold(),
                item.line,
                item.definition.casefold(),
            ),
        )
    ]
    payload = json.dumps(
        {"term": term, "sites": rows},
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    )
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def build_findings(sites: Iterable[DefinitionSite]) -> list[GlossaryFinding]:
    grouped: dict[str, list[DefinitionSite]] = defaultdict(list)
    for site in sites:
        grouped[site.term].append(site)

    findings: list[GlossaryFinding] = []
    for term, term_sites in grouped.items():
        files = tuple(
            sorted({site.file for site in term_sites}, key=str.casefold)
        )
        if len(files) < 2:
            continue
        ordered_sites = tuple(
            sorted(
                term_sites,
                key=lambda item: (
                    item.file.casefold(),
                    item.line,
                    item.definition.casefold(),
                ),
            )
        )
        definitions = tuple(
            sorted(
                {site.definition for site in ordered_sites},
                key=str.casefold,
            )
        )
        findings.append(
            GlossaryFinding(
                term=term,
                files=files,
                sites=ordered_sites,
                definitions=definitions,
                fingerprint=_finding_fingerprint(term, ordered_sites),
            )
        )
    return sorted(findings, key=lambda finding: finding.term.casefold())


def _validate_ruling(
    repo: Path,
    raw: object,
    index: int,
) -> tuple[OwnerRuling | None, RulingIssue | None]:
    if not isinstance(raw, dict):
        return None, RulingIssue(f"ruling[{index}]", "must be an object")
    required = (
        "term",
        "fingerprint",
        "owner",
        "disposition",
        "reason",
        "evidence",
        "plan",
    )
    missing = [field for field in required if not isinstance(raw.get(field), str)]
    if missing:
        return None, RulingIssue(
            str(raw.get("term", f"ruling[{index}]")),
            f"missing string fields: {', '.join(missing)}",
        )
    ruling = OwnerRuling(**{field: raw[field].strip() for field in required})
    if not ruling.term:
        return None, RulingIssue(f"ruling[{index}]", "term must not be blank")
    if not SHA256_RE.fullmatch(ruling.fingerprint):
        return None, RulingIssue(ruling.term, "fingerprint must be SHA-256")
    if not ruling.owner or not ruling.reason or not ruling.evidence:
        return None, RulingIssue(
            ruling.term,
            "owner, reason, and evidence must not be blank",
        )
    if ruling.disposition not in RULING_DISPOSITIONS:
        return None, RulingIssue(
            ruling.term,
            f"unsupported disposition {ruling.disposition!r}",
        )
    plan_relative = PurePosixPath(ruling.plan)
    plan_parts = plan_relative.parts
    canonical_plan = (
        bool(ruling.plan)
        and "\\" not in ruling.plan
        and not plan_relative.is_absolute()
        and ".." not in plan_parts
        and len(plan_parts) == 4
        and plan_parts[:3] == ("Agentic", "Plans", "TODO")
        and plan_relative.suffix.casefold() == ".md"
        and plan_relative.as_posix() == ruling.plan
    )
    plan_path = repo.joinpath(*plan_parts)
    if (
        not canonical_plan
        or not plan_path.is_file()
    ):
        return None, RulingIssue(
            ruling.term,
            "repair-plan ruling must name a canonical existing "
            "Agentic/Plans/TODO/*.md plan",
        )
    return ruling, None


def load_rulings(
    repo: Path,
    path: Path,
) -> tuple[dict[str, OwnerRuling], list[RulingIssue]]:
    resolved = path if path.is_absolute() else repo / path
    if not resolved.exists():
        return {}, [RulingIssue("*", f"ruling file not found: {resolved}")]
    try:
        document = json.loads(resolved.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return {}, [RulingIssue("*", f"cannot read ruling file: {error}")]
    if not isinstance(document, dict):
        return {}, [RulingIssue("*", "ruling document must be an object")]
    if document.get("schema_version") != 1:
        return {}, [RulingIssue("*", "ruling schema_version must be 1")]
    if not isinstance(document.get("rulings"), list):
        return {}, [RulingIssue("*", "ruling document must contain a rulings list")]

    rulings: dict[str, OwnerRuling] = {}
    issues: list[RulingIssue] = []
    for index, raw in enumerate(document["rulings"]):
        ruling, issue = _validate_ruling(repo, raw, index)
        if issue:
            issues.append(issue)
            continue
        assert ruling is not None
        key = ruling.term
        if key in rulings:
            issues.append(RulingIssue(ruling.term, "duplicate ruling term"))
            continue
        rulings[key] = ruling
    return rulings, issues


def evaluate(
    findings: Iterable[GlossaryFinding],
    rulings: dict[str, OwnerRuling],
    ruling_issues: Iterable[RulingIssue],
) -> tuple[list[GlossaryFinding], list[RulingIssue]]:
    current = {finding.term: finding for finding in findings}
    unruled: list[GlossaryFinding] = []
    issues = list(ruling_issues)

    for key, finding in current.items():
        ruling = rulings.get(key)
        if ruling is None:
            unruled.append(finding)
        elif ruling.fingerprint != finding.fingerprint:
            issues.append(
                RulingIssue(
                    finding.term,
                    "current definition sites or wording changed",
                )
            )
    for key, ruling in rulings.items():
        if key not in current:
            issues.append(
                RulingIssue(
                    ruling.term,
                    "stale ruling has no current multi-file finding",
                )
            )
    return unruled, issues


def gate_status(
    strict: bool,
    unruled: Iterable[GlossaryFinding],
    issues: Iterable[RulingIssue],
) -> int:
    if strict and (list(unruled) or list(issues)):
        return 1
    return 0


def _result_payload(
    files_scanned: int,
    sites: list[DefinitionSite],
    findings: list[GlossaryFinding],
    rulings: dict[str, OwnerRuling],
    unruled: list[GlossaryFinding],
    issues: list[RulingIssue],
) -> dict[str, object]:
    term_count = len({site.term for site in sites})
    drifted = sum(finding.drifted for finding in findings)
    exact_ruled = sum(
        rulings.get(finding.term) is not None
        and rulings[finding.term].fingerprint == finding.fingerprint
        for finding in findings
    )
    return {
        "summary": {
            "files_scanned": files_scanned,
            "definition_sites": len(sites),
            "unique_terms": term_count,
            "multi_file_terms": len(findings),
            "drifted_terms": drifted,
            "exact_current_rulings": exact_ruled,
            "unruled_terms": len(unruled),
            "ruling_issues": len(issues),
        },
        "findings": [
            {
                **{
                    key: value
                    for key, value in asdict(finding).items()
                    if key != "sites"
                },
                "drifted": finding.drifted,
                "sites": [asdict(site) for site in finding.sites],
                "ruling": (
                    asdict(rulings[finding.term])
                    if finding.term in rulings
                    else None
                ),
            }
            for finding in findings
        ],
        "unruled_terms": [finding.term for finding in unruled],
        "ruling_issues": [asdict(issue) for issue in issues],
    }


def report(payload: dict[str, object], output_format: str) -> None:
    if output_format == "json":
        print(json.dumps(payload, indent=2, ensure_ascii=False))
        return

    summary = payload["summary"]
    assert isinstance(summary, dict)
    print(
        "Glossary-term inventory: "
        f"files={summary['files_scanned']} "
        f"definitions={summary['definition_sites']} "
        f"unique_terms={summary['unique_terms']} "
        f"multi_file={summary['multi_file_terms']} "
        f"drifted={summary['drifted_terms']} "
        f"ruled={summary['exact_current_rulings']} "
        f"unruled={summary['unruled_terms']} "
        f"ruling_issues={summary['ruling_issues']}"
    )
    findings = payload["findings"]
    assert isinstance(findings, list)
    for finding in findings:
        assert isinstance(finding, dict)
        status = "ruled" if finding["ruling"] else "UNRULED"
        drift = "drift" if finding["drifted"] else "same wording"
        print(
            f"{status}: {finding['term']} "
            f"files={len(finding['files'])} "
            f"variants={len(finding['definitions'])} {drift} "
            f"fingerprint={finding['fingerprint']}"
        )
    for issue in payload["ruling_issues"]:
        assert isinstance(issue, dict)
        print(f"RULING ERROR: {issue['term']}: {issue['message']}")


def _fixture_header(entries: str) -> str:
    return f"""/*
File: fixture.cpp
Purpose:
  Exercise glossary parsing.

Summary:
  Keeps planted terms inside a learning header.

Glossary:
{entries}

Invariants:
  - The fixture is comment-only evidence.
*/
int Fixture();
"""


def self_test() -> int:
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as temp:
        repo = Path(temp)
        plan = repo / "Agentic/Plans/TODO/glossary.md"
        plan.parent.mkdir(parents=True)
        plan.write_text("# Fixture plan\n", encoding="utf-8")
        (repo / "README.md").write_text("# Not a plan\n", encoding="utf-8")
        report = repo / "Agentic/Reports/fixture.md"
        report.parent.mkdir(parents=True)
        report.write_text("# Not a plan\n", encoding="utf-8")
        source_a = _fixture_header(
            """  Shared term: First wording.
  Drift term: Wording from A.
  Local term: Defined only here.
  Multiline term: First line
    continues here.
  - Bullet term: Bullet form parses."""
        )
        source_b = _fixture_header(
            """  Shared term: First wording.
  Drift term: Wording from B.
  Multiline term: First line continues here.
  Bullet term: Bullet form parses."""
        )
        source_c = _fixture_header(
            """  Namespace term: Keeps SkullbonezCore::Scene::Capacity text
    as continuation, not extra entries."""
        )
        source_after_code = """int Value();
// Glossary:
//   Not a header term: Must not be inventoried.
"""
        sites = (
            scan_text("a.cpp", source_a)
            + scan_text("b.h", source_b)
            + scan_text("c.cpp", source_c)
            + scan_text("after.cpp", source_after_code)
        )
        findings = build_findings(sites)
        by_term = {finding.term: finding for finding in findings}

        expected = {"Shared term", "Drift term", "Multiline term", "Bullet term"}
        if set(by_term) != expected:
            failures.append(
                f"multi-file classification mismatch: {sorted(by_term)}"
            )
        if by_term.get("Shared term") and by_term["Shared term"].drifted:
            failures.append("identical wording must not be drift")
        if by_term.get("Drift term") and not by_term["Drift term"].drifted:
            failures.append("changed wording must be drift")
        if by_term.get("Multiline term") and by_term["Multiline term"].drifted:
            failures.append("normalized multiline wording must match")
        if any("SkullbonezCore" in site.term for site in sites):
            failures.append("namespace continuation was parsed as a term")
        if any(site.term == "Not a header term" for site in sites):
            failures.append("a glossary comment after source code was parsed")

        unruled, issues = evaluate(findings, {}, [])
        if len(unruled) != len(findings) or issues:
            failures.append("unruled findings were not preserved")
        if gate_status(True, unruled, issues) != 1:
            failures.append("strict mode must fail on an unruled finding")

        raw_rulings = []
        for finding in findings:
            raw_rulings.append(
                {
                    "term": finding.term,
                    "fingerprint": finding.fingerprint,
                    "owner": "Fixture owner",
                    "disposition": "repair-plan",
                    "reason": "The fixture plan removes the copied definition.",
                    "evidence": "fixture",
                    "plan": "Agentic/Plans/TODO/glossary.md",
                }
            )
        ruling_path = repo / DEFAULT_RULINGS_PATH
        ruling_path.parent.mkdir(parents=True)
        ruling_path.write_text(
            json.dumps({"schema_version": 1, "rulings": raw_rulings}),
            encoding="utf-8",
        )
        rulings, load_issues = load_rulings(repo, DEFAULT_RULINGS_PATH)
        unruled, issues = evaluate(findings, rulings, load_issues)
        if unruled or issues:
            failures.append("exact current rulings must pass")
        if gate_status(True, unruled, issues) != 0:
            failures.append("strict mode must pass exact current rulings")

        changed = list(findings)
        first = changed[0]
        changed[0] = GlossaryFinding(
            term=first.term,
            files=first.files,
            sites=first.sites,
            definitions=first.definitions,
            fingerprint="0" * 64,
        )
        _, changed_issues = evaluate(changed, rulings, [])
        if not any("changed" in issue.message for issue in changed_issues):
            failures.append("fingerprint drift must fail")
        if gate_status(True, [], changed_issues) != 1:
            failures.append("strict mode must fail fingerprint drift")

        _, stale_issues = evaluate(findings[1:], rulings, [])
        if not any("stale" in issue.message for issue in stale_issues):
            failures.append("stale rulings must fail")
        if gate_status(True, [], stale_issues) != 1:
            failures.append("strict mode must fail stale rulings")

        _, malformed = _validate_ruling(
            repo,
            {
                **raw_rulings[0],
                "fingerprint": "not-a-digest",
            },
            0,
        )
        if malformed is None:
            failures.append("malformed ruling evidence must fail")

        invalid_plans = (
            "README.md",
            "Agentic/Reports/fixture.md",
            str(plan.resolve()),
            "Agentic/Plans/TODO/../glossary.md",
        )
        for invalid_plan in invalid_plans:
            _, plan_issue = _validate_ruling(
                repo,
                {
                    **raw_rulings[0],
                    "plan": invalid_plan,
                },
                0,
            )
            if plan_issue is None:
                failures.append(
                    f"non-canonical repair plan must fail: {invalid_plan}"
                )

        ruling_path.write_text(
            json.dumps({"schema_version": 2, "rulings": raw_rulings}),
            encoding="utf-8",
        )
        _, version_issues = load_rulings(repo, DEFAULT_RULINGS_PATH)
        if not any("schema_version" in issue.message for issue in version_issues):
            failures.append("unsupported ruling schema version must fail")

    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1
    print("PASS: glossary-term inventory self-test")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Inventory multi-file learning-header glossary definitions."
    )
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--rulings", type=Path, default=DEFAULT_RULINGS_PATH)
    parser.add_argument("--format", choices=("text", "json"), default="text")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    repo = args.repo.resolve()
    paths = _tracked_source_files(repo)
    sites = scan_paths(repo, paths)
    findings = build_findings(sites)
    rulings, ruling_issues = load_rulings(repo, args.rulings)
    unruled, issues = evaluate(findings, rulings, ruling_issues)
    payload = _result_payload(
        len(paths),
        sites,
        findings,
        rulings,
        unruled,
        issues,
    )
    report(payload, args.format)
    return gate_status(args.strict, unruled, issues)


if __name__ == "__main__":
    sys.exit(main())
