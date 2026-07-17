# Scene Controller Ownership Decomposition — Kill The Relocated God Object

Date: 2026-07-17
Status: Active — 4/7 tasks
Branch: `nightrunner-17th-july` (owner-ratified at T0)
Impact area: `SkullbonezSource/Runtime/Scene/*`, `Runtime/Run*.cpp`,
`Runtime/RuntimeFrameViews.h`, project filters
Owner: runtime scene subsystem

## Problem And Evidence (measured 2026-07-17 at the `main` tip, 0d77d51a4)

The 2026-07-17 hostile review found that the runtime-shell god-object cleanup
relocated authority into `SceneController` instead of eliminating it:

- `SceneController` owns scene queue/navigation, browser state, UI override
  state, the request ring, `SceneEntityStore`, `CameraCollection`,
  `WorldEnvironment`, `SceneTerrain`, `RenderInstanceStore`, perf-pass
  counters, cross-scene pause locks, automation contact/broadphase gates, and
  the entire `Physics::PhysicsEngine`
  (`SkullbonezSource/Runtime/Scene/SceneController.h:299-312`). That is input
  policy, presentation state, physics topology, and navigation UI in one type
  — the exact "extracted owner that absorbs unrelated domains" failure named
  in `AGENTS.md`.
- `SceneController::Load` and `SceneController::ExecutePending` each take
  **22 reference parameters** (`SceneController.h:221-267`) — essentially
  every member of `Run` as a flat list. This is the god-object dependency
  graph with the struct removed. The prior wide-call campaign's ≥12-argument
  conversion threshold makes these two the largest surviving offenders.
- The public parameter names are member-style copies: `m_config`,
  `m_launchOptions`, `m_inputRouter`, `m_renderer` appear as *parameter*
  names in a public header — a frozen copy-paste artifact from hoisting code
  out of `Run`.
- `SceneController.Objects.inl` (132 lines) is textually `#include`d inside
  the class body (`SceneController.h:146`) — the mechanical split pattern
  `AGENTS.md` explicitly rejects for god-object closure.

## Goal

`SceneController` shrinks to a scene lifecycle coordinator: queue/navigation,
load transactions, request ring, and lifecycle events. Cross-cutting authority
moves to concrete owners with typed value boundaries, the 22-parameter
signatures are decomposed into narrow per-domain load participants, and the
`.inl`-in-class splice is eliminated.

## Non-Goals

- No change to `PhysicsSceneObjectId` identity policy or per-subsystem handle
  currency (2026-07-11 binding owner ruling).
- No `SimulationController` or unified entity registry (same ruling).
- No behavioral change: scene load order, physics stepping, replay recording,
  and presentation output are byte/pixel-identical throughout.
- No baseline, golden, or screenshot refresh of any kind.
- No universal context bag, `*Services` type, or callback pack as a
  replacement for the 22 parameters — that is the disease, not the cure.

## Tasks

- [x] T0 — Ownership census and target map. Inventory every `SceneController`
  member, public method, and the full 22-parameter list of `Load` /
  `ExecutePending` with per-parameter usage evidence (which load phase reads
  which owner). Produce the target owner map: what stays (queue, requests,
  load transaction, entity store, physics lifetime), what moves (browser/UI
  override state toward the UI/interaction boundary; automation contact and
  broadphase gates toward the validation/automation boundary), and what each
  load phase actually needs. Owner ratifies the map and the branch before any
  edit. Evidence: dated census table committed under `Agentic/Reports/`.
- [x] T1 — Rename the member-style parameters. Mechanical rename of every
  `m_`-prefixed parameter in `SceneController` public signatures to plain
  names. No behavior change. Gate: `validate_fast`.
- [x] T2 — Decompose the load surface into phase participants. Split scene
  load into typed per-phase borrow structs owned by the scene subsystem (for
  example: config/policy inputs, physics-and-store rebuild participants,
  presentation reset participants, diagnostics/replay notification
  participants), each 4-6 references, constructed at the call site and never
  retained — matching the ratified frame-view convention. `Load` and
  `ExecutePending` drop to at most 6 parameters each. No slice may span the
  complete former list. Gate: `validate_full`.
- [x] T3 — Move browser and UI override state to their consumer boundary per
  the T0-ratified map, with value-only requests flowing back into the scene
  request ring. Gate: `validate_full`.
- [ ] T4 — Move automation contact/broadphase gate state
  (`RequiredContacts`, `RequiredBroadphaseXCells`) behind the automation /
  validation-harness owner per the T0 map, reading physics through the
  existing store boundary. Gate: `validate_full` (plus
  `validate_physics` if any physics-adjacent call order moves).
- [ ] T5 — Eliminate the `.inl`-in-class splice. Re-home the
  `SceneController.Objects.inl` declarations either into the class proper
  (if the T3/T4 shrink leaves a cohesive owner) or into the concrete
  store-coordination owner the census selects. No forwarding facade. Gate:
  `validate_full`.
- [ ] T6 — Independent ownership review and closure. One final independent
  review over the logical `SceneController` module (header, all TUs, the
  former `.inl`, and the new phase participants) under the `AGENTS.md`
  god-object closure rule. Any credible finding reopens the owning task.
  Final gates: `validate_full`; replay-adjacent load-path edits also run the
  one-invocation `tools\validate_replay_visual_fidelity.bat` per MASTER rule
  11. Update MASTER-PLAN and delete this plan on closure.

## Dependencies And Decisions

- T0 owner ratification of the target owner map and branch name is required
  before T1.
- Owner decision at T3/T4 if any member fits no destination cleanly: it stays
  on `SceneController` with a recorded reason rather than entering a bag.
- Zero-baseline-refresh is binding for the whole plan; any physics CSV or
  replay golden diff is a revert, never a fix-forward.

Owner-ratified T0 decisions (2026-07-17): use branch
`nightrunner-17th-july`; keep scene lifecycle/topology state in
`SceneController`; group the 22 synchronous borrows into the four narrow
participant values recorded in the census; move browser/UI override state to
UI-owned `SceneNavigationModel`; move automation gate state to
`RuntimeValidationHarness`-owned `SceneAutomationGateTracker`; and re-home the
cohesive cross-store declarations from the `.inl` into the class header. The
complete evidence is recorded in
`Agentic/Reports/2026-07-17/scene-controller-ownership-census.md`.

## Acceptance

- `Load`/`ExecutePending` at ≤6 parameters; no public `m_`-named parameters
  remain anywhere in the scene subsystem.
- No `#include` inside a class body anywhere in `SkullbonezSource/`.
- `SceneController` member list is scene-lifecycle-cohesive per the ratified
  map; the independent review records zero credible god-object findings.
- All mapped gates pass with unchanged committed baselines and goldens.

## Validation

Per-task gates as listed; final closure runs `validate_full` (the plan
touches `Run*`/`Runtime/*`), plus the replay mega gate when load-path work
touches replay-facing sequencing.
