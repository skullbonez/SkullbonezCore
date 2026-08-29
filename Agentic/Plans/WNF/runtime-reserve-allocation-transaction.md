# Runtime Reserve Allocation Transaction

Date: 2026-08-29
Status: WNF — owner requested plan only; restore to `TODO/` only by explicit
owner decision. 0/4 tasks complete.
Impact area: Core allocation policy, Physics capacity commits, Replay retained
growth, Prediction retained growth, focused allocation tests, and cumulative
allocation validation.
Owner: Core allocation policy owner
Priority: Parked readability and misuse-resistance follow-up
Commit name: `RESERVE_TRANSACTION`

## Owner Direction

Replace the repeated allocation-phase, reserve-owner, and approved-growth scope
sequence with one composite transaction. Keep `RuntimeReserveAllocator` generic:
this plan does not introduce owner-specific pools, allocators, or container
types.

This plan is parked under `WNF/`. It grants no production-edit, baseline, or
validation-policy authority until the owner moves it to `TODO/` and registers
it in `Agentic/Plans/MASTER-PLAN.md`.

## Problem

A replay-approved allocation currently requires callers to assemble three RAII
objects in the correct order before the allocation:

```cpp
RuntimeAllocationScope allocationScope( RuntimeAllocationPhase::Replay );
RuntimeReserveOwnerScope ownerScope( owner );
RuntimeReserveGrowthScope growthScope( owner, RuntimeReservePhase::Replay, result );
values.reserve( requestedCapacity );
```

The variables look unused because the allocation hook consumes their ambient
thread-local state. The code is correct only while all three lifetimes overlap
the intended allocation:

1. `RuntimeAllocationScope` publishes the lifecycle phase.
2. `RuntimeReserveOwnerScope` publishes the owner used for attribution.
3. `RuntimeReserveGrowthScope` installs and later releases the one-use grant.
4. The allocation hook observes the combined state and consumes the granted
   bytes.
5. Destruction must restore grant, owner, and phase in reverse order.

That relationship is not represented by one type. A caller can omit or reorder
a scope, insert another heap allocation into the approval window, or make the
approved allocation after one scope has expired. Review must reconstruct the
protocol from local variable lifetime at every site.

## Current Production Inventory

The current tree has 14 direct `RuntimeReserveGrowthScope` allocation sites.
The count is descriptive inventory for this plan, not a permanent source-count
gate.

| Area | Current sites | Allocation family |
|---|---:|---|
| `Physics/PhysicsFixedList.h` | 1 | Scene-load backing commit; Replay consumes an already-open outer grant |
| `Physics/PhysicsSceneVectorReserve.h` | 1 | Scene-load vector backing; Replay consumes an already-open outer grant |
| `Physics/PhysicsWorld.cpp` | 1 | Aggregate solver-snapshot vector growth |
| `Runtime/Replay/ReplayRecorder.cpp` | 2 | Recorder sample and delta vector growth |
| `Runtime/Prediction/ReplayPredictionReserve.h` | 2 | Generic prediction vector and batched frame-payload growth |
| `Runtime/Prediction/ReplayPredictionReserve.cpp` | 1 | Prediction engine or retained-object backing transaction |
| `Runtime/Prediction/ReplayPredictionArchive.cpp` | 1 | Archive candidate object backing |
| `Runtime/Prediction/ReplayPredictionSolverEvidenceStore.cpp` | 3 | Frame, contact, and pipeline evidence segments |
| `Runtime/Prediction/TrajectoryStore.cpp` | 2 | Trajectory-record and point backing |

Phase-only scopes used for lifecycle attribution and owner-only scopes used
without a new grant are different operations. They are not migration targets
unless source review proves they participate in the same approved-allocation
transaction.

## Goal

Introduce one generic Core RAII type, provisionally named
`RuntimeReserveAllocationScope`, that establishes the complete allocation-hook
context for an approved reserve operation. Production call sites should read as:

```cpp
RuntimeReserveAllocationScope allocation{ owner, phase, result };
values.reserve( requestedCapacity );
```

The type must enforce this invariant:

> From successful construction until destruction, the calling thread exposes
> one coherent phase, owner, and grant to the allocation hook; destruction
> closes the grant before restoring the previous owner and phase.

The composite remains generic over registered owner and phase. Replay policy,
owner caps, growth counts, exhaustion behavior, and allocation-hook accounting
remain owned by the existing allocator.

## Design Decisions

### One invariant owner, not a parameter bag

The composite is legitimate because member construction and destruction order
enforce a rule callers can currently break. It owns no borrowed subsystem
object, callback pack, or broad context. Its only retained state is the three
existing scope objects required to establish and restore the allocation-hook
transaction.

Declare members in activation order:

1. allocation phase;
2. reserve owner;
3. approved growth grant.

C++ destroys members in reverse order, so the grant closes before the owner and
phase are restored. The implementation must not reproduce that ordering with
manual destructor calls or Boolean cleanup branches.

### Preserve the generic allocator

The composite belongs in `Core/Allocation` and accepts
`RuntimeReserveOwnerHandle`, `RuntimeReservePhase`, and
`RuntimeReserveGrowthResult&`. It must not include or name Replay, Prediction,
Planning, Physics, or another upper-layer feature contract.

### Preserve approval and accounting semantics

This is an API-cohesion refactor, not new allocation authority. It must preserve:

- the private one-use grant identity;
- exact granted-byte consumption by the real allocation hook;
- unused pending-byte release;
- nested-scope restoration;
- thread-local isolation;
- behavior when allocation measurement is disabled;
- existing owner, phase, hard cap, growth count, and failure policy;
- aggregate grants that intentionally cover several backing allocations; and
- existing allocation headers and free-time accounting generations.

The composite must not request growth itself. Callers continue to build their
domain request, handle denial according to their current policy, and construct
the transaction only around the approved allocation operation.

### Keep narrow scopes for genuinely narrow work

`RuntimeAllocationScope` remains the lifecycle-attribution primitive.
`RuntimeReserveOwnerScope` remains available for attribution that does not open
a new growth grant. `RuntimeReserveGrowthScope` may remain as the composite's
low-level implementation and focused-test seam, but production allocation
owners must not assemble it beside the other scopes once the composite exists.

Do not add a regex count ratchet or allowlist to enforce that review rule.

## Non-Goals

- Do not add bounded memory resources, `std::pmr` containers, arenas, or custom
  STL allocators.
- Do not replace the generic reserve-owner registry with owner-specific pools.
- Do not alter any owner name, phase, cap, measured high-water, growth limit,
  exhaustion rule, or row in `ReplayReserveInventory.h`.
- Do not change which allocations are permitted after gameplay starts.
- Do not redesign `RuntimeReserveGrowthResult` into a new move-only capability
  token in this plan.
- Do not widen the transaction to unrelated phase-only render, worker, capture,
  diagnostic, ImGui, or Tracy scopes.
- Do not change container capacities, growth formulas, allocation ordering, or
  the number of allocations covered by an aggregate grant.
- Do not change Physics results, Replay serialization, Prediction publication,
  allocation-header layout, or public binary contracts.
- Do not refresh Physics, Replay, visual, performance, or other baselines.

## Tasks

### RAT0 — Add the composite contract and focused negative controls

- Add `RuntimeReserveAllocationScope` beside the existing reserve scope types in
  `Core/Allocation`.
- Compose the existing scopes as members in phase, owner, grant order; rely on
  reverse member destruction for cleanup.
- Delete copy construction and copy assignment. Review whether move operations
  should also be explicitly deleted so a live thread-local transaction cannot
  change lexical owner.
- Document the coherent-context and destruction-order invariant beside the
  type. Do not repeat allocator policy already owned by the file header.
- Add focused tests proving:
  - the requested phase and owner are visible inside the transaction;
  - the previous phase and owner are restored afterward;
  - an exact approved allocation consumes the grant once;
  - an unused grant releases all pending bytes;
  - nested transactions restore the outer transaction;
  - a wrong owner or phase cannot manufacture approval;
  - guard mode `Off` still consumes and accounts an approved grant; and
  - two threads cannot observe each other's transaction context.
- Retain focused lower-level scope tests where they prove the composite's
  implementation primitives rather than caller ceremony.

Acceptance:

- One named object establishes the complete approved-allocation context.
- Tests fail if member order is changed, restoration is omitted, the token is
  reusable, pending bytes leak, or context becomes process-global.
- No allocation owner, cap, phase, or privilege changes.

### RAT1 — Migrate Physics and Replay allocation owners

- Replace direct owner/growth scope assembly in `PhysicsFixedList` and the scene
  vector reserve helper with the composite while preserving SceneLoad behavior.
- Replace the solver-snapshot aggregate grant window with one composite and
  preserve the exact set and order of vector allocations covered by that grant.
- Replace both replay-recorder vector growth windows with the composite.
- Preserve the recorder's Capture-owned cold path; it must continue bypassing
  the retained Replay grant.
- Confirm that nested Physics reserves during Replay still consume the outer
  approved owner and do not register or request a second Replay privilege.

Acceptance:

- Physics and Replay callers contain one visibly bounded transaction around
  each approved allocation sequence.
- Solver snapshots and recorder storage retain their exact owner, cap, request
  bytes, failure behavior, and growth-counter order.
- SceneLoad and Capture allocations retain their existing attribution.

### RAT2 — Migrate Prediction allocation owners and remove production ceremony

- Replace direct three-scope assembly in prediction reserve helpers,
  trajectory storage, archive candidate construction, and all three solver-
  evidence segment stores.
- Preserve batched grants that cover multiple frame payloads or object
  allocations; do not split them into per-container growth requests.
- Inspect every remaining production `RuntimeReserveGrowthScope` reference.
  Each must be either the composite implementation or a concrete documented
  reason that the composite cannot represent the transaction.
- Review allocations lexically between transaction construction and intended
  grant consumption. Narrow the transaction window when possible without
  changing allocation order.
- Do not replace phase-only or owner-only scopes merely to make a search empty.

Acceptance:

- No production caller manually assembles phase, owner, and grant scopes.
- Every approved grant window contains only the intended backing allocation or
  the existing explicitly aggregated allocation sequence.
- Prediction cancellation, archive failure, and evidence-bank exhaustion retain
  their current behavior and leave no pending grant bytes.

### RAT3 — Terminal review, validation, and closure

- Run the focused reserve-allocator and prediction-reserve test families first.
- Run changed-source formatting and the compiler-backed source-design check.
- Run the allocation-policy self-test and repository scan.
- Run dependency proof/repository checks and confirm no downward Replay include
  or new growth privilege appeared.
- Compile every affected first-party target before claiming a local lane.
- Run the cumulative Core Allocation, Physics, Replay, and Prediction validation
  mapped by `AGENTS.md`, including performance and replay visual fidelity.
- Run one independent terminal ownership review. It must explicitly check:
  - the composite enforces a real lifetime/order invariant;
  - no broad context, callback pack, owner reach-back, or renamed parameter bag
    was introduced;
  - no allocation authority, cap, phase, or aggregate grant boundary changed;
  - nested and cross-thread behavior remains correct; and
  - production call sites no longer reconstruct the protocol manually.
- After the focused gates and review pass, run
  `tools\agent_validate.bat --plan-completion` exactly once.
- Record every command and meaningful result in the closing commit body. If an
  inherited baseline mismatch is reproduced, preserve it as evidence and do
  not refresh the oracle.

Acceptance:

- All mapped focused and terminal gates pass, or an inherited failure is
  reproduced and attributed without changing a baseline.
- Independent review reports no blocking ownership, lifetime, accounting,
  dependency, or evidence finding.
- Move or delete the completed plan according to the live plan convention and
  reconcile `MASTER-PLAN.md` and `SessionState.md` in the closing change.

## Exact Validation Map When Activated

Use the smallest focused commands available for the named test families, then
the cumulative repository gates required by the touched files:

```bat
tools\validate_tests.bat
python tools\check_source_design.py --repo . --files <changed-source-paths>
python tools\check_allocation_policy.py --self-test
python tools\check_allocation_policy.py --repo .
python tools\check_dependency_graph.py --check-proof AGENTS.md
python tools\check_dependency_graph.py --repo .
tools\validate_perf.bat
tools\validate_physics.bat
tools\validate_replay_visual_fidelity.bat
tools\validate_fast.bat
tools\agent_validate.bat --plan-completion
```

Do not run the terminal command until RAT0-RAT2, their focused evidence, and the
independent review are complete. Do not repeat validation already included by a
cumulative command merely to inflate the evidence list.

## Acceptance Criteria

The plan is complete only when all of the following are true:

- One generic Core transaction represents the coherent phase/owner/grant
  lifetime.
- The type's member order mechanically guarantees grant, owner, then phase
  cleanup.
- Every production growth-grant allocation site uses the composite or carries
  a reviewed concrete reason why it cannot.
- Callers still own request construction and domain-specific denial behavior.
- No owner, cap, phase, growth privilege, allocation count, allocation order,
  serialized value, Physics result, or baseline changes.
- Exact-byte consumption, unused-byte release, nesting, guard-off behavior, and
  thread isolation have focused negative controls.
- Allocation, dependency, Physics, Replay, Prediction, performance, and terminal
  validation evidence is recorded.
- Independent review is blocker-free.

## Reactivation Condition

Move this file from `WNF/` to `TODO/` and register it in
`Agentic/Plans/MASTER-PLAN.md` only when the owner explicitly asks to implement
the composite reserve-allocation transaction. Until then, agents must ignore it
for plan selection and make no production changes from it.
