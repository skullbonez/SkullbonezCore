# Cause Hierarchy Scientific Inspector

Date: 2026-08-20
Status: Active; 0/7 phases complete. Bound after `REPOSITORY_CLEANUP`.
Impact areas: Runtime Replay cause-window state and input, Runtime Planning
inspection state and rendering, App input composition, UI presentation, replay
Automation, deterministic screenshots, and tests
Owner: ReplayAuthoring owns the cause hierarchy anchor, size, filtering, and
row selection; ReplayCauseInspection owns the attached detail drawer lifecycle,
tab, animation, and exact detached evidence; App composes typed commands and
camera/transport effects without retaining a second UI owner
Priority: Fourth active queue item; CHUI0 follows RC5
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

## Phases

### CHUI0 - Ratify Pixels, States, And Deterministic Fixtures

- [ ] Verify the committed concept hash and record an implementation-facing
      geometry/type/color/spacing sheet derived from it plus the owner's six
      amendments above.
- [ ] Inventory the current cause-window/drawer layout, input controls, state
      owners, draw order, exact-value sources, screenshot directives, and
      Automation probes. Confirm findings against current source.
- [ ] Define deterministic fixtures for hierarchy-only, opening midpoint,
      Summary, Raw top/scrolled, Iterations, filtered, unavailable, moved,
      resized, and 931 x 643 compact states with fixed seed/window/mouse/frame.
- [ ] Add focused failing tests for the compound layout, one-anchor rule, tab
      footprint, animation endpoints/midpoint, and filtered source-row mapping.

**CHUI0 acceptance:** the visual contract is reproducible from a durable image
and exact state table; tests fail against the old separate/static panel behavior;
no production presentation is changed yet.

### CHUI1 - One Compound Layout And Placement Owner

- [ ] Replace the separate solver-panel placement projection with one pure
      hierarchy-plus-drawer compound layout containing hierarchy, visible
      drawer, target drawer, shared seam, title bars, tabs, content, scrollbars,
      resize handle, and combined bounds.
- [ ] Keep one ReplayAuthoring anchor and resize owner. Dragging either title
      bar moves the same anchor; the drawer stores no x/y coordinates.
- [ ] Clamp closed and open surfaces against normal, compact, resolution-change,
      and edge/corner placements while keeping title, close, and resize controls
      reachable.
- [ ] Publish the exact same control rectangles to renderer, pointer input,
      Automation, and tests.

**CHUI1 acceptance:** closed/open/mid-animation geometry is exact at 1920 x
1080 and 931 x 643; a drag or resize moves the joined surface without relative
drift; no second retained placement or pointer owner exists.

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
