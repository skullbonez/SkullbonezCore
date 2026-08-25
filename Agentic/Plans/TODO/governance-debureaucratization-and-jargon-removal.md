# Governance De-Bureaucratization and Jargon Removal

Date: 2026-08-25
Status: Active by owner direction 2026-08-25. 0/8 phases complete.
Owner: Engine owner
Priority: Sole active master-plan item. DB0-DB7 execute in strict order.
Commit name: `DE_BUREAUCRATIZE`
Impact area: `tools/` governance checkers and ruling registries; `AGENTS.md`;
`Agentic/` reference docs, plan templates, and session ledgers; source comments,
identifiers, and test descriptions across the tree. No engine runtime behavior,
no physics output, no determinism change.

Supersedes the two parked drafts this plan builds from:
`Agentic/Plans/WNF/governance-simplification-and-scar-removal.md` and
`Agentic/Plans/WNF/slop-reduction-plan.md`. Delete both in this plan's first
commit.

## Goal

Make governance **leaner and harder to evade at the same time**, remove LLM
jargon from every document and every source file, and add a permanent validation
ban so that wording cannot return — without weakening one core invariant.

## Core Principle

Delete the bookkeeping, keep the invariant. Never retire an enforcement without
a stronger replacement in the same commit, proven by a negative control that
fails on a real violation. The target is brittleness — line-number-pinned JSON,
regex parsers you can slip past, ceremony — not enforcement. When in doubt,
governance ends up stricter, not looser.

Distinguish two things the current setup conflates:

- **Rule data** (the allowed dependency edges, coverage floors, the allocation
  allowlist): legitimate policy config. Keep it; only de-brittle its format.
- **Per-site exemption ledgers** (line-pinned "this 500-line function is fine"
  rulings): the bookkeeping to kill. Replace the fragile ledger with an
  unevadable check, or fold the rule into review plus one negative-control test.

## Invariants That Must Not Weaken (re-verified every phase)

- `/W4 /WX` zero warnings across Debug, Profile, Release, Automation.
- Dependency-graph direction (the acyclic layer graph).
- Global zero-allocation policy — ends **stricter** (closes the scan holes below).
- Byte-exact physics determinism at 0/1/4 workers.
- No-throw engine error handling.
- No god-objects, god-functions, or authority-free data bags — re-enforced by
  leaner unevadable checks, not abandoned.

## Evidence (2026-08-25)

The governance is simultaneously over-heavy and holey. Both facts motivate this
plan; the holes are the proof that "delete and trust good engineering" is the
wrong move.

- **Evadable check.** The 13-parameter `PhysicsBroadphaseStage::Run`
  (`SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h`) is well past the
  12-parameter review trigger, yet `python tools/inventory_wide_signatures.py
  --repo . --strict` passes and lists no ruling for it. The regex inventory does
  not parse that signature, so the mandated review was silently skipped. This is
  the seed negative control for the wide-signature replacement.
- **Under-enforced allocation.** `tools/check_allocation_policy.py` omits five
  source roots (Assets, Maths, Scene, UI, World) containing ~97 unscanned STL
  growth-call sites (bug `TOOL-003`), and reports 33 unreviewed findings
  (`TOOL-002`). The zero-allocation guarantee is not actually enforced there.
- **Brittle by admission.** `AGENTS.md` states that "removing or adding a comment
  line above a ruled aggregate shifts its recorded `site` and fails
  `validate_fast`." Comment edits break CI. That is the line-pinning to remove.
- **Heavy golden ritual.** Each physics golden transition requires manual
  multi-step staging of archived old/new `.exe` producers plus a schema-2
  `manifest.json`.
- **Stale bug ledger.** `Agentic/Bugs/master_bug_report.csv` still marks
  `PHYS-001`..`PHYS-010` as `fixed=No` although FP0 closed them.

Registry/script inventory (baseline for the ≥50% reduction target): `tools/`
holds 13 JSON (10 are ruling/rule registries) and 56 Python scripts (6
`inventory_*`, 24 `check_*`). Re-measure exactly in DB0.

## Classification

| Mechanism | Protects | Flaw | Action |
|---|---|---|---|
| `dependency_graph_rules.json` + checker | dependency direction (critical) | verbose generated proof block | KEEP (rule data); shrink the proof block |
| `allocation_policy_allowlist.json` + checker | zero-allocation (critical) | 5 roots + 97 sites unscanned | **STRENGTHEN** |
| `determinism_math_rulings.json` + checker | determinism (critical) | line-pinned | DE-BRITTLE (content identity) |
| `build_config_rulings.json` + checker | cross-project config parity | line/digest pinned | DE-BRITTLE |
| `coverage_floors.json` + checker | test coverage floors | fine | KEEP |
| `wide_signature_ownership_rulings.json` + inventory | no god-functions | parser evadable | **STRENGTHEN** then retire ledger |
| `function_complexity_rulings.json` + inventory | no monster functions | line + body-digest churn | DE-BRITTLE / fold into review |
| `aggregate_ownership_rulings.json` + `inventory_authority_free_aggregates.py` + `inventory_extraction_scars.py` | no data-bags / god-object scars | brittle, name-scoped evadable | fold into ONE god-object check; retire 2 tools + ledger |
| `reachability_rulings.json` + `inventory_unreachable_symbols.py` | no dead production symbols | 761-line micro-ledger | replace with linker dead-strip / broad test; DELETE ledger |
| `glossary_term_rulings.json` + `inventory_glossary_terms.py` | duplicate glossary terms | ceremony | DELETE |
| `check_related_paths.py` (`Related:` links) | doc cross-refs | blocks CI on file moves | DE-BRITTLE → advisory |
| custom formatters (`align_header_inline_comments.py`, `separate_multiline_cpp_declarations.py`) | layout | duplicates clang-format | DELETE |
| `validate_governance_inventories.py` | polices the inventories | meta-checker | DELETE |
| golden transition ritual | byte-exact + reproducible | manual multi-step staging | DE-BRITTLE (one command) |
| pseudo-legal jargon (docs + code) | nothing (concept survives) | cognitive load | normalize words, keep rules |

## Phases

### DB0 — Inventory and freeze the contract
- Exact `git ls-files tools/*.json tools/*.py`; classify each entry
  STRENGTHEN / DE-BRITTLE / DELETE / KEEP with the invariant it protects.
- Build the **negative-control catalog**: for every check slated to be
  strengthened or retired, capture one real violation the replacement must
  catch. Seeds: the 13-parameter `Run`; a data-bag whose consumer destructures
  every member at entry; a non-deterministic `<cmath>` call reachable from
  `Physics`; an STL `push_back` growth site under one of the five omitted roots.
- No deletions in this phase. Acceptance: classification table + control catalog
  written and reviewed.

### DB1 — Line-number decoupling (pure win)
- Remove physical line-number pinning from every retained checker. Key rulings
  on content identity (symbol + normalized signature, or a body hash), never on
  a line number, so comment and whitespace edits cannot fail CI.
- Acceptance: inserting a blank line or comment anywhere in `SkullbonezSource/`
  produces zero CI failures; every retained gate still passes.

### DB2 — Retire the ceremony (pure win)
- `check_related_paths.py` → advisory, off the blocking fast-gate. Keep the
  cross-references; stop failing CI when a file moves.
- Delete: the two custom formatters (clang-format is the sole layout authority),
  the glossary registry + inventory, and `validate_governance_inventories.py`.
- Acceptance: fast-gate is faster; clang-format still owns layout; no invariant
  lost (glossary and link resolution are advisory-grade).

### DB3 — Strengthen-then-retire the design-invariant checks (load-bearing)
For each of wide-signature, function-complexity, aggregate/authority-free, and
extraction-scar:
1. Stand up **one unevadable replacement** — prefer an AST/compiler-level count
   or a single coarse assertion; or fold the rule into a focused god-object
   review test. Consolidate aggregate + extraction-scar into one god-object /
   data-bag check.
2. Ship a negative-control test that **fails** on the DB0 violation (the
   13-parameter `Run` must now be flagged) and passes on clean source.
3. Only then delete the brittle JSON ledger and regex inventory it replaces.
- Acceptance: every retired check has a passing negative control proving the
  replacement catches what the ledger protected; the 13-parameter `Run` is now
  flagged and is either narrowed or explicitly ruled in the leaner scheme.

### DB4 — Close the enforcement holes (strengthen)
- Extend `check_allocation_policy.py` to Assets, Maths, Scene, UI, World; review
  and resolve the ~97 growth sites and 33 findings (`TOOL-002`, `TOOL-003`).
- Reconcile `master_bug_report.csv` with FP0 closures (`PHYS-001`..`PHYS-010`)
  and any other stale rows.
- Acceptance: the allocation gate scans every engine root and is clean; the bug
  ledger matches reality.

### DB5 — Golden and baseline workflow streamlining (de-brittle)
- `check_physics_regression.py` output → first diverging frame, body id, and
  metric delta, not a 44k-line dump.
- One command (`tools/update_baselines.py --physics`) that updates the golden,
  auto-archives the old/new producer plus content hashes, and writes the
  manifest. Keep the integrity binding (byte-exact output, reproducible
  producer); remove the manual staging.
- Acceptance: a golden transition is one command; determinism and producer
  reproducibility are unchanged; `validate_physics` passes byte-exact.

### DB6 — Remove jargon from source code and documentation
Behavior-preserving and byte-exact. Rails: no source file deleted or renamed; no
functional change; **do not rename** serialized keys, CLI flags, config keys,
golden-referenced strings, or identifiers other tools grep for.
- Apply the lexicon table below across all tracked first-party source code,
  comments, tool scripts, test descriptions, `AGENTS.md`, `Agentic/` material,
  root documentation, plan templates, and reference documents.
- Rewrite internal identifiers and `TEST_CASE` strings only where the rename is
  behavior-neutral and not consumed as a stable external or validation value.
- Strip 40-line ritual headers to a concise purpose line plus the comments that
  actually teach a non-obvious invariant, lifetime, hazard, unit, or concurrency
  constraint. Delete the rest.
- Rewrite the repository rules in plain systems-programming English. Every
  removed term must map to a direct replacement that preserves the rule's
  meaning; no rule may disappear merely because its old label was removed.
- Acceptance: physics remains byte-exact, all tests pass, and the banned lexicon
  is absent from all tracked first-party source code and documentation except
  this active plan's lexicon table, which DB7 deletes at closure.

### DB7 — Permanently ban the removed wording and close the plan
- Add one focused tracked-file scanner under `tools/` that rejects every banned
  lexicon term, case-insensitively, in first-party source code and documentation.
  It must inventory tracked files rather than rely on a hand-maintained directory
  sample, and it must cover C/C++, shaders, substantial tool scripts, tests,
  `AGENTS.md`, root documents, and all `Agentic/` documents.
- Add negative tests proving that one banned term in a source fixture and one in
  a documentation fixture both fail, while clean fixtures pass. The checker may
  construct its match terms internally, but no production source or document may
  receive a permanent path or per-site exemption.
- Run the scanner from `validate_fast` and the mandatory hosted CPU validation
  lane. A reintroduced term must fail ordinary local and hosted validation.
- Delete this completed plan under the repository convention in the same closure
  change, remove any temporary exact-plan exclusion, and leave the scanner with
  zero source/document exemptions.
- Replace `AGENTS.md`'s temporary link to this active plan with the permanent
  scanner and plain-language rule before deleting the plan.
- Acceptance: the full tracked source/document scan is clean; both negative
  tests fail for the intended reason; fast and hosted validation invoke the
  scanner; the plain-language instruction in `AGENTS.md` remains binding.

## Lexicon Table (DB6 / DB7)

| Jargon | Plain replacement |
|---|---|
| authority-free aggregate | struct |
| extraction scar | (drop; describe the actual issue) |
| owner ruling / adjudication | review decision |
| false-pass control | negative control / mutation test |
| authoritative witness | regression scenario / test case |
| owner-borrow | borrowed reference / view |
| courier struct / context bag | parameter object / frame context |
| standing automated-transition authority | approved golden-update policy |
| closure failure | blocking defect |
| load-bearing | required / relied upon |

## Non-Goals

- No change to engine runtime behavior, physics output, or determinism.
- No weakening of `/W4 /WX`, dependency direction, zero-allocation, determinism,
  or no-throw handling.
- No change to test behavior — only test descriptions.
- No source file deletions or renames (DB6/DB7 are in-place).

## Validation Map

| Phase | Required evidence |
|---|---|
| DB1–DB2 | `validate_fast`; run each changed checker's self-test + repo scan; prove a comment insert triggers no CI failure |
| DB3 | each new check's self-test **fails** on the seeded violation and passes clean; `validate_fast` |
| DB4 | `python tools/check_allocation_policy.py --repo .` clean across all roots; `validate_perf` if guard semantics change |
| DB5 | `validate_physics` byte-exact after the streamlined path; one-command transition reproduces the same digest |
| DB6 | scan all tracked first-party source and documentation; comment/doc-only diffs need focused documentation checks; identifier renames require `validate_fast` + `validate_all_cpu_tests` + `validate_physics` byte-exact |
| DB7 | scanner negative tests; direct clean repo scan; `validate_fast`; confirm the hosted CPU lane invokes the scanner and every removed term maps to a retained rule |
| Plan close | `tools/agent_validate.bat --plan-completion` |

## Acceptance (whole plan)

- Every deleted check names its invariant and its robust replacement, and ships
  a passing negative control.
- Determinism, `/W4 /WX`, and dependency direction are unchanged; allocation is
  strictly stronger (five roots now scanned).
- `tools/` file count down ≥50%; zero line-number CI traps; measurably faster
  fast-gate.
- The banned lexicon is absent from all tracked first-party source and docs;
  validation rejects any reintroduction; all tests pass byte-exact.
- The two superseded WNF drafts are deleted.
