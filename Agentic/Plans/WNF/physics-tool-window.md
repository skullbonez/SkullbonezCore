# Physics Tool Window Beside Causal

Date: 2026-09-17
Status: WNF - owner-requested plan only; 0/8 phases complete.
Owner: Runtime UI composition, Physics settings/diagnostics, Planning inspection
Impact area: right dock, operator input, live solver settings, body inspection, debug drawing, prediction/replay, tests
Commit name: `PHYSICS_WINDOW`

## Owner Direction And Scope

Create a dedicated **Physics** window alongside **Causal** in the **top-right
dock space**. Consolidate existing Physics controls there and add real-time
solver parameterisation, impulse visualisation and selected-body properties,
including mass. The supplied Solver2D screenshot is the feature reference;
the window must fit SkullbonezCore's existing UI and ownership model.

This plan stays in WNF until the owner explicitly activates it. It does not
activate the selectable-solver plans, change active portfolio counts, or
authorize implementation, PR creation or merge. On activation follow
`Agentic/Skills/orchestrator/SKILL.md` and recheck current repository instructions.
Drafting this documentation requires no runtime validation.

The deliverable is useful with the current Skullbonez solver (custom PGS-based implementation). Additional algorithms,
independent position-solver iterations and adjustable physics frequency are
explicit dependency/future items below, not hidden requirements for closing
the current-solver window. No nonfunctional algorithm choices may be shipped.

## Dated Evidence And Existing Owners

Read-only inspection on 2026-09-17, refreshed at HEAD `b7f74557a`. These are
source findings, not claims of native runtime acceptance:

| Owner/source | Existing capability and implication |
|---|---|
| `SkullbonezSource/Runtime/UI/GameUI/GameUILayout.h` and `.cpp` | Own right-dock bounds, folded state, Causal controls/detail geometry and Canvas/Editor layouts. Extend these shared bounds for Physics. |
| `SkullbonezSource/Runtime/UI/GameUI/UIWindowInteractionOwner.cpp` and `UI.cpp` | Route and draw dock input; Physics needs peer navigation, focus and clipping here. |
| `SkullbonezSource/Runtime/UI/GameUI/UITabPhysics.cpp` | Existing overlays, contact linger/alpha, ray impulse and projectile speed, gravity, friction, sleep enable, terrain probe and tornado controls. Move their presentation while retaining their command owners. |
| `SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.cpp` | Maps typed overlay commands to Runtime presentation state. |
| `SkullbonezSource/Physics/PhysicsRuntimeSettings.h` and `PhysicsEngine.cpp` | Per-engine settings snapshot and config application; Skullbonez contact iterations default to 12, with slop, Baumgarte bias and position-correction strength. No live solver UI or algorithm selector in inspected settings. |
| `SkullbonezSource/Physics/PhysicsTimestep.h` | Canonical outer tick is 120 Hz. Catch-up ticks are not solver substeps. |
| `SkullbonezSource/Physics/PersistentContactSolver.cpp` | Warm starts, terrain support seeds, contact sweeps, joint interleaving and convergence/early exit. Requested contact iterations need not equal actual combined sweeps. |
| `SkullbonezSource/Physics/PhysicsBodyStore.h` | Stable identity, authored mass, inverse mass, motion and full inertia components. Fixed bodies retain authored mass. |
| `SkullbonezSource/Physics/PhysicsDebugData.h` and `Runtime/Render/PhysicsDebugVisualizer.cpp` under `SkullbonezSource/` | Contact point/normal/tangents and normal impulse; current arrow length mixes penetration and impulse, and tangent lines show directions rather than friction magnitude. |
| `SkullbonezSource/Physics/PhysicsDiagnosticsView.h` | Contact tangent impulses, effective masses, solver statistics and bounded convergence samples. |
| `SkullbonezSource/Runtime/Planning/ReplayCauseInspection.cpp` | Existing recorded contact/body readouts, including body mass, effective contact mass and normal/friction impulses. Reuse semantics without treating recorded values as live state. |
| `SkullbonezSource/Runtime/Render/BroadphaseVisualizer.cpp` | Grid visualisation exists; this is distinct from per-body AABBs. |
| `SkullbonezSource/Runtime/Planning/PhysicsComparisonPanel.cpp` | Existing Solver Lab comparison product. Reuse it for comparisons rather than creating another viewer. |

At activation recheck these findings, staged solver-plan work, hull acceptance,
the actual branch and all dirty files. Do not absorb unrelated edits.

## Placement And Interaction

- Add **Physics** as a peer tab/window beside **Causal** in the existing
  top-right dock. The default arrangement shares the dock, with one peer active
  at a time; it is not a floating OS window or another bottom Tools page.
- Preserve the current Causal entry and Solver Lab's Differences presentation.
  Both Physics and Causal remain directly reachable from the same dock header;
  folding retains access to both. Opening Physics must not launch Solver Lab.
- Reuse dock resizing, clipping, panel transitions and presentation preferences.
  Define Canvas access through the same right-side presentation system; Editor
  uses the dock. Preserve the user's layout rather than switching workspaces.
- Retain Physics section, scroll and overlay preferences across close/reopen
  and layout changes. Persist active peer through the owned preference format
  with migration/default handling. Never persist a live borrowed body pointer.
- Use shared bounds for drawing, hit testing, popup placement and camera input
  exclusion. Clicking, scrolling or dragging controls must not fire a ray,
  select the world or pan a camera behind the dock.
- Switching peers retains Causal selection/transport/camera state. Its detail
  drawer is visible and reserves space only while the corresponding Causal
  presentation is active; hidden content cannot leave an invisible input blocker.
- Replace the old Tools > Phys content with an action opening this window, or
  remove that tab once routing/preferences/tests are migrated. There must be one
  owner for each setting and no second independently retained slider value.

## Window Contents

A compact persistent header shows selected body, source context, tick and run
state. Use four short sections/tabs with a single-column fallback in narrow
docks. Keep Pause, One Tick and Restart reachable without scrolling.

### Simulation

- Pause/resume, exactly one fixed tick while paused, restart, and time scale.
  Route to existing simulation owners; distinguish simulation tick from render
  frame and recorded playback step. Restart follows the existing scene reset
  contract and labels its effect on transient objects/settings.
- Current solver and effective settings. Skullbonez contact-iteration budget is live,
  bounded and integer-valued; show actual completed sweeps and early-exit state.
  Explain any joint iteration floor instead of claiming every body used the
  requested contact count.
- Slop, Baumgarte bias and existing position-correction strength. Separate
  object and terrain settings where they have different owners/meaning.
- Warm-start enable with defined cached-contact, terrain seed and joint
  semantics. Disabling must not retain an undisclosed cached impulse path.
- Existing gravity, object/terrain/rolling friction, sleep enable, ray impulse,
  projectile speed and tornado controls. Add spin friction, restitution
  threshold and sleep speed/frame thresholds through existing numerical owners.
  Keep advanced material/terrain/sleep and tornado groups collapsed initially.
- Display fixed tick frequency and duration (currently 120 Hz / about 8.33 ms).
  Time scale changes playback pace, not this timestep.
- Show pending versus applied values, restore startup settings, and explicit
  save-to-scene/default actions through the appropriate existing persistence
  owners. Dragging a control does not silently overwrite authored files.

### Visualisation

Independent toggles for collider shapes (primitive and hull), body axes, COM,
joint anchors/links, joint error, body AABBs, broadphase grid, contact points,
normals, normal impulses, friction impulses, sleep states, pipeline stages and
terrain probes. Preserve collision colouring, transparency and field visuals.

- Normal-direction arrows have a chosen fixed display length; impulse arrows
  are separate vectors scaled by actual solver impulse. Penetration must not
  inflate a vector labelled impulse.
- Draw normal impulse as `normal * accN`, and friction impulse as
  `tangent1 * accT1 + tangent2 * accT2`, with a documented body/sign convention.
  Publish missing tangent values through Physics-owned detached diagnostics.
- Provide arrow scale, magnitude threshold, numeric labels, legend and contact
  linger. Mark capped arrows and truncation; preserve true numeric values.
  Distinguish current contacts from faded historical impacts and support rows.
- Filter all bodies / selected body and its contacts. Use stable identities for
  body pairs/features; scene reset or removal cannot attach old arrows to a new
  body that reuses a dense index. Bound display history and line capacity.
- Use engine units until the unit convention is verified. Impulse is not force;
  an optional average-force readout must explicitly divide by its actual
  simulation interval. Keep applied test impulses separate from contact impulses.

### Body

- Select through existing picking/selection, with a clear empty/stale selection
  state and optional pin by stable identity. Show body/scene identity and shape.
- Show authored mass, inverse mass, dynamic/fixed and awake/sleeping state.
  A fixed body's finite authored mass must not be presented as zero mass.
- Show world COM, authored origin where available, pose, linear/angular velocity,
  full inertia tensor (including products) and applicable material values.
  Density/volume are shown only when known or validly derived and labelled.
- Show per-contact body pair, penetration, normal and tangent impulse, effective
  normal/tangent mass, warm-start status and friction limit. Body mass and
  effective constraint mass are distinct fields.
- Provide velocity/angular-velocity vectors and readouts; reuse existing edit
  operations if available. Publish values from one coherent body/tick snapshot.
- Reuse ray impulse and add a precise impulse vector plus application point,
  with world/local choice explicitly converted and a COM-to-point lever arm.
  Preview the arrow; apply exactly once through the Physics owner. Fixed bodies
  follow their existing reject/release policy, reported visibly.
- Mass is inspectable in the first body phase. A later phase adds a bounded
  mass-edit operation for supported dynamic shapes: update inertia and inverse
  inertia consistently, wake/invalidate required contact state, preserve undo
  and authored-save semantics, and label unsupported compound/derived cases.
  No raw write to `mass` alone and no editable arbitrary tensor in this scope.

### Statistics

Show physics cost, active/sleeping/fixed bodies, contacts/joints, requested and
actual iterations, warm-start/cache effectiveness, correction magnitude and
bounded convergence history. Separate solver residual/impulse-delta measures
from physical energy; do not call convergence a correctness guarantee.
Expose dropped samples/ticks and unavailable diagnostics. Empty data is not
zero work. Reuse profiler/diagnostic sources and a bounded graph history.

## Numerical And Data Ownership Contract

- UI consumes detached values and emits typed commands. Runtime routes settings
  and lifecycle operations; Physics owns normalization, cache policy and solve
  behavior. Respect existing package directions; do not add upward Physics/UI
  or Rendering/Planning dependencies or per-body UI fields.
- Normalize finite ranges and work budgets once at the numerical owner. The
  current broad config iteration ceiling is not a suitable interactive budget;
  choose and test a practical cap during PW0, showing effective values.
- Apply live numerical changes atomically between completed fixed ticks, never
  during a solve. Establish one precedence rule for startup, scene and session
  overrides. Distinguish harmless display changes from numerical edits.
- Specify which edits retain caches, clear them, wake support islands or need
  restart. Warm-start tests must separately cover prior-contact reuse, terrain
  support seeding and joints; avoid changing default behavior while adding UI.
- Settings changes invalidate incompatible prediction work/publications through
  existing generation ownership. New prediction clones inherit effective world
  settings. Preserve reproducibility through versioned setting-change events or
  the existing supported restart/recording boundary; do not silently lose edits
  from resumable recordings. Resolve this policy before exposing live edits.
- Label Live / Recorded / Predicted context and actual tick. Historical/Causal
  inspection is read-only; never combine live body mass with a recorded impulse
  under one unlabelled state. Missing retained evidence stays unavailable.
- Diagnostic toggles do not affect physics results, sleep or row ordering.
  Use bounded stage-owned snapshots, line storage and history with no new
  post-startup allocation privilege. Hidden windows stop unnecessary UI work.

## Related Plans And Deferred Algorithm Work

| Work | Relationship |
|---|---|
| [Skullbonez foundation](selectable-solvers-01-foundation.md) | Coordinate per-world solver/settings identity if active. Basic current-Skullbonez inspection does not require activating it. |
| [Eight solver implementations](selectable-solvers-02-tgs.md) | Owns eight new algorithms, including temporal substeps, position iterations and algorithm-specific numerical behavior. New PGS is distinct from Skullbonez; no relabelling catch-up ticks or existing sweeps as TGS. |
| [Solver product integration](selectable-solvers-03-integration.md) | Owns selection/restart, persistence, prediction and Solver Lab compatibility across algorithms. When implemented, this Physics window is its settings surface. |
| [Debug panel reorganisation](debug-panel-reorganization-and-sleep-policy.md) | Older proposal puts Physics under Tools and splits its overlays into Diagnostics. This newer owner direction takes precedence for Physics placement. Reconcile that overlap at activation; do not activate its wider tab redesign or new slope-sleep behavior implicitly. |
| [Physics A/B comparison](../TODO/physics-ab-comparison.md) | Reuse current Solver Lab comparison, preserving independent recordings and provenance. Opening Physics does not simulate or reconstruct missing archived evidence. |

The screenshot's PGS, PGS NGS, PGS NGS Block, PGS Soft, TGS Sticky, TGS Soft,
TGS NGS and XPBD are algorithm development, not checkbox plumbing. Only expose
implemented and accepted variants. Independent position iterations require an
actual position-solver loop. Adjustable outer Hz requires a separate timing,
CCD, prediction and replay design; display it read-only in this plan.

All eight additions are required by the related solver campaign. Their implementation remains outside this window plan; Skullbonez stays the default and this window exposes each additional choice only after its owning stage accepts it.

Reference: [Erin Catto, Solver2D](https://box2d.org/posts/2024/02/solver2d/).
The 2D experiment is design guidance, not a ready-made 3D backend.

## Phases And Acceptance

- [ ] **PW0 - Fix ownership and contracts.** Refresh the source inventory; map
  every migrated control, right-dock input/layout owner, snapshot producer and
  affected saved format. Set ranges, cache/wake/restart policy, units, settings
  precedence and recording/prediction behavior. Record bounded memory and work
  budgets plus the dependencies that are implemented versus deferred.
- [ ] **PW1 - Add the docked Physics surface.** Implement peer navigation beside
  Causal, four sections, folding/resizing, preference migration and input capture.
  Migrate every existing Physics-tab control without losing behavior. Retire its
  duplicate content and update shortcuts, tooltips and automation navigation.
- [ ] **PW2 - Add body and contact inspection.** Expose coherent stable-identity
  snapshots for mass/inverse mass, COM, inertia, motion, materials and contacts.
  Add selected/all filtering and context labels. Prove removal/reset, fixed
  bodies and missing recorded evidence cannot show misleading values.
- [ ] **PW3 - Add truthful overlays.** Split points/normals/impulses, publish
  tangent impulses, draw COM, primitive/hull shapes, AABBs and joints. Add scale,
  labels, linger, filtering and capacity reporting. Verify vector magnitudes,
  directions, zero impulses, clipping and unchanged physics with overlays off/on.
- [ ] **PW4 - Add live solver and environment tuning.** Implement bounded Skullbonez
  iterations, stabilization, warm-start policy and advanced material/sleep
  controls at tick boundaries. Wire pending/applied values, reset and explicit
  persistence. Prove clone/prediction and supported replay semantics, including
  setting changes while a prediction worker runs. Keep outer frequency fixed.
- [ ] **PW5 - Add controlled body experiments.** Implement exact one-tick
  transport, precise one-shot point impulses and supported mass edits with
  inertia updates, cache/wake handling, undo and save semantics. Verify centred
  versus off-centre impulses, invalid edits, fixed bodies and restart behavior.
- [ ] **PW6 - Complete statistics and native integration.** Add cost/counters and
  bounded convergence graph; connect to existing Solver Lab and Causal context
  without replacing their owners. Extend Skarness commands/state where missing.
  Complete normal/narrow, Canvas/Editor, four-view and active-Causal UI cases.
- [ ] **PW7 - Terminal validation and closure.** Finish cumulative tests,
  independent numerical/ownership and rubber-duck review, final fixes, screenshot
  inspection and performance evidence. Run the final plan-completion gate,
  leave local Profile built and verify an unchanged no-op rebuild. Reconcile
  MASTER-PLAN and SessionState, repair dependent links, and delete the completed
  plan under repository lifecycle. Deferred algorithms are reported explicitly.

## Required Behavioral Proof

| Scenario | Acceptance evidence |
|---|---|
| Physics/Causal switch, fold, resize, close/reopen | Correct shared dock bounds; independent retained state; no hidden hit blockers or camera/transport mutation |
| Widths 640, 1280, 1680; Canvas/Editor; four views | Readable sections, contained popups/drawers, scroll reachability and camera/picking isolation |
| Dynamic/fixed body, hull COM offset, body removed/reset | Correct identity, mass/inverse mass/inertia and COM; no stale selection |
| Contact at rest, normal impact, sliding contact | Solver-matching normal/friction values; directions distinct from impulses; linger labelled |
| Centred/off-centre test impulse in an isolated fixture | Expected linear momentum change and angular response from actual mass/inertia; exactly one application |
| Supported mass edit and undo | Coherent mass/inertia/inverse values, required wake/cache invalidation, restored authored value |
| Slider drag while running, paused, or predicting | Owner reports effective value and application tick; no partial updates or stale prediction publication |
| Warm starts off/on for objects, terrain and joints | Defined cache/seed policy observed; no hidden retained impulses |
| One Tick with varying time scale/render rate | Exactly one physics tick, then paused; no catch-up burst or repeated held-button action |
| Save/load, restart, recording and prediction | Effective settings/identity preserved by declared format policy; historical data remains immutable |
| Overlays/window off versus on | Same physics outputs in the pinned build envelope; bounded memory/work; explicit dropped diagnostics |
| Solver Lab/Causal active | Existing selection, findings, exact-frame inspection and transport continue to work |

## Validation And Closure Evidence

During implementation use affected builds and focused owning-subsystem tests.
Concentrate heavy suites, independent reviews and final fixes at PW7, subject
to mandatory earlier push-boundary gates. Do not refresh goldens merely to
accept new UI controls; diagnostic-only changes must preserve numerical output.

Terminal commands from repository root, mapped to the actual final diff:

```bat
tools\validate_fast.bat
tools\validate_dependency_graph.bat
tools\validate_physics.bat
tools\validate_physics_deep.bat
tools\validate_perf.bat
tools\validate_ui.bat
tools\validate_ui_stress.bat
tools\validate_skarness.bat
tools\validate_replay_v2_artifact.bat
tools\validate_replay_visual_fidelity.bat
tools\validate_full.bat --plan-completion
```

Extend the existing UI owners `tools/validate_unified_physics_ui.py`,
`tools/validate_ui_side_panels.py`, `tools/validate_causal_viewports.py`,
`tools/validate_four_views.py` and `tools/validate_unified_solver_lab_ui.py`.
Use their current documented arguments and current-build fixtures; register new
acceptance cases in ordinary gates. Reconcile format/persistence and allocation
checks against AGENTS.md. Add `tools\validate_dx12_renderer.bat` and
`tools\run_graphics_stress.bat 1` if renderer/backend changes require them.
Avoid redundant reruns of gates already covered by the terminal umbrella.

For native proof use `Agentic/Skills/skarness/SKILL.md`: inspect capabilities,
extend missing controls/state, bind assertions to identities and effective
values, capture and inspect screenshots, and stop owned sessions. Preserve
commands, settings, executable identity, observations, timings and screenshots
under `TestOutput/skarness/physics-window/`. A command acknowledgement or new
checkbox alone is not acceptance. Report inherited failures separately without
changing archived comparison inputs or weakening assertions.

Any deliberate numerical/default change follows the applicable active Physics
golden-transition policy with retained producers, negative controls and final
mapped gates. Preference/schema changes require owned version migration and
legacy/current/future-format tests. No implementation or validation runs are
authorized merely by this file being parked in WNF.
