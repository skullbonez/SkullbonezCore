# Replay Boundary Containment

Status: Active — 2/3 tasks (RB0-RB1 complete; RB2 remains)
Owner: repository owner; registered 2026-07-20 as campaign plan 8 of 8
Evidence: `../../Reports/2026-07-20/engine-architecture-review.md` (finding H)
Ledger: RB0-RB2
Depends on: `dependency-direction-restoration` L2 (the solver-snapshot move
creates the boundary this plan makes permanent).

## Objective

Keep replay contained without shrinking it. Replay (34,594 lines at the
review tip — larger than all of Physics or Rendering) is working, validated,
and guarded by the frame-exact mega gate; its size is an asset, not a bug.
The risks are boundary and privilege creep: replay types leaking back into
physics/render headers, and allocation privileges growing without owner
visibility. This plan writes the boundary into `AGENTS.md`, audits the
current privilege inventory, and closes with grep-enforceable rules — it is
governance and audit, not refactoring.

## Problem / Evidence

Before plan 1, replay types had leaked into solver stage headers
(`ReplaySolverSnapshot.h` included by four physics headers) and
`PhysicsWorld.cpp` included `ReplayRetainedMemory.h`. Replay is the only
subsystem holding a `RuntimeReserveAllocator` growth privilege. The 2026-07-16
replay mass-reduction campaign fixed compilation mass but did not codify a
standing inbound-dependency rule, so nothing stops the leak from recurring.

## Non-Goals

- No replay refactoring, decomposition, TU splitting, or line-count target
  of any kind (the 2026-07-16 campaign's six owner boundaries stand).
- No replay behavior, artifact-format, or schema change.
- No new validation script or regex checker (Governance Review Model bans
  recreating frozen-count gates); enforcement is review-time grep proofs
  recorded in `AGENTS.md`, same as the plan-1 direction rule.
- Any source defect discovered by the RB1 audit spawns a follow-up plan row
  in `MASTER-PLAN.md`; it is not fixed inside this documentation-scoped plan
  unless it is a one-line include deletion.

## Binding Decisions

1. Standing rule for `AGENTS.md`: `Physics/`, `Rendering/`, `Scene/`,
   `World/`, and `Core/` must not include `Runtime/Replay/*`. Replay
   consumes physics state through `PhysicsApi.h`/`PhysicsEngine` public
   surfaces and the physics-owned solver snapshot; it contributes no types
   downward. Grep proof: `grep -rn "Runtime/Replay" SkullbonezSource/Physics
   SkullbonezSource/Rendering SkullbonezSource/Scene SkullbonezSource/World
   SkullbonezSource/Core` returns zero rows.
2. Replay's allocation privilege stays exactly as the Runtime Static
   Allocation Policy defines it: `RuntimeReserveAllocator`-registered owners
   with replay phase check, hard cap, and logged growth counter. The RB1
   audit table (owner, phase, cap, counter, policy comment) becomes the
   reference inventory; adding a registration later requires updating it in
   the same commit.
3. Reviews of replay-touching work check two questions: did an inbound
   dependency appear (rule 1), and did a privilege appear or grow outside
   the inventory (rule 2). Either finding blocks the touching plan, not
   this one.

## RB1 Privilege And Surface Audit (2026-07-21)

The registration census used
`rg -n -C 8 "RuntimeReserveSubsystem::Replay" SkullbonezSource` and confirmed
exactly three `RegisterOwner` initializers. The fixed
`REPLAY_GROWTH_OWNER_POLICIES` table also has three rows; `MainMemoryReplayStats`
has the same three-row shape, and Runtime samples it into memory diagnostics and
Tracy high-water plots. No fourth or hidden Replay registration exists.

| Registered owner | Registration / policy | Phase and cap | Counter and exhaustion evidence | RB1 status |
|---|---|---|---|---|
| `replay_recorder_samples` | `ReplayRecorder.cpp:97`; `ReplayRetainedMemory.h:104-116` | Replay-only request/scope; initial 0; aggregate 32 MiB hard cap; measured high-water 6,206,626 bytes; growth-count limit unbounded but counted | `CopyOwnerStatsByName` publishes active/high-water bytes, growths, failures, high-water capacity, and last frame; denial is fatal because partial retained samples are nondeterministic | Registration accepted. The older nearby “per vector” comment contradicts the aggregate allocator rule and is filed as RB1-F3. |
| `replay_solver_snapshot` | `PhysicsWorld.cpp:205`; `PhysicsSolverSnapshot.h:41-44`; policy row `ReplayRetainedMemory.h:117-121` | Replay-only request/scope; initial 0; aggregate 8 MiB hard cap; measured high-water 1,437,696 bytes; unbounded-but-counted growths | One batched byte request covers all snapshot-vector reserves under the owner/growth scopes; common diagnostics publish the same counters; denial is fatal | Accepted: Physics owns the value snapshot and Replay only triggers capture/restore. |
| `replay_prediction_working_set` | `ReplayPredictionReserve.cpp:45`; policy row `ReplayRetainedMemory.h:108-109,122-126` | Replay-only request/scope; initial 0; aggregate 256 MiB hard cap; measured high-water 211,376,304 bytes; unbounded-but-counted growths | Prediction/frame/trajectory requests use byte capacities and narrow owner/growth scopes; common diagnostics publish counters; denial cancels the prediction build instead of publishing a partial prefix | Registration accepted. Strict-path owner-zero allocations outside these scopes are filed as RB1-F1. |

All three descriptors set subsystem/phase to Replay, `allowReplayGrowth=true`,
and carry owner-specific `capacityReason` text. `RuntimeReserveAllocator`
independently rejects owner mismatch, non-Replay phase, non-increasing or
over-capacity requests, aggregate active-byte overflow, and exhausted growth
limits. The inventory therefore accepts the registrations and their counters;
it does not excuse allocations that occur outside their scopes.

| Downward-facing surface | Current boundary | Reason / policy status |
|---|---|---|
| Solver snapshot value | Physics owns `PhysicsSolverSnapshot`; `PhysicsEngine::CaptureReplaySolverSnapshot` and `RestoreReplaySolverSnapshot` accept that value plus typed body count, and `PhysicsReplaySolverSnapshotView` is a borrowed Physics API view | Accepted. Replay types do not enter Physics; capture/restore ordering and fatal reserve denial remain Physics-owned. |
| Solver implementation | `PhysicsWorld` performs the actual capture/restore and batched reserve behind `PhysicsEngine` | Accepted as internal implementation, not a Replay-owned facade or callback. |
| One-frame render pose | Replay presentation resolves a body, then calls `Rendering::RenderInstanceStore::OverridePose(modelIndex, id, position, orientation, colliders)` directly; no callback or retained command queue crosses downward | Shape accepted: bounded in-place value mutation. Its raw legacy identity parameter is RB1-F2 and must converge to `PhysicsSceneObjectId`. |
| Frame presentation view | `ReplayRenderFrameView` carries frame-local borrowed pointers to selected presentation, solver, prediction, visual packet, and focus-mask values into `Runtime/Render` | Accepted. It is Runtime-to-Runtime composition and cannot reach Replay mutation/scheduling authority. |
| Visual presentation packet | Replay owns immutable `ReplayVisualPacket` spans/metadata; `RuntimeRenderer` consumes the packet synchronously for graph callbacks and validation | Accepted. No `Rendering/` header includes a Replay type; the callback payload is stack-scoped and value-only. |
| Cross-system object identity | Replay defines `ReplayBodyId`; Physics and Rendering retain raw `replayBodyId` fields derived from scene identity | Not accepted under the standing Scene Object Identity Policy. Filed as RB1-F2; serialized scalar compatibility must be preserved during convergence. |

### Filed follow-up findings

- **RB1-F1 — strict live Replay allocation ownership:** the exact
  two-generation prediction/presentation run in
  `TestOutput/agent_logs/gameplay_t3_tornado_prediction_probe_stdout.log`
  reported 41,606 gameplay allocation violations and 41,603 reserve-policy
  violations. The top owner-zero rows occur in Replay, render, and steady
  phases. The ordinary perf guard remains clean because it does not exercise
  this path. Owner: Replay/Runtime presentation. Deletion condition: the same
  strict probe returns zero/zero with complete callsite attribution and mapped
  gates pass.
- **RB1-F2 — legacy replay identity:** `ReplayBodyId` plus Physics/Rendering
  `replayBodyId` fields duplicate the policy-owned `PhysicsSceneObjectId`
  boundary. Owner: Replay with Physics/Rendering value consumers. Deletion
  condition: live cross-system surfaces use `PhysicsSceneObjectId`, model rows
  remain hints only, and existing artifact bytes/schema remain unchanged.
- **RB1-F3 — recorder cap comment:** `ReplayRecorder.cpp` still calls the 32
  MiB cap “per vector” although allocator enforcement and the allowlist make it
  aggregate. Owner: Replay recorder. Deletion condition: source policy comment
  agrees with the aggregate owner cap.

All three findings are owned by the newly registered
`replay-policy-debt-closure.md` plan (RP0-RP3), as required by this plan's
non-goal. RB1 made no source edit.

## Tasks

- [x] RB0 — Codify: add the boundary rule, grep proof, and privilege-
  inventory contract to `AGENTS.md` (adjacent to the plan-1 direction rule);
  cross-link from `Agentic/README.md` hot-path/validation notes if needed.
  Run the rule-1 grep at current tip and record the result (expected zero
  after plan 1 L2; if plan 1 has not landed, record the pre-existing rows as
  plan-1-owned, not new debt). Documentation-only: no repository validation
  required.
- [x] RB1 — Privilege and surface audit: enumerate every
  `RuntimeReserveAllocator` replay registration (owner, phase gate, cap,
  high-water/growth counter, policy comment) and every replay-facing surface
  physics/render expose (solver snapshot capture/restore, render pose
  override queue, presentation packets); confirm each against the allocation
  policy and boundary rules; record accepted surfaces with reasons. Output:
  audit tables committed to the closure report; follow-up rows filed for any
  defect. Documentation-only unless a one-line include deletion is applied
  (then `tools\validate_fast.bat`).
- [ ] RB2 — Closure: reconcile RB0 rule text against RB1 findings;
  independent rubber-duck review (is the rule enforceable as written; is the
  inventory complete); final grep proofs at closure tip recorded in the
  report. Documentation-only: no repository validation required.

## Acceptance

- `AGENTS.md` carries the inbound-dependency rule with its grep proof and
  the privilege-inventory contract.
- RB1 tables are complete: every replay reserve registration and every
  downward-facing replay surface is listed with its policy status; defects
  have filed follow-up rows.
- Rule-1 grep returns zero rows at closure tip (or names only rows owned by
  the still-open plan-1 task).
- Independent review clear.

## Validation Summary

RB0 complete 2026-07-21. `AGENTS.md` now makes Replay an upper Runtime
consumer, forbids `Runtime/Replay/*` includes from Physics, Rendering, Scene,
World, and Core, names the Physics-owned value surfaces replay may consume,
and requires replay-touching reviews to check both downward dependencies and
reserve-privilege drift. The reserve contract requires an inventory update in
the same commit as any registration, cap, phase-gate, or counter-coverage
change. `Agentic/README.md` cross-links the rule from the hot-path guidance.

Exact proof at the RB0 tip:

```powershell
rg -n '^#include[[:space:]]+.*Runtime/Replay/' SkullbonezSource/Physics SkullbonezSource/Rendering SkullbonezSource/Scene SkullbonezSource/World SkullbonezSource/Core
```

Result: zero rows. The diff is documentation-only, so no repository validation
is required. `validate_fast` remains required only if RB1 applies a one-line
include deletion.

RB1 complete 2026-07-21. The tables above reconcile all three Replay reserve
registrations, their phase/cap/high-water/counter/comment contracts, and the
Physics/Rendering-facing solver, pose, and presentation surfaces. The three
registration shapes and bounded value seams are accepted. RB1-F1 through
RB1-F3 are concrete defects, so `replay-policy-debt-closure.md` is registered
with owner, reason, deletion conditions, and validation rather than hiding
them inside this documentation plan. No include deletion was needed; the RB1
diff is documentation-only and requires no repository validation.
