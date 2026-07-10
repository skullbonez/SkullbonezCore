# SkullbonezCore Session State

Date: 2026-07-10

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history.

## Current State

| Field | Value |
|---|---|
| Branch | `engine-cleanup-10th-july`, tracking `origin/engine-cleanup-10th-july` |
| Current pushed baseline | `c0d691cb refactor: move placement scale commit into tools` |
| Current objective | Continue B1f by moving new transform pointer gestures to RuntimeTools, then delete editor/outer pointer routing and TakeInput |
| Last broad local gate | `tools\validate_full.bat` passed RuntimeTools-owned active transform drags with 129/129 doctest cases, 2,755 assertions, zero-warning builds, DX12 with zero InfoQueue errors/matching screenshots, standalone physics smoke, and 20,001-line byte-exact physics in 54.3s |
| Native evidence | Injected heap-use-after-free caught; healthy ASan and five-file `/analyze` passed in 16.185s |

## Pushed Cleanup Commits

- `3d25cf48 docs: rebuild the engine cleanup control plane`
- `31395ba5 docs: reconcile stale engine plan references`
- `125bb8a9 docs: inventory DX12 failure surfaces`
- `0c9097ec fix: reduce box manifolds by depth and spread`
- `e752c395 style: format manifold reducer`
- `6d8a3aff fix: make broad validation a CPU-test superset`
- `c13e26ba docs: bind scene asset round-trip ownership`
- `6976c61e docs: hand off the active engine cleanup wave`
- `1e7846d5 ci: add honest CPU and native validation lanes`
- `4a326189 docs: refresh the engine cleanup handoff`
- `2592c0ac fix: make DX12 command failures fail closed`
- `acdc994c feat: preserve scene asset provenance`
- `670a9fcb feat: add runtime input and exit owners`
- `5b56af13 fix: preserve application exit failures`
- `e7c2e4a2 feat: route runtime keyboard actions through InputRouter`
- `225b9688 feat: centralize runtime pointer input ownership`
- `fd48d658 feat: split runtime requests by owner`
- `d817a995 feat: stabilize authored scene identities`
- `28a8b205 feat: move scene entity metadata to scene owner`
- `e147f9b8 feat: make scene creation transactional`
- `119b359c feat: preserve live asset scene snapshots`
- `7fdd91d3 feat: stabilize scene behavior group roots`
- `936eda3f fix: complete DX12 failure-safe recreation`
- `ac9c4aea fix: reconcile replay owner-action artifacts`
- `824fafaf refactor: delete obsolete replay compatibility paths`
- `dfab2043 refactor: move replay workspace ownership out of Run`
- `cb2f4dc4 refactor: move render composition behind RuntimeRenderer`
- `f5cbeb57 fix: bound replay retained memory by owner`
- `8e39056c refactor: narrow replay live-owner identity`

## Current Queue

`InputRouter` now owns the complete device snapshot, semantic and pointer edge
memory, all-of binding contexts, one post-UI hit value, focus cancellation, and
native capture/cursor intent. Direct later hardware polls, duplicate UI/replay/
editor pointer memories, and both input callback packs are deleted. B1a-B1e are
complete except the intentionally separate B1f final `Run` method/state
extraction proof.

Owner queue B2b-B2e is complete. Capture, render-default persistence, and scene
requests now use fixed owner storage; application exit remains value-only.
The generic queue/type/mixed switch and dead advance/quit cases are deleted.
Replay records only successful owner events on explicit stable wire codes, and
the scene batch accepts at most the first same-frame transition. The final fast,
CPU, interaction, perf, and full gates passed from the completed boundary.

The first perf run exposed eight duplicate names in the varied physics bench
fixture after C1a made name collisions honest. The ball rows now have unique
names; the final performance and full gates pass without relaxing validation.

DX12 D1-D5 is complete. Required startup and optional-feature failure paths
retain one result, resize publishes only complete replacement sets, device loss
is sticky and tears down without later queue/present work, and the Debug fault
probe exits 1 before the sole submission site with one 457-byte diagnostic and
zero submissions/InfoQueue errors. The plan-level adversarial review found and
fixed device-loss teardown issuing a later fence signal; the repeat review was
clean. Final fast, architecture, fault, three consecutive renderer, allocation,
and full gates passed.

Replay R0 is complete at exactly 28 scoped files / 23,814 lines, plus the
117-line outside Run probe-state adjunct. Capacity, high-water, raw artifacts,
Run business methods, validation lanes, and deletion/merge candidates are
reconciled in `Agentic/Reports/replay_r0_inventory_20260710.md`. The artifact
query/gate now treats the deleted omnibus wire kind 2 as unsupported and
decodes explicit owner-action kind 10; the v2 gate passed in 25.4s and fast
passed in 19.4s.

Replay R1 is complete at 24 files / 23,347 lines. The unused JSON exporter,
behavior-free scrubber save bridge, redundant save-test/play CLI synonyms, and
the solver recorder's `void*` visitor are deleted; binary v2 is the sole saved
artifact. The first scrub gate exposed terrain contact index `-1` reaching
ragdoll scene metadata; the replay helper now preserves non-entity sentinels,
and the complete scrub/prediction gate passes. Final fast, allocation, CPU, v2,
interaction, replay, and full gates passed. That source was the R2 starting point.

Replay R2 is complete at 24 files / 24,350 lines. `ReplayRuntime` owns the one
typed workspace tick, scrub/cause/velocity/prediction and inspection-camera
decisions, fixed-capacity overlay production, live restore/hash transactions,
scene timeline reset, startup artifact/probe workflows, and frame-probe
sequencing. `Run.h` and every `Run*.cpp` expose zero replay business methods;
replay owner headers retain no Run pointer/reference, `void*`, callback pack, or
friend backdoor. The added lines are explicit owner code moved out of Run; R4
owns live-view narrowing and R5 owns size closure. CPU, allocation, replay,
interaction, physics, DX12, and full gates passed. Detailed evidence is in
`Agentic/Reports/replay_r2_workspace_20260710.md`.

RuntimeRenderer composition A1-A2 is complete. The renderer receives the five
named owner views, stores explicit render/world owners rather than
`RunSubsystemState`, owns pass/resource lifecycle and submission, and invokes
tool/replay record owners after replay overrides. The old binding bag, Run C
hooks, `void*` callback user, texture callback path, and raw sky alias are
deleted. The first adversarial pass found and fixed overlay ordering plus a
disguised broad host; the required repeat pass was clean. Architecture,
renderer, full, fast, allocation-policy, project/filter, and comment gates pass.
Evidence is in `Agentic/Reports/runtime_renderer_composition_20260710.md`.

Replay R3 is complete. `ReplayRetainedMemory` names presentation, solver,
prediction-prefix, and cold-v2 ownership plus all three registered growth
owners. Guarded high-water evidence right-sizes recorder/solver caps to 32/8
MiB; prediction remains 256 MiB after reaching 211,376,304 bytes. Aggregate
active owner bytes are enforced, counters are exposed in memory JSON, and
fatal-vs-cancel exhaustion is tested. CPU, allocation, scrub, v2, interaction,
physics, perf, and full gates pass. Evidence is in
`Agentic/Reports/replay_r3_retained_memory_20260711.md`.

Replay R4-R5 and the full replay architecture plan are complete. Production
startup/restore no longer accepts `ReplayLiveWorld`; frame-scoped owner views
separate sample restore, cold topology rebuild, and Debug-only probes. Stable
ids override stale row hints and reject duplicates before mutation. Restore
captures actual live state before mutation, reapplies it on recoverable failure,
and hash-verifies rollback; the named v2 gate injects a target-hash mismatch to
prove that path. The final inventory is 26 files / 24,904 lines with five
cohesion-based size exceptions and `ReplayRuntime.h` at 1,485 lines. Both
required adversarial passes are resolved. Fast, CPU, scrub, v2, interaction,
physics, perf, DX12, and full gates pass. Evidence is in
`Agentic/Reports/replay_r4_live_owner_identity_20260711.md` and
`Agentic/Reports/replay_r5_closure_20260711.md`.

The first SceneController C1 dependency is complete locally. SceneController
physically owns `PhysicsEngine`; GameModelCollection requires a borrow and no
longer publishes physics authority. All production consumers receive the scene
owner explicitly, and browser/adjacent/deck/reset/advance policy moved out of
the deleted `SceneRuntimeCoordinator` object. Fast, CPU, physics, perf, and full
gates pass; the remaining C1 work is load/save execution and lifecycle-event
ownership, including deletion of the temporary scene action dispatcher.
The save sub-boundary is also local: SceneController owns editable snapshot and
defaults persistence through a synchronous borrowed view, the Run save method
is deleted, and failed reads/parses/writes return owned results without replay
events. Fast and full gates pass from this source.

The dependent B1f scene seam now has one fewer `Run` forwarding method:
`RuntimeTools` owns mouse-pickup cancellation, including capture-intent release,
gesture termination, and handle reset. Scene load and every input/physics
cancellation path call the tool owner directly. B1f remains open for the full
`TakeInput` and pointer-routing deletion proof. The CPU umbrella, interaction
clicks, and full gate pass from this source; the touched-source comment audit is
6/6.

Scene lifecycle publication is being converted from passive labels into an
enforced transaction. The scene runtime rejects skipped phases while allowing
explicit restart after a recoverable failure, and SceneController refuses to
publish cleared/populated/activated phases unless entity, body, and collider
topology matches the phase contract. The unused last-event forwarding accessor
is deleted. At this slice C2 was still open for concrete non-scene owner
consumption and callback-pack deletion; both are closed by the later milestone
and adversarial correction below.

All 135 authored scenes also pass the load-only sweep with empty stderr, and
the focused physics gate remains byte-exact. The lifecycle transition test is
part of the 126-case CPU suite. Comment audit: 5/5 touched source/test files.

The scene execution callback pack is now deleted locally. Navigation returns a
value-only accepted `SceneLoadRequest`, including the no-load result for the
already-active browser scene, and cinematic selection is a separate index
decision. No scene action retains `Run`, `void*`, callbacks, mutable capture/
scene state, or a style-owner context. B2f still requires deletion of
`DrainSceneRequests` and promotion of the remaining Run load sequence. CPU,
fast, one-minute graphics stress (8,309 frames / 231 loads / empty stderr), and
full gates pass; comment audit is 9/9.

SceneController now physically owns GameModelCollection beside the entity and
physics stores. The Run collection field and all 114 direct member uses are
deleted; save, required-contact updates, and replay trimming use controller-
owned topology without passing it back as a duplicate owner argument. The C1
load boundary still needs world/terrain/camera population and orchestration.
Fast, CPU, replay scrub, replay v2, physics, and full gates pass; comment audit
is 15/15.

SceneController now physically owns the active WorldEnvironment beside its
entity, model, and physics state. The Run field and every direct use are gone;
save and replay contexts derive the world through the scene owner instead of
republishing duplicate mutable authority. Terrain/camera ownership and final
load orchestration remain the next C1 edges. Fast, CPU, replay scrub, replay v2,
physics, and full gates pass; comment audit is 15/15.

The active CameraCollection is moving from RunSubsystemState into
SceneController. The old value owner and nullable alias are deleted; camera
consumers and Director playback use the concrete scene owner, and replay/save
contexts no longer duplicate controller-owned camera authority. Terrain and
final load orchestration remain the next C1 edges. Fast, CPU, interaction,
replay scrub, and full gates pass; comment audit is 22/22.

SceneController now owns the replaceable terrain and its flat-slope
classification through one SceneTerrain publication boundary. RunSubsystemState
has no terrain authority, while renderer passes borrow the stable terrain owner
instead of mutable unique-owner storage. Final Run load orchestration is the
remaining C1 seam. Allocation, project filters, fast, CPU, one-minute graphics
stress, all 135 scene loads, physics, and full gates pass; comment audit is
18/18.

SceneController now implements the complete cold load transaction with explicit
per-call concrete owners. Run::LoadScene and both broad load/reset context bags
are deleted. The remaining C1 seam is the scene-only pending-request switch and
its Run::DrainSceneRequests wrapper. Fast, CPU, interaction, replay scrub,
one-minute graphics stress, all 135 scene loads, physics, and full gates pass;
comment audit is 11/11.

The SceneController C1-C3 milestone and owner-queue B2f are complete locally.
`SceneController::ExecutePending` owns the fixed pending batch and scene-only
operation switch, while `SceneController::Load` owns the ordered cold lifecycle
transaction. `Run::LoadScene`, `DrainSceneRequests`, the scene callback/context
packs, and collection scene wrappers are deleted; cold-operation dependencies
are explicit per-call borrows and are never retained. Fast, CPU umbrella,
interaction clicks, and full gates pass; comment audit is 3/3. The required
milestone adversarial review is next, before B1f resumes.

The first scene-milestone adversarial pass found and fixed C2 completion
overclaim plus a pre-drain mutation defect. Scene-load preparation is now
read-only through a successful GPU drain; diagnostics, simulation, audio,
tools, interaction, and replay attach enforced receipts beside their concrete
phase calls, and controller bookkeeping/manual-reset/interactive state commit
after preparation. Final fast, CPU, all 135 authored scene loads, and full
gates pass; the final comment audit is 13/13. The required repeat pass then
found the same pre-drain interactive mutation in the pre-UI adjacent-navigation
and graphics-stress helpers. Both duplicate seams are deleted; final fast,
one-minute graphics stress (8,533 frames / 238 loads / empty stderr),
interaction, and full gates pass. The scene-milestone adversarial loop is
closed, so B1f resumes next.

B1f is progressing from values outward. `InputRouter::BuildRuntimeSnapshot` now
owns the single device/UI pointer join, `Run::BuildRuntimeInputSnapshot` is
deleted, and the Run-capturing pointer/camera callback pack is replaced by
direct composition over owner values. Fast, CPU umbrella, interaction, perf,
and full gates pass; comment audit is 5/5. B1f remains open for the
`RouteRuntimePointerInput`/camera helper surface and final `TakeInput` deletion.

`RuntimeInteractionController` now owns camera-look gesture begin/cancel sync;
the corresponding Run methods are deleted and standalone policy coverage calls
the owner API directly. Fast, CPU, interaction, and full gates pass; comment
audit is 5/5. The next B1f edge is world-pointer routing and camera/presentation
composition.

`InputRouter` now owns the pointer-presentation policy join over device/UI
snapshots and scalar editor/replay facts. Run's mouse-look and cursor-hide query
methods are deleted, and render camera cycling consumes committed interaction
capture. Fast, CPU, interaction, perf, and full gates pass; comment audit is
6/6. B1f continues with mutating pointer presentation and world-click routing.

Run's two mutating pointer-presentation wrappers are now deleted. Callers
request router visibility/capture directly from the owner policy and reset
camera deltas explicitly when mouse-look is inactive. Fast, interaction, and
full gates pass; comment audit is 3/3. B1f continues with world-pointer routing,
focus/keyboard composition, camera helpers, and final `TakeInput` deletion.

`AttachedCameraController` now physically owns Attach target/orbit/follow state;
Run stores only the concrete controller and all consumers borrow `State()`.
Fast, CPU, interaction, perf, and full gates pass; comment audit is 9/9. B1f
continues by moving attached-camera behavior and the world-pointer route.

Attach return-mode and visible render-pose capture/restore now belong to
`AttachedCameraController`; the duplicate RunCameraState field and two Run
methods are deleted. Fast, interaction, and full gates pass; comment audit is
5/5. Target selection/submode/pin/orbit/follow and world-pointer routing remain.

Attach target recovery and per-frame follow pose application now execute inside
`AttachedCameraController`; Run's resolve/follow methods are deleted. Fast,
interaction, perf, and full gates pass; comment audit is 6/6. Selection,
submode/pin/orbit commands and the outer world-pointer route remain.

Attach submode, pin, and orbit mutation now execute inside
`AttachedCameraController`; three Run command methods and the UI-frame orbit
callback slot are deleted. Fast, CPU, interaction, perf, and full gates pass;
comment audit is 4/4. Selection and the outer world-pointer route remain before
focus/keyboard callback-pack and `TakeInput` closure.

Attach target reuse, seeded selection, ray picking, identity capture, and
initial relative-pose capture now execute inside `AttachedCameraController`.
Four Run methods and three obsolete pose helpers are deleted; composition sees
only an exact selection receipt. The interaction gate now includes a passing
Attach object-click regression. Fast, CPU, three interaction scenarios, perf,
and full gates pass; comment audit is 4/4. The wider pointer route remains.

Manipulator pickup pointer routing now belongs to `RuntimeTools` through a
value-only input/result boundary; Run's pickup input method is deleted. A new
dynamic-body interaction assertion proves pickup begin and release cancellation.
Fast, CPU, four interaction scenarios, perf, and full gates pass; comment audit
is 6/6. Editor and replay/launcher pointer composition remain.

Editor/Inspect selection commands now prepare and commit inside `RuntimeTools`
around the existing interaction-owner transition. Run's command executor and
event publisher are deleted. Fast, CPU, four interaction scenarios, perf, and
full gates pass; comment audit is 6/6. The remaining editor gesture/placement
route and replay/launcher composition still block B1f closure.

Editor-active and Inspect-gizmo-active queries now belong to `RuntimeTools` and
their Run methods are deleted. Fast, four interaction scenarios, and full pass;
comment audit is 7/7. Mutating editor transition/pointer work remains.

Editor transition cleanup now belongs to `RuntimeTools`; Run retains only the
cross-owner pointer-presentation reconciliation and its editor cleanup method is
deleted. Fast, four interaction scenarios, and full pass; comment audit is 4/4.

Cursor application and UI capture release now execute inside `InputRouter`;
four free presentation wrappers are deleted and camera delta reset consumes the
router's explicit release result. Fast, CPU, four interaction scenarios, perf,
and full pass; comment audit is 3/3.

Launcher click gating, ray construction, replay event publication, topology
repair, firing, and scene-count commit now execute inside `RuntimeTools`. A fifth
interaction scenario proves accepted laser feedback. Fast, CPU, interaction,
perf, and full pass; comment audit is 5/5. Editor/replay routing remains.

Replay path-target world-pointer gating, picking, and optional inspection-camera
exit now execute inside `ReplayRuntime`; Run observes only `consumed`. The Attach
probe now asserts press/release selection timing. Fast, CPU, five interaction
scenarios, perf, and full pass; comment audit is 3/3. Editor routing remains.

Interaction transition cancellation now sequences through `InputRouter`, which
borrows ReplayRuntime/RuntimeTools only to invoke their own cancellation APIs.
Run's broad transition cleanup method is deleted. Fast, CPU, five interaction
scenarios, perf, and full pass; comment audit is 3/3.

Transition cancellation plus final workspace/tool activation now execute inside
`InputRouter`; Run's finalization wrapper is deleted. Fast, CPU, five interaction
scenarios, perf, and full pass; comment audit is 3/3.

World-owner workspace selection, cancellation, and final owner activation now
execute inside `InputRouter`; Run's world-owner forwarding wrapper is deleted.
Editor, replay automation, and placement-mode callers use the owner API
directly. Fast, CPU, five interaction scenarios, perf, and full pass; comment
audit is 5/5. B1f continues with the editor world-pointer route.

Editor pointer preview, invalid-selection cleanup, picking, and selection-plan
construction now execute inside `RuntimeTools` from value-only ray inputs. Run
retains only transition-before-commit composition for that path. Fast, CPU,
five interaction scenarios, perf, and full pass; comment audit is 2/2. B1f
continues with placement and transform gestures.

Placement-scale release, placement commit, replay recording, selection, and
gesture teardown now execute inside `RuntimeTools`; Run consumes only explicit
composition facts. Five interaction scenarios and full pass; comment audit is
2/2. B1f continues with transform gestures.

Active translate/rotate/scale drags, bounded release-time replay recording, and
gesture teardown now execute inside `RuntimeTools` from a value-only pointer
sample. Five interaction scenarios and full pass; comment audit is 2/2. B1f
continues with new gesture starts.

Scene provenance C1a is complete: parser-owned library/instance/ordered-part
records retain exact shape sources, hierarchy transforms use rotated offsets
and quaternion composition, duplicate explicit/asset/ragdoll names fail
atomically, and runtime ragdoll names preflight before the first append. Parser,
CPU umbrella, physics, and full gates passed from the final source.

Stable scene identity C1b is complete. Schema v2 requires explicit nonzero ids
for direct objects and ordered asset parts; duplicate/missing/wrong-version
input fails atomically. Version 1 upgrades once in the historical runtime
section order, authored creation consumes stored ids rather than allocating by
loop order, and later runtime spawns continue above the highest sparse id. The
parser, CPU umbrella, physics, and full gates passed from the final source.

Scene entity ownership C2 is complete. `SceneController` owns the preallocated
stable-id/body, display-name, material, and asset-affiliation rows; `GameModel`
retains only transient contact-highlight timers. Creation callers publish
`SceneEntityCreateDesc`, and replay/save/style/selection/automation consumers
read the scene owner. An initial eager-array +5.3 MB regression was corrected by
reserving configured cold rows before population; allocation policy, CPU, fast,
physics, performance, and full gates pass from the final source.

Transactional creation C3 is complete. `TryCreateSceneEntity` preflights every
same-row metadata, physics, and render owner before mutation; recoverable input
failure leaves every count unchanged, while topology/reservation drift is
fatal. Render rows publish during creation, the old append API and duplicate id
parameter are deleted, and clear proves zero topology. The waited standalone
smoke reports `creation_atomic=pass`; CPU, allocation, fast, physics,
performance, and full gates pass from the final source.

Scene snapshot ownership C4 is complete. `SceneSnapshotWriter` borrows explicit
owner data, resolves body/collider rows through stable scene identity, emits
schema-v2 asset instances with authoritative per-part live state, and fails
topology drift fatally instead of silently skipping rows. The collection save
facade is deleted. A mixed-shape no-`Run` writer/parser regression and a waited
production building-asset save/reload probe pass; allocation policy, CPU, and
full gates pass from the final source.

Stable behavior ownership C5 is complete. `SceneEntityStore` owns behavior
groups separately from asset affiliation and stores stable root object ids;
collection physics paths derive dense rows only at cold compatibility
boundaries. The collection group sidecar/types/creation argument and scoped
row-root spellings are deleted. The C1-C5 adversarial review found and fixed
parser publication of missing roots plus incomplete no-`Run` evidence. The
corrected fixture recreates fresh owners and passes 444 stable-id comparisons;
the follow-up review is clean. Parser, CPU, allocation, physics, performance,
and full gates pass from the final source.

## Workstreams To Prioritize

1. Close the dependent B1f scene/input seam and promote `SceneController` to
   own real load/reset/save lifecycle and delete `Run` scene callbacks.
2. Finish physics stable-identity D1-D4 and the remaining interaction/UI work.
3. Finish validation-gate V3-V4 and behavioral-test P3/P5/P6 evidence.
4. Close remaining interaction/UI and physics authority items.
5. Close renderer decomposition and shadow quality after the five `Run`
   ownership extractions establish their boundaries.
6. Run the final ownership and campaign adversarial reviews, fixing every
   credible finding before closure.

## Binding Decisions And External Blocker

- `Run` remains process/frame composition after five owner extractions:
  `InputRouter`, owner queues/application exit, `SceneController`,
  `ReplayRuntime`, and `RuntimeRenderer`.
- Input uses a pre-UI immutable device snapshot, one post-UI hit snapshot, then
  post-UI routing. Later phases do not poll hardware directly.
- Persistent self-hosted DX12 CI may run trusted `main`/manual refs only. A
  disposable isolated GPU runner is required before public-PR GPU execution can
  become merge-blocking.

## Non-Negotiable God-Object Closure Gate

Do not close the runtime plan or engine-cleanup campaign merely because
`Run.cpp` becomes short. Treat `Run.h`, every `Run*.cpp`, shared internal
headers, callback/context bags, and forwarding facades as one logical object.
Its only permitted responsibilities at closure are owner construction/wiring,
startup/shutdown, OS message pumping, top-level frame order, and final exit
reporting. `RunInternal.h` and equivalent renamed shared-state hubs must be
gone, and the five extracted owners must remain cohesive rather than becoming
replacement god objects.

The final independent adversarial review must report zero credible god-object,
callback-bag, forwarding-facade, or disguised shared-state-hub findings across
the runtime shell and current cleanup hotspots. Any credible finding reopens
the relevant checklist item and blocks completion; log and fix it rather than
deferring it as optional follow-up.

`Agentic/Plans/MASTER-PLAN.md` remains the authoritative plan index.
