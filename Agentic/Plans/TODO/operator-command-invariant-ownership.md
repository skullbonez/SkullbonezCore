# Operator Command Invariant Ownership

Date: 2026-07-26
Status: IN PROGRESS — OC0 closed 2026-07-27 with the exact eight-edge phase
order, same-frame winner table, operation destinations, and complete acceptance
ledger census. OC1 installed and exhaustively proved the non-copyable,
value-only transaction. OC2 is binding. Drafted from the 2026-07-26 from-source
architecture review of `nightrunner-26th-JUL-26` at tip `35f6de4e`. Registered in
`MASTER-PLAN.md` on 2026-07-26 as plan 9 of the Architecture Follow-Up Campaign
Round 5. Starts after `ceremonial-aggregate-elimination` closes. 2/4 phases
complete.
Impact area: `Runtime/Interaction/OperatorCommandApplier.{h,cpp}`,
`Runtime/App/InputRouter.Interactions.cpp`, `Runtime/App/InputFrame.cpp`,
`UI/UICommands.h`
Owner: runtime interaction
Priority: Medium — this is the campaign's clearest instance of the extrusion
signal `AGENTS.md` defines, and the rule requires it be reported as a design
finding with a proposed invariant owner rather than a parameter reshuffle.

## Problem And Evidence (measured 2026-07-26)

`Runtime/Interaction/OperatorCommandApplier.h` holds, inside
`namespace RunInternal`, three parallel sibling families for applying one frame
of operator UI commands:

**Eight context inputs** — `TornadoUICommandContext:78`,
`PhysicsSleepPolicyUICommandContext:87`, `PhysicsFrictionUICommandContext:95`,
`RuntimePresentationUICommandContext:104`, `CinematicUICommandContext:119`,
`RunSimulationUICommandContext:172`, `RenderDeviceUICommandContext:212`,
`SceneFixedStepUICommandContext:221`.

**Result records** — `TornadoUICommandResult:128`,
`PhysicsFrictionUICommandResult:137`, `RuntimePresentationUICommandResult:145`,
`CinematicTuningUICommandResult:159`, `RunSimulationUICommandResult:196`,
`RunCameraModeUICommandResult:204`, plus `WorldOverrideChange:186`.

**Wide apply free functions** — `ApplyRenderVsyncUICommand`,
`ApplySceneFixedStepUICommand`, `DecodeRunCameraModeUICommand`,
`ApplyRunSimulationUICommands`, `ApplyUIWorldOverride`, and siblings.

`AGENTS.md` states the test directly:

> Three or more sibling input/participant/output structs combined with a wide
> apply free function and ordering/arbitration comments are an extrusion signal:
> the operation needs an invariant owner. Rearranging parameters is not a
> remediation.

And under Reviews:

> Reviews must report the extrusion signal ... as a design finding with a
> proposed invariant owner. A parameter reshuffle does not close that finding.

Three further pieces of evidence that the invariant is real and unowned:

1. **Ordering is load-bearing and lives in comments.**
   `RuntimePresentationUICommandContext:106-108` records "ApplySceneFixedStepUICommand
   owns fixed-step reset; this helper owns presentation and render-config mutation
   plus queued save intent." That is a mid-batch which-operation-wins rule written
   in prose. `SceneFixedStepUICommandContext:222-224` states "The simulation reset
   is immediate so the next frame cannot retain old accumulator state under the new
   tick policy" — an ordering constraint between two sibling operations that no type
   enforces. GV2 and GV3 fixed exactly this shape for scene load and generated
   controls.

2. **The result records exist to report acceptance back to a different owner.**
   `PhysicsFrictionUICommandResult:139-141` and
   `RuntimePresentationUICommandResult:147-149` both state that their flags "report
   accepted UI commands for InputFrame transition recording; they are not
   change-detection flags." So the acceptance ledger is a cross-owner protocol
   carried by seven separate structs.

3. **`namespace RunInternal` is a banned shape by name.** The God-Object Closure
   Rule lists "`*Internal`" among the broad bags that constitute closure failure.
   It survives here as the enclosing namespace for the whole family.

`concrete-parameter-bag-elimination` PB0-PB7 did not census this family;
`ceremonial-aggregate-elimination` CA1 deletes the eight context inputs but is
explicitly forbidden from restructuring the operations, which leaves the
ordering and acceptance-ledger invariant still unowned. That is this plan.

## Goal

One owner applies a frame of operator commands, enforces the order that
currently lives in comments, arbitrates which command wins when two touch the
same value, and produces one acceptance ledger. `RunInternal` is gone.

## Non-Goals

- No parameter reshuffle. Widening or narrowing the apply signatures without an
  owner for the ordering invariant does not close this finding — `AGENTS.md`
  says so explicitly.
- No re-introduction of the eight context inputs CA1 deletes, under any name.
- No behavior change. Every UI tab control must produce the identical observable
  effect, and the acceptance flags `InputFrame` records must remain identical so
  input transition recording and automation reports do not move.
- No inheritance, virtual dispatch, callback pack, or type erasure to express
  command dispatch. The transaction shape GV2/GV3 ratified is the model: values
  plus a phase cursor, borrowed owners expiring when phase calls return.
- No change to `UI/UICommands.h`'s value contracts or the UI/Runtime boundary
  established by `ui-renderer-hard-boundary`. UI keeps emitting typed command
  values; only Runtime's application of them changes.
- No scope on pointer routing, which PB2 already closed.

## Phases

- [x] **OC0 — Census the ordering and arbitration invariant.**
  Enumerate every operator-command apply operation, the state each mutates, and
  every ordering or arbitration constraint currently expressed in a comment or
  implied by call-site sequence in `InputRouter.Interactions.cpp` and
  `InputFrame.cpp`. Identify every case where two commands in one frame can touch
  the same value, and record today's actual winner — measured from source, not
  assumed. Enumerate the acceptance-ledger fields the seven result records carry
  and which consumer reads each one. Acceptance: the exact phase order and
  arbitration rule is written as a specification before any implementation; every
  result flag has a named consumer; any flag with no consumer is listed for
  deletion.
  Closed 2026-07-27. The binding cursor is
  `DeviceAndMode -> PhysicsControl -> RuntimePresentation -> SimulationPolicy
  -> PhysicsMaterial -> WorldPolicy -> CinematicPolicy -> Complete`, with every
  interleaved concrete-owner/GV3 barrier recorded. Every operation, same-frame
  winner, and acceptance consumer is ruled; the sole unused artifact is the
  discarded Boolean return of `ApplyCinematicModeUICommand`. Evidence:
  `../../Reports/2026-07-27/operator-command-invariant-ownership-oc0-census.md`.

- [x] **OC1 — Install the operator command transaction.**
  Add a non-copyable `OperatorCommandTransaction` (name subject to OC0's finding)
  that owns the phase cursor for the OC0 order, makes an out-of-order or repeated
  phase call lane-F fatal, stores values and the cursor only, retains no owner
  across phase calls, and exposes arbitration methods that replace the prose
  which-command-wins rules. Its header names the exact phase-order and arbitration
  invariant per the Invariant Ownership Rule. Acceptance: an exhaustive
  phase-cursor test walks the legal order and asserts a fatal on every illegal
  transition; the transaction stores no long-lived owner pointer; the header states
  the invariant.
  Closed 2026-07-27. `OperatorCommandTransaction` copies the normalized value
  packet, owns one acceptance ledger and the exact eight-edge cursor, and stores
  no owner pointer or reference. The complete 10-by-10 cursor matrix is checked;
  all 82 illegal calls from reachable phases terminate in isolated Lane-F
  children, while the legal walk reaches `Complete`. Evidence:
  `../../Reports/2026-07-27/operator-command-invariant-ownership-oc1-transaction.md`.

- [ ] **OC2 — Move the operations onto the transaction and unify the ledger.**
  Convert every apply operation to a transaction phase. Replace the seven result
  records with one acceptance ledger the transaction produces, keeping exactly the
  fields OC0 proved have consumers. Delete `namespace RunInternal`. Acceptance:
  `rg -n 'RunInternal' SkullbonezSource SkullbonezTests` returns no rows; no free
  apply function over a command context remains; every UI tab control behaves
  identically; the acceptance flags `InputFrame` records are unchanged;
  automation interaction reports are unchanged.

- [ ] **OC3 — Reconcile, review, and hand off.**
  Complete the comment audit for touched files, correcting every header that
  described the deleted families. Obtain one independent review asking: does one
  type now own the ordering and arbitration invariant, is that invariant named in
  a header and exercised by a test, did the sibling families reappear under other
  names, and does the transaction retain any owner. Acceptance: review clear;
  `validate_full.bat`, `validate_dx12_renderer.bat` plus
  `run_graphics_stress.bat 1`, `validate_physics.bat`, and the Automation lane
  pass with no baseline or report-format change.

## Dependencies And Decisions

- Depends on `ceremonial-aggregate-elimination` CA1, which deletes the eight
  context inputs and hands over any operation left wide.
- Depends on `governance-shape-to-judgment-conversion` G1 for OC3's review test.
- Binding prior precedent: GV2's `SceneLoadTransaction` and GV3's
  `SceneGeneratedControlTransaction` are the ratified shape. OC1 must match them —
  private detached record plus exhaustive phase cursor, borrowed owners
  synchronous only.
- Owner-overridable default, agent does not stop: whether the arbitration
  behavior OC0 measures is the *intended* behavior. If OC0 finds a case where the
  current winner looks accidental, the transaction preserves today's behavior and
  the case is reported for a separate owner ruling; it is not silently corrected,
  because that would be an unrequested behavior change.

## Acceptance

- One type owns the operator-command phase order and arbitration, with the
  invariant named in its header and exercised by an exhaustive cursor test.
- One acceptance ledger replaces seven result records; every field has a consumer.
- `namespace RunInternal` deleted.
- Zero behavior change in UI command effects, input transition recording, or
  automation reports.

## Validation

- `tools\validate_tests.bat` — new exhaustive phase-cursor and operator-command
  coverage.
- `tools\validate_physics.bat` — friction, sleep-policy, tornado, and fixed-step
  commands mutate physics settings.
- `tools\validate_dx12_renderer.bat` then `tools\run_graphics_stress.bat 1` —
  vsync, render tuning, water, shadow, and cinematic commands touch the renderer.
- `tools\validate_full.bat` — `Runtime/*` changed; includes the Automation lane
  that consumes the acceptance flags.
