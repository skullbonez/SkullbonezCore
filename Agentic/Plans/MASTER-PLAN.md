# MASTER PLAN

Date: 2026-08-25
Status: Ragdoll Physics Unification and Recorded Interaction Playback Cursor active; 110/118 tasks complete

## Owner Direction

- Real-Time Physics Pacing is complete. `SIM_PACING` SP0-SP2 separate live
  wall-clock scheduling from deterministic render-frame lockstep, preserve the
  fractional wall-clock remainder across capped catch-up, and publish explicit
  dropped-tick/hitch diagnostics. All 716 unit cases and every assertion pass;
  the fast, byte-exact Physics, Automation, DX12, coverage, and terminal plan-
  completion gates also pass. The 44,401-line Physics golden remains unchanged.
  The separately mapped Replay visual gate passes its 18 typed-packet controls
  before reproducing the inherited reveal-0 `header.futureNodeCount` rejection
  of the corrected 201-node/806-record topology against the retired
  200-node/802-record oracle. No Physics, Replay, or visual golden was refreshed.

- Deterministic Collision Modes And Ragdoll Unification was activated by
  explicit owner direction on 2026-08-22 and placed ahead of every existing
  master-plan item for scarce-slot priority and fan-in order, not as a Runtime
  or UI predecessor. `RAGDOLL_PHYSICS` executes FP0 through FP9 in strict
  internal order: correctness bugs first, then deterministic Discrete collision
  and automatic Swept TOI promotion, then the completed ragdoll constraint/
  speculative path. No later Physics phase may bypass an earlier acceptance
  boundary.
  On 2026-08-23 the owner activated FP2 with motion policy version 2: absolute
  travel of `0.1` metres per Physics tick promotes, `0.075` metres demotes, and
  collider thickness does not participate.

- Runtime Boundary Separation And Project Topology was activated by owner
  direction on 2026-08-22. RBS0-RBS7 make Runtime/App a true composition root,
  remove reverse App dependencies and multi-package Runtime cycles, separate UI
  projection/commands from GPU submission and process command application, move
  frame metrics out of Rendering mutation, and adopt an evidence-driven acyclic
  Visual Studio project topology. No project-count or source-line target is
  authorized; separation is judged by ownership, direction, behavior, and
  terminal validation. RBS phases may run beside Physics when their exact
  path-owner and mutable validation-resource leases are disjoint.

- Game UI Component Library Separation was activated by owner direction on
  2026-08-22 with phase-local RBS prerequisites and no Physics predecessor.
  The owner explicitly replaced the whole-plan RBS7 stop with this staged
  concurrency ruling on 2026-08-23.
  UI0-UI2 may run beside Physics/RBS; UI3 consumes RBS4, UI4 consumes RBS5, UI5
  consumes RBS6, and UI6 consumes RBS7. The plan retains the existing
  `SKULLBONEZ_UI` static library as the reusable backend-neutral component
  foundation, moves Skullbonez-specific composition and commands above it, and
  converges owner-local Runtime surfaces on shared component presentation. It
  creates no new production project and does not modify an active RBS-owned
  package before that phase's named output exists.

- Recorded Interaction Playback Cursor was activated by owner direction on
  2026-08-22 and is binding after UI6. RIC0-RIC3 publish and draw a detached
  fake cursor for complete recorded interaction-manifest playback, hide only
  that fake cursor in replayed cursorless states, and prove that the live
  hardware cursor is never hidden, moved, captured, released, restyled, or
  otherwise changed by the feature. The plan does not add pointer history to
  `.skreplay` or change interaction-manifest schema.

- Causal Event Inspection is complete. Its synchronized exact-frame transport,
  dedicated camera follow, bounded manifold presentation, and four-row solver
  panel closed with focused host/policy tests, every ownership inventory green,
  and an independent ownership review finding no second retained transport,
  camera, pivot, input, or diagnostics owner.

- Active Physics plans have standing authority to make their governed golden
  transitions without a per-update prompt or another owner ruling. This covers
  Physics, SkullScope, replay, visual, causal, and performance goldens when the
  Physics phase owns the behavior change; non-Physics work keeps its separate
  owner controls.
  Each transition remains exact-digest and must explain the behavior change,
  land with source and tests, and create a new immutable old/new launch bundle
  under the owning Physics plan artifacts. The schema in
  `Agentic/Plans/Artifacts/README.md` binds golden hashes, first-party game
  executable/DLL paths, sizes, hashes, source commits, dependency scan, and
  launch command. System and third-party binaries are never committed. Blind
  refreshes merely to clear a gate remain forbidden.

- Determinism Envelope Tier-2 Hardening is complete. Physics-visible rotation
  and vector-angle construction now use the repository-owned deterministic math
  owner, the policy checker reports no Physics-reachable CRT transcendental,
  portable Linux diagnostics cover both compilers and three sanitizers, and T8
  proved one shared Debug artifact byte-exact on two fresh hosted Windows
  runners. T3 and T4 retained their direct and full-gate candidate executables
  under `TestOutput/validation/candidates/` for later behavior review; their
  produced CSVs are byte-identical to each other, and no golden was refreshed.
- Source Modernization Sweep is complete. No further work remains.
- Dense Pile Sleep Resolution is complete by owner direction. No further work
  will be performed and no additional baseline or solver change is requested.
- Broadphase Dense Dedup Restoration is complete. The dense pair-dedup bitset
  is retained because its roughly 4 MiB maximum memory cost avoids the measured
  CandidatePairs CPU regression.
- Look Lab Random Style Authoring is closed. No further work remains.
- Catto Divergence Repairs is complete by owner direction. CD1 removed
  manifold-row multiplication
  from position correction; its physics and replay candidates remain preserved,
  the same-scene attribution review is clear, and the owner accepted the
  improved simulation and authorized its physics-derived golden refresh on
  2026-08-17. CD2 unified terrain restitution, removed solver-local quiet
  snapping, preserved the divergence executables, and refreshed the affected
  physics/replay goldens by explicit owner direction. CD3 replaced summed
  convergence with max-row stopping and preserved its deterministic physics and
  replay divergences without refreshing goldens. CD4 added scalar point-joint
  warm starting plus durable v3 solver-checkpoint state and preserved its
  deterministic physics and replay divergences without refreshing goldens. CD5
  retained partial-TOI advancement while deriving Baumgarte and constant-
  friction terms from the participating awake dynamic bodies' local remaining-
  time interval. Its deterministic physics and replay candidates are preserved
  without refreshing goldens. R2 stages (b)-(d) and speculative contacts remain
  outside the active ledger.
- Deterministic Trigonometry Adoption was moved to `WNF/` by owner direction on
  2026-08-18. Its exact-site platform-trig exception policy remains authored,
  but none of DT0-DT7 is selectable until the owner reactivates the plan.
- At-Rest Ball Stability is complete. Its uncapped authoritative witness ends
  only when all three named balls are Physics-asleep; the final semantic oracle
  passes vertical stability, slip, reversal, support/sleep latency, and control
  preservation with deterministic 0/1/4-worker evidence. The terminal plan-
  completion gate passed every phase through DX12 and Automation before the
  mapped 35,303-line protected Physics-oracle stop. No baseline was refreshed.
- Invariant Enforcement And Assertion Hardening is complete. It rebaselined the
  exact repository assertion/invariant worklist, assigned every finding an owner
  and Release-safe enforcement lane, repaired the selected sites, and closed
  with negative proof plus independent review.
  IH1 now validates Terrain height-map dimensions and all derived counts before
  construction, and its Profile witness proves malformed, overflow, exact-
  minimum, normal, and retired 5/2 mismatch behavior without unsafe mutation.
  IH2 adds Release-safe texture and tornado capacity/lifecycle enforcement,
  retains Debug-only lock/math tripwires with focused negative controls, and
  separates mechanical capacity invariants from runtime allocation policy.
  IH3 adds transactional collider rebinding, guarded shape compaction,
  Release-safe disjoint-set/body-store/solver consequence bounds, and a tested
  Debug-only awake-list classifier without changing hot Physics storage.
  Its cumulative 650-test unit gate is green; the fast gate clears its static
  checks and build matrix, with terminal reachability deferred solely by the
  Visual Studio-owned live Profile executable lock.
  IH4 replaces Rendering/DX12/World assert-only safety with embedded production
  epoch, scope, backend-identity, rebuild-lease, and bounded-preview owners.
  Focused Profile lifecycle/capacity proof and warnings-as-errors builds pass;
  its 19/19 touched-source audit has no deferrals and independent post-fix
  review is blocker-free. IH5 makes Run's unique renderer owner its sole
  lifecycle truth, makes owned gesture rejection atomic across every owner and
  capture rule, closes Sky/Profile UI leases at real teardown boundaries, skips
  unavailable memory sampling, and proves SceneWorld handle-map repair. No new
  Run bag, Runtime dependency edge, or Replay growth privilege was introduced.
  IH6 completes the 45-site allocation-comment taxonomy, reclassifies the
  worker threshold as a performance reason, and brings all six substantial
  diagnostic tools under truthful bounded-output/hazard learning headers. The
  exact source/tool checklist is 94/94 with zero deferrals; Related paths and
  glossary ownership are clean, and independent post-fix review is blocker-free.
  IH7 reconciled the final 96/96 source checklist with zero deferrals and the
  terminal inventory at 67 production asserts and 18 reviewed assert-only
  candidates. A focused policy test pins ordinary-versus-cinematic frame-
  resource selection, while Automation and the one-minute graphics stress pin
  the corrected production Sky publication path. The current fast gate passes
  663 tests plus terminal Debug/Profile reachability; Automation, DX12, the
  dependency gate, and all ownership inventories pass. The terminal full gate
  stopped only at the protected 35,303-line Physics CSV mismatch beginning at
  line 1,239; no golden was refreshed. Two independent post-fix reviews are
  blocker-free.
- Cause Hierarchy Scientific Inspector is complete. Its seven phases replaced the
  existing causal window with the owner-approved dark-blue filtered hierarchy and
  turned exact solver detail into a flush-attached Summary / Raw Record / Iterations
  drawer. The drawer animates left from behind the hierarchy, shares one anchor and
  drag/resize owner with it, and never owns an independent placement or vertically
  expanding raw surface. CHUI0 ratified the exact visual/state contract and negative
  controls; CHUI1 landed the one-anchor compound layout, shared clamping, drawer-title
  routing, and Automation rectangles; CHUI2 landed the navy hierarchy styling and
  bounded filtering; CHUI3 added the clipped 180 ms drawer animation and Summary tab;
  CHUI4 landed the grouped Raw Record tab and cold clipboard copy action; CHUI5 added
  the Iterations pipeline stage projection and modular drawer dispatch; CHUI6 passed
  independent rubber-duck review, full unit tests, and terminal validation.
- Full Source Comment Truth Replacement is complete. Its six phases audited all
  843 tracked source-bearing files with standardized line 1 headers, 341 canonical
  glossary definitions, durable Related paths, and strict zero-anomaly governance scans.
- Full Validation Time And Value Audit is complete. Its six phases evaluated all
  26 stages of full validation, parallelized reachability and AST inventories,
  reduced preflight duration by 49% and critical path by 31.7%, while preserving 100%
  of test oracles, coverage floors, DX12 checks, and physics determinism.
- Repository Hygiene Cleanup is complete. Its six phases reclaimed ~79.80 GiB of disk
  space across historical test output, 7 clean detached worktrees, and derived build/IDE
  caches, while removing 10 approved unreferenced tracked candidates and reconciling
  all repository documentation.
- Core Engine Evidence-Driven Code Reduction is complete. Against the clean
  `cc194f9aa` baseline, its six phases corrected the GameUI surface name,
  consolidated the approved DX12, Replay, cold-I/O, and render-lifecycle
  duplication, and removed 70 net production lines (`+303/-373`) without
  approving speculative dead-code deletion. All seven inventories, dependency
  and project gates, 692 tests / 2,537,437 assertions, Automation, physics,
  DX12, the terminal plan-completion gate, and one-minute graphics stress pass.
  Independent review is blocker-free. The unchanged Replay visual oracle still
  rejects the pre-existing corrected 201-node/806-record topology against its
  retired 200-node/802-record baseline; no golden was refreshed.
- Continuous Orbital Forecast was reactivated by owner direction on
  2026-08-17. It follows predicted solver detail and owns the separate
  interactive continuous prediction mode, coherent rolling 120-second path
  window, and observed-stability diagnostics without changing bounded
  `PREDICT`.
- Predicted Solver Cause Hierarchy is complete. High retains exact predicted
  Body -> Manifold -> SolverRow evidence behind the on-by-default timeline
  toggle; Low keeps the lightweight selected-root path, clears and suppresses
  the predicted cause window, and releases every exact-detail bank. The final
  workflow records complete replay/category snapshots on both sides of the
  synchronous High -> Low release and proves the identical positive delta of
  1,925,120 bytes instead of relying on a reconstructed total.
- PSD7's at-rest High -> inspect -> Low -> rebuild -> High workflow passes 19
  assertions through frame 2,094, and its multi-body witness passes six
  assertions through frame 1,002 with three manifolds and three solver rows.
  All seven ownership inventories, 604 tests / 2,484,279 assertions, strict
  replay allocation, four-generation frame-spike, replay artifact, dependency,
  fast, Automation, and DX12 gates pass. The 13/13 touched-source comment audit
  has zero deferred files, and the post-fix independent review is clean.
- The immutable replay visual gate still stops at the inherited reveal-0
  `header.topologyVersion` mismatch after its launcher and 17/17 false-pass
  controls pass. Physics still reproduces the inherited 20,394-line varied CSV
  mismatch beginning at frame 102, and the relative Physics performance sample
  remains noisy while absolute DX12/Physics budgets pass. No baseline was
  refreshed. The terminal `tools\agent_validate.bat --plan-completion` rerun
  passes preflight, every mandatory CPU lane, Automation, and DX12 before
  stopping at that exact inherited Physics mismatch; its verbatim log is
  `TestOutput/validation/PREDICT_SOLVER_DETAIL_PSD7_agent_validate.log`.

- ORBIT_FORECAST OF0 is complete. The scene-authored fixed sun is the primary,
  Earth and Mars are the system-wide core cohort, and the ship is an auxiliary
  whose own orbital-configuration failure stays visible without ending the
  core horizon; numerical health remains globally blocking. Exact radial
  envelopes, sustained positive-energy/outward/radius escape with a 600-tick
  grace, core-only collision blocking, reset/retirement behavior, informational
  conservation drift, and the existing 5.0 ms worker slice plus separate
  frame-admission deadline are ratified in the owning plan. Two fixed-step
  120-second live captures are byte-identical
  at SHA-256
  `F3D71F660228561D155E11511FDF58DBAD8F5EF966765A21B92B711420C2AE62`.
  The two isolated bounded witnesses pass and share their submitted-geometry
  hash, but their private simulation hashes and value fingerprints differ; OF2
  and OF6 explicitly own closure of that pre-existing determinism defect.

- ORBIT_FORECAST OF1 is complete. A Prediction-owned, fixed-capacity all-body
  sample ring now publishes coherent absolute-tick rows through odd/even slot
  versions and one release cursor, exposes chronological one/two-segment wrap
  views, rejects counter rollover, and performs no post-start growth. Its four
  backing reserves reuse the existing `replay_prediction_working_set` owner and
  cap; focused concurrency stress and the complete unit-test gate pass.

- ORBIT_FORECAST OF2 is complete. A lower Prediction-owned producer snapshots
  live body/solver values into a private Physics engine, advances unlimited-
  target whole ticks under the ratified separate frame-admission and worker
  slice clocks, and publishes only complete all-body positions through the OF1
  ring. The focused witness advances through three production-size wraps with
  flat warmed bytes/growth, unchanged live and bounded-PREDICT state, exact
  zero-thread/one-worker tick-1,024 position equality, and joined retirement.

- ORBIT_FORECAST OF3 is complete. Scene authoring now names one explicit fixed-
  capacity stability cohort, setup resolves it to stable IDs, snapshot saving
  round trips the contract, and a Planning-owned analyzer consumes detached
  complete ticks. Numerical publication failures are globally blocking;
  primary/core collisions, inclusive envelopes, and 600-tick softened-energy
  escape block the system horizon; auxiliary orbital failures stay separately
  visible. Informational all-member conservation drift and first-failure
  latching are covered by focused tests without adding a Replay include,
  reserve privilege, Physics row field, or post-start growth path.

- ORBIT_FORECAST OF4 is complete. Planning composes the continuous producer and
  stability analyzer while App owns lifetime, mutual exclusion with bounded
  `PREDICT`, frame admission, joined scene transitions, and shutdown. Typed
  controls and detached status values reach both operator surfaces without a
  second retained owner.

- ORBIT_FORECAST OF5 is complete. Planning converts coherent ring snapshots
  into fixed-capacity, double-buffered generic ribbon and head-marker packets.
  Every configured member uses authored presentation, the newest absolute tick
  is coherent, and chronological downsampling never draws across the physical
  wrap seam.

- ORBIT_FORECAST OF6 is complete. Automation now reports source clock metadata
  separately from deterministic private-simulation hashes and hashes every
  private frame, body, sleep, tornado, contact-completeness, and contact value.
  The exact 14,401-frame workers-0/workers-1 witness matches all component
  hashes and submitted geometry while Replay reserve growth stays flat. The
  independent post-fix review is clean; all ownership inventories, tests,
  dependency/allocation gates, DX12, graphics stress, and performance pass.
  Physics and replay visual fidelity reproduce only the inherited owner-
  controlled CSV and `header.topologyVersion` stops, and no oracle was
  refreshed.

Completed plan files were deleted; git history is the archive.

## Active Plans

| Plan | Commit name | Tasks | Done | File |
|---|---|---|---|---|
| Deterministic Collision Modes And Ragdoll Unification | `RAGDOLL_PHYSICS` | 10 | 4 | `TODO/ragdoll-physics-unification.md` |
| Runtime Boundary Separation And Project Topology | `RUNTIME_BOUNDARIES` | 8 | 8 | `TODO/runtime-boundary-separation.md` |
| Game UI Component Library Separation | `GAME_UI_COMPONENTS` | 7 | 7 | `TODO/game-ui-component-library-separation.md` |
| Recorded Interaction Playback Cursor | `RECORDED_CURSOR` | 4 | 3 | `TODO/recorded-interaction-playback-cursor.md` |

## Parked, Backlog, And Completed Plans

These plan files exist in the repository but are not active work. They are
recorded here because a plan file the ledger never names is invisible to the
governance this document owns: a reader cannot tell whether it was parked
deliberately or dropped by accident. None of the rows below is selectable. A
plan runner may not begin one, and moving a row into the Active table above is
an owner decision, not a run decision.

| Plan | Status | Phases | File |
|---|---|---|---|
| Contact Stack Stability Techniques | Owner-parked 2026-08-02 | 0/7 | `WNF/contact-stack-stability-techniques.md` |
| Deterministic Trigonometry Adoption | Owner-parked 2026-08-18 | 0/8 | `WNF/deterministic-trigonometry-adoption.md` |
| Reversible GPU Fracture Replay | Backlog; blocked | 0/7 | `WNF/fracture-replay-feature.md` |

One detail in that table is recorded:

- `WNF/fracture-replay-feature.md` sits in `WNF/` but its own header reads
  `Status: Backlog`, not the owner-parked wording the other two WNF plans use.
  Parked and backlog are different dispositions, so the file and this row
  disagree with the directory. An owner ruling should settle which it is.

## Binding Order

1. `RAGDOLL_PHYSICS` FP0 through FP9 - close verified Physics correctness bugs,
   make Discrete the deterministic default with automatic Swept TOI promotion,
   prove its correctness and isolated performance, then complete the ragdoll
   joint and late speculative-contact path with independent determinism and cost
   evidence.
2. `RUNTIME_BOUNDARIES` RBS0 through RBS7 - ratify and enforce an acyclic Runtime
   package/project graph, remove every reverse Runtime/App dependency, separate
   native host and frame-metrics ownership, split operator projection/commands
   from GPU submission and process command application, close the logical `Run`
   surface, and adopt only VC project boundaries that strengthen ownership.
3. `GAME_UI_COMPONENTS` UI0 through UI6 - close the component foundation in
   UI0-UI2 while RBS/Physics may continue, then consume RBS4/RBS5/RBS6/RBS7 at
   UI3/UI4/UI5/UI6 respectively; migrate GameUI and owner-local Runtime surfaces
   without adding another production project.
4. `RECORDED_CURSOR` RIC0 through RIC3 - consume the final product-UI and
   submission boundary, publish one detached recorded-pointer presentation
   value, draw a topmost fake cursor across GameUI and ImGui, and prove the
   hardware cursor and native capture path remain completely untouched.

This order allocates scarce slots and orders coordinator fan-in; it is not an
implicit dependency chain. Named phase prerequisites are the edges. In
particular, ready `RAGDOLL_PHYSICS`, `RUNTIME_BOUNDARIES`, and
`GAME_UI_COMPONENTS` phases may occupy separate
worktrees concurrently when their canonical subsystem, unmatched path-owner,
and mutable resource leases are pairwise disjoint.

`CAUSAL_INSPECT` no longer re-steps an old frame to regenerate solver detail, so
it does not consume the tier-2 determinism guarantee and has no ordering
dependency on `TIER2_DETERMINISM`. Its true-slerp camera path is presentation-
only and must be classified by the later T2 gate without moving it into a
physics-reachable Maths owner.

`CATTO_REPAIRS` changes the solver values the causal panel may display, but the
panel reads exact retained values rather than freezing expected numbers. Its
tests must pin field mapping and availability, not pre-Catto impulse values.
`PREDICT_SOLVER_DETAIL` is complete against the bounded prediction owner before
`ORBIT_FORECAST` adds continuous publication. Forecast work must not
feed per-tick solver evidence into its continuous path or infer all-body
presentation from generic Physics force state. `REST_STABILITY` follows both;
their exact-value displays must continue mapping Physics-owned values rather
than pinning pre-repair impulses, activation times, or sleep frames.
`CAUSE_HIERARCHY_UI` is presentation and interaction work above those completed
exact-evidence owners. It may project retained values and compose existing
selection/transport commands, but it must not rebuild evidence, add a second
selection/placement owner, widen Replay reserve privilege, or move feature
contracts into Rendering.
## Portfolio Progress

111/118 tasks complete with Deterministic Collision Modes And Ragdoll Unification
first, Runtime Boundary Separation And Project Topology second, Game UI Component
Library Separation third, and Recorded Interaction Playback Cursor fourth.
Causal C0-C8, Determinism T0-T8,
Catto CD0-CD5, and Predicted Solver Cause Hierarchy PSD0-PSD7 are complete.
Continuous Orbital Forecast OF0-OF6 and At-Rest Ball Stability RS0-RS7 are complete.
Invariant Enforcement And Assertion Hardening IH0-IH7 is complete.
Cause Hierarchy Scientific Inspector CHUI0-CHUI6 is complete. Full Source Comment Truth
Replacement CT0-CT5 is complete. Full Validation Time And Value Audit VTA0-VTA5 is complete.
Repository Hygiene Cleanup RC0-RC5 is complete.
Core Engine Evidence-Driven Code Reduction CR0-CR5 is complete.
Real-Time Physics Pacing SP0-SP2 is complete.
Deterministic Collision Modes And Ragdoll Unification FP0-FP9 is active at 4/10;
FP0-FP3 are complete and FP4 is active. FP2's archived automated transition
retains the prior and new producers, its core/deep Physics gates pass, and its
2,401-tick replay-visual gate passes with one prediction generation, all 200
wall bricks moved, 200 causal nodes, and every registered false-pass control.
Runtime Boundary Separation And Project Topology RBS0-RBS7 is complete at 8/8.
RBS7 closed its allocation rulings, Replay startup continuation, Operator UI
projection ownership, GPU submission, and cross-owner command application.
Exact Runtime package direction is enforced with zero forbidden sites, zero
repair debt, zero reverse-App edges, and no multi-package SCC; the approved
Rendering project owns the exact Rendering closure in an acyclic five-project
production graph. Integration commit `7a3e952d2` passed cumulative
`validate_fast`, the ASSET-001 texture regression at 13/13 assertions, Operator
UI projection at 29/29, and the Debug startup continuation at 88/88 before it
was pushed to `origin/nightrunner-24th-aug`.
Game UI Component Library Separation UI0-UI6 is complete at 7/7. UI6 projects
Prediction-owned reveal bounds and Replay/Planning-owned horizon bounds through
the post-RBS App composition seam without moving policy into Interaction or
DevelopmentTools. Its exactly-once plan-completion gate passed 760/760 cases,
2,685,294 assertions, all coverage floors, Automation, and DX12 with unchanged
baselines; independent reviewer `01a03529-b219-7d82-9550-fef25af0fe31`
returned CLEAN. The inherited causal-depth oracle and historical Physics
performance baseline remain external recorded failures, not UI6 passes or
refresh authority.
Recorded Interaction Playback Cursor RIC0-RIC3 is active at 3/4. RIC2 consumes
the Automation-owned frame value through one Runtime/UI bounded compositor and
one honest App submission edge after GameUI, replay overlays, UI finalization,
and ImGui but before screenshots and Present. The exact unchanged recording and
native captures prove the two-triangle marker appears before right-look,
disappears during captured look, and returns afterward while GameUI remains
minimized; focused RIC0-RIC2 tests pass 9/9 cases and 204/204 assertions. RIC3
terminal audit, independent review, and final validation are next.
Deterministic
Trigonometry is owner-parked and excluded from progress.
