# Governance De-Bureaucratization and Jargon Removal

Date: 2026-08-25
Status: Active by owner direction 2026-08-25. 7/8 phases complete; DB7 next.
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
- No god-objects, god-functions, or invariant-free data bags — re-enforced by
  leaner unevadable checks, not abandoned.

## Evidence (2026-08-25)

The governance is simultaneously over-heavy and holey. Both facts motivate this
plan; the holes are the proof that "delete and trust good engineering" is the
wrong move.

- **Evadable check.** The 12-parameter `PhysicsBroadphaseStage::Run`
  (`SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h`) is well past the
  12-parameter review trigger, yet `python tools/inventory_wide_signatures.py
  --repo . --strict` passes and lists no ruling for it. The regex inventory does
  not parse that signature, so the mandated review was silently skipped. This is
  the seed negative control for the wide-signature replacement.
- **Under-enforced allocation.** `tools/check_allocation_policy.py` omits five
  source roots (Assets, Maths, Scene, UI, World). The DB0 scan passes across its
  current 567 files and reports 55 direct-heap, 94 dynamic-member, and 722 STL-
  growth findings with zero allowlist errors; the earlier ~97-site and 33-
  finding estimates no longer reproduce and are not closure evidence. DB4 must
  inventory the omitted roots directly before changing the policy.
- **Brittleness reproduced before DB1.** The pre-DB1 repository contract stated
  that moving a comment above a ruled aggregate shifted its recorded `site` and
  failed `validate_fast`. DB1 removes that physical-coordinate identity.
- **Heavy golden ritual.** Each physics golden transition requires manual
  multi-step staging of archived old/new `.exe` producers plus a schema-2
  `manifest.json`.
- **Stale bug ledger.** `Agentic/Bugs/master_bug_report.csv` still marks
  `PHYS-001`..`PHYS-010` as `fixed=No` although FP0 closed them.

Registry/script inventory: `tools/` holds 13 tracked JSON files and 56 tracked
Python scripts. DB0 defines a 20-file governance-administration cohort; the
≥50% reduction target applies to that cohort, not to unrelated domain validators
whose deletion would weaken behavior coverage. All 69 files remain listed below
so this scope cannot hide a validator.

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
| Former aggregate and incomplete-extraction inventories plus their shared registry | no data bags / incomplete moves | brittle, name-scoped evadable | fold into ONE god-object check; retire 2 tools + ledger |
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
  catch. Seeds: the 12-parameter `Run`; a data-bag whose consumer destructures
  every member at entry; a non-deterministic `<cmath>` call reachable from
  `Physics`; an STL `push_back` growth site under one of the five omitted roots.
- No deletions in this phase. Acceptance: classification table + control catalog
  written and reviewed.

#### DB0 closure evidence — 2026-08-25

`git ls-files "tools/*.json" "tools/*.py"` reports exactly 13 JSON files and
56 Python files. CodeGraph is current at 1,219 files / 38,033 nodes /
115,104 edges. The current wide-signature inventory reports eleven ruled
operations at the 12-or-more threshold but omits the current 12-parameter
`PhysicsBroadphaseStage::Run` declaration at
`SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h:91`. This replaces the
stale 13-parameter wording above with a reproduced parser miss.

The action labels mean:

- **KEEP:** required domain validation, generation, migration, or evidence.
- **STRENGTHEN:** retain and expand coverage or make the safe workflow complete.
- **DE-BRITTLE:** retain the protected rule while removing line, proof, or manual
  workflow coupling.
- **DELETE:** remove only after the named replacement and its negative test pass.

##### JSON inventory

| File | Action | Protected rule or disposition |
|---|---|---|
| `aggregate_ownership_rulings.json` | DELETE | Replaced by the compiler-backed DB3 design check; no per-site ledger remains. |
| `allocation_policy_allowlist.json` | STRENGTHEN | Legitimate owner/phase/cap policy data; DB4 extends coverage to every engine root. |
| `build_config_rulings.json` | DE-BRITTLE | Intentional project metadata differences remain reviewed without line/digest churn. |
| `coverage_floors.json` | KEEP | Versioned subsystem coverage floors. |
| `dependency_graph_rules.json` | KEEP | Authoritative layer, Runtime edge, content, and project-ownership policy data. |
| `determinism_math_rulings.json` | DE-BRITTLE | Exact reviewed platform-math exceptions remain content-identified. |
| `function_complexity_rulings.json` | DELETE | Replaced by the DB3 design check and negative fixtures. |
| `glossary_term_rulings.json` | DELETE | Duplicate wording becomes non-blocking cleanup rather than a permission ledger. |
| `native_diagnostics_suppressions.json` | KEEP | Reviewed compiler/ASan diagnostic suppressions. |
| `physics_baseline_approval.json` | KEEP | Content-bound accepted Physics golden identity. |
| `reachability_rulings.json` | DELETE | Replaced by link/dead-strip and focused reachability evidence. |
| `shader_contracts.json` | KEEP | Shipping shader entry/profile/contract data. |
| `wide_signature_ownership_rulings.json` | DELETE | Replaced by the compiler-backed DB3 design check. |

##### Python inventory

| File | Action | Protected rule or disposition |
|---|---|---|
| `align_header_inline_comments.py` | DELETE | Pinned clang-format becomes the sole token-layout owner. |
| `analyze_at_rest_stability.py` | KEEP | Semantic at-rest Physics regression analysis. |
| `analyze_replay_prediction_spikes.py` | KEEP | Bounded prediction-spike attribution. |
| `archive_validation_artifacts.py` | KEEP | Repeatable validation-artifact collection. |
| `bake_hulls.py` | KEEP | Deterministic authored hull generation. |
| `bake_shaders.py` | KEEP | Shipping shader compilation, reflection, and freshness. |
| `check_allocation_policy.py` | STRENGTHEN | Static no-growth policy; DB4 adds five omitted roots and resolves all findings. |
| `check_at_rest_stability_analyzer.py` | KEEP | Negative tests for the at-rest analyzer. |
| `check_broadphase_pair_stream_oracle.py` | KEEP | Deterministic broadphase pair-stream integrity. |
| `check_build_config_consistency.py` | DE-BRITTLE | Effective project metadata and project-DAG parity without fragile sites. |
| `check_causal_tree_interaction.py` | KEEP | Causal hierarchy interaction behavior. |
| `check_contact_energy_scenes.py` | KEEP | Physics contact-energy scene behavior. |
| `check_coverage.py` | KEEP | Coverage-floor enforcement from Cobertura data. |
| `check_dependency_graph.py` | DE-BRITTLE | Keep the resolved edge evaluator; shrink only its generated Markdown proof. |
| `check_determinism_math_policy.py` | DE-BRITTLE | Keep Physics/Maths math enforcement; key exceptions by content identity. |
| `check_dx12_baselines.py` | KEEP | DX12 image-baseline comparison. |
| `check_perf_budgets.py` | KEEP | Absolute performance budgets. |
| `check_physics_baseline_guard.py` | DE-BRITTLE | Keep exact staged-content and producer integrity behind the DB5 one-command path. |
| `check_physics_commit_gate.py` | KEEP | Fresh exact-index Physics validation before affected commits. |
| `check_physics_known_issue_regression.py` | KEEP | Named Physics known-issue behavior. |
| `check_physics_query_regression.py` | KEEP | SkullScope query output contract. |
| `check_physics_regression.py` | DE-BRITTLE | Keep byte-exact comparison; replace raw dumps with bounded semantic first-difference output. |
| `check_related_paths.py` | DE-BRITTLE | Keep a useful report but remove it from blocking validation. |
| `check_replay_prediction_determinism.py` | KEEP | Replay prediction byte-exact behavior. |
| `check_replay_scrub_regression.py` | KEEP | Replay scrub/restore behavior. |
| `check_replay_v2_artifact.py` | KEEP | Replay artifact schema and byte integrity. |
| `check_replay_visual_fidelity.py` | KEEP | Immutable recorded reveal-frame contract. |
| `check_shooting_reaction.py` | KEEP | Physics shooting response. |
| `check_staged_file_sizes.py` | KEEP | Reject oversized staged artifacts. |
| `check_ui_blur.py` | KEEP | UI blur visual behavior. |
| `cpp_source_scan.py` | DELETE | Retires with its three lexical design-inventory consumers after DB3 replacement. |
| `export_screenshot_png.py` | KEEP | Deterministic screenshot conversion helper. |
| `generate_doctest_from_recording.py` | KEEP | Recorded-interaction test generation. |
| `generate_physics_scale_sleepy_scene.py` | KEEP | Deterministic 5,000-body scale fixture. |
| Former aggregate inventory | DELETE | Replaced by one compiler-backed design check. |
| Former incomplete-extraction inventory | DELETE | Replaced by one compiler-backed design check. |
| `inventory_function_complexity.py` | DELETE | Replaced by one compiler-backed design check. |
| `inventory_glossary_terms.py` | DELETE | Duplicate definitions become non-blocking DB6 cleanup. |
| `inventory_unreachable_symbols.py` | DELETE | Replaced by link/dead-strip and focused reachability evidence. |
| `inventory_wide_signatures.py` | DELETE | Its reproduced parser miss is covered by the DB3 compiler-backed replacement. |
| `measure_causal_inspection_perf.py` | KEEP | Causal inspection performance measurement. |
| `measure_dense_pile_sleep.py` | KEEP | Dense-pile sleep measurement. |
| `migrate_data_formats.py` | KEEP | Versioned authored-data migrations. |
| `physics_query.py` | KEEP | SkullScope Physics query interface. |
| `replay_query.py` | KEEP | Replay artifact query interface. |
| `separate_multiline_cpp_declarations.py` | DELETE | Pinned clang-format becomes the sole layout owner. |
| `test_analyze_replay_prediction_spikes.py` | KEEP | Prediction-spike analyzer tests. |
| `time_validation_pipeline.py` | KEEP | Validation stage timing and critical-path measurement. |
| `update_baselines.py` | STRENGTHEN | DB5 adds the content-bound one-command Physics transition. |
| `validate_concepts.py` | KEEP | Finite concept-scene validation. |
| `validate_governance_inventories.py` | DELETE | Direct retained checks and the DB3 replacement make the inventory meta-runner unnecessary. |
| `validate_look_lab_reuse.py` | KEEP | Cross-process Look Lab reuse. |
| `validate_native_diagnostics.py` | KEEP | ASan and compiler static-analysis lane. |
| `validate_project_filters.py` | KEEP | Project/filter single-owner parity. |
| `validate_scene_loads.py` | KEEP | Authored scene-load matrix. |
| `validate_shaders.py` | KEEP | Shader build/contract validation. |

The 20-file governance-administration cohort is the seven JSON files for
aggregate, build-config, deterministic-math, complexity, glossary,
reachability, and wide-signature administration plus thirteen Python files:
the two custom formatters, three retained/de-brittled checkers, shared lexical
scanner, six inventory scripts, and inventory meta-runner. Fifteen are marked
DELETE, so the ratified end-state reduction is 75% for that cohort while all
unrelated validators remain protected.

##### Negative-test catalog

| Mechanism | Seed violation | Replacement must prove |
|---|---|---|
| Wide signatures | Current 12-parameter `PhysicsBroadphaseStage::Run`, which the lexical inventory misses | Compiler-backed enumeration flags the declaration independent of formatting. |
| Function size/nesting | A long, deeply nested function plus a once-called helper split | The design check reports the real operation shape rather than accepting helper-name evasion. |
| Parameter struct | A data-only parameter struct whose consumer immediately unpacks every field | The replacement identifies the unnecessary argument bundle without rejecting ordinary structs. |
| Refactor leftovers | An `m_`-prefixed local and a pure parameter alias | The replacement reports both concrete local-code problems. |
| Reachability | An ordinary production `.cpp` definition with no production caller outside its translation unit | Link/dead-strip or focused symbol evidence exposes the unused production symbol. |
| Duplicate glossary cleanup | The same term defined differently in two source comments | DB6's one-shot cleanup reports and removes the duplicate without a per-site permission ledger. |
| Custom formatting | A C++ fixture with layout rejected by pinned clang-format | clang-format alone fails the fixture; no second formatter is needed. |
| Inventory meta-runner | A retained replacement check returning non-zero | The direct fast-gate call propagates the failure without a checker-of-checkers. |
| Allocation coverage | `std::vector::push_back` in `SkullbonezSource/Assets` | The expanded policy scan sees the omitted root and fails an unapproved growth site. |
| Deterministic math | A Physics-reachable `std::sin`/`sinf` call | Content-keyed policy rejects it while comment/line movement stays green. |
| Build configuration | One shared source compiled with a different exception/RTTI/FP contract | Content-keyed comparison fails real metadata drift but ignores comment movement. |
| Dependency proof | A forbidden Core-to-Runtime include | The resolved-edge evaluator fails it after the Markdown proof is reduced. |
| Related paths | A missing path in a `Related:` block | Advisory mode reports the path but does not fail fast validation. |
| Physics comparison | One changed CSV value with frame, body, and metric columns | Output names the first semantic difference while byte-exact failure remains. |
| Physics baseline update | A changed golden with a missing producer or mismatched hash | The DB5 command refuses partial evidence and writes a complete bound transition in one operation. |
| Banned wording | One banned phrase in source and one in documentation | DB7's tracked-file scanner fails both local fast and hosted validation. |

DB0 review found two stale premises and corrected them: the reproduced wide
signature has 12 parameters, not 13, and the allocation check no longer reports
the earlier 33 unreviewed findings. No tool, rule data, source, or validation
behavior changes in DB0.

Authority did not move. DB0 records future replacement ownership but changes no
current checker or subsystem owner.

### DB1 — Line-number decoupling (pure win)
- Remove physical line-number pinning from every retained checker. Key rulings
  on content identity (symbol + normalized signature, or a body hash), never on
  a line number, so comment and whitespace edits cannot fail CI.
- Acceptance: inserting a blank line or comment anywhere in `SkullbonezSource/`
  produces zero CI failures; every retained gate still passes.

#### DB1 closure evidence — 2026-08-26

Physical source coordinates are now diagnostics only in every blocking checker
that still used them as policy identity at the start of DB1:

- Deterministic-math rulings use path, call, normalized statement digest, and a
  stable same-statement occurrence. Schema 2 rejects `line` and `column` fields.
- Runtime repair-debt rulings use source, resolved target, exact include
  spelling, stable same-edge occurrence, and a line-free policy fingerprint.
  A duplicate identical include receives a new occurrence and fails unruled.
- Aggregate rulings use type key, declaration path, and member count under
  schema 3; the extraction inventory already used path plus local name, and its
  obsolete recorded site was removed.
- Function-complexity body length counts non-empty masked code lines and its
  body digest uses comment-free, whitespace-normalized text. Thirty-eight
  current review rows remain ruled; three rows selected only by physical prose
  lines are no longer trigger rows.

Negative fixtures prove that blank lines and ordinary comments leave each join
current, while semantic body edits, math-statement edits, include-spelling
changes, duplicate forbidden includes, deleted debt, moved declaration files,
and reintroduced physical-coordinate fields fail. Focused results: deterministic
math 29/29 current; aggregate gate 78/78 ruled; extraction gate 1/1 ruled;
function complexity 38/38 ruled; dependency graph zero findings. The generated
dependency proof was refreshed from rule data. No engine source, runtime
behavior, Physics output, baseline, or subsystem authority changed.

### DB2 — Retire the ceremony (pure win)
- `check_related_paths.py` → advisory, off the blocking fast-gate. Keep the
  cross-references; stop failing CI when a file moves.
- Delete: the two custom formatters (clang-format is the sole layout authority),
  the glossary registry + inventory, and `validate_governance_inventories.py`.
- Acceptance: fast-gate is faster; clang-format still owns layout; no invariant
  lost (glossary and link resolution are advisory-grade).

#### DB2 closure evidence — 2026-08-26

The two custom source-layout formatters, glossary inventory and permission
registry, and governance meta-runner are deleted. `validate_format.bat` now
checks changed first-party C++ files directly with the pinned clang-format
binary, and `format_fix.bat` applies that same sole layout authority. Untouched
legacy layout is not rewritten repository-wide. A planted spacing defect in a
changed source file failed with exit 1; the restored tree passed. The changed-
file format phase measured 0.274 seconds with no C++ diff, compared with the
tracked prior 51.104-second format-stage measurement.

`validate_fast.bat` names every retained inventory command directly. Its seven
self-tests run concurrently, followed by six concurrent live scans; the exact
batch passed in 36.102 seconds. A planted nonzero child returned the fast gate's
inventory exit code 8, proving failure propagation without the deleted wrapper.
The standalone Related-path report is now explicitly advisory and absent from
blocking validation; its self-test passed and its live scan found zero unresolved
paths across 647 scanned files and 1,901 path entries.

Mechanical layout authority moved from two repository-specific post-processors
to pinned clang-format alone. Glossary consolidation and Related-path quality
remain direct review responsibilities rather than per-site permission ledgers
or CI blockers. No engine source, runtime behavior, baseline, golden, binary,
or generated runtime artifact changed. Full `validate_fast` was not run because
this intermediate phase changed no build or runtime input; all changed-script
paths and their planted negative cases were exercised directly.

### DB3 — Strengthen-then-retire the required design checks
For each wide-signature, function-complexity, aggregate-ownership, and incomplete-
extraction check:
1. Stand up **one unevadable replacement** — prefer an AST/compiler-level count
   or a single coarse assertion; or fold the rule into a focused god-object
   review test. Consolidate aggregate ownership and incomplete extraction into one god-object /
   data-bag check.
2. Ship a negative-control test that **fails** on the DB0 violation (the
   12-parameter `Run` must now be flagged) and passes on clean source.
3. Only then delete the brittle JSON ledger and regex inventory it replaces.
- Acceptance: every retired check has a passing negative control proving the
  replacement catches what the ledger protected; the 12-parameter `Run` is now
  flagged and is either narrowed or explicitly ruled in the leaner scheme.

#### DB3 closure evidence — 2026-08-26

`tools/check_source_design.py` replaces five lexical inventories and their
shared scanner with compiler syntax trees. Clang-Tidy rejects changed functions
with 12 parameters, more than 400 lines, or nesting beyond five levels. Clang
Query rejects member-prefixed locals, pure parameter aliases, and parameter
structs copied into four or more locals at function entry. The check has no
per-site permission ledger.

The compiler contexts come from effective MSBuild metadata for the owning
project. Debug, Profile, and Automation definitions, forced includes, language
mode, exception mode, and additional include directories are applied where the
project declares those configurations. Assets and Gameplay headers resolve to
CORE rather than a synthetic fallback. The planted controls cover the exact
multiline 12-parameter broadphase declaration shape, a 403-line function,
six-level nesting in a once-called helper, both parameter-struct forms, both
local-refactor remnants, missing effective `/Gy`, and a late `/OPT:REF`
override. A native MSVC compile/link fixture proves that the used function
survives and the unreferenced function is removed.

`PhysicsBroadphaseStage::Run` is narrowed from 12 parameters to three. Its
fixed-step borrows now travel in `PhysicsBroadphaseStepInput` and are consumed
directly rather than copied into locals; the stage retains no new state. Four
per-site JSON ledgers, five lexical inventories, and their shared scanner are
deleted only after the replacement controls pass. Together with DB2, all 15
ratified governance-administration deletions are complete.

Three independent read-only review passes found and closed configuration,
effective-link-setting, negative-fixture, include-directory, and header-owner
gaps. The final review reports no implementation blocker. Focused compiler and
checker controls pass, including the legitimate `TextureCollection.cpp` probe.
No validation suite was intentionally run. The commit hook nevertheless started
`validate_physics` and was stopped during its Debug build; no test executable,
runtime comparison, or determinism phase ran. Broad evidence remains deferred
by explicit owner direction.

### DB4 — Close the enforcement holes (strengthen)
- Extend `check_allocation_policy.py` to Assets, Maths, Scene, UI, World; review
  and resolve the ~97 growth sites and 33 findings (`TOOL-002`, `TOOL-003`).
- Reconcile `master_bug_report.csv` with FP0 closures (`PHYS-001`..`PHYS-010`)
  and any other stale rows.
- Acceptance: the allocation gate scans every engine root and is clean; the bug
  ledger matches reality.

#### DB4 closure evidence — 2026-08-26

`tools/check_allocation_policy.py` now scans all ten first-party engine roots.
Every repository permission binds an exact source statement to its enclosing
function or type and nearby normalized code; physical line numbers and broad
file-wide substrings are not policy identity. The gate rejects any allowed
identity that resolves to more than one finding. Its negative controls cover an
allocation under Assets and two cloned full-context statements inside one
function, proving omitted roots and duplicate-site inheritance both fail.

The newly visible Assets, Scene, UI, and World findings are recorded with
owner, phase, reason, hard cap, and removal path. AssetSystem reserves four
published process-lifetime registry ceilings during construction and fails
before exhaustion can relocate borrowed records. Terrain retains its cold-built
CPU upload vertices for resource retries. WorldEnvironment prepares and retains
its fixed-topology water vertices when scene terrain bounds are applied, so a
Render-phase retry reads existing backing; a focused regression control records
pointer and capacity stability across a guarded re-prepare.

The bug ledger now agrees with FP0: PHYS-001 through PHYS-006 and PHYS-008
through PHYS-010 are closed, while PHYS-007 remains open for FP5. TOOL-002 and
TOOL-003 are closed by the clean all-root scan. ASSET-003 is also closed because
successful registrations cannot exceed constructor-reserved backing, with a
focused reference-stability regression case added.

Four serial read-only reviews found and closed inaccurate render-phase
classification, broad permission inheritance, missing registry caps, premature
bug dispositions, duplicate exact statements, and 22 duplicated neighborhood
identities. The final review reports CLEAN. Focused checker self-tests and the
all-root scan pass at 647 files, 58 direct-heap findings, 124 dynamic-container
members, and 823 growth calls with zero errors. No build, unit suite, runtime
test, `validate_*` command, or other broad validation ran by explicit owner
direction.

### DB5 — Golden and baseline workflow streamlining (de-brittle)
- `check_physics_regression.py` output → first diverging frame, body id, and
  metric delta, not a 44k-line dump.
- One command (`tools/update_baselines.py --physics`) that updates the golden,
  auto-archives the old/new producer plus content hashes, and writes the
  manifest. Keep the integrity binding (byte-exact output, reproducible
  producer); remove the manual staging.
- Acceptance: a golden transition is one command; determinism and producer
  reproducibility are unchanged; `validate_physics` passes byte-exact.

#### DB5 closure evidence — 2026-08-26

`tools/check_physics_regression.py` now parses the canonical single CSV run and
reports the first differing frame, body id, name, row, and up to eight changed
metrics with numeric deltas. Every CSV-derived label and value is escaped and
length-bounded; truncated rows and parser-limit errors produce bounded failure
messages instead of tracebacks or full-output dumps.

`tools/update_baselines.py --physics` now performs the complete core Physics
golden transition. It discovers the sole active plan and the exact committed
predecessor whose new hash matches the accepted golden, validates and copies
the predecessor producer and first-party DLL bytes from the clean Git index,
archives the current Debug producer and DLLs, writes the schema-2 manifest,
invokes the existing content-bound override guard, force-stages the otherwise
ignored retained binaries, and rechecks the complete index. A late failure
restores the prior golden, acceptance record, local receipt, and empty index and
deletes only the newly created transition bundle.

Four serial read-only reviews found and closed ignored-binary staging,
working-tree predecessor substitution, malformed-row handling, parser-error
handling, and unbounded CSV-cell and label output. The final review reports
CLEAN. Python syntax checks, both isolated script self-tests, and a read-only
lookup of the live `1b984310` predecessor pass. No golden was updated, and no
build, unit suite, runtime test, `validate_physics`, `validate_*`, or other broad
validation ran by explicit owner direction.

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

#### DB6 closure evidence — 2026-08-26

The tracked first-party source and documentation scan reports the retired terms
only in this plan's ten-row lexicon table. Morphological variants are included
in that review. Stable report-schema keys and legacy Runtime-contract child-case
arguments retain their exact external bytes through split compatibility
literals, while internal variables and test descriptions use direct wording.

Fifty-six 40-line boilerplate headers were reduced to concise purpose and local
engineering constraints. The only remaining preamble at least 40 lines long is
the 54-line Runtime-contract test preamble, whose content is entirely purpose
and concrete invariants rather than empty template sections. `AGENTS.md`, the
comment style guide, and the comment-audit skill now require concise, local,
plain-language comments without mandatory Summary/Glossary/Related ceremony.

Two serial read-only reviews found and closed two blockers: missed morphological
word variants, and a parked patch whose edited preimage no longer matched its
recorded base. The patch now omits that commentary-only hunk, retains the
remaining applicable implementation hunks, and binds its new SHA-256 in the
parked plan. No source file was deleted or renamed. No build, test, Physics
comparison, or validation command ran by explicit owner direction.

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
- Governance-administration cohort file count down ≥50%; zero line-number CI
  traps; measurably faster fast-gate.
- The banned lexicon is absent from all tracked first-party source and docs;
  validation rejects any reintroduction; all tests pass byte-exact.
- The two superseded WNF drafts are deleted.
