# Build Configuration Parity

Date: 2026-07-30
Status: NOT STARTED — 0/6 phases complete
Impact area: All five vcxproj files, shared engine TUs compiled into the test
binary, authored-JSON failure lanes, FP determinism envelope, validation tooling
Owner: Build configuration + Scene/Replay JSON boundaries
Priority: High — this is the only campaign plan holding a correctness divergence
between the test binary and the shipping binary

## Problem And Evidence

Source-only review at tip `91a8403d` on 2026-07-30 found that repository
governance validates source text and never validates the build. Two concrete
instances exist today.

**1. Shared engine TUs compile with two different exception configurations.**
`SKULLBONEZ_TESTS.vcxproj` compiles four engine sources that include
`ThirdPtySource/nlohmann/json.hpp`:

- `SkullbonezSource/Scene/SceneSnapshotWriter.cpp`
- `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp`
- `SkullbonezSource/Runtime/Direction/DemoDirector.cpp`
- `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp`

The tests project sets `_HAS_EXCEPTIONS=0` with `<ExceptionHandling>Sync</...>`
and does **not** define `JSON_NOEXCEPTION`. `SKULLBONEZ_CORE.vcxproj` sets
`_HAS_EXCEPTIONS=0` with `<ExceptionHandling>false</...>` and **does** define
`JSON_NOEXCEPTION`.

`ThirdPtySource/nlohmann/json.hpp:2535` selects on
`(__cpp_exceptions || __EXCEPTIONS || _CPPUNWIND) && !defined(JSON_NOEXCEPTION)`.
`/EHsc` defines `_CPPUNWIND`, so in the test binary `JSON_THROW` is `throw` and
`JSON_TRY`/`JSON_CATCH` are real; in the engine binary they are `std::abort()`,
`if(true)`, and `if(false)`.

Consequence: a malformed-authored-data path that throws in the test binary calls
`std::abort()` in the shipping engine. The repository has zero `catch` in
`SkullbonezSource/` by policy, so nothing intercepts it. `AGENTS.md` Lane R
exists specifically so scene/asset/replay-artifact failures report an
owner/message instead of terminating, and the test suite is structurally unable
to observe that Lane R does not hold on these paths. `_HAS_EXCEPTIONS=0`
combined with `/EHsc` is additionally an unsupported MSVC combination.

**2. Seven translation units silently lose the FP-contraction contract.**
`SKULLBONEZ_CORE.vcxproj:238-247` force-includes
`SkullbonezSource/Core/FloatingPointContract.h` into every `ClCompile` item.
`SKULLBONEZ_CORE.vcxproj:253-289` then sets per-file `<ForcedIncludeFiles>` on
six ImGui TUs and `TracyClient.cpp` **without appending
`%(ForcedIncludeFiles)`**, overriding rather than extending. Those seven TUs
compile without `#pragma fp_contract(off)`.

Measured practical risk is low: all seven are third-party, perform no physics
math, and carry
`<ExcludedFromBuild Condition="'$(Configuration)'=='Release' Or '$(Configuration)'=='Profile-WPO'">`.
The defect is that `FloatingPointContract.h:22` states the invariant "Every
project force-includes this file in every configuration", and that statement is
not true of the built artifact. This is the exact silent-drop class the
fp-envelope work exists to prevent.

## Goal

Make the built artifact match what the source and project files claim, and add
one repeatable inventory that reports build-configuration divergence so this
class cannot recur silently.

## Non-Goals

- No new runtime allocation exception, hot-path change, or physics behavior
  change. This plan is strictly byte-exact for physics.
- No baseline, golden, scene, shader, or schema refresh. A differing physics
  byte means the change altered evaluation rather than configuration, and the
  task is reverted rather than baselined.
- No conversion of the new inventory into a count threshold, ratio, or budget.
  It reports current configuration facts for review, following the existing
  four-inventory contract in `AGENTS.md`.
- Not a general exception-policy revisit. Engine code stays exception-free and
  doctest keeps its own exception machinery.

## Phases

- [ ] **BP0 — Census build configuration and JSON accessor safety.** Produce two
  exact current-tree tables. First: for every source file, which of the five
  projects compile it, and with what `PreprocessorDefinitions`,
  `ExceptionHandling`, `LanguageStandard`, `FloatingPointModel`,
  `RuntimeLibrary`, and effective `ForcedIncludeFiles`; flag every file compiled
  two different ways. Second: for the four shared JSON TUs, classify every
  `get<T>()`, `operator[]`, `.at()`, and iterator dereference as
  (a) reading a value this code just wrote, (b) reading external authored data
  behind the `AuthoredSceneParserSchema.h` `RequireMember`/`ReadFloat`/
  `ReadString` validation layer, or (c) reading external data with no validation
  — a live `std::abort()` reachable from a malformed file. Record which category
  (c) sites exist and which authored formats reach them. Evidence:
  `Agentic/Reports/2026-07-30/build-configuration-parity-bp0-census.md`.
- [ ] **BP1 — Give the tests project production JSON semantics.** Add
  `JSON_NOEXCEPTION` to every `SKULLBONEZ_TESTS.vcxproj` configuration so the
  four shared TUs compile with the same `JSON_THROW`/`JSON_TRY`/`JSON_CATCH`
  expansion in both binaries. Keep `<ExceptionHandling>Sync</...>` — doctest's
  `REQUIRE` needs it, and `JSON_NOEXCEPTION` forces the abort path regardless of
  `_CPPUNWIND`, so no doctest behavior changes. Record every test that changes
  outcome; a test that now aborts is BP0 category (c) evidence, not a test
  defect to be silenced. Do not add `DOCTEST_CONFIG_NO_EXCEPTIONS`.
- [ ] **BP2 — Close every unvalidated external-JSON accessor.** For each BP0
  category (c) site, route the read through the existing schema validation layer
  so a malformed authored file produces a Lane R `SbResult` with owner and
  message, and the `std::abort()` path becomes unreachable from file content.
  Add one focused malformed-input regression per repaired authored format
  (scene, asset library, replay v2 artifact, launch resolution, demo direction as
  applicable) asserting the recoverable result rather than the abort. Do not
  introduce a compatibility wrapper, a second parser, or a `catch`.
- [ ] **BP3 — Restore the FP contract to all seven overriding TUs.** Append
  `;%(ForcedIncludeFiles)` to the seven per-file `<ForcedIncludeFiles>` rows in
  `SKULLBONEZ_CORE.vcxproj` so `FloatingPointContract.h` and
  `DevelopmentToolsCapability.h` both apply. Confirm the resulting compile is
  clean at `/W4` for the ImGui and Tracy TUs, which currently build with
  `TurnOffAllWarnings`. If the contract header cannot apply to a third-party TU,
  correct `FloatingPointContract.h`'s stated invariant instead of leaving a
  false claim, and record the exclusion with owner and reason.
- [ ] **BP4 — Add the build-configuration consistency inventory.** Create
  `tools/check_build_config_consistency.py` following the existing inventory
  contract: it parses all five vcxproj files, resolves per-file and per-project
  `ClCompile` metadata including `%(...)` inheritance, and reports every source
  file compiled under divergent settings plus every per-file metadata override
  that drops an inherited project value. Divergence that an owner has ruled
  intentional lives in `tools/build_config_rulings.json` keyed by file and exact
  setting; an unruled divergence fails, a ruled one passes. Ship `--self-test`
  fixtures covering inheritance with and without `%(...)`, per-configuration
  divergence, a planted unruled divergence, a stale ruling, and both MSBuild
  item spellings. Wire the repository scan and self-test into
  `tools/validate_fast.bat` and add the row to `tools/README.md` and the
  `AGENTS.md` file-to-gate mapping in the same commit. Add no count threshold or
  budget.
- [ ] **BP5 — Close the plan.** Reconcile `AGENTS.md` Governance Review Model and
  the inventory table with the now-five inventories, complete the touched-file
  comment audit against `Agentic/Reference/comment-style-guide.md`, obtain one
  independent rubber-duck review answering all five ownership questions, and run
  the mapped gates. Evidence:
  `Agentic/Reports/2026-07-30/build-configuration-parity-closure.md`.

## Dependencies And Decisions

- No barrier into this plan. It is campaign plan 1 because BP1/BP2 are the only
  correctness divergence in the campaign and BP4's inventory also covers the
  finding class behind BP3.
- Barrier out: BP1 changes what the test binary compiles, so
  `maths-surface-reachability` MR1's coverage-floor recheck runs against the
  post-BP1 test configuration, not the current one.
- Open decision for BP2: if a category (c) site reads an authored format that
  has no schema validation layer at all, the repair is a new validation entry
  point rather than an inline check. Name the owning format and record the
  choice in the BP0 census before implementing.
- `JSON_NOEXCEPTION` is a compile-time selection in third-party code and is not
  a repository allowlist entry; it needs no allocation-policy or dependency-rule
  change.

## Acceptance

Every source file compiled by more than one project compiles with identical
exception, JSON, floating-point, and forced-include semantics, or carries an
owner ruling naming why it does not. No `std::abort()` is reachable from
malformed authored file content in any of the four shared JSON TUs. All seven
overriding TUs receive `FloatingPointContract.h`, or the header's stated
invariant is corrected to match reality. `check_build_config_consistency.py`
reports zero unruled divergences and fails closed on planted drift.

## Validation

`tools\validate_fast.bat` (tools and vcxproj changes; includes the new checker's
self-test and repository scan), then `python tools\check_build_config_consistency.py --self-test`
and `--repo .` directly per the `tools/*` rule, then
`tools\validate_all_cpu_tests.bat` (tests project configuration change reaches
every CPU target), then `tools\validate_full.bat` (multi-area project-file
change). Physics must remain byte-exact; a CSV difference means BP1-BP3 altered
evaluation and the task is reverted rather than baselined.
