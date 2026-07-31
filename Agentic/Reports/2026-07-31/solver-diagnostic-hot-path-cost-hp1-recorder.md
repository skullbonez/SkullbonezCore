# Solver Diagnostic Hot-Path Cost — HP1 Recorder

Date: 2026-07-31
Plan: `Agentic/Plans/TODO/solver-diagnostic-hot-path-cost.md`
Phase: HP1 — separate counting from recording with exact count preservation
Status: COMPLETE

## Result

`PhysicsPipelineTraceRecorder` now owns one saturated logical event count and
an optional ordered payload list. Both modes stop at
`PHYSICS_MAX_PIPELINE_TRACE_RECORDS` (4,096). Count-only mode retains zero
payload rows; full-record mode retains exactly one complete row per counted
event in producer order.

The allocation contract is unchanged. The recorder keeps the existing
`PhysicsStepDiagnostics.physicsPipelineTrace` reserve-owner identity, the
4,096-row hard capacity, and the 229,376-byte reservation for the current
56-byte record.

## Consumer Selection

| Execution path | Full records | Reason |
|---|---:|---|
| Ordinary Runtime step | No | Replay sample identity reads the saturated count directly. |
| Replay capture | Yes | Solver snapshots, hashes, artifacts, restore, and prediction require ordered payload fields. |
| Pipeline overlay | Yes | `PHYSICS_DEBUG_PIPELINE` visualization consumes payload records. |
| Debug SkullScope frame log | Yes | The diagnostics sink enables payload retention inside `BeginStep`. |
| Direct PhysicsEngine/prediction engine default | Yes | Preserves the pre-HP1 behavior unless a Runtime sequencer explicitly selects count-only mode. |

`Run::TickPhysics` samples Replay capture and overlay presentation before the
fixed-step loop and selects the next-step mode through the Physics owner. Debug
SkullScope remains sink-owned and ORs its live frame-log state into the same
selection. No configuration macro changes count behavior.

## Producer Closure

Every producer identified by HP0 reaches the recorder:

- `PhysicsStepDiagnostics::RecordPipelineStage` covers PhysicsWorld commits,
  narrowphase events, terrain events, and persistent-solver side effects.
- `PhysicsBroadphaseStage` borrows the recorder and uses `CanRecord` plus
  `Record` at its existing producer positions.
- `PhysicsSleepController` borrows the recorder and records sleep decisions and
  transitions at its existing producer positions.
- `PersistentContactSolveTransaction` still receives the recorder-owned
  remaining count capacity, so its local bounded side-effect list preserves the
  same saturation point before PhysicsWorld commits it.

There is no remaining producer-side push into the former raw diagnostics list.
Replay presentation and the determinism sample now call
`PhysicsEngine::ReadPipelineRecordCount`; full-record consumers continue to call
`ReadPipelineTrace`.

## Exactness Guards And Coverage

The focused recorder test drives both modes below, at, and 17 rows beyond the
4,096 ceiling. It proves:

- both modes report exactly 4,096 after overflow;
- both report zero remaining capacity;
- count-only mode retains no rows;
- full mode retains 4,096 rows;
- stage, bodies, feature ID, point, normal, and all three scalar fields survive;
- `BeginStep` resets count and retained rows in both modes.

Replay snapshot capture fails loud if a caller selected count-only mode but then
asks for full solver state. Restore rejects an over-ceiling payload and restores
the full-mode count from the accepted ordered rows. This prevents a missing
consumer declaration from silently producing an incomplete Replay snapshot.

## Validation

Focused checks:

- Profile recorder coverage: 1 case, 62 assertions, pass.
- Debug recorder coverage: 1 case, 62 assertions, pass.
- Profile and Debug consumer-selection coverage: 1 case, 7 assertions in
  each configuration, pass.
- Existing Physics sleep coverage: 6 cases, 47 assertions, pass.
- Allocation-owner regression after preserving the original reserve label:
  1 case, 6,320 assertions, pass.

Commit-gate validation:

- `tools\validate_fast.bat`: pass.
- Formatting: 575 source files and 320 headers clean.
- Project filters: 791/791 rows, zero errors.
- Dependency graph: 27 include rules, one content rule, one project rule,
  zero findings.
- Aggregate inventory: 85/85 gated rows ruled, zero unruled.
- Wide-signature and function-complexity inventories: pass.
- Profile and Debug builds: pass with zero warnings/errors.
- Release `SKULLBONEZ_PHYSICS.vcxproj`: pass, proving the configuration-neutral
  recorder and producer surface compiles in the shipping Physics library.
- Unit tests: 455/455 cases and 2,423,400/2,423,400 assertions.
- Compiled-symbol reachability: pass after the three trivial recorder methods
  were made header-inline instead of receiving artificial retain rulings.

The first complete unit run correctly rejected a renamed reserve-owner label.
The implementation restored the existing owner identity; no allocation ruling
or expected inventory was changed.

An additional full Release solution build reached and produced
`Release/SKULLBONEZ_PHYSICS.lib`, then stopped in unchanged Release-only
Runtime/UI test surfaces: unused `UiTextPass` parameters and
`ImGuiEditorOwner.h` references to unavailable `DevelopmentUiMode`. Those files
are outside the HP1 diff and the direct Release Physics target passes.

## Comment Audit

`Agentic/Skills/comment-style-audit/skill.md` was applied to all 13 touched
source-bearing files. Result: 13 checked, zero deferred. The audit updated the
step-diagnostics learning headers for count-only/full-record vocabulary and
invariants, documented the Runtime selection edge, and pinned the mode contract
in focused test-file invariants.

## Independent Review

The required independent ownership review accepted HP1 with zero blockers
after the focused coverage was strengthened. It confirmed the producer and
consumer routing, Runtime and Debug selection edges, direct/prediction default,
Replay guard, allocation contract, saturation/order/field assertions, and clean
ownership inventories. End-to-end Replay, overlay, SkullScope, and byte-exact
Physics equivalence remain explicitly assigned to HP3.

## Deferred To HP2

HP1 deliberately preserves producer payload construction. Count-only mode now
discards that payload centrally, but producer-side record fills, vector loads,
and the solver-iteration `sqrtf` remain. HP2 owns hoisting the mode branch and
eliminating those constructions without changing the count established here.
