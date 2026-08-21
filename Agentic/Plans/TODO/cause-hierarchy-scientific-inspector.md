# Cause Hierarchy Scientific Inspector

Date: 2026-08-20
Status: Active; 2/7 phases complete. First active queue item by owner direction.
Impact areas: Runtime Replay cause-window state and input, Runtime Planning
inspection state and rendering, App input composition, UI presentation, replay
Automation, deterministic screenshots, and tests
Owner: ReplayAuthoring owns the cause hierarchy anchor, size, filtering, and
row selection; ReplayCauseInspection owns the attached detail drawer lifecycle,
tab, animation, and exact detached evidence; App composes typed commands and
camera/transport effects without retaining a second UI owner
Priority: First active queue item; CHUI2 follows completed CHUI1
Commit name: `CAUSE_HIERARCHY_UI`

## Goal

Replace the existing causal window's current grey, dense presentation with the
owner-approved dark-blue Cause Hierarchy, then turn the solver detail surface
into an attached inspector drawer that animates horizontally out of the
hierarchy only after a row is selected. The hierarchy and drawer form one
compound movable window: there is one retained anchor, dragging either title
bar moves the whole visible surface, resizing the hierarchy changes the shared
height, and the drawer has no independent placement state.

The implementation must look like the approved concept rather than merely
copying its information architecture. Typography, dark-blue surfaces, cyan
selection, lime/cyan/orange node semantics, tab geometry, table density,
spacing, borders, opacity, and readable numeric alignment are acceptance
requirements. Raw evidence and iteration detail occupy tabs inside a fixed
drawer footprint; neither expands the surface downward.

## Owner-Approved Concept Reference

The visual reference is committed beside this plan:

- `Agentic/Plans/TODO/cause-hierarchy-inspector-concept.png`
- SHA-256:
  `7BE01EB203D15B4E3F41D4E5E1D0B0B78EC8EB296C6A602405ABC7AE019C71E0`
- Size: 1,891,274 bytes

The concept controls the visual language, drawer/table treatment, and overall
density. The owner's later directions below are authoritative where the image
differs:

1. The permanent right-hand panel is titled `CAUSE HIERARCHY` and replaces the
   existing causal window in its current position.
2. The hierarchy retains a bounded filter field and filter controls.
3. `Solver row 16`, not its parent manifold, is the selected row in the example.
4. The drawer is flush-joined to the hierarchy with a shared border seam; there
   is no gutter and it must visually emerge from behind the main window.
5. The hierarchy and open drawer drag as one compound surface.
6. The raw record is a tab, not an accordion or vertically expanding section.

The concept PNG is planning evidence, not a production runtime asset. It is
deleted with this completed plan after final screenshots and closure evidence
are retained in Git history. Source `Related:` blocks must not cite it.

## Non-Negotiable Interaction Contract

### Closed State

- The default surface is the existing right-side causal window footprint,
  restyled and retitled `CAUSE HIERARCHY`.
- It exposes a fixed-capacity text filter, a funnel control, and compact
  `ALL`, `PREDICTION`, and `CONTACTS` filters without hiding the tree's root.
- No empty solver panel consumes viewport space. A muted footer may state
  `Select an evidence row to inspect`.
- The hierarchy remains movable and resizable through its existing input owner.

### Selection And Drawer Motion

- Selecting a transportable evidence row immediately begins opening the drawer
  to the left while the exact-frame transport/detail request proceeds. Pending
  detail uses a stable loading/availability surface; panel geometry never
  resizes when evidence arrives.
- The drawer uses a time-based ease-out over a ratified 160-200 ms interval.
  Opening progress derives from total elapsed time, not accumulated frame deltas,
  so variable frame rate cannot change the terminal geometry.
- At progress zero the drawer is concealed behind the opaque hierarchy. It moves
  left to its final width while the hierarchy draws above the overlap. The
  visible leading edge, border, shadow, text, and controls may not bleed through
  the hierarchy during the transition.
- Closing reverses the same path. `Escape`, the drawer close button, selection
  cancellation, unavailable-mode transitions, and the established inspection
  exit path close the drawer without moving the hierarchy anchor.
- The open surface is one compound rectangle for clamping and dragging. Dragging
  either title bar changes only the hierarchy anchor; the drawer rectangle is a
  pure projection from that anchor, shared height, drawer width, and animation
  progress. No second x/y pair, host pointer, callback pack, or placement owner
  is allowed.
- At the 931 x 643 owner viewport, the target 380 px hierarchy and 520 px drawer
  fit flush with reachable outer margins. Narrower viewports shrink the drawer
  deterministically to its tested minimum before shrinking the hierarchy; the
  surface never stacks vertically or leaves either title/close control
  unreachable.

### Tabs And Fixed Footprint

- The drawer header identifies the selected evidence (`SOLVER ROW 16`, frame,
  feature, and bodies) and owns exactly three tabs: `SUMMARY`, `RAW RECORD`, and
  `ITERATIONS`.
- `SUMMARY` opens by default for a new selection. Switching tabs changes only
  the content viewport; drawer width, height, anchor, and animation state remain
  stable.
- `RAW RECORD` is a grouped, internally scrollable property table. `ITERATIONS`
  is a compact internally scrollable solver-stage/iteration table. Each tab
  retains its own bounded scroll offset and resets only when the selected exact
  evidence identity changes.
- Tabs that lack honest evidence are visibly disabled or show the established
  availability feedback. The UI must not synthesize discarded rows or infer
  values from current Physics state.

## Visual Fidelity Contract

- Use the concept's near-opaque dark navy hierarchy and drawer surfaces. Busy
  world/debug geometry must not bleed through enough to reduce text contrast.
- Primary text is soft white, secondary evidence is cool grey, selection and
  focus are cyan, Prediction nodes are pale lime, Manifold nodes are cyan, and
  Solver Row nodes are orange. Red remains reserved for failure.
- Use one-pixel rules, restrained radii, no glow, no ornamental gradients, and
  one continuous joined outline. The shared seam must read as one instrument,
  not two unrelated floating windows.
- Body labels remain readable at the owner's compact viewport. Numeric values
  use a monospace face only where alignment helps; labels remain in the normal
  UI face. Values align by column and display explicit units.
- Selected rows receive a filled dark-cyan surface plus a bright cyan left rule.
  Hover is distinct but weaker. The selected row must not be truncated before
  its identifying name.
- Use local inspector presentation or existing palette roles. Do not globally
  restyle unrelated widgets merely to obtain the concept's dark-blue appearance.
- Fixed controls must not resize on hover, selection, loading, availability,
  value width, or tab changes.

## Data And Ownership Contract

- ReplayAuthoring remains the sole retained owner of cause rows, hierarchy
  placement, hierarchy pointer gestures, filtering text/mode, filtered source-
  row mapping, and selection.
- ReplayCauseInspection remains the sole retained owner of exact detached solver
  detail, selected detail identity, drawer lifecycle, active tab, animation
  timing, and tab scroll offsets.
- App sequences row selection, exact-frame transport, camera focus, clipboard
  action, and typed close/tab/filter commands. It does not retain duplicate
  hierarchy or drawer state.
- Layout and hit testing share one pure compound-surface projection. Rendering,
  input, Automation, and screenshot directives consume that projection rather
  than independently recreating rectangles.
- Filtering is bounded and allocation-free in steady runtime. A text match keeps
  matching rows and their ancestor path, preserves original depth/identity, and
  maps selection back to the exact source row. Filter chips combine with text
  filtering deterministically.
- No new Replay reserve registration, post-start growth privilege, Physics hot
  row field, downward Replay include, feature-specific Rendering contract, or
  owner reach-back is permitted.

## Content Mapping

### Summary

Summary uses exact values already retained for the selected row:

- metric cards: penetration, accumulated normal impulse, tangent impulse
  magnitude, and normal effective mass;
- contact basis: numeric world-space `n`, `t1`, and `t2` triples with simple
  colored component bars made from ordinary 2D lines/rectangles, not a 3D
  contact-plane renderer;
- row dynamics: normal/tangent effective masses, bias velocity, friction limit,
  accumulated impulses, and warm-start state;
- explicit units and sign guidance close to the values they explain.

Do not reproduce the illustrative concept's unsupported ERP, CFM, separate
static/dynamic friction, or infinite impulse-clamp rows. Shared solver policy is
shown only if the exact policy snapshot is already available through an honest
detached value seam; it must never be inferred from defaults.

### Raw Record

`RAW RECORD` groups the exact selected contact into stable sections:

- Identity: row index, feature, body A/B, manifold point count, source frame,
  and evidence identity where useful.
- Geometry: point, `rA`, `rB`, penetration, terrain/contact facts.
- Contact basis: normal, tangent 1, and tangent 2 numeric triples.
- Solver values: normal/tangent masses, bias, separation bias when retained,
  friction limit, and applicable rolling/spin values.
- Accumulated impulses: normal, tangent, rolling, and spin values that the exact
  retained record actually carries.
- Flags: warm started, terrain, resting support, tangent friction, normal-
  coupled friction, and sleep inhibition.

Rows use aligned names/values, subtle alternating fills, section rules, a thin
scrollbar, and a typed `Copy record` action. Copying is an explicit cold user
action routed through the existing platform/clipboard boundary; it is not a
steady-runtime allocation exception hidden in the renderer.

### Iterations

`ITERATIONS` projects exact pipeline records for the selected feature into
columns such as stage/iteration, normal delta, accumulated normal impulse,
tangent magnitude, clamp state, and relevant writeback. Warm start, solver
iterations, cache store, position correction, and velocity writeback remain
distinguishable. The table is preferred over tiny wrapped prose or a chart that
would require a new plotting owner.

## CHUI0 Ratified Visual Contract

The committed concept was re-read from disk on 2026-08-21. Its SHA-256 is
`7BE01EB203D15B4E3F41D4E5E1D0B0B78EC8EB296C6A602405ABC7AE019C71E0`,
its byte length is 1,891,274, and its decoded pixel extent is 1509 x 1042.
The six owner amendments above override the concept's illustrated 12-pixel
gutter and illustrated Manifold selection.

### Geometry, Type, Color, And Spacing Sheet

| Role | Ratified runtime value |
|---|---|
| hierarchy target width | 380 px |
| drawer target width | 520 px |
| compact viewport | 931 x 643 px |
| normal viewport | 1920 x 1080 px |
| joined seam | 0 px gutter; one 1 px shared rule |
| drawer motion | 180 ms cubic ease-out, sampled from total elapsed time |
| title / body / evidence type | 14 px / 12 px / 10 px normal face |
| aligned numeric type | 12 px monospace face |
| outer padding / dense row gap | 12 px / 6 px |
| major / minor vertical rhythm | 12 px / 6 px |
| corner treatment | restrained 6 px outer radius; 3 px controls; square shared seam |
| hierarchy/drawer surface | sampled navy `#001322`, near opaque |
| alternate property row | sampled navy `#081929` |
| selected row fill / left rule | sampled `#003156` / `#00A4EC` |
| Prediction / Manifold / Solver Row | sampled `#A6CE7B` / `#21B1D3` / `#EE6A35` |
| text | soft white primary, cool-grey secondary; red reserved for failure |

The drawer starts fully behind the hierarchy at progress 0.0. Its full 520 px
rectangle translates left as `1 - (1 - t)^3`, while its visible clip and the
compound bounds grow by the same eased width. At progress 0.5 the eased value
is exactly 0.875 and 455 px of the target drawer is exposed. Summary, Raw
Record, and Iterations reuse the same title, tab strip, content, scrollbar, and
compound rectangles; only the content projection and bounded scroll value
change.

### Current-Source Inventory And Negative Controls

| Concern | Current owner/path | CHUI0 finding |
|---|---|---|
| anchor, size, rows, selection | `Runtime/Replay/ReplayAuthoringPackets.h` and `ReplayAuthoring` | one retained hierarchy anchor already exists; no filter or tab state exists |
| hierarchy layout and controls | `Runtime/Replay/ReplayOverlayLayout.*` | four shared draw/hit rectangles exist for panel, title, content, and resize |
| pointer routing | `Runtime/Replay/ReplayAuthoringCauseTreeInput.cpp` | ReplayAuthoring owns move, resize, scroll, and row selection through InputRouter |
| exact detail and transport | `Runtime/Planning/ReplayCauseInspection.*` | Planning owns detached evidence and transition state; the legacy detail panel is separately projected |
| App sequencing | `Runtime/App/ReplayScrubberTools.cpp` | App composes selection, restore, pause, camera, and exit effects without owning geometry |
| draw order | `Runtime/Planning/ReplayOverlayRenderer.cpp` | hierarchy draws first and the detached solver panel draws afterward, so it cannot emerge from behind the hierarchy |
| exact values | `Runtime/Planning/ReplayCauseInspection.*` plus Physics-owned contact/pipeline records | current detached samples already carry honest values; no Physics inference is needed |
| Automation | `Runtime/Automation/InteractionAutomationController.cpp` and `InteractionAutomationReportWriter.cpp` | row selection and current window/detail facts exist; tab/filter/compound facts do not |
| screenshot probes | `causal_inspection_visual_qa.json`, `causal_tree_retarget_visual_qa.json`, and `multi_body_prediction_cause_visual.json` | deterministic legacy captures exist but do not cover the complete CHUI state matrix |

The current 1920 x 1080 capture
`TestOutput/interaction/multi_body_prediction_cause_high.bmp` proves the
negative control: the solver surface has a 10 px gutter, a height unrelated to
the 520 px hierarchy, generic grey at 0.78 opacity, no tabs, and a selected
Manifold rather than Solver Row. The current 1125 x 640 capture
`TestOutput/interaction/causal_inspection_compact.bmp` proves that unavailable
detail still consumes a detached rectangle and overlaps another UI surface.
Neither image is a golden and neither was refreshed.

### Deterministic State Fixtures

All states use Automation DX12, `--fixed-step --vsync off --shadows off
--hide-top-text --replay on --replay-seconds 2 --seed 424242`, the authored
`interaction_replay_prediction_harness.scene.json`, and a pinned mouse at
`[8, 631]` unless a row/control coordinate is named. CHUI5 owns the missing
typed Automation actions; CHUI6 owns the final captures. The exact state table
is the source for those actions rather than screenshot-time manual input.

| Fixture | Window | Stable frame/state | Pinned facts |
|---|---|---|---|
| hierarchy-only | 1920 x 1080 | frame 91, before selection | drawer progress 0; no tab; selected row -1 |
| opening-midpoint | 1920 x 1080 | selected source row 3; elapsed 90 ms | drawer progress 0.5, eased 0.875; Summary |
| summary-open | 1920 x 1080 | selected Solver Row source row 3; detail available | progress 1; Summary; scroll 0 |
| raw-top | 1920 x 1080 | same exact evidence identity | progress 1; Raw Record; scroll 0 |
| raw-scrolled | 1920 x 1080 | same exact evidence identity | progress 1; Raw Record; scroll 6 rows |
| iterations | 1920 x 1080 | same exact evidence identity | progress 1; Iterations; scroll 0 |
| filtered | 1920 x 1080 | text `solver row 16`, Contacts chip | source rows `[0, 1, 2, 3]`; selected source row 3 |
| unavailable | 1920 x 1080 | selected Solver Row, exact detail unavailable | stable fixed drawer; honest unavailable feedback |
| moved | 1920 x 1080 | hierarchy anchor `[1180, 140]` | drawer target `[660, 140, 520, H]`; one compound drag owner |
| resized | 1920 x 1080 | hierarchy `[1180, 140, 430, 500]` | drawer `[660, 140, 520, 500]`; shared height |
| compact | 931 x 643 | hierarchy `[528, 84, 380, 520]` | drawer `[8, 84, 520, 520]`; compound `[8, 84, 900, 520]` |

The focused CHUI0 tests use these numbers as an executable target model and
also assert that the legacy detached/static panel fails the joined seam,
shared-height, 180 ms endpoint, tab-footprint, and filtered source-mapping
contracts. Later phases replace each negative control with the production
projection while keeping the same target values.

## Phases

### CHUI0 - Ratify Pixels, States, And Deterministic Fixtures

- [x] Verify the committed concept hash and record an implementation-facing
      geometry/type/color/spacing sheet derived from it plus the owner's six
      amendments above.
- [x] Inventory the current cause-window/drawer layout, input controls, state
      owners, draw order, exact-value sources, screenshot directives, and
      Automation probes. Confirm findings against current source.
- [x] Define deterministic fixtures for hierarchy-only, opening midpoint,
      Summary, Raw top/scrolled, Iterations, filtered, unavailable, moved,
      resized, and 931 x 643 compact states with fixed seed/window/mouse/frame.
- [x] Add focused failing tests for the compound layout, one-anchor rule, tab
      footprint, animation endpoints/midpoint, and filtered source-row mapping.

**CHUI0 acceptance:** the visual contract is reproducible from a durable image
and exact state table; tests fail against the old separate/static panel behavior;
no production presentation is changed yet.

**CHUI0 evidence (2026-08-21):** the concept hash, pixel extent, sampled palette,
geometry, ownership inventory, negative screenshots, and eleven deterministic
states are recorded above. A focused Profile build passed with warnings as
errors, and `SKULLBONEZ_TESTS.exe --test-case="Cause hierarchy inspector*"`
passed 5 cases / 48 assertions. The legacy-panel negative control rejects its
10 px gutter, unrelated height, 1.5-second transition, and 0.78 opacity. The
touched-source comment audit is 1/1 with zero deferred files. Production
presentation is unchanged.

### CHUI1 - One Compound Layout And Placement Owner

- [x] Replace the separate solver-panel placement projection with one pure
      hierarchy-plus-drawer compound layout containing hierarchy, visible
      drawer, target drawer, shared seam, title bars, tabs, content, scrollbars,
      resize handle, and combined bounds.
- [x] Keep one ReplayAuthoring anchor and resize owner. Dragging either title
      bar moves the same anchor; the drawer stores no x/y coordinates.
- [x] Clamp closed and open surfaces against normal, compact, resolution-change,
      and edge/corner placements while keeping title, close, and resize controls
      reachable.
- [x] Publish the exact same control rectangles to renderer, pointer input,
      Automation, and tests.

**CHUI1 acceptance:** closed/open/mid-animation geometry is exact at 1920 x
1080 and 931 x 643; a drag or resize moves the joined surface without relative
drift; no second retained placement or pointer owner exists.

**CHUI1 evidence (2026-08-21):** Planning now publishes one compound layout for
the hierarchy, moving/visible/target drawer, seam, both title bars, close, tabs,
content, both scrollbars, resize handle, and current/target compound bounds.
ReplayOverlayLayout clamps a generic attached-left extent while ReplayAuthoring
retains the only anchor, size, drag offsets, and resize state; App routes drawer
title hits into that owner. Renderer, pointer routing, Automation JSON, and tests
consume the same rectangles. Exact normal/compact, midpoint, edge/corner,
drawer-title drag, and resize cases pass 7 cases / 64 assertions; the existing
solver scroll case passes 29 assertions. Profile app/tests and the Automation
solution build with warnings as errors. `validate_tests` passes 670 cases /
2,522,048 assertions. The touched-source audit is 11/11 with zero deferrals and
Related paths are clean. `validate_fast` stopped only on its formatter stage;
the mandated formatter repair is applied and the rerun is deferred to the next
visible UI checkpoint by owner priority.

### CHUI2 - Cause Hierarchy Visual System And Filtering

- [ ] Restyle the existing causal window to the concept's dark-blue `CAUSE
      HIERARCHY` surface, node colors, hierarchy rules, selected/hover states,
      footer, opacity, spacing, and readable two-line evidence rows.
- [ ] Add a fixed-capacity filter field, funnel control, and compact All /
      Prediction / Contacts filters through ReplayAuthoring's existing input
      owner. Text entry, focus, clear, escape, and pointer capture must compose
      with camera and scrubber input.
- [ ] Build a bounded filtered row projection that keeps ancestors, preserves
      source identity/depth, performs no steady-runtime growth, and keeps a
      selected match visible without reparenting it.
- [ ] Cover empty/no-match, long-input truncation, Unicode/unsupported input
      handling, chip/text combinations, selection mapping, scroll clamping, and
      mode-transition clearing/persistence.

**CHUI2 acceptance:** the hierarchy alone matches the approved closed-state
appearance, filters exact rows without allocation or identity drift, and leaves
the viewport unobstructed until selection.

### CHUI3 - Attached Animation And Summary Tab

- [ ] Add Planning-owned open/close animation timing driven by total elapsed
      time and a tested ease curve. Selection opens immediately; close and
      retargeting remain generation-safe.
- [ ] Render the drawer behind the opaque hierarchy during motion, then as one
      flush joined surface with continuous outline/seam, shared height, stable
      tab bar, and no content bleed.
- [ ] Implement the Summary metric cards, numeric contact-basis component bars,
      row-dynamics table, availability/loading states, units, and faithful
      exact-field mapping.
- [ ] Ensure the visible animation rectangle owns hit testing: concealed or
      occluded controls cannot steal clicks, and rapid open/close/retarget
      sequences cannot leave pointer capture or stale detail active.

**CHUI3 acceptance:** selecting a row produces a smooth joined slide-out that
looks like the concept at rest, behaves consistently at variable frame rates,
and shows only exact selected evidence.

### CHUI4 - Raw Record Tab And Copy Action

- [ ] Implement the grouped Raw Record property table with aligned numeric
      columns, alternating fills, explicit units, thin rules, bounded internal
      scrolling, and the full retained-field mapping above.
- [ ] Keep drawer geometry invariant while switching Summary <-> Raw Record;
      preserve/reset scroll only under the ratified selected-identity rules.
- [ ] Route `Copy record` as a typed cold action and serialize one stable,
      complete, locale-independent record without retaining clipboard/service
      authority in Planning or Replay.
- [ ] Cover unavailable/truncated evidence, maximum field widths, terrain and
      object rows, warm/cold rows, flags, copied text, and mouse-wheel ownership.

**CHUI4 acceptance:** clicking `RAW RECORD` produces the concept's readable
scrolling table inside the same drawer footprint and copies exactly the values
shown without changing runtime ownership.

### CHUI5 - Iterations, Responsive Polish, And Interaction Closure

- [ ] Implement the Iterations table from exact selected-feature pipeline
      records, including warm start, per-iteration deltas/accumulators/clamp,
      correction, cache, and writeback stages.
- [ ] Finish tab hover/active/disabled states, scrollbar behavior, keyboard tab
      traversal, close/escape behavior, filter focus, cursor order, and
      interaction priority against camera, scrubber, and world picking.
- [ ] Validate compact width/height, maximum row/value density, selected-row
      visibility, text truncation/tooltips, DPI/resolution changes, dragging from
      both title bars, resizing while open, closing while dragged, and reopening
      at the retained anchor.
- [ ] Add Automation assertions for closed/open state, tab, animation progress,
      compound bounds, selected source row, filter result count, scroll state,
      and no stale drawer after High -> Low or scene transition.

**CHUI5 acceptance:** every interaction state is usable and visually stable at
the owner viewport and normal desktop sizes; the drawer never detaches, grows
downward, or blocks unrelated input outside its visible bounds.

### CHUI6 - Screenshot Fidelity, Stress, Review, And Closure

- [ ] Capture every CHUI0 deterministic state using fixed window rect, fixed
      scene/seed, pinned UI mouse, and stable screenshot frame. Inspect the
      actual pixels for hierarchy fidelity, joined seam, animation midpoint,
      opacity, alignment, clipping, contrast, scrollbars, tabs, and cursor order.
- [ ] Compare the final open Raw state side by side with the committed concept
      and record every intentional difference. Any unexplained generic-grey
      styling, detached-window appearance, wrong selection, missing filter,
      text wall, bleed-through, or spacing regression reopens its phase.
- [ ] Run the cumulative focused unit, UI, Automation, replay-visual, allocation,
      dependency, DX12, and stress gates from the validation map. Do not refresh
      an owner-controlled visual or Physics oracle without explicit approval.
- [ ] Audit every touched source-bearing file with the comment-style skill and
      obtain an independent ownership plus visual QA review. A second placement,
      input, drawer, tab, exact-evidence, or clipboard owner is blocking.
- [ ] Reconcile `MASTER-PLAN.md` and `Agentic/SessionState.md`, delete this plan
      and concept PNG under repository convention, and retain screenshots,
      commands, elapsed times, review findings, and validation evidence in the
      closing commit and approved validation artifact paths.

**CHUI6 acceptance:** the actual running UI visibly matches the owner-approved
dark-blue concept and amendments, all deterministic states are pixel-reviewed,
all mapped gates pass or stop only at a preserved owner-controlled inherited
oracle, independent review has no blocker, and the plan closes without a new
owner or baseline refresh.

## Validation Map

| Change | Required pre-commit/PR evidence |
|---|---|
| Pure layout, animation, filter, tab, text-projection, and source-row mapping | Focused doctest cases in the owning Replay/Planning test files, then `tools\validate_tests.bat` |
| Runtime Replay/Planning/App/UI source | `tools\validate_fast.bat` plus every focused row below |
| Hierarchy/drawer interaction and Automation directives | `tools\validate_automation.bat` and the deterministic CHUI screenshot scenes |
| UI layout, clipping, cursor, filtering, tabs, scrolling, drag/resize | `tools\validate_ui.bat` |
| Repeated open/close/retarget/drag/resize and pointer-capture behavior | `tools\validate_ui_stress.bat` |
| Replay-facing presentation or submission | `tools\validate_replay_visual_fidelity.bat`; preserve inherited oracle stops and do not refresh a golden without approval |
| DX12-visible overlay rendering or screenshot timing | `tools\validate_dx12_renderer.bat`, then `tools\run_graphics_stress.bat 1` |
| Allocation-sensitive filtering/copy/tab storage | `python tools\check_allocation_policy.py --repo .`; add `tools\validate_perf.bat` if a steady-frame path changes materially |
| Runtime package/dependency changes | `tools\validate_dependency_graph.bat` and the strict allocation/dependency scans included by the fast gate |
| Terminal CHUI6 closure | All cumulative focused gates, comment audit, independent review, then `tools\agent_validate.bat --plan-completion` |

Repository validation is deferred during implementation iteration. Use focused
unit builds, deterministic launches, and screenshot inspection while working;
run the mapped scripts only for task commit/PR preparation or when explicitly
requested.

## Acceptance Criteria

- [ ] CHUI0-CHUI6 are complete with evidence recorded in their commits.
- [ ] The permanent causal window is the dark-blue `CAUSE HIERARCHY` with
      functional bounded filtering and the concept's semantic node colors.
- [ ] No detail drawer occupies space before selection.
- [ ] Selection animates one flush-attached drawer out to the left; closing
      reverses it without moving the hierarchy.
- [ ] One anchor and one gesture owner move the hierarchy and drawer together;
      the drawer has no independent placement or resize state.
- [ ] Summary, Raw Record, and Iterations are fixed-footprint tabs with bounded
      internal scrolling and honest exact-evidence availability.
- [ ] Summary and Raw values map current Physics-owned fields with explicit
      units; unsupported illustrative coefficients are absent.
- [ ] Filtering preserves ancestry, source identity, exact selection, and zero
      steady-runtime allocation.
- [ ] The 931 x 643 owner viewport and normal desktop sizes keep the compound
      title bars, tabs, close control, selected row, and scrollbars reachable.
- [ ] Deterministic closed, midpoint, Summary, Raw, Iterations, filtered,
      unavailable, moved, resized, and compact screenshots have been visually
      inspected against the concept and amendments.
- [ ] No second retained placement, pointer, selection, exact-evidence, tab,
      clipboard, camera, or transport owner was introduced.
- [ ] Touched-source comment audit and independent ownership/visual review have
      no blocker.
- [ ] All cumulative mapped validation passes or preserves an exact inherited
      owner-controlled oracle stop without refreshing it.
- [ ] `MASTER-PLAN.md` and `SessionState.md` are reconciled and this plan plus
      concept PNG are deleted on closure, with Git history retaining the design
      and evidence.

## Non-Goals

- Replacing the cause hierarchy with a frame-by-frame timeline.
- Adding a second free-floating solver window or independent drawer placement.
- Rendering a 3D contact plane, world-space gizmo, or new feature-specific
  Rendering contract for the contact basis.
- Reconstructing solver evidence, re-stepping an old frame, or inferring missing
  values from current Physics state.
- Adding illustrative ERP, CFM, separate static/dynamic friction, or infinite
  clamp values that the exact retained record does not own.
- Changing Physics behavior, solver policy, replay evidence capacity, prediction
  publication, or owner-controlled baselines.
- Globally restyling unrelated UI widgets to obtain this panel's appearance.
- Keeping the concept PNG as a production runtime asset after plan closure.
