# Authoritative Replay Rollback Plan

Date: 2026-06-21
Status: In progress - smooth backwards presentation scrubbing is now the
primary goal; retained in-memory solver restore plus path/contact visualizer and
live prediction v1 are implemented; binary `.skreplay` v2 presentation artifacts
and `tools\replay_query` are implemented; sparse checkpoint/event rollback,
solver chunks, and saved authoritative restore are not implemented.
Related: `Agentic/Plans/Done/replay-system-plan.md`, `Agentic/Reference/runtime-reference.md`
Impact area: physics, runtime replay, scene system, SkullScope diagnostics, tests, UI
Validation note: plan-only edits require no validation. Implementation changes
that touch replay runtime or `SkullbonezRun*` require `tools\validate_full.bat`.
Changes to solver state, physics checkpoints, deterministic replay, or baselines
require `tools\validate_physics.bat`; SkullScope baseline/query additions require
`tools\validate_physics_deep.bat`.

## Goal

Upgrade replay from visual/presentation scrubbing to authoritative solver
rollback.

The current replay scrubber can draw an older presentation sample, pause physics
while inspecting it, resume from the live edge, and save the retained
presentation buffer. Authoritative rollback means the user can choose a replay
time, restore the actual solver state for that tick, and continue simulation
from there as a new live branch.

Implementation note, 2026-06-21: the first runtime implementation uses retained
per-frame in-memory solver snapshots rather than sparse checkpoints plus an event
stream. This is intentionally memory-heavier, but it makes scrub-to-branch
correctness available now. Future work can replace the dense retained snapshot
with checkpoint/event replay once restore hashes and visualization tooling are
proven.

Audit note, 2026-06-22: the saved `solver_replay_####.skreplay` artifact is not
v2. It is version 1 JSON debug/inspection output with dense per-frame body
samples and compact authoritative snapshot summaries. The presentation save path
now writes `replay_v2_####.skreplay` chunked binary v2 artifacts for smooth
visual scrub data only. V2 does not yet serialize full persistent-contact rows,
event streams, authoritative solver checkpoints, or a reloadable authoritative
checkpoint format. Current restore still works only from the retained in-memory
`ReplaySolverFrameSample` chain.

The core rule remains:

```text
Do not run physics backward.
Restore an earlier authoritative checkpoint, then replay deterministic fixed
physics ticks forward to the selected target tick.
```

## User-Facing Shape

1. The runtime records recent replay history as it does today.
2. The scrubber still previews old frames immediately using presentation data.
3. When the user commits to an old frame, runtime restores a solver checkpoint
   before that frame and replays fixed ticks forward to the exact selected tick.
4. The selected point becomes the new live simulation branch.
5. Hashes and SkullScope rows prove that restored state matches the original
   timeline at the selected tick.

## Non-Goals

| Non-goal | Reason |
|----------|--------|
| Reverse integration | Contact solving, sleep, warm-starting, friction, and broadphase state are not reversible. |
| Arbitrary sub-tick restore | Authoritative restore targets fixed physics ticks only; UI interpolation can still preview between ticks. |
| Multiplayer rollback netcode | This is a local simulation debugging and authoring feature. |
| Video-first replay | Video cannot branch, inspect contacts, or validate solver hashes. |
| Lossy authoritative state | Compression is allowed only after deterministic restore tests prove the uncompressed schema. |

## Visualizer V1

The follow-up visualizer is now layered on the retained solver replay samples.
Left-click selects a root body when world input is not already owned by UI,
editor, or launcher firing; `Ctrl+Left Click` selects while launcher mode is
active. The root draws a retained past path from red to white and a retained
future path from white to green, using the current solver scrub sample as
present. Future contact rows involving active traced bodies add child bodies to
the trace; child bodies draw amber incoming paths and amber target rings before
their first collision, then grey post-contact paths plus grey first-contact
markers. `SkullbonezData/scenes/replay_path_pool.scene.json` provides a
pool-table-style chain for manual/screenshot inspection.

The right-side `CAUSE TREE` panel lists the active root and causal children as
an indented hierarchy. Rows are sourced from the retained replay chain or, when
prediction rows are available, from the predicted chain. Clicking a row pauses
into inspection mode when needed and points the camera view target at that body.

The scrubber now also exposes a `PREDICT` checkbox with compact `-`, `+`, and
slider horizon controls, default off and defaulting to 10 seconds. The horizon
clamps from 1 to 10 seconds. When a root body is selected and prediction is
enabled, the runtime starts a sandboxed solver lookahead from the current live
state and advances it as a cancellable drawing job capped at 5 ms of
speculative work per frame. Each slice restores the live world before rendering,
while the saved prediction cursor resumes on the next frame. Partial samples
draw as soon as they are captured, so long horizons fill in over multiple
frames instead of blocking the demo scene. Target, horizon, and velocity edits
cancel the current job, clear the partial paths, and restart from the new live
state. The predicted root path draws white-to-green from the live state; future
bodies contacted by that path draw amber incoming traces and target rings before
first impact, then grey post-contact traces and contact markers. Shift-click can
add multiple retained history roots; the most recently selected root remains the
prediction root. The scrubber also has a `PAUSE`/`PLAY` button for frozen live
inspection with free-camera movement and right-mouse look. The new work is
profiled under `Frame/Replay/Prediction/BeginJob`,
`Frame/Replay/Prediction/Slice`, `Frame/Replay/Prediction/Steps`,
`Frame/Replay/Prediction/StepPhysics`,
`Frame/Replay/Prediction/ApplyJobState`,
`Frame/Replay/Prediction/CaptureJobState`,
`Frame/Replay/Prediction/RestoreLive`, `Frame/Replay/PathVisualizer/...`,
`Frame/Replay/ScrubberInput`, `Frame/Replay/SimulationPause`,
`Frame/Replay/ScrubberOverlay`, and the existing physics scopes, so PIX and the
in-engine profiler can isolate the speculative lookahead and scrubber UI cost.

Replay velocity edit is layered onto that same live prediction path. Outside
editor mode, `Alt` or the scrubber `ALT VEL` toggle pauses into inspection,
enables prediction, and shows a selected dynamic body's linear-axis handles plus
angular velocity rings. Dragging linear handles or angular rings writes the live
body's velocity, wakes it when needed, invalidates the retained physics streams,
and cancels/restarts prediction so the future path and cause tree redraw from
the new state. `SkullbonezData/scenes/replay_velocity_four_ball.scene.json`
provides four resting balls in a line for manually ramping the cue ball and
watching the downstream predicted children appear.

Future work can add numeric velocity fields, explicit parent collision timing
labels, and deeper tree controls.

## Priority Update, 2026-06-22

The immediate user priority is smooth backwards scrubbing, not full
authoritative branch-from-file. That changes the next useful slice:

1. Preserve smooth visual reverse scrub as a dense presentation track.
2. Do not promise saved authoritative restore until `.skreplay` v2 actually
   contains solver checkpoint/event/hash chunks and a loader/verifier.
3. Keep authoritative solver restore as a separate track layered behind the
   scrub UI.

Smooth scrub means previewing retained or saved presentation samples in reverse.
It does not mean running physics backward. If the user later commits to a
historical tick, authoritative restore is still a discrete operation: restore a
solver checkpoint, replay deterministic fixed ticks/events forward, compare the
solver hash, and only then make the state live.

## Current Starting Point

| Area | Current behavior |
|------|------------------|
| Replay recording | `ReplayRecorder` stores a bounded 30-second presentation ring by default for generated/interactive runs. |
| Solver recording | `ReplaySolverRecorder` stores same-tick body data plus retained world snapshots with sleep, contact cache, persistent contacts, tornado state, debug contacts, and launcher visual state. |
| Scrubbing | The bottom hot-zone scrubber previews historical body/camera presentation samples and pauses physics while away from live; solver preview also hides future bodies and swaps in solver-sample launcher visuals for the draw. |
| Branch restore | Press `Enter` while paused on the solver row to directly restore the selected retained in-memory solver frame as the new live branch. This does not yet restore a sparse checkpoint, replay events forward, or hash-gate the restored tick before going live. |
| Path visualizer | Mouse-selected root body draws retained past/future paths; Shift-click adds more retained history roots. Future contacts light child bodies before impact with amber incoming traces/rings, then continue with grey post-contact traces. The right-side cause tree lists the root/child hierarchy and focuses the camera on clicked rows. Optional `PREDICT` runs a sandboxed live solver lookahead for a 1-10 second UI-selected horizon and draws predicted root/child futures from the live edge. `ALT VEL` edits selected dynamic-body linear/angular velocity and rebuilds the predicted chain from the edited live state. |
| Saving | `SAVE` on the presentation row writes `replays\replay_v2_####.skreplay` binary v2 presentation artifacts with `MANI`, `BODY`, `PRES`, and `INDX` chunks. Solver row save still writes `replays\solver_replay_####.skreplay` v1 JSON with compact authoritative snapshot summaries. These files are not reloadable authoritative replay artifacts. |
| Hashing | Replay samples include a presentation hash and optional `--replay-hashes` CSV output; solver hashes include hidden authoritative snapshot state. |
| Determinism evidence | `tools\validate_replay_scrub.bat` uses SkullScope to prove a selected visual replay sample maps to queried body state. `tools\validate_replay_v2_artifact.bat` writes a real runtime v2 presentation artifact, reloads it through the C++ v2 reader, proves an older loaded pose can be applied/restored, queries it, exports a bounded SkullScope slice, and imports that slice through `physics_query`. There is no restore-to-tick hash-match gate or saved authoritative restore test yet. |

## Implementation Status Audit, 2026-06-22

| Plan phase | Status | Current evidence and gap |
|------------|--------|--------------------------|
| Phase 1: State inventory and hash contract | Partial | Solver hashes include body fields and `ReplaySolverWorldSnapshot`, but there is no standalone authoritative-state inventory report and no restore hash gate. |
| Phase 2: In-memory solver checkpoint ring | Partial shortcut | Dense per-frame `ReplaySolverFrameSample` storage exists. `m_checkpoints` stores `ReplayCheckpointSummary` only, not full restorable checkpoints. |
| Phase 3: Event stream | Missing | No typed `ReplayEventStream`, event cursors, or deterministic event replay for launcher, reset/load, world edits, editor commits, or generated-scene rebuilds. |
| Phase 4: Restore to tick without branch resume | Missing | Current restore applies the selected retained in-memory sample directly. It does not restore the nearest checkpoint, replay ticks/events to the target, compare hashes, or emit `replay_restore`. |
| Phase 5: Branch and resume | Partial | `Enter` on the solver row makes the selected retained sample live and resets the replay timeline, but there is no branch id or parent-branch metadata. |
| Phase 6: Saved authoritative replay artifacts | Partial | Presentation saves now produce chunked binary v2 artifacts for smooth scrub playback/querying, with a body dictionary, 32-byte pose records, seek index, C++ presentation loader, `tools\replay_query` bridge, and `tools\validate_replay_v2_artifact.bat` runtime artifact proof. Missing: solver checkpoint chunks, event stream chunks, per-tick solver hash chunks, loaded-file UI source, restore verifier, and branch-from-file. |

Observed local artifact sizing during the 2026-06-22 audit:

| Artifact | Shape | Size |
|----------|-------|------|
| `replays\solver_replay_0001.skreplay` | v1 JSON, 1,751 frames, 3 bodies after frame 256, 30 checkpoint markers | 5.27 MB |
| `replays\solver_replay_0000.skreplay` | v1 JSON, 537 frames, roughly 300 bodies | 118.7 MB |
| `replays\replay_0000.skreplay` | v1 JSON presentation export at similar scale | 133.3 MB |

The current JSON shape repeats body metadata and text numbers per frame. For a
300-body scene it is roughly in the tens of MB per second and is not acceptable
as the normal saved scrub format.

## Definitions

| Term | Meaning |
|------|---------|
| Presentation sample | Render-facing body/camera/world state used for immediate scrub preview. |
| Solver checkpoint | Complete authoritative physics/runtime state needed to resume exactly at a fixed tick. |
| Event stream | Frame-ordered record of user/runtime commands that can affect future solver state. |
| Restore target | The fixed physics tick selected by the scrubber for authoritative resume. |
| Branch | A new live timeline created after restoring an old tick and continuing forward. |

## Recording Frequency

Recommended v1 frequencies:

| Track | Frequency | Purpose |
|-------|-----------|---------|
| Solver checkpoint | Every 0.5 seconds, or every 60 fixed ticks at 120 Hz | Bounded seek cost without storing every solver state. |
| Event stream | Every fixed physics tick | Replays launcher actions, resets, world changes, scene commands, and other solver-affecting inputs. |
| Solver state hash | Every fixed physics tick | Detects divergence after checkpoint restore and tick replay. |
| Presentation sample | 120 Hz initially | Avoids scrub popping and lets preview match current fixed-tick cadence. |
| Optional presentation downsample | 60 Hz or 30 Hz with interpolation after v1 is stable | Reduces saved replay size without weakening authoritative restore. |

Full solver checkpoints control how long restore takes. Dense presentation
samples control how smooth scrubbing feels. They should be treated as separate
tracks.

## Authoritative Checkpoint Contents

The first implementation should prefer correctness and explicit fields over
clever compression. A checkpoint needs enough state to restore, rebuild derived
caches, and then produce the same future fixed-tick hashes.

Checkpoint candidates:

| State | Notes |
|-------|-------|
| Frame counters | Replay frame index, scene frame, fixed physics tick, branch id, and simulation seconds. |
| Fixed-step timing | Fixed-step accumulator, `PHYSICS_FIXED_DT` assumptions, pause/scrub state, and any scene automation counters that affect physics stepping. |
| World scalars | Gravity, fluid height, fluid density, water/terrain debug flags, fixed-step flag, scene physics/text flags. |
| Body transforms | Position, orientation, linear velocity, angular velocity for every physics body. |
| Body constants | Mass, inverse mass, inertia, inverse inertia, fixed flag, restitution, shape identity, contact-release parameters. |
| Sleep state | Sleeping, sleep-supported, sleep-inhibited, sleep timers/counters, island ids, and any wake/sleep thresholds if runtime-mutated. |
| Persistent contacts | Contact manifolds, feature ids, accumulated impulses, warm-start rows, contact ages, friction/restitution bias data. |
| Solver caches | Any cached row ordering or island ordering that can affect determinism. |
| Broadphase state | Prefer deterministic rebuild from restored bodies; serialize only if rebuild order is not byte-stable. |
| Runtime event cursors | Current offset in the replay event stream and command queues. |
| Random state | Generated-scene RNG state and any runtime random seeds that can influence physics. |
| Launcher/projectile state | Projectile pools, raycast/launcher mode state, pending actions, and active projectile bodies. |
| Diagnostics ids | SkullScope run ids and checkpoint ids for query correlation, not for gameplay correctness. |

The inventory phase should decide which derived structures can be rebuilt
deterministically and which must be serialized exactly.

## Event Stream Contents

Between checkpoints, authoritative replay should not record every derived solver
result. It should record the causes that can change future solver state.

Event candidates:

| Event | Examples |
|-------|----------|
| Scene lifecycle | Load scene, reset scene, enter interactive scene run, apply generated object count changes. |
| User physics actions | Fire launcher, spawn projectile, place editor object, move/scale/rotate object when it commits to physics. |
| World changes | Gravity/fluid edits, no-water override, terrain mode changes, tornado defaults or runtime tornado edits. |
| Runtime toggles | Fixed-step-affecting toggles, physics enable/disable, collision/sleep controls if they mutate solver state. |
| Deterministic inputs | Any key/mouse action that creates an impulse, object, force, or runtime command. |

Each event should include the fixed tick it applies on, a stable event type,
payload version, and enough payload data to replay without consulting current UI
state.

## Restore Algorithm

1. Convert scrubber position to a fixed physics tick.
2. Find the latest solver checkpoint at or before the target tick.
3. Pause live simulation and mark the current timeline as branchable.
4. Restore checkpoint state into the physics/runtime owners.
5. Rebuild deterministic derived caches such as broadphase, islands, and render
   body streams.
6. Replay event stream entries and fixed physics ticks until the target tick.
7. Compare restored tick hash against the original recorded tick hash.
8. If hashes match, make the restored state live and resume from that branch.
9. If hashes diverge, keep the current live branch paused, emit a SkullScope
   diagnostic marker, and report restore failure rather than hiding divergence
   with visual blending.

## UI Policy

| Interaction | Behavior |
|-------------|----------|
| Dragging scrubber | Use presentation samples only; no expensive solver restore while dragging. |
| Holding on old frame | Keep visual/presentation pause as today. |
| Commit restore | Add an explicit action later, or treat mouse release away from live as restore only after the user confirms the workflow. |
| Commit restore, v1 | Press `Enter` while paused on the solver row. |
| Dragging back to live | Keep current behavior: resume live simulation without restore. |
| Save replay | Continue saving presentation buffer for now; future `.skreplay` v2 should include checkpoints and event chunks. |

The scrubber must not pop visually during preview. If authoritative restore
lands on a slightly different pose than the preview, that is a correctness bug
to expose with hashes and SkullScope.

## File Format Direction

The current `.skreplay` artifact is JSON and presentation-oriented. A future v2
format should be binary, chunked, seekable, and explicitly split by track:

```text
header
small manifest
chunk table
body dictionary
presentation chunks
solver checkpoint chunks
event stream chunks
per-tick hash chunks
seek index
```

For the smooth-scrub priority, the v2 presentation track should come first:

| Track | Frequency | Contents | Purpose |
|-------|-----------|----------|---------|
| Presentation | 60 or 120 Hz | Body id/index stream, position, orientation, optional visibility, optional camera/world presentation state | Smooth backwards/forwards scrub preview. |
| Solver checkpoint | Sparse, e.g. every 60 fixed ticks | Full authoritative solver/runtime checkpoint | Restore source for branch/replay. |
| Event stream | Every mutating action | Versioned commands with target fixed tick | Deterministic forward replay between checkpoints. |
| Hashes | Every fixed tick | Presentation and solver hashes | Cheap divergence detection and restore proof. |
| Diagnostics | Optional/sliced | Contact/sleep/island summaries or references to SkullScope traces | Debugging without inflating every file. |

Rough uncompressed dense presentation sizing target:

```text
300 bodies * 32 bytes/body/frame * 120 fps * 30 sec = about 34.5 MB
300 bodies * 32 bytes/body/frame *  60 fps * 30 sec = about 17.3 MB
```

That assumes body metadata is deduplicated and dense samples are binary, not
JSON. Compression and delta encoding can come later, after the uncompressed
format and query path are correct.

Short-term solver export can remain JSON for inspectability while the legacy
debug path is still useful. Presentation save now writes binary v2 because
smooth backwards scrubbing is the immediate priority and the dense pose stream
was too large as JSON. Compression and delta encoding can come later, after the
uncompressed format and query path are correct.

### SkullScope And Agent Queryability

Binary v2 files must not become opaque to debugging. The required companion is a
focused query bridge:

```bat
tools\replay_query.bat replays\sample.skreplay summary
tools\replay_query.bat replays\sample.skreplay frame 1240
tools\replay_query.bat replays\sample.skreplay body 7 --frames 1200:1260
tools\replay_query.bat replays\sample.skreplay contacts --frames 1200:1260
tools\replay_query.bat replays\sample.skreplay export-skullscope --frames 1200:1260 --out Debug\sample_slice.physicsdiag.ndjson
```

Implementation options:

1. `tools\replay_query.py/.bat` decodes v2 chunk tables and exposes bounded JSON
   answers for agents and humans.
2. Reuse the existing `physics_query.py` cache style by importing selected v2
   chunks into SQLite tables.
3. `export-skullscope` writes selected frame windows so existing
   `tools\physics_query.bat` workflows can inspect replay slices without loading
   huge raw artifacts into the model.

The rule for future saved replay debugging is the same as SkullScope: query
small windows and selected bodies/contacts, never paste or ingest whole replay
files.

## Implementation Phases

### Phase 1: State Inventory And Hash Contract

1. Audit physics/runtime owners touched by one fixed tick.
2. List every mutable field that can affect the next tick.
3. Split state into authoritative, derived-rebuildable, diagnostic-only, and
   presentation-only categories.
4. Define a solver hash that includes authoritative state, not just presentation
   state.
5. Add a report documenting any unknown or suspicious state.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| State inventory | Every solver-affecting owner has an explicit checkpoint decision. |
| Hash draft | Hash changes when any authoritative body/contact/sleep state changes. |
| Validation selection | Implementation phase names `tools\validate_physics.bat` or deeper gate as needed. |

### Phase 2: In-Memory Solver Checkpoint Ring

1. Add `ReplaySolverCheckpoint` records for full authoritative snapshots.
2. Capture one checkpoint every 60 fixed ticks by default.
3. Keep checkpoints in a bounded ring aligned with replay retention seconds.
4. Add stats for checkpoint count, capacity, bytes, and latest checkpoint tick.
5. Do not expose restore in UI yet.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Capture overhead | Bounded memory use; no per-frame unbounded allocations after warmup. |
| Checkpoint cadence | 30-second buffer has roughly 60 full checkpoints at 120 Hz. |
| Existing scrubber | Presentation scrub still behaves as before. |

### Phase 3: Event Stream

1. Add a typed event stream for solver-affecting runtime actions.
2. Route launcher fire, scene reset/load, world changes, editor commits, and
   generated-scene rebuilds through explicit event records.
3. Add event payload versions and deterministic tick ordering.
4. Store event cursors in checkpoints.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Event coverage | Known solver-mutating user actions are represented by event records. |
| No UI dependency | Event replay does not read current UI widget state. |
| Ordering | Multiple events on one tick replay in original order. |

### Phase 4: Restore To Tick Without Branch Resume

1. Implement restore into a temporary paused runtime state.
2. Restore the checkpoint before a selected tick.
3. Replay fixed ticks/events forward to the target tick.
4. Compare the restored solver hash with the originally recorded hash.
5. Emit a SkullScope `replay_restore` row with checkpoint tick, target tick,
   hash match, body count, and first mismatch summary.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Hash match | Restoring to recent retained ticks matches original solver hash. |
| SkullScope query | Query can show checkpoint tick, target tick, and restore result compactly. |
| Failure mode | Divergence is reported and does not silently become live state. |

### Phase 5: Branch And Resume

1. Add a deliberate UI action for "resume from here" or equivalent workflow.
2. On success, make restored state the live branch and resume fixed physics.
3. Assign a branch id and reset future presentation/checkpoint/event history.
4. Preserve enough parent-branch metadata for saved replay artifacts.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Resume behavior | Simulation continues from the restored tick, not from the old live edge. |
| Branch isolation | Old future samples are not mixed into the new branch. |
| Live-edge behavior | Dragging scrubber back to live still resumes without creating a branch. |

### Phase 6: Saved Authoritative Replay Artifacts

1. Add `.skreplay` v2 chunks for solver checkpoints, event streams, and hashes.
2. Keep body metadata deduplicated from per-frame samples.
3. Add optional compression only after uncompressed restore tests pass.
4. Add a loader or offline verifier that can restore selected ticks from the
   saved artifact.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Artifact restore | Saved replay can restore a selected tick in a fresh process or verifier path. |
| Size accounting | Export reports raw and compressed sizes by chunk type. |
| Compatibility | v1 presentation-only replay artifacts fail gracefully or load as preview-only. |

## Test Strategy

| Test | Purpose |
|------|---------|
| Focused restore smoke | Capture a short deterministic scene, restore several retained ticks, assert solver hash match. |
| SkullScope restore query | Verify restored body/contact/sleep state against original trace rows without loading raw traces into the model. |
| Branch resume test | Restore an old tick, resume, and prove new branch diverges only after intentional new input. |
| Long buffer test | 30-second generated 300-object scene retains bounded checkpoints/events without memory growth. |
| Save/load test | Saved `.skreplay` v2 restores a selected tick after process restart. |

Required gates by implementation phase:

| Phase | Gate |
|-------|------|
| Docs only | No validation required. |
| Replay UI/runtime only | `tools\validate_full.bat`. |
| Solver checkpoint/restore | `tools\validate_physics.bat`. |
| SkullScope replay restore diagnostics | `tools\validate_physics_deep.bat`. |
| Hot-path memory/perf changes | `tools\validate_perf.bat` in addition to physics gate. |

## Risks And Mitigations

| Risk | Mitigation |
|------|------------|
| Missing hidden solver state causes divergence | Phase 1 inventory plus hash mismatch failure mode before live branch resume. |
| Checkpoint memory gets too large | Start with 0.5-second checkpoints, report checkpoint bytes, then dedupe/compress after correctness. |
| Restore stutters during drag | Keep solver restore out of drag preview; restore only on explicit commit. |
| Event stream misses a mutating action | Route known solver mutations through named runtime commands and add tests for each event family. |
| Broadphase rebuild order is nondeterministic | Add deterministic rebuild ordering or serialize required broadphase state. |
| Saved replay format hardens too early | Keep v1 JSON inspectable; move to chunked/binary only after restore proof. |

## Open Questions

1. Should "resume from here" happen on mouse release away from live, a small
   scrubber button, or a confirmation prompt?
2. Should checkpoint cadence be configurable from CLI, scene files, or only
   engine config?
3. Which solver caches can be rebuilt deterministically versus serialized?
4. Should `.skreplay` v2 prioritize fresh-process restore, in-process branch
   resume, or both in the first shipping slice?
5. How much mismatch detail should the UI show before handing the user to
   SkullScope queries?

## Suggested First Agent Task

Create a read-only authoritative-state inventory:

1. Inspect `SkullbonezGameModelCollection`, physics world/caches, persistent
   contact solver, sleep island system, broadphase, runtime fixed-step timing,
   and replay capture points.
2. Produce a table of every field needed for checkpoint restore.
3. Classify each field as serialize, rebuild deterministically, diagnostic-only,
   or presentation-only.
4. Propose the first `ReplaySolverCheckpoint` struct shape and hash inputs.

No code changes should happen until that inventory is reviewed.
