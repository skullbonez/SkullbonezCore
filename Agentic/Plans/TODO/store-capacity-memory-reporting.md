# Store Capacity Memory Reporting

Date: 2026-07-26
Status: NOT STARTED — drafted from the 2026-07-26 from-source architecture
review of `nightrunner-26th-JUL-26` at tip `35f6de4e` and the owner's same-day
request to track memory more closely. Registered in `MASTER-PLAN.md` on
2026-07-26 as plan 3 of the Architecture Follow-Up Campaign Round 5. Starts
after `scene-sized-store-capacity` closes. 0/4 phases complete.
Impact area: `Core/Allocation/RuntimeReserveAllocator.*`, `Physics/` store
registration, `UI/UITabMemory.cpp`, scene-unload diagnostics
Owner: core allocation + physics
Priority: Medium-High — the owner cannot judge whether a capacity is right
without seeing what the scenes actually use.

## Problem And Evidence (measured 2026-07-26)

The engine already has the right registry and almost none of the reporting.

`Core/Allocation/RuntimeReserveAllocator.h:82-96` defines
`RuntimeReserveOwnerDesc` with `ownerName`, `subsystem`, `initPhase`,
`initialCapacity`, `hardCapacity`, and a `capacityReason` string. The header's
own summary states the intent: "Gameplay owners register fixed capacity so
diagnostics can report their budget and high-water use." The view types carry
`highWaterBytes` (`:143`) and `highWaterCapacity` (`:148`).

What is missing:

1. **The dense physics stores are not registered owners.** They carry an owner
   *name* for fatal diagnostics only — `PhysicsFixedList` takes a
   `const char* ownerName` (`Physics/PhysicsFixedList.h:98`) used solely by
   `FailCapacityExceeded` and `FailPopFromEmpty`. Its `m_highWater` member
   (`:389`) is tracked and then never read by anything outside those two fatal
   paths. So the engine measures high-water use for 25 stores and discards it.

2. **The UI memory tab shows growth events, not capacity.**
   `UI/UITabMemory.cpp` touches capacity only at `:1055-1069`, where it formats
   `oldCapacity->grantedCapacity` for replay reserve *growth events*. There is no
   per-owner row showing current capacity, live count, or high-water — the three
   numbers needed to answer "is this store the right size for the scenes I run?"

3. **Nothing reports at scene unload.** High-water is the only figure that
   distinguishes a store sized correctly from one sized 40x too large, and it is
   observable only across a whole scene session. No unload-time dump exists.

`scene-sized-store-capacity` makes capacity a runtime value per scene. Without
this plan the owner gains a tuning knob with no readout.

## Goal

Every scene-sized and fixed store is a registered reserve owner. For each one the
engine can report owner, subsystem, element size, current capacity, live count,
and session high-water — live in the memory tab and as a dump at scene unload —
without allocating to do it.

## Non-Goals

- No committed memory budget file and no gating on footprint. The owner selected
  per-owner high-water reporting only; a budget ratchet was considered and
  rejected at registration.
- No per-frame allocation break-into-debugger behavior. Considered and rejected
  at registration; the existing post-hoc attribution stays as-is.
- No new allocation during reporting. `RuntimeReserveAllocator.h:26` already
  requires that reporting and hook attribution must not allocate; this plan is
  bound by it.
- No change to the Replay three-owner growth privilege, its hard caps, or its
  inventory.
- No new context/service bag for reporting. Rows are produced by the owning store
  and consumed as a bounded read-only view.
- No behavior change to physics, render, or replay.

## Phases

- [ ] **MR0 — Register every dense store as a reserve owner.**
  Give `PhysicsFixedList` and the runtime-capacity path from
  `scene-sized-store-capacity` SC1 a registered `RuntimeReserveOwnerHandle`
  instead of a bare name string, reusing the existing
  `RuntimeReserveSubsystem::Physics` classification. Every registration supplies
  a real `capacityReason` naming the scene quantity that sizes it — the strings
  come from the SC0 census, not from invention. Keep `m_highWater` as the
  authoritative per-store figure and stop discarding it. Acceptance: every one of
  the stores SC0 censused resolves to a registered owner; handle zero
  (`INVALID_RUNTIME_RESERVE_OWNER`, `:56`) appears for none of them; registration
  performs no allocation outside the startup/scene-load phases.

- [ ] **MR1 — Publish the capacity readout.**
  Add a bounded, allocation-free query that returns one row per registered store:
  owner name, subsystem, element size in bytes, current capacity, live count,
  session high-water, and total resident bytes. Row storage is fixed and owned by
  the allocator; consumers receive a `std::span` of const rows. No consumer may
  retain the span across a scene mutation. Acceptance: focused tests assert row
  count, monotonic high-water, and that a fill-to-capacity-then-clear cycle
  leaves high-water at the peak; the perf gate reports zero allocations
  attributable to the query.

- [ ] **MR2 — Surface it in the memory tab and at scene unload.**
  Add a per-owner capacity section to `UI/UITabMemory.cpp` beside the existing
  growth-event list: capacity, live, high-water, resident bytes, and a
  utilisation figure, sorted by resident bytes descending so the largest store is
  always the first thing visible. Emit the same table to the log at scene unload
  so a headless or automation run leaves the evidence behind. Respect the
  UI/Rendering hard boundary established by `ui-renderer-hard-boundary`: UI
  records draw values only and receives the rows as a detached value snapshot.
  Acceptance: the tab shows `ColliderStore` and `PhysicsBodyStore` rows with real
  numbers on a loaded scene; the unload dump appears in the log for a scripted
  scene-queue run; `UIDrawList` capacity is not exceeded and its measured
  high-water is recorded; the UI dependency gate passes.

- [ ] **MR3 — Reconcile, review, and hand off.**
  Run a three-scene session (200-body, 2,000-body, regression scene) and publish
  the resulting capacity/high-water table as the plan's closure evidence — this
  is the artifact that tells the owner which capacities are still wrong. Complete
  the comment audit for touched files. Obtain one independent review covering:
  does any reporting path allocate, does any store now have two capacity
  authorities, and is any row's `capacityReason` a restatement of its name rather
  than a real sizing rule. Acceptance: review clear; `validate_perf.bat` reports
  zero steady-gameplay allocations; `validate_full.bat` and the UI dependency
  gate pass with no baseline change.

## Dependencies And Decisions

- Hard dependency on `scene-sized-store-capacity`. MR0 registers the capacities
  SC1-SC6 create; running MR0 first would register the 8,192-row constants and
  report nothing useful.
- Depends on `governance-shape-to-judgment-conversion` G1 for MR3's review test.
- The MR3 table is expected to justify follow-up capacity corrections. Those are
  a future owner decision, not a commitment of this plan.

## Acceptance

- Every dense store is a registered reserve owner with a real capacity reason.
- Capacity, live count, and high-water are queryable, allocation-free, and
  visible in the memory tab and the unload log.
- The closure report carries a real three-scene capacity/high-water table.
- Zero steady-gameplay allocations introduced.

## Validation

- `tools\validate_perf.bat` — `Core/Allocation/*` changed.
- `python tools\check_allocation_policy.py --self-test` and `--repo .`
- `tools\validate_tests.bat` — new focused reporting tests.
- `tools\validate_dependency_graph.bat` — UI consumes a new Runtime-produced
  value snapshot.
- `tools\validate_full.bat` — required at the closure gate; `UI/*` and
  `Runtime/*` changed.
