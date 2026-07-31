# Runtime Contract Hygiene — CH0 Exit Contract

Date: 2026-07-31
Plan: `Agentic/Plans/TODO/runtime-contract-hygiene.md`
Branch: `nightrunner-30th-JUL-26`

## Outcome

`Run::Execute` no longer depends on a caller remembering to pair a returned
failure status with a separate `ApplicationExitState` mutation. Direct frame
phase boundaries now return either no value or status-free control/presentation
values. A frame-reachable Lane R failure must call
`ApplicationExitState::RequestPhaseFailure` before its phase returns, and
`Execute` observes the exit latch between ordered phases.

`RequestPhaseFailure` rejects a success value with a Lane F fatal error. For a
failure, it retains the first immutable diagnostic and requests exit. Resolving
platform message code zero therefore cannot turn a phase failure into a
successful process result.

## Phase Audit

| `Run::Execute` boundary | Public result | Failure signal |
|---|---|---|
| automation and input | `FrameInputPhaseResult` control values only | `RequestPhaseFailure` |
| simulation | `FrameSimulationPhaseResult` presentation values only | `RequestPhaseFailure` in frame-reachable failure hooks |
| render preparation | `FrameRenderPhaseResult` presentation alpha only | `RequestPhaseFailure` |
| render-model publication/world draw | detached value or `void` | no returned failure status |
| operator UI | `void` | `RequestPhaseFailure` |
| post-draw diagnostics | `void` | `RequestPhaseFailure` |
| screenshots and completion | `bool` restart control only | `RequestPhaseFailure` |
| presentation | `void` | `RequestPhaseFailure` |
| platform message pump | integer process exit code | `ApplicationExitState::Resolve` |

The remaining startup-time `RequestOwnedFailure` call is outside the frame
phase contract. Existing scene-load `.Ok()` conversions retain their
owner-approved load/skip policy; CH0 does not change Lane R semantics.

## Function Complexity Ownership Review

`RunInputPhase` remains one ordered synchronous input turn from automation and
device sampling through routing and typed command production. Its participants
are `Run`-owned borrows with one frame lifetime; introducing a context or
callback bag would not create a new invariant owner.

`RenderOperatorUiPhase` remains one composition-root presentation phase. It
projects detached render and presentation facts, runs the active operator
surface, submits typed commands, and latches a process failure before returning.
Changing its result to `void` removes a droppable status without moving or
splitting its ownership.

The exact current signatures and full-body digests are recorded in
`tools/function_complexity_rulings.json`.

## Focused Coverage

`TestApplicationExitState.cpp` includes
`Status-free frame phase failure latch cannot resolve as process exit zero`.
It submits a phase failure, resolves message exit code zero, and proves that
exit is requested, the failure is retained, the result is non-success, and the
original owner/message diagnostics survive.

## Validation

| Command | Result |
|---|---|
| `tools\validate_build.bat Profile` | PASS; zero warnings/errors |
| focused CH0 test | PASS; 1 case / 5 assertions |
| `tools\validate_tests.bat` | PASS; 457 cases / 2,424,712 assertions |
| function-complexity strict inventory | PASS; 40/40 triggered bodies ruled |
| compiled-symbol reachability inventory | PASS; 79/79 rows ruled, zero blocking diagnostics |
| `tools\validate_full.bat` | PASS; default repository gate in 590.7 seconds |

The repository formatter touched otherwise unchanged source timestamps, so the
first current-symbol scan correctly rejected stale compiler objects. Explicit
non-destructive Automation, Debug, and Profile rebuilds refreshed all three
object roots; the isolated reachability inventory and final full gate then
passed. No tracked file outside the CH0 scope changed.

## Comment Audit

All 7/7 touched source-bearing files were inspected against the comment style
guide. Learning headers remain complete, and the local contract comments name
the status-free phase boundary, sole failure channel, first-failure invariant,
and process-exit consequence. No file is deferred.

## Independent Review

The read-only CH0 review returned **ACCEPT/CLEAR**. It confirmed that all direct
frame-phase boundaries are status-free, every current process-failure branch
latches before returning, boolean results encode restart control rather than
success/failure, first-failure precedence remains intact, and the focused test
adequately covers the exit-state contract.
