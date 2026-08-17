# Predicted Solver Cause Hierarchy

Date: 2026-08-18
Status: Registered; 0/8 phases complete
Impact area: Runtime Prediction and Planning, replay cause-tree UI/input,
prediction archives, retained-memory reporting, tests, documentation, and visual QA
Owner: Runtime Prediction detail retention with Planning-owned causal inspection
Priority: Fourth and last in the active binding order, after `ORBIT_FORECAST`
Commit name: `PREDICT_SOLVER_DETAIL`

## Registration Note

The owner directed registration in `Agentic/Plans/MASTER-PLAN.md` on
2026-08-18, at the bottom of the active order after Continuous Orbital
Forecast. This supersedes the earlier branch-local instruction not to edit the
master ledger.

Plan authoring is documentation-only and requires no repository validation.
Implementation follows the repo-local orchestrator skill when explicitly
started.

## Owner Direction

Prediction has two operator-selectable retention modes:

1. **High detail** is on by default. Prediction retains the exact solver rows
   and pipeline trace required to display a body -> manifold -> solver-row
   hierarchy and the existing solver-detail panel for predicted frames.
2. **Low detail** preserves the current lightweight prediction/replay behavior.
   It should preserve the same selected-root -> contacted-child -> downstream-
   child body hierarchy using the debug-contact topology already retained. Its
   per-body explanation may remain flat/synthetic because this mode is intended
   for capture and playback rather than close Physics inspection.

In ordinary collision/demo prediction there is exactly one root: the selected
object. Its line plays first; when it contacts another body, that body's line is
revealed as a child, and later contacts reveal further descendants. Publishing
every ball as an independent root is a regression and blocks both modes. The
existing all-body-root presentation remains valid only for explicitly qualified
mutual-gravity/space visualization; it must not leak into the ordinary demo.

The bottom replay timeline bar exposes a `HIGH DETAIL` checkbox beside controls
such as `ALT VEL`. It replaces the existing mouse pause button in that exact
layout slot; `P` remains the sole play/pause control. Turning High detail off
cancels the current prediction generation, hides and clears the causal
window/inspection, releases every high-detail buffer capacity, and recalculates
prediction without those buffers. The F6 memory surface must show the released
bytes returning. Turning it on starts a fresh high-detail prediction from the
then-current exact source state.

The owner accepts the additional high-detail memory cost because exact causal
inspection is a defining engine feature. This authorizes a measured, finite
increase to the existing `replay_prediction_working_set` hard cap if the
representative 120-second high-detail matrix requires it. It does not authorize
unbounded allocation, an unstamped cross-frame join, a second growth owner, or
retaining detail capacity after switching to low detail.

## Current Failure And Evidence - 2026-08-18

- `RunReplayPredictionFrame` retains body samples and lightweight
  `PhysicsDebugContact` rows, but not the private frame's
  `PhysicsSolverPersistentContactSample` rows or `PhysicsPipelineRecord` trace.
- `CaptureReplayPredictionFrame` publishes the frame after copying only bodies
  and debug contacts. The private `PhysicsEngine` still owns the richer values
  at that instant, but they are discarded before Planning can inspect them.
- `ReplayPrediction::BuildCauseTreeRows` selects Prediction whenever a coherent
  prediction prefix is visible. Its Prediction branch emits one synthetic
  `PredictionContact` or `PredictionMotion` row below a body and returns before
  the recorded body -> manifold -> solver-row builder runs.
- `EvaluateReplayCauseSolverDetail` requires an exact frame stamp, Manifold or
  SolverRow kind, SolverHistory seek source, a non-prediction row, and matching
  retained contact data. Those guards prevent stale joins, but together reject
  every current predicted row by construction.
- `ReplayRuntime::ApplyCauseInspectionTransition` passes empty contact and
  pipeline spans for a Prediction seek, making the generic unavailable result
  inevitable.
- `ReplayPredictionArchive` schema v3 serializes frame bodies and derived
  future-node topology, but no solver evidence. A loaded high-detail prediction
  cannot preserve exact inspection without an archive schema extension.
- Full `PhysicsPipelineRecord` production is consumer-gated. The private
  prediction engine must explicitly acquire that existing gate for High builds
  and balance it on every finish, cancellation, restart, cap denial, and mode
  transition; merely copying the trace after a step can still produce no rows.
- `UpdateReplayPredictionAllBodyTrajectories` currently selects all-body mode
  directly from `predictionWorldForces.mutualGravity.enabled` and creates one
  `FutureRoot` trajectory record for every body. That is intentional for
  authored space scenes. Seeing the same shape in the ordinary ball demo means
  either this qualification leaked or another publication path lost
  `parentId`/child-lane identity. PSD0 must capture the failing runtime facts
  before choosing which branch to repair.
- Prediction growth is already registered as
  `replay_prediction_working_set`, Replay-phase only, with a 256 MiB hard cap
  and measured high water of 18,701,760 bytes. Current F6 category accounting
  covers prediction frames, bodies, debug contacts, world state, engine state,
  and future-tree/trajectory storage, but has no high-detail evidence category.
- Current cancellation and Predict-off behavior intentionally retain warmed
  frame-bank capacity. That is correct for ordinary prediction reuse but does
  not satisfy the explicit high-detail -> low-detail memory-release contract.
- `TestReplayCauseInspection.cpp` explicitly pins Prediction as unavailable for
  solver detail. That obsolete expectation is the negative control for this
  plan.
- The current archive loader mutates/clears destination state before the whole
  artifact is proven valid. High-detail payloads also challenge the existing
  archive byte ceiling, so schema work must become staged and transactional
  rather than extending that destructive load path in place.

## Product Contract

### One setting, two retention modes

- `ReplayPrediction` is the sole retained owner of a
  `ReplayPredictionDetailMode` value with `High` and `Low` states. It defaults to
  `High` once when the application owner is constructed. The operator's choice
  survives scene changes, Predict off/on, target or horizon rebuilds, worker
  restarts, and prediction-owner resets. UI and App pass typed commands;
  neither retains a second truth. An archive's captured capability is metadata,
  not permission to overwrite the active operator preference.
- The `HIGH DETAIL` checkbox occupies the existing bottom-timeline pause-button
  rectangle beside `ALT VEL`. `ReplayScrubberControl::Pause` and the pointer
  `ReplayScrubberAction::TogglePause` route are removed rather than moved or
  retained as hidden compatibility controls. The checkbox emits one typed
  detail-mode command and displays the Prediction owner's current state.
- Keyboard `P` continues through the existing transport play/pause path. High
  detail does not change pause ownership, key binding, historical scrubbing, or
  programmatic pause actions; it removes only the redundant mouse pause button.
- A mode transition is a prediction-generation boundary. It joins/cancels
  worker work, invalidates all committed/build publications, clears selected
  cause rows and solver inspection, changes the mode, then seeds a fresh
  prediction from the current exact source. No generation mixes modes.
- Switching to `Low` destroys high-detail capacities after worker join and
  before the low-detail rebuild starts. Clearing vector sizes is insufficient:
  the Prediction owner must release the backing allocations and publish an
  observable post-join/pre-rebuild F6 checkpoint with zero current high-detail
  capacity bytes.
- Switching to `High` reacquires detail capacity only through the existing
  Replay-phase reserve owner as the new generation builds. Ordinary low-detail
  body/debug-contact/trajectory capacities retain their existing reuse policy.
- Turning Predict itself off keeps its existing warmed lightweight behavior.
  High-detail capacity release is specifically guaranteed by selecting Low
  detail, because the user is choosing the capture/playback memory profile.
  The checkbox remains enabled while Predict is off: selecting Low immediately
  releases retained High evidence and controls the next generation.

### Exact high-detail evidence

- High detail retains the exact persistent-contact rows and pipeline trace
  produced by the private Physics step for each published prediction frame.
  These are detached Physics-owned values, not a second Physics state owner and
  not a full `PhysicsSolverSnapshot` per tick.
- Use a paired build/committed `ReplayPredictionSolverEvidenceStore`, not two
  new heap vectors inside every prediction frame. Each store owns coarse,
  immutable fixed-capacity segments for contacts and pipeline rows plus
  frame-stamped range metadata. Growth appends a segment but never relocates or
  mutates storage reachable from an already published frame. Bank promotion is
  atomic, published spans never survive retirement, and one mode transition can
  release every segment observably without thousands of per-frame allocations.
- Each evidence-frame record names its prediction generation, detail mode,
  evidence-bank epoch, publication/topology version, `ReplayFrameIndex`, contact
  ranges, pipeline ranges, and completeness. Body samples, debug contacts,
  topology, cause rows, and evidence ranges carry the same identity tuple.
  Publication of a sealed frame record is the release boundary; readers never
  see ranges before every referenced segment and flag is final, and a replaced
  same-index frame can never satisfy an old row.
- Existing debug contacts remain the future-node topology input in the first
  implementation so high detail does not silently change which bodies enter
  the predicted causal chain. Persistent contacts and pipeline records explain
  that topology; they do not redefine it in the same phase.
- Low detail never allocates or appends to the evidence stores. Its publication,
  trajectories, capture/playback artifacts, and lightweight per-body
  explanation format remain behaviorally identical except for the separately
  required selected-root topology repair.
- High-detail capacity growth is bounded, overflow-checked, and routed through
  the existing Prediction reserve adapter. A denied request must cancel the
  high-detail generation with specific feedback identifying the retained limit;
  it must not publish a partly detailed hierarchy or silently fall back to a
  nearby frame.
- High mode explicitly enables the existing full-pipeline-record consumer on
  the private prediction `PhysicsEngine` before stepping and balances it on all
  exits. Low mode keeps it disabled. Reused private engines must be reset to the
  requested state; the live-world Physics engine is never enabled by this
  feature.

### High-detail hierarchy and row identity

- A predicted root body uses the first published prediction frame. Every
  contact-derived affected body uses its exact `firstFrame` from the existing
  causal topology. The builder resolves evidence from that frame only.
- Exact contacts involving the visible body are grouped into manifolds by
  counterpart pair and terrain identity. The visible structure is Body ->
  Manifold -> SolverRow. A manifold carries point count, centroid, normal,
  maximum penetration, counterpart identity, and exact frame; each solver row
  carries feature id, accumulated impulses, bias, effective mass, friction
  limit, warm-start state, and a durable ordered pipeline range/sequence
  identity. Stage, iteration, point/feature identity, and record order must
  distinguish the multiple pipeline records that one contact can produce; a
  coincident numeric index is not an exact join.
- The manifold on the causal parent edge appears first. Other simultaneous
  manifolds involving the body remain visible because they participate in the
  solver transaction that determined its response. Counterpart traversal stays
  cycle-safe and bounded by durable scene-object identity.
- `PredictionMotion` remains only where the topology names movement without a
  contact-derived parent edge. In Low detail, existing `PredictionContact` and
  `PredictionMotion` rows remain acceptable. In High detail,
  `PredictionContact` must not replace an exact manifold/solver hierarchy.
- Manifold and SolverRow are shared semantic row kinds. The `prediction` bit
  plus a typed source stamp selects the Prediction evidence bank; do not add
  duplicate `PredictionManifold` or `PredictionSolverRow` kinds.

### Causal path topology in both modes

- Ordinary collision/demo prediction publishes exactly one `FutureRoot`
  trajectory: `path.targetId`. No unselected or disconnected body receives a
  root record merely because it exists in the private frame bank.
- The existing debug-contact topology remains the mode-independent discovery
  source. A body becomes visible only when a contact chain transitively reachable
  from the selected root first activates it. Its trajectory record retains the
  causal `parentId`, depth, `firstFrame`, contact-derived flag, and child lane.
- A child's incoming line begins at the appropriate pre-contact history needed
  to see what was struck; its affected/outgoing line reveals from the activation
  frame. Descendants repeat the same rule. Publication may resume across budget
  slices but cannot promote a child to root while work is incomplete.
- High and Low builds from the same source must publish the same body topology,
  parent edges, depths, first frames, reveal order, and trajectory points. High
  detail only adds exact manifolds/solver rows addressable from those nodes.
- If Low cannot provide manifold/solver depth, it may retain the existing
  `PredictionContact` / `PredictionMotion` explanation under the correct body
  chain. It may not flatten body ancestry or publish every body as a root.
- All-body `FutureRoot` publication is a separate explicitly qualified space
  presentation owned by a typed `ReplayPredictionPathPresentation` policy with
  `SelectedCausalTree` and `AllBodiesSpace` states. Authored space/demo setup
  selects it explicitly and prediction seeding, archive load, bank promotion,
  and draw-list state preserve it. Generic/private `mutualGravity.enabled`
  cannot infer or change presentation policy. Ordinary generated demos and
  non-space scenes must prove `SelectedCausalTree`; explicitly authored space
  scenes retain `AllBodiesSpace`.
- Topology is a bounded rooted tree/forest projection of a cyclic contact graph:
  the selected root is queued first, each body is assigned at most once, parent
  edges are durable scene-object ids, and cycles/back-edges never create another
  root or infinite expansion.

### Inspection and presentation

- Solver-detail evaluation becomes bank-neutral but remains stamp-strict. It
  accepts SolverHistory evidence only for recorded rows and Prediction evidence
  only for predicted rows, requiring exact row frame, evidence frame, contact
  range/index, feature, focused body, counterpart, and terrain agreement.
- A cause-tree view may inspect a committed bank or a sealed, coherently
  published prefix of the current build bank. Every row retains the complete
  generation/mode/bank-epoch/frame/publication identity. Selecting a predicted
  manifold or solver row validates that tuple, resolves the exact immutable
  evidence ranges, and detaches bounded matching rows into the Planning
  inspection owner before a rebuild can retire the source bank. No borrowed
  span crosses segment growth, bank promotion, or retirement.
- Predicted manifold geometry uses the exact predicted frame's body poses and
  matching ManifoldRow pipeline points. It must not construct a fake
  `ReplaySolverFrameSample`, borrow the live pose, or re-step prediction.
- The same solver panel labels, signs, units, iteration rows, scrolling, camera
  focus, and aftermath/return lifecycle apply to recorded and predicted rows.
  The panel visibly identifies `PREDICTED FRAME <n>` versus
  `RECORDED FRAME <n>`.
- Switching to Low while an inspection is active exits the detail lifecycle,
  releases any inspection-owned pause, clears focus/manifold packets, and hides
  the cause window before the low-detail rebuild begins.
- Low retains the selected-root body topology for trajectory playback and
  archives, but the causal window is neither rendered nor interactive while
  Low is selected. The bottom-timeline checkbox remains the re-entry control.

### Archive behavior

- A prediction archive records captured detail capability separately from the
  operator's active detail preference. Low-detail archives keep the
  lightweight body/topology/trajectory payload and do not serialize empty
  solver-evidence blocks merely to satisfy a schema shape.
- The new high-detail archive schema retains exact solver evidence for the root
  event frame and every unique contact-derived future-node `firstFrame` needed
  by the visible hierarchy. It does not serialize diagnostics for ticks no
  visible cause row can address.
- Loading a high-detail archive while the active preference is High restores
  body -> manifold -> solver-row inspection without a live Physics source.
  Loading it while the active preference is Low validates the full artifact but
  commits only its lightweight projection and allocates no evidence bank; a
  later High selection requires a fresh live build or explicit archive reload.
  Loading a Low archive never upgrades it, and selecting High without an exact
  source reports that rebuild/reload is required rather than fabricating rows.
- Legacy schema v2/v3 archives remain readable as Low detail. They never join
  loaded topology to live or coincident diagnostics.
- Archive section sizes, contact/pipeline/event counts, cumulative offsets,
  ordering, non-overlap, referential closure, and total bytes are
  overflow-checked before allocation. Parsing uses a reserve-accounted staging
  owner and commits atomically only after complete validation; any failure
  preserves the previous prediction state byte-for-byte. Reconcile the artifact
  byte ceiling with the retained working-set cap including old-state plus parse
  staging peak. Current-schema round trips remain canonical and unsupported
  future schemas fail closed.

### Memory reporting and release proof

- Add explicit F6 categories/rows for prediction solver-contact evidence,
  prediction pipeline evidence, and their combined high-detail capacity. Counts
  report committed and build banks separately where both coexist.
- F6 reports allocator-owned current capacity bytes separately from lifetime
  peak/high-water, not merely logical sizes. On a
  High -> Low transition, contact/pipeline evidence sizes, capacities, ranges,
  and combined category bytes reach zero after the worker join. Before any Low
  rebuild allocation, a release checkpoint reconciles the same-snapshot total
  tracked replay/engine delta to the released evidence categories. Lifetime
  peaks remain historical. A later Low-build snapshot may differ in lightweight
  categories and must be reconciled category-by-category rather than forced to
  the release delta. The process OS resident set is not used as the release
  oracle because the Windows heap may retain freed pages.
- Measure retained bytes for `at_rest.scene.json`,
  `replay_velocity_four_ball.scene.json`, `replay_path_pool.scene.json`, and a
  dense contact scene at 20 and 120 seconds in both modes. Record High/Low delta,
  peak coexistence during bank promotion, and second-generation reuse.
- Any hard-cap change updates `ReplayPredictionRetainedMemory.h`,
  `ReplayReserveInventory.h`, allocation allowlist/rulings, measured high-water
  comment, exhaustion policy, and tests in the same commit. The cap covers peak
  build plus committed coexistence, not just one completed evidence bank.

## Non-Goals

- Do not change contact generation, solver arithmetic, iteration order, sleep,
  or any Physics baseline to expose diagnostics.
- Do not retain a complete world/solver checkpoint for every predicted frame or
  re-simulate an old predicted frame after selection.
- Do not replace future-node causal topology, trajectory publication, contact
  activation timing, or low-detail capture/playback behavior except to repair
  the observed root-flattening regression and restore the original causal
  parent/child contract.
- Do not add solver payloads to `PhysicsBodyRecord`, hot Physics arrays, UI
  state, Rendering contracts, or `RuntimeFrameViews.h`.
- Do not add a new replay growth owner, weaken the Replay phase check, hide an
  upward dependency, or introduce a callback/service/context bag.
- Do not feed bounded high-detail evidence into Continuous Orbital Forecast;
  that mode does not retain per-tick contact trees.
- Do not treat `clear()`, zero logical rows, or hidden UI as memory release.
- Do not silently truncate a manifold, publish half an evidence frame, or fall
  back to recorded/live/nearby diagnostics.
- Do not refresh Physics, Replay, SkullScope, prediction-archive, or visual
  goldens without an exact owner-approved candidate transition.

## Phases

- [ ] **PSD0 - Pin both modes and the current failure.** Preserve the current
  Prediction-unavailable result as a negative-control oracle, then add pure
  mode-transition tests: High default, no-op same-mode command, exact generation
  restart, preference persistence across scene/owner/Predict resets, archive
  capability independence, High -> Low clear/hide/release request, Low -> High
  rebuild, and recorded inspection independence. Capture an `at_rest.scene.json` witness of
  the current flat detail and generic unavailable panel. Capture the ordinary
  generated-demo regression with the selected id, private mutual-gravity flag,
  every trajectory record's lane/body/parent/depth/firstFrame, future-node
  topology, and draw-list `showAllFuturePaths` value. Add a failing oracle that
  requires exactly one root plus only transitively contacted descendants, and a
  separate positive oracle preserving intentional all-body roots in an authored
  space scene. Name the one presentation-policy owner and prove generic force
  configuration alone cannot select all-body roots.
- [ ] **PSD1 - Add the typed mode command and timeline control.** Put the sole
  mode value on `ReplayPrediction`; route one typed command through the
  established replay input/command seams. Replace the bottom timeline's Pause
  control/action with the checked-by-default High Detail toggle in the same
  rectangle beside `ALT VEL`; remove the old pointer pause rendering and hit
  path while preserving keyboard `P` through `ReplayTransportAction`. Extend
  the shared layout surface so drawing and hit-testing use the same checkbox
  rectangle. Prove pointer ownership, timeline geometry, keyboard/mouse
  blocking, active-inspection exit, low-mode re-enable reachability, and that
  clicking the former pause slot never changes pause state. Audit every removed
  Pause enum, switch, layout, help-text, and automation route while proving
  programmatic and inspection-owned pause remain intentionally separate.
- [ ] **PSD2 - Implement bounded, releasable evidence banks.** Add paired
  build/committed immutable segmented evidence stores with the full generation,
  mode, bank-epoch, frame, and publication/topology identity tuple,
  overflow-safe reserve operations, bank promotion, reset, and explicit
  `ReleaseCapacity`. Extend current-capacity and lifetime-peak F6 rows and add a
  post-join/pre-rebuild release checkpoint. Measure the scene and horizon
  matrix, select exact segment sizes/cap, and reconcile every reserve-policy
  inventory/ruling. Unit tests cover empty, append, published-prefix concurrent
  growth, range stability, same-frame replacement, promotion, cancellation,
  denied cap, repeated release, committed/build banks independently and
  together, and zero-current-capacity F6 accounting with historical peaks.
- [ ] **PSD3 - Restore one-root causal trajectory publication.** Trace the
  generated-demo all-body qualification and every root/child record producer,
  then restore the original selected-root -> first contact -> downstream contact
  publication contract without removing intentional space-scene all-body paths.
  Introduce the explicit `ReplayPredictionPathPresentation` owner and carry it
  through scene/demo seeding, archive metadata, bank promotion, and draw lists;
  do not derive it from private Physics force configuration.
  Keep topology discovery on existing debug contacts so High and Low publish
  identical parent ids, depths, first frames, reveal order, and trajectory
  points. Prove disconnected bodies stay absent; cycles queue each body once;
  partial budget slices never expose a promoted root; archive/load and bank
  promotion keep the same qualification; and ordinary demo plus authored
  non-space scenes have exactly one FutureRoot.
- [ ] **PSD4 - Capture and publish exact high-detail evidence.** In High mode,
  acquire the existing full-pipeline-record consumer on the private Physics
  engine, copy persistent contacts and ordered stage/iteration pipeline ranges
  after each complete step, and publish a frame only after its evidence ranges
  are final. Balance the gate on completion, cancellation, restart, cap denial,
  and mode transition, including engine reuse; never acquire it on the live
  engine. In Low mode, keep the gate disabled and execute none of that
  reserve/copy path. Prove initial frame, no-contact, object contact, terrain,
  dense growth, worker cancellation,
  sealed build-prefix publication during later appends, promotion, restart, and
  generation isolation. Profile tests require non-empty exact High pipeline
  rows, zero Low rows, balanced consumer counts, and unchanged low-detail,
  live-solver, and private-solver/body hashes while toggling.
- [ ] **PSD5 - Build and inspect body/manifold/solver hierarchy.** Refactor the
  recorded-only manifold grouping into source-neutral exact-frame logic. In High
  mode emit causal-parent manifold first, simultaneous manifolds next, and exact
  solver rows below each; retain PredictionMotion only for non-contact motion.
  Generalize detail evaluation to the typed Prediction evidence source, project
  geometry from predicted poses, and detach selected evidence into Planning.
  Tests pin row order/depth, multi-point contacts, terrain, simultaneous pairs,
  cycles, iterations, labels/units, camera focus, retargeting, retirement, and
  strict wrong-generation/mode/bank-epoch/frame/publication/range/sequence/
  feature rejection. Include a same-frame replacement race proving old rows
  cannot resolve new evidence.
- [ ] **PSD6 - Make archives mode-aware and transactional.** Introduce the next
  RVPD schema with captured capability, explicit path-presentation policy, and
  bounded unique event-frame evidence for High archives. Keep Low artifacts
  lightweight and load v2/v3 as Low. Preflight cumulative sizes and range
  closure, parse into reserve-accounted staging, and swap only after complete
  validation. Reconcile artifact and working-set caps including old-state plus
  staging peak. Prove current High and Low canonical round trips, legacy
  migration, future-schema rejection, section reorder/overlap/overflow and
  dangling-range rejection, prior-state byte identity after every failure,
  loaded High inspection without Physics under an active High preference, High
  artifact load under active Low with zero evidence allocation, and loaded Low
  playback without evidence allocation.
- [ ] **PSD7 - Close the combined operator workflow.** Automate a full
  High-build -> inspect -> Low-toggle -> low rebuild -> High-toggle sequence.
  Require the cause window to disappear in Low, the compact re-enable control to
  remain usable, evidence categories/current capacities to reach zero at the
  pre-rebuild checkpoint, same-snapshot total bytes to reconcile exactly, and
  fresh High detail to rebuild correctly. Run `at_rest` and multi-body chain
  visual witnesses, focused tests,
  replay allocation/frame-spike/archive/visual gates, dependency and ownership
  inventories, mapped fast/Physics/DX12/performance validation, touched-source
  comment audit, project-file/build-configuration consistency checks, and final
  independent ownership review.

## Acceptance

The plan closes only when one final source state proves all of the following:

- High detail is checked by default. In `at_rest.scene.json`, the predicted
  causal surface shows Body -> Manifold -> SolverRow and selecting a manifold or
  row displays exact impulses, masses, bias, friction, warm start, iterations,
  and geometry instead of `Solver detail not available`.
- A multi-body predicted chain preserves durable parent/child causality while
  exposing the exact manifold/row transaction at every affected body's first
  frame. Simultaneous contacts and terrain remain distinguishable.
- In ordinary demo/collision scenes, both High and Low have exactly one selected
  FutureRoot. Only bodies reached by the contact cascade appear, each with one
  causal parent, stable depth, and first activation frame. No disconnected ball
  is drawn as a root. Explicitly authored space scenes retain their separate
  all-body-path presentation independent of generic force configuration.
- Turning High detail off during a completed build or active inspection joins
  work, exits inspection safely, hides/clears the causal window, releases all
  high-detail backing capacity, and starts a Low rebuild from the current exact
  source.
- F6 shows zero prediction solver-contact/pipeline evidence capacity after the
  post-join/pre-rebuild transition checkpoint, its lifetime peak remains
  historical, and the same-snapshot total tracked memory delta reconciles to the
  released categories. Later Low allocations are reported separately. The
  bottom timeline High Detail checkbox remains available to turn it back on.
- The old mouse pause button/control/action no longer exists. Pressing `P`
  retains the current play/pause behavior in both detail modes, including
  historical and inspection pause ownership.
- Low detail produces the pre-change lightweight frames, trajectories, causal
  contact discovery, capture/playback artifacts, and memory profile without
  allocating high-detail evidence. Its body hierarchy matches High; only its
  per-body solver explanation may remain flat.
- Turning High detail back on starts a fresh generation and restores exact
  hierarchy/detail; no stale Low/High bank, selection, pause, focus, or geometry
  crosses the transition.
- High frame publication is coherent under worker execution, cancellation,
  sealed build-prefix reads during later appends, same-frame generation
  replacement, bank promotion, restart, and cap denial. No recorded,
  live, nearby, or re-simulated frame substitutes for exact evidence.
- High archives restore exact inspection without Physics when High is active;
  active Low never allocates their evidence, and Low/legacy archives remain
  lightweight. Archive failure preserves the prior state byte-for-byte and no
  load path fabricates solver evidence.
- The representative 120-second matrix fits the recorded finite policy including
  peak committed/build coexistence. Same-mode second-generation reuse does not
  grow; High -> Low still releases rather than warms detail capacity.
- Existing Physics behavior/baselines, live and private solver hashes,
  low-detail trajectory fingerprints, recorded-history inspection, dependency
  rules, allocation policy, comment quality, replay visual fidelity, DX12, and
  performance gates pass from the same final source state.

## Validation Mapping

Validation is deferred until each implementation phase is prepared for commit.

| Scope | Required pre-commit evidence |
|---|---|
| Mode state, commands, layout, and input | Focused owner-request/layout/input tests, then `tools\validate_tests.bat` |
| Evidence store, release, and F6 accounting | Focused memory/store tests plus `tools\validate_replay_allocation_policy.bat` |
| Frame capture, banks, worker publication | Focused prediction scheduling/publication/consumer-gate Profile tests and `tools\validate_replay_prediction_frame_spikes.bat` |
| Runtime package or cap/policy change | `tools\validate_dependency_graph.bat` and `tools\validate_fast.bat` |
| Hierarchy, detail, and exact geometry | Focused `TestReplayCauseInspection` and cause-tree filters plus `tools\validate_replay_visual_fidelity.bat` |
| Prediction archive schema | Focused transactional RVPD schema/round-trip/failure-preservation automation plus High/Low artifact and staging-peak evidence |
| Overlay and mode-transition visuals | `tools\validate_dx12_renderer.bat` and `tools\run_graphics_stress.bat 1` |
| Physics-adjacent diagnostic capture | `tools\validate_physics.bat` and unchanged solver-hash witnesses |
| High-detail hot path and 120-second memory | `tools\validate_perf.bat` plus High/Low/release and two-generation evidence |
| Final combined source state | `tools\validate_full.bat --plan-completion` after every focused gate above |

## Reference Sites

- `SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h`
- `SkullbonezSource/Runtime/Prediction/ReplayPrediction.h`
- `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp`
- `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h`
- `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.cpp`
- `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedMemory.h`
- `SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp`
- `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.cpp`
- `SkullbonezSource/Runtime/Planning/ReplayCauseInspection.h`
- `SkullbonezSource/Runtime/Planning/ReplayCauseInspection.cpp`
- `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp`
- `SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h`
- `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h`
- `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp`
- `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp`
- `SkullbonezSource/Runtime/App/ReplayReserveInventory.h`
- `SkullbonezSource/Runtime/App/RunFrame.cpp`
- `SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h`
- `SkullbonezSource/Physics/PhysicsDebugData.h`
- `SkullbonezSource/Physics/PhysicsEngine.h`
- `SkullbonezSource/Physics/PhysicsStepDiagnostics.h`
- `SkullbonezSource/Physics/PhysicsStepDiagnostics.cpp`
- `SkullbonezSource/Core/MainMemoryStats.h`
- `SkullbonezSource/UI/UITabMemory.cpp`
- `SkullbonezTests/TestReplayCauseInspection.cpp`
- `SkullbonezTests/TestReplayPredictionScheduling.cpp`
- `SkullbonezTests/TestReplayVisualPacket.cpp`
- `SkullbonezTests/TestOwnerRequestQueues.cpp`
- `tools/allocation_policy_allowlist.json`
- `Agentic/Reference/runtime-reference.md`
- `Agentic/Reference/physics-overview.md`
