# Replay System Plan

Date: 2026-06-11
Status: Draft implementation plan
Source: Extracted from `Agentic/Plans/architecture_pass_2026-06-02.md`
Impact area: scene system, runtime, physics diagnostics, tests, UI, render capture
Validation for this document-only change: none required

## Goal

Build a replay system that continuously records simulation history, lets the
operator scrub backward and forward through recent frames, branches from an old
frame, and exports interesting moments as deterministic regression artifacts.

The central design rule is:

```text
Do not run physics backward.
Restore earlier state, then replay deterministic fixed ticks forward.
```

Reverse timesteps are not a reliable target for this engine. Contact solving,
friction, warm-starting, sleeping, restitution bias, broadphase pair order,
projectile pooling, and floating-point truncation all lose information. A
scrubbable replay should instead be built from checkpoints, compact per-frame
samples, and deterministic event replay.

## User-Facing Shape

The desired workflow is:

1. Start any scene with replay recording enabled.
2. The runtime keeps a bounded always-on replay buffer.
3. The user pauses and drags a timeline slider or steps frame by frame.
4. The viewport renders the selected historical frame without advancing physics.
5. The user can resume from the selected frame as a new branch.
6. The selected frame or branch can be exported as a `.scene`, `.suite`, or
   replay artifact for validation.

Initial use cases:

| Use case | Value |
|----------|-------|
| Scrub a stack collapse or high-speed collision | Inspect exact frame-to-frame behavior without rerunning manually. |
| Step through contact solver stages | Make impulses, manifolds, and sleep decisions visible. |
| Branch from a suspicious frame | Try a different nudge, water height, force, or renderer state. |
| Export a bug moment | Turn a visual or physics surprise into a regression case. |
| Compare GL, DX11, and DX12 on the same frame | Anchor renderer parity to one recorded timeline. |

## Non-Goals

| Non-goal | Reason |
|----------|--------|
| Reverse physics integration | Not stable or bit-exact for this solver. |
| Video capture as the primary replay | Video is useful for sharing, but cannot branch or inspect state. |
| Raw full-diagnostic ingestion | Use compact timeline records and SkullScope queries instead of loading whole NDJSON/CSV files into the model. |
| Network rollback architecture | This plan is for local simulation debugging, not multiplayer prediction. |
| Whole-engine editor rewrite | The first version should work inside the existing runtime and UI. |

## Current Ingredients

The engine already has most of the foundations:

| Ingredient | Current role |
|------------|--------------|
| Fixed 120 Hz physics tick | `PHYSICS_FIXED_DT` is the deterministic simulation quantum. |
| Scene `fixed_step` mode | Maps exact fixed physics ticks to rendered frames. |
| Scene and CLI `seed` | Makes generated scenes and repros repeatable. |
| F2 scene snapshots | Saves a live moment as `ball_state` scene data. |
| Nudge repro snapshots | Captures focused body state and replay hints in Debug builds. |
| SkullScope physics diagnostics | Emits queryable frame/contact/island/solver data. |
| Pipeline debug overlay | Already has a stage cursor stepped with F7/F8. |
| Profiler timeline | Provides a UI precedent for per-frame/timeline visualization. |
| Screenshot and renderer validation | Provides render checkpoints and parity comparison artifacts. |

The missing piece is a first-class replay owner that records these ingredients
as one coherent timeline instead of one-off debug files.

## Architecture Overview

```text
SkullbonezRun
  -> ReplayController
       -> ReplayRecorder
       -> ReplayPlayback
       -> CheckpointRing
       -> ReplayEventStream
       -> PhysicsTimelineStore
       -> RenderCheckpointStore
       -> ReplayExporter
```

| Component | Responsibility |
|-----------|----------------|
| `ReplayController` | Owns recording, pause, scrub, branch, playback, and export state. |
| `ReplayRecorder` | Samples runtime state after each committed fixed tick and records high-level events. |
| `ReplayPlayback` | Restores checkpoints, replays events forward, and presents historical frames. |
| `CheckpointRing` | Keeps bounded authoritative snapshots so rewind never starts from frame zero. |
| `ReplayEventStream` | Stores frame-ordered runtime commands, input-derived actions, camera changes, resets, nudges, and renderer switches. |
| `PhysicsTimelineStore` | Stores compact per-frame body/contact/island/sleep/solver records for UI and SkullScope-style queries. |
| `RenderCheckpointStore` | Stores screenshot references, viewport size, renderer name, and optional pass capture IDs. |
| `ReplayExporter` | Writes selected frames or branches as `.scene`, `.suite`, `.skreplay`, or validation cases. |

Early implementation can live as an adapter around `SkullbonezRun` and
`GameModelCollection`. Longer term, replay should consume stable boundaries
such as `SceneRuntime`, `SimulationSystem`, `RenderPipeline`, and `PhysicsWorld`
rather than scraping state from monolithic runtime code.

## Replay Semantics

### Live Recording

Live recording appends one timeline frame per committed simulation tick. In
scene `fixed_step` mode that is usually one replay frame per rendered frame. In
non-fixed scene mode, physics can run zero or more fixed ticks during a render
frame, so the replay frame index should track physics ticks, not wall-clock
frames.

Record after the tick has fully committed:

```text
process input/UI commands
apply queued runtime commands
run zero or more fixed physics ticks
after each fixed tick:
  capture transform sample
  capture timeline records
  capture hash
render current frame
```

### Scrubbing

Scrubbing selects an existing replay frame. The simulation clock is paused while
scrubbing. The renderer gets historical presentation data from the replay store
instead of the live simulation state.

Two tiers make this responsive and bounded:

| Tier | Contents | Purpose |
|------|----------|---------|
| Presentation samples | Per-frame transforms, velocities, sleep flags, contact summaries, camera, world presentation state | Instant slider scrubbing and visual inspection. |
| Authoritative checkpoints | Full restore state, including solver caches and runtime state | Branching, exact resume, and deterministic forward replay. |

For visual-only scrubbing, presentation samples are enough. For "resume from
here" or exact state inspection, restore the nearest authoritative checkpoint at
or before the target frame and replay deterministic events forward to the target.

### Branching

Resuming from a past frame creates a new branch:

```text
main branch:    0 ... 1200 ... 1500
selected frame:          ^
new branch:              1200' ... 1201' ... 1202'
```

The original future frames should remain inspectable until the ring buffer
evicts them. The active branch becomes the live recording head. Branch metadata
records its parent replay id and source frame.

### Playback

Playback of a saved `.skreplay` should force deterministic settings:

| Setting | Playback behavior |
|---------|-------------------|
| `fixed_step` | Force on for simulation replay. |
| `PHYSICS_FIXED_DT` | Must match the recorded manifest. |
| RNG seed/state | Restore from the manifest and checkpoints. |
| Wall-clock time | Ignored for deterministic simulation. |
| Vsync | Not part of physics replay; may be overridden for presentation. |
| Renderer | Can default to recorded renderer, but may be overridden for parity tests. |

## Data Model

### Replay Manifest

The manifest is small and human-readable. It should contain enough context to
reproduce the run and detect incompatible playback.

```text
replay_version 1
engine_commit <git-sha-or-dirty-marker>
created_utc 2026-06-11T09:37:57Z
scene SkullbonezData/scenes/example.scene
renderer dx12
viewport 1280 720
fixed_step 1
physics_dt 0.008333333
seed 12345
branch main

config {
  vsync 0
  pipeline_sync 1
  time_scale 1.0
  gravity -9.8
  fluid_height 0.0
  fluid_density 1000.0
}

files {
  events events.bin
  checkpoints checkpoints.bin
  timeline timeline.ndjson
}
```

The first implementation can use a simple line-oriented text format matching
existing scene-style parsing. If binary chunks are added, keep the manifest text
and version every chunk.

### Event Stream

Record high-level commands after runtime input processing, not raw hardware key
states. High-level commands are easier to replay deterministically and stay
stable if the UI layout changes.

Event examples:

```text
frame 12 command reset_scene
frame 18 camera_pose eye=0.0,4.0,-9.5 view=0.0,-0.3,1.0 up=0.0,1.0,0.0
frame 24 nudge body=box_03 impulse=0.0,4.0,0.0
frame 33 projectile_fire origin=1.0,3.0,-5.0 dir=0.0,0.0,1.0 speed=80.0
frame 40 world fluid_height=2.5
frame 55 renderer dx11
frame 72 ui time_scale=0.25
```

Minimum event types:

| Event | Needed for |
|-------|------------|
| Scene load/reset/rerun | Reproduce repeated `R` and scene queue behavior. |
| RNG seed/state changes | Reproduce generated object populations. |
| Camera pose and tracking changes | Replay screenshots and branch context. |
| Nudge/projectile fire | Reproduce manual interactions. |
| Water/gravity/time-scale changes | Reproduce runtime simulation edits. |
| Renderer switch | Reproduce render parity/debug sessions. |
| Physics debug mode changes | Reproduce contact/pipeline inspector state. |
| UI command summaries | Replay slider/toggle edits independent of raw mouse coordinates. |

### Presentation Sample

Captured every replay frame for immediate scrubbing:

| Field | Notes |
|-------|-------|
| Frame index and simulation time | Use physics tick count as source of truth. |
| Camera pose | Eye, view, up, active tracking state. |
| World presentation state | Gravity, fluid height, terrain/water visibility, render toggles. |
| Body sample array | Stable body id, position, orientation, linear/angular velocity, sleep/support/contact flags. |
| Contact summary | Bounded contacts for overlays and timeline rows. |
| State hash | Fast comparison for replay validation. |

### Authoritative Checkpoint

Captured every N frames, plus on branch/export boundaries. The interval should be
configurable. A good starting default is 30 ticks for large scenes and every
frame for small debug scenes.

Checkpoint fields:

| Area | Required fields |
|------|-----------------|
| Runtime | Scene path, scene mode flags, frame counters, time scale, fixed-step accumulator, scene load/reset counts, active renderer, UI overrides. |
| RNG | Explicit RNG state. If the runtime still uses CRT `rand`, migrate replay paths to an owned deterministic RNG before claiming exact replay. |
| World | Gravity, fluid surface height, fluid density, water/terrain visibility and animation state. |
| Camera | Camera collection state, fly/nudge/tracking state, tween/autocycle state. |
| Physics bodies | Stable id, shape, mass/inertia, position, orientation, velocities, force accumulators if any, fixed/dynamic state, sleep state, support metadata. |
| Solver | Persistent contact cache, warm-start impulses, terrain support classification, sleep timers/inhibition state. |
| Projectile pool | Active projectiles and pooled bullet state. |
| Diagnostics | Pipeline cursor and debug overlay mode. |

Bit-exact branching depends on capturing solver and sleep state, not just
transforms. A transform-only checkpoint is acceptable only for the first visual
scrub milestone.

### Timeline Records

Timeline records are not authoritative restore state. They are compact query and
UI data:

| Record | Purpose |
|--------|---------|
| Body state summary | Show tracked bodies and state hash deltas. |
| Contact manifold | Show contact normals, penetration, feature ids, and owning bodies. |
| Solver row | Show accumulated normal/tangent impulses, bias, restitution, and warm-start source. |
| Island/sleep | Explain why bodies sleep, wake, or remain inhibited. |
| Broadphase stats | Explain candidate-pair spikes. |
| Pipeline stages | Reuse the Catto stage visualizer data. |
| Profiler markers | Align physics/render cost with replay frames. |
| Render checkpoints | Associate screenshots and pass captures with replay frames. |

SkullScope can either consume these records or emit a sibling query cache. The
rule remains: expose bounded query results to agents, not whole raw traces.

## Stable Identity

Replay needs stable object ids. Vector indices are usable for a prototype, but
they are fragile once objects are pooled, removed, reset, or exported.

Add a replay-facing id layer:

```text
ReplayBodyId
ReplayContactId
ReplayBranchId
ReplayFrameIndex
```

Requirements:

| Requirement | Reason |
|-------------|--------|
| Body ids persist across reset/replay within a branch | Timeline rows need stable references. |
| Exported scenes preserve names when available | Debugging should stay human-readable. |
| Projectile pool entries have ids | Bullet/nudge interactions must replay exactly. |
| Contact ids are deterministic | Contact inspector should track the same pair/features over time. |

## File Layout

Saved replay directory:

```text
TestOutput/replays/<name>/
  replay.txt
  events.bin
  checkpoints.bin
  timeline.ndjson
  screenshots/
    frame_000120_dx12.png
  exports/
    frame_000120.scene
```

In-memory always-on replay can use the same data model without writing to disk.
Disk persistence should be opt-in at first:

```bat
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\solver_smoke.scene --replay-record TestOutput\replays\smoke
```

Possible future command-line switches:

| Switch | Meaning |
|--------|---------|
| `--replay on` | Enable bounded in-memory recording. |
| `--replay off` | Disable replay recording. |
| `--replay-seconds N` | Keep the last N seconds in the ring. |
| `--replay-record path` | Write a replay artifact while running. |
| `--replay-play path` | Load and play a replay artifact. |
| `--replay-export-frame N path` | Export frame N as a scene or validation case. |

## Memory Budget

Always-on replay must be bounded and transparent.

Rough order-of-magnitude estimate:

```text
body presentation sample ~= 80 to 160 bytes per body per frame
300 bodies * 120 Hz * 30 seconds * 128 bytes ~= 138 MB
1200 bodies * 120 Hz * 30 seconds * 128 bytes ~= 552 MB
```

So the default design should be tiered:

| Data | Retention |
|------|-----------|
| Presentation samples | Last N seconds, configurable. |
| Authoritative checkpoints | Every 30 ticks by default, denser for small scenes. |
| Timeline records | Bounded per frame, with caps for contact/solver rows. |
| Screenshots/pass captures | Explicit bookmarks only, not every frame. |

OpenGL/DX render resources should never be serialized into replay checkpoints.
Rebuild GPU resources from scene/assets and replay only source-level state.

## UI Plan

Add replay controls to the existing in-game UI rather than introducing a new
tool window in the first pass.

Expected controls:

| Control | Behavior |
|---------|----------|
| Record toggle | Enables or disables the bounded ring. |
| Pause/scrub mode | Freezes live simulation and presents selected replay frame. |
| Timeline slider | Selects replay frame or branch frame. |
| Step previous/next | Moves one replay frame. |
| Branch from here | Restores selected frame and resumes recording as a branch. |
| Export frame | Writes `.scene` or replay artifact for the selected frame. |
| Overlay toggles | Contacts, islands, sleep, profiler, render checkpoints. |

Keyboard bindings should be chosen after checking current runtime bindings. Avoid
colliding with F2/F3 snapshots, F7/F8 pipeline cursor, and existing fly/nudge
controls.

## Implementation Phases

### Phase 0: Standalone Plan And Naming

Status: this document.

Tasks:

1. Keep this plan as the source of truth for replay work.
2. Use the architecture pass only as an index/pointer.
3. Decide names for replay file extension, command-line flags, and UI labels.

Validation: none for documentation-only changes.

### Phase 1: Snapshot Interfaces And State Hashes

Goal: define the minimal state needed to sample and restore a frame.

Tasks:

1. Add replay-facing structs for presentation samples and authoritative
   checkpoints.
2. Add stable body ids to runtime physics objects.
3. Add a deterministic physics state hash over body state and important solver
   state.
4. Add capture-only code paths with no gameplay behavior change.
5. Add a small test scene or harness that prints frame hashes for a fixed-step
   scene.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Fixed-step scene run twice | Frame hashes match byte-for-byte. |
| Capture disabled | No measurable behavior change. |
| Capture enabled | No gameplay behavior change except bounded memory use. |

Validation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

Use `tools\validate_full.bat` if the work touches `SkullbonezRun*` broadly.

### Phase 2: Event Recording And Manifest

Goal: record reproducible runtime events without restoring yet.

Tasks:

1. Add `ReplayManifest` writer.
2. Add `ReplayEventStream` writer.
3. Record high-level runtime commands after input/UI processing.
4. Record scene load/reset counts and RNG seed/state.
5. Add command-line opt-in for writing replay artifacts.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Run a scene with `--replay-record` | `replay.txt` and event stream are written. |
| Press reset, switch renderer, fire nudge projectile | Event stream records high-level events at deterministic frames. |
| Replay artifact inspected by hand | Manifest has enough context to relaunch the run. |

Validation:

```bat
tools\validate_fast.bat
```

Use `tools\validate_full.bat` if scene load/reset behavior changes.

### Phase 3: In-Memory Presentation Scrub

Goal: make the viewport scrub recent history without branching or resim.

Tasks:

1. Add bounded `CheckpointRing` storage for presentation samples.
2. Add pause/scrub UI controls.
3. Render selected historical transforms while simulation is paused.
4. Overlay contacts/sleep/island summaries from timeline records.
5. Keep live recording bounded and evict old frames predictably.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Run physics scene for 30 seconds | Timeline contains the configured time window. |
| Pause and scrub backward | Viewport shows old body positions without advancing live physics. |
| Scrub forward | Viewport returns to later recorded frames. |
| Resume at live head | Simulation continues normally. |

Validation:

```bat
tools\validate_full.bat
tools\validate_perf.bat
```

### Phase 4: Authoritative Restore And Branch

Goal: restore simulation from a selected frame and continue as a new branch.

Tasks:

1. Implement authoritative checkpoint capture.
2. Implement checkpoint restore.
3. Replay events from checkpoint to target frame.
4. Add `Branch from here`.
5. Preserve original branch history until ring eviction.
6. Compare restored frame hash with recorded target frame hash.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Restore to checkpoint frame | Hash matches recorded frame. |
| Restore to non-checkpoint frame | Restore plus forward replay reaches matching hash. |
| Branch and nudge differently | New branch diverges while old branch remains inspectable. |
| Export branch frame | Exported scene loads with the selected state. |

Validation:

```bat
tools\validate_physics.bat
tools\validate_full.bat
```

Run `tools\validate_perf.bat` if restore/capture changes hot path storage or
allocation behavior.

### Phase 5: Saved Replay Playback

Goal: load `.skreplay` artifacts and replay them deterministically.

Tasks:

1. Add replay artifact reader.
2. Add `--replay-play <path>`.
3. Force deterministic playback settings.
4. Rebuild scene/assets from source, then apply checkpoint/event state.
5. Add per-frame hash compare mode.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Record then play fixed-step scene | Physics hashes match. |
| Override renderer during playback | Physics hashes still match. |
| Incompatible manifest | Runtime fails clearly with a useful message. |

Validation:

```bat
tools\validate_full.bat
```

If renderer screenshots are added:

```bat
tools\validate_renderers.bat
```

### Phase 6: Physics Timeline And Contact Inspector

Goal: make replay useful for solver debugging, not just visual scrubbing.

Tasks:

1. Store bounded contact/manifold/solver/sleep/island timeline records.
2. Add UI inspection for selected body/contact/frame.
3. Link pipeline stage cursor to replay frame selection.
4. Add SkullScope query export for replay segments.
5. Keep GPT-facing query output bounded.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Select a contact | UI shows normal, penetration, impulse, friction, bodies, and support source. |
| Step pipeline stage | Overlay stays aligned with selected replay frame. |
| Export SkullScope segment | Query tools can inspect the selected replay window. |

Validation:

```bat
tools\validate_physics.bat
```

If SkullScope query tooling changes, also run the relevant `tools\physics_query.bat`
smoke queries on a generated trace and report query sizes.

### Phase 7: Renderer And Profiler Integration

Goal: align replay frames with render artifacts and performance investigation.

Tasks:

1. Bookmark screenshots/pass captures for selected frames.
2. Store renderer, viewport, and capture metadata in the replay timeline.
3. Add cross-renderer playback/capture for the same replay frame.
4. Link profiler markers to replay frame selection.
5. Add slow-frame bookmarks with replay metadata.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Capture frame N in GL/DX11/DX12 | Images align to the same replay frame. |
| Select profiler spike | UI can jump to the replay frame and show relevant scene state. |
| Export render regression | Generated suite/captures can be validated later. |

Validation:

```bat
tools\validate_renderers.bat
tools\validate_perf.bat
```

### Phase 8: Regression Export

Goal: turn replay discoveries into durable tests.

Tasks:

1. Export selected frame as `.scene` using full body state.
2. Export selected segment as `.skreplay`.
3. Export screenshot expectations and physics hash expectations.
4. Optionally generate `.suite` files for replay/capture batches.
5. Document replay artifact workflow in `Agentic/Reference/runtime-reference.md`.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Export frame as scene | Scene loads and visually matches the selected replay frame. |
| Export segment as replay | Replay plays back with matching hashes. |
| Export renderer comparison | Validation can compare all three backends on the same selected frame. |

Validation:

```bat
tools\validate_full.bat
tools\validate_renderers.bat
tools\validate_physics.bat
```

## First Useful Slice

The smallest worthwhile implementation is not saved playback. It is an
in-memory visual scrubber:

1. Add replay presentation samples for body transforms, velocities, sleep flags,
   camera pose, world presentation state, frame index, and hash.
2. Keep the last 30 seconds in a bounded ring.
3. Add a pause/scrub UI slider.
4. Render selected historical samples without changing live simulation.
5. Export selected sample through the existing scene snapshot path.

This gives immediate value and teaches the engine what state is missing before
the harder exact-branch work.

## Risks And Mitigations

| Risk | Mitigation |
|------|------------|
| Missing solver cache state causes branch divergence | Treat transform-only scrub as visual only; require warm-start/contact/sleep cache capture for authoritative restore. |
| `SkullbonezRun` remains too broad | Start with a replay adapter, then move ownership behind future runtime subsystem boundaries. |
| CRT `rand` cannot be restored portably | Introduce an owned deterministic RNG state for replay-affecting paths. |
| Memory grows too quickly | Use tiered samples, configurable retention, bounded per-frame contact rows, and explicit screenshot bookmarks. |
| Object ids shift after reset/pooling | Add stable replay ids for bodies, contacts, projectiles, and branches. |
| UI events replay differently than raw input | Record high-level commands after UI/input interpretation. |
| Renderer state leaks into physics replay | Serialize source/runtime state only; rebuild backend resources from assets. |
| Hot path allocations hurt perf scenes | Preallocate ring storage and run `validate_perf` for capture paths. |

## Open Questions

| Question | Default answer for planning |
|----------|-----------------------------|
| Should always-on replay be enabled by default? | Start opt-in through config/CLI; later enable for Debug/Profile with a conservative buffer. |
| What extension should saved replay use? | Use `.skreplay` unless a better project convention appears. |
| How long should the default ring be? | 30 seconds for large scenes, configurable. |
| Should every frame be an authoritative checkpoint? | Only for small debug scenes; use periodic checkpoints for broad scenes. |
| Should saved replays include screenshots? | Only bookmarked frames, not every frame. |
| Should replay support non-fixed wall-clock scenes? | Record fixed physics ticks; presentation can note render frames, but deterministic playback should force fixed-step. |

## Definition Of Done

A complete replay system is done when:

1. A fixed-step scene can be recorded and played back with matching physics
   hashes.
2. The user can scrub backward and forward through recent simulation history.
3. The user can branch from a previous frame and continue simulation.
4. Contact, sleep, island, and solver timeline data are inspectable for selected
   frames.
5. Selected frames can be exported as deterministic scenes or replay artifacts.
6. Renderer captures can be tied to exact replay frames for GL/DX11/DX12 parity.
7. Replay memory and hot-path overhead are bounded and validated.
8. Runtime documentation explains the command-line and UI workflow.

