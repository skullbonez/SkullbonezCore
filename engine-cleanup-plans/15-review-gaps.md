# 15 — Review Gaps (2026-07-09 External Review)

Date: 2026-07-09
Status: Proposed
Owner: Architecture cleanup
Source: external top-5 architecture review, measured against the worktree on
2026-07-09. Captures findings **not already owned by an active plan**. Where an
active plan owns part of a finding, the item defers to it and adds only the
missing acceptance.

Measurements referenced below (2026-07-09 worktree): 376 first-party source
files / 145,023 lines; 2,796 test lines; `Runtime/` 68,491 lines (47% of
source); `Runtime/Replay/` 18,147 lines (12.5%); largest TUs:
`RunReplayTools.cpp` 3,699, `PhysicsWorld.cpp` 3,517, `Init.cpp` 3,203,
`UI.cpp` 3,117, `TestSceneParser.cpp` 2,962, `RunInput.cpp` 2,904.

## 15.1 Behavioral test depth, phase 2 (P1)

**Finding:** test-to-source ratio is ~1.9%. The suite covers math primitives,
a determinism harness, and a handful of boundary fixtures; there is no focused
unit coverage for `PersistentContactSolver` stages, `ObjectContactManifold`
reduction, `TestSceneParser` error paths, or replay snapshot round-trips.
Regression safety instead leans on two end-to-end oracles (byte-exact physics
CSV, DX12 screenshot diff) that cannot localize a failure and invalidate
wholesale on any intentional behavior change.

**Relation to active work:** promoted 2026-07-09 to its own plan,
`Agentic/Plans/TODO/behavioral-test-depth.md`, because it is the stated
enforcement replacement for plan 03's apparatus deletion and needs first-class
priority. Track progress there; this entry records the review finding only.

## 15.2 Runtime mega-TU decomposition completion (P1)

**Finding:** the plan-01 Run split moved code into 16+ `Run*.cpp` files that
share private state through `RunInternal.h` (using-declaration dump, loose
constants, inline helpers) — partial-class emulation, not ownership transfer.
The mega-TUs persist (sizes above). `Runtime/` holds 47% of the engine.

**Relation to active work:** this item now executes via
`Agentic/Plans/TODO/runtime-shell-decomposition.md` (sections D and E), which
absorbed the run-shell-extraction and fable-04 mega-file plans in the
2026-07-09 consolidation. Track progress there; this entry records the review
finding only.

## 15.3 Replay code-size right-sizing, phase 2 (P2)

**Finding:** `Runtime/Replay/` is 18,147 lines across 24 files — 12.5% of the
engine, larger than the DX12 backend, with the repo's largest file
(`RunReplayTools.cpp`). Plan 09 closed its checklist, but the subsystem's
footprint remains the largest single feature cost in the codebase. Replay also
carries the only approved runtime-allocation exception, which drives allocator
policy complexity engine-wide.

**Relation to active work:** this item now executes via
`Agentic/Plans/TODO/replay-prediction-and-memory.md` (section C), which
absorbed the fable-03 prediction plan and the replay-memory tuning draft in
the 2026-07-09 consolidation. Track progress there; this entry records the
review finding only.

## 15.4 Comment-standard boilerplate cleanup (P2)

**Finding:** the mandated learning headers have decayed into templating: 35
source files share a byte-identical "Mental model" paragraph and 38 share the
identical "Validation gate" glossary entry; glossaries define generic terms
(CLI, HUD) that teach nothing local. The comment audit gate passes this
boilerplate, so the gate measures presence, not value.

**Relation to active work:** plan 03 step 4.1 relaxes the gate *going
forward*; this item cleans up the *existing* boilerplate.

**Steps:**

- [ ] 15.4.1 After plan 03 step 4.1 lands, sweep the 35+ duplicated headers:
  delete paragraphs that state nothing file-specific; keep genuinely local
  `Concept:`/`Why:`/`Invariant:`/`Hazard:` comments.

**Validation:** comment-only source edits; prove the diff is comments-only. No
repository validation required.

**Acceptance:** no two source files share an identical learning-header
paragraph; remaining headers name only file-specific concepts.

## 15.5 Encapsulation: public member fields crossed module boundaries (P3)

**Finding:** `Window` exposes public `m_s*` fields consumed across module
boundaries — `window->m_sWindowDimensions.x` in `Runtime/Init.cpp` (~line
2976) and `Runtime/Editor/RunEditorTools.cpp` (~line 1575), plus `m_sWindow`
and `m_sDevice` handed to render init. Encapsulation exists by naming
convention only. 21 `m_sWindowDimensions` references repo-wide.

**Steps:**

- [ ] 15.5.1 Give `Window` value accessors (dimensions snapshot, native
  handles); make the `m_s*` fields private.
- [ ] 15.5.2 Sweep for other owners with cross-module public `m_` reads and
  record them here (fix or justify).

**Validation:** `tools\validate_full.bat` (window/init are broad-scope files).

**Acceptance:** no cross-module `->m_` member reads on `Window`.

## 15.6 Dead-pointer hygiene in source comments (P3)

**Finding:** live source comments point at deleted or completed plans:
`Core/Common.h` (~line 90) names "authoritative-plan-02" as the owner of the
`Cfg()` compatibility path — that plan completed and was deleted in the
2026-07-09 consolidation. `Common.h` also still includes `<stdexcept>` under
the exception ban (plan 04 closure item).

**Steps:**

- [ ] 15.6.1 Comment-only pass: retarget stale plan pointers in source
  comments at the surviving owner (for the `Cfg()` path that is
  `Agentic/Plans/TODO/runtime-shell-decomposition.md` E3) or delete the
  pointer.
- [ ] 15.6.2 Remove `<stdexcept>` from `Core/Common.h` when plan 04 closes and
  nothing includes it transitively.

**Validation:** 15.6.1 comment-only, none required. 15.6.2 is a source change:
`tools\validate_full.bat` (Common.h row in the validation map).

**Acceptance:** no source comment names a plan file that does not exist.
