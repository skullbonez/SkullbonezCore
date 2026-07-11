# 15 — Review Gaps

Date: 2026-07-10 (reconciled against current `main`)
Status: In progress
Owner: architecture cleanup

## Current Measurements

Scope: tracked engine/shader source-bearing files and first-party test files.

- Engine/shader source: 406 files / 172,036 lines.
- First-party tests: 26 files / 5,823 lines (approximately 3.4% by line count;
  ratio is a warning, not an acceptance metric).
- `Runtime/`: 82,359 lines; `Runtime/Replay/`: 23,806 lines / 28 files.
- Largest files: `RunReplayTools.cpp` 4,779; `PhysicsWorld.cpp` 3,982;
  `TestSceneParser.cpp` 3,651; `Init.cpp` 3,495; `ReplayRuntime.cpp` 3,423;
  `UI.cpp` 3,381; `RunInput.cpp` 3,140; `ReplayRecorder.cpp` 3,105.
- 25 source files exceed 1,500 lines; eight exceed 3,000.

## 15.1 Behavioral Test Depth

**Finding:** named gaps remain in manifold reduction, scene asset-instance
round-trip, fault-injection evidence, and mandatory execution of all CPU suites.
Golden physics/DX12 artifacts do not localize failure.

**Owner:** `Agentic/Plans/TODO/behavioral-test-depth.md` plus
`Agentic/Plans/TODO/validation-gate-integrity.md`.

**Closure:** behavioral plan acceptance and validation-gate V1/V2/V5 complete.

## 15.2 Runtime God Object And Mega-TUs

**Finding:** 127 `Run::` methods and 27 `RunInternal.h` includers show that the
earlier file split did not transfer authority. Input and replay UI still build
large callback/boolean graphs around `Run` state.

**Owner:** `Agentic/Plans/TODO/runtime-shell-decomposition.md`, now with five
concrete ownership extractions and deletion proofs. UI/interaction details are
owned by their coordinated plans.

**Closure:** all five runtime-shell extraction deletion proofs pass, the final
logical-`Run` inventory maps every remaining method/field to a permitted thin
shell responsibility, and an independent adversarial ownership review reports
zero credible god-object or disguised shared-state-hub findings. The review
includes all `Run*.cpp` files and extracted owners; shortening `Run.cpp`, moving
state into a broad context, or adding forwarding facades does not close 15.2.
Any credible finding reopens the owning runtime-shell item and blocks this
campaign rather than becoming optional follow-up debt.

## 15.3 Replay Architecture And Size

**Finding:** replay is 23,806 lines and the largest single feature subsystem;
the previous right-sizing owner was deleted while live plans still depended on it.

**Owner:** `Agentic/Plans/TODO/replay-architecture-and-right-sizing.md`.

**Closure:** R0-R5 complete, measurements reconcile, replay business methods
leave `Run`, and retained memory/identity rules are proven.

## 15.4 Comment Boilerplate

Status: Complete 2026-07-10. The 186-file execution checklist and completion
evidence remain in git history; the tip-tree checklist was deleted according to
the master-plan completion convention. Duplicate mental-model paragraph groups
and the generic validation-gate glossary entry reached zero. Changes were
comment-only and `git diff --check` passed.

## 15.5 Window Encapsulation

Status: Complete 2026-07-10. Native window/device/dimension state is private
behind accessors and acquire/release helpers. Cross-module Window `->m_` reads
reached zero. Project filters, fast validation, and full validation passed at
the completed checkpoint; details remain in git history and reports.

## 15.6 Stale Plan References And Common Include Hygiene

**Finding:** the earlier plan named one `Common.h` comment, but the reconciled
inventory found 87 missing-plan reference rows across 86 source/test/tool files
and 25 missing target paths. `Common.h` also retains `<stdexcept>` under the
engine exception ban.

**Comment-only owner:**
`Agentic/Plans/TODO/stale-plan-reference-cleanup-15.6-checklist.md`, generated
from the exact tracked inventory with one checkbox per scoped file.

**Include owner:** `Agentic/Plans/TODO/runtime-shell-decomposition.md` E3.
Removing `<stdexcept>` from `Common.h` is not part of the comment-only checklist
and requires the Common.h broad gate.

**Closure:**

- [ ] 86/86 checklist files inspected and checked.
- [ ] Zero source/test/tool references to nonexistent plan paths.
- [ ] Comment cleanup diff is comments/documentation only and `git diff --check`
  passes.
- [ ] Common include cleanup lands separately with `tools\validate_full.bat`.

## Review-Gap Closure

This campaign closes only when 15.1-15.3 owning-plan acceptance criteria are
complete, 15.6 is complete, and the master inventory removes this file. Do not
derive a percentage from delegated work; use each owning plan's checked count.
The final master-plan ownership review must also cover the current high-fan-in
and mega-module inventory, including `PhysicsWorld`, `Init`, `ReplayRuntime`,
`ReplayRecorder`, `RenderBackendDX12`, `TestSceneParser`, `UI`,
`GameModelCollection`, and any newer equivalent hotspot. Campaign closure
requires a committed final ownership-review report and zero credible god-object
findings in scope; a cohesive large file is acceptable only with recorded
ownership/invariant evidence.
