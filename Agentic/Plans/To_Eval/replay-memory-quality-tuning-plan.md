# Replay Memory Quality Tuning Plan

Date: 2026-06-25
Status: Draft
Impact area: replay runtime, replay artifact model, in-game UI, command-line/config, diagnostics, performance
Validation note: plan-only edits require no validation. Runtime implementation
changes should use the phase-specific validation gates below.

## Goal

Reduce replay memory substantially while keeping the default visual scrubber
lossless in look and feel.

The intended default is:

```text
Visual replay remains exact by default.
Solver replay becomes compact by default.
The user can intentionally trade precision, density, and solver detail for memory
through explicit sliders and presets.
```

This is not a request to make replay look worse. The default path should produce
the same visible object positions and orientations as the current full per-frame
samples. More aggressive quantization, lower sample rates, and sparse thresholds
should exist as visible controls for high-object-count scenes, stress scenes, or
memory-limited runs.

## Current Situation

Replay currently keeps large resident rings of full presentation and solver
samples.

Relevant source:

| Area | File | Current behavior |
|------|------|------------------|
| Replay retention defaults | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` | `REPLAY_PAST_BUFFER_SECONDS` defaults the replay window; `ReplayRecorderConfig` exposes retention and checkpoint interval. |
| Replay tick cadence | `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` | Samples are sized from seconds times `REPLAY_TICKS_PER_SECOND`, currently 120 Hz. |
| Presentation body sample | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` | Stores id, model index, name, shape, pose, velocity, flags, and contact summary per body per frame. |
| Solver body sample | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` | Stores presentation fields plus solver mass/inertia fields per body per frame. |
| Solver snapshot | `SkullbonezSource/Runtime/Replay/ReplaySolverSnapshot.h` | Stores many per-body vectors, contact rows, cache rows, debug contacts, pipeline trace, and collision keys. |
| Solver snapshot capture | `SkullbonezSource/Physics/PhysicsWorld.cpp` | Copies full physics snapshot vectors every solver replay frame. |
| Runtime recorder config | `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp` | Configures presentation and solver recorders together, with solver checkpoint interval hard-coded separately. |
| Visual scrub application | `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp` | Applies stored sample positions and orientations directly to models during scrub. |
| Saved artifact precedent | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` | V2 artifacts already deduplicate body metadata and use dense 32-byte pose records for smooth scrub preview. |
| Existing sliders | `SkullbonezSource/UI/UITabOptions.cpp` and `SkullbonezSource/UI/UITabOptions.h` | Options tab has the simplest existing slider command pattern. |

The big memory cost comes from storing everything every tick, especially solver
snapshots. A 4000-object replay can climb into multi-GiB resident memory even
for the default retention window.

## Design Principles

1. Preserve visual quality by default.
2. Separate visual replay quality from solver/debug fidelity.
3. Split static metadata from per-frame dynamic state.
4. Use sparse carry-forward state for unchanged bodies.
5. Make every lossy behavior explicit and user-adjustable.
6. Keep restore/debug correctness measurable with hashes.
7. Keep hot-path allocation bounded and preallocated.
8. Keep UI controls understandable: presets first, sliders for detail.

## Terminology

| Term | Meaning |
|------|---------|
| Body dictionary | Stable metadata stored once per retained body: replay id, model index, name, shape, mass, inertia, fixed/static traits. |
| Visual pose | Position plus orientation for a rendered body. This applies to ordinary objects, debris, projectiles, vehicles, ragdoll parts, and any model captured as replay state. |
| Visual delta | A per-frame body record emitted only when a visual pose or visible flag changed enough to matter under the current policy. |
| Carry-forward | If a body has no delta for a frame, its previously reconstructed pose remains active. |
| Visual keyframe | A full visual pose frame that resets reconstruction cost and gives precise seek points. |
| Solver keyframe | A full solver restore snapshot. This is for authoritative restore/debug, not visual smoothness. |
| Solver delta | Changed-index/cell/contact updates between solver keyframes. |
| Quantization | Encoding positions/orientations into a lower precision grid or integer representation. |
| Sparsity threshold | Minimum change before emitting a delta. A zero threshold means exact carry-forward only. |

## User-Facing Shape

Add a Replay Quality section to the in-game UI. The first version can live in
Options if adding a new tab is too much surface area, but the end state should be
a dedicated Replay tab once replay controls grow beyond a compact section.

The UI should expose presets and sliders. Presets set a group of slider values,
but the sliders remain editable after a preset is chosen.

### Presets

| Preset | Visual goal | Solver/debug goal | Expected memory impact |
|--------|-------------|-------------------|------------------------|
| Lossless Look | Exact 120 Hz visual poses, no quantization, zero sparse threshold | Periodic solver keyframes plus compact exact deltas | Large reduction versus current full solver snapshots; visuals should match today. |
| Balanced | 120 Hz visual sparse deltas with tiny thresholds and conservative quantization | 1-2 second solver keyframes plus contact/cache deltas | Big reduction with negligible visual difference in normal scenes. |
| Memory Saver | 60 Hz visual poses with interpolation, moderate quantization, stronger sparsity | Sparse solver checkpoints, summaries over full debug rows | Much lower memory; visual scrub may soften fast impacts. |
| Diagnostics Heavy | 120 Hz lossless visual, frequent solver keyframes, high contact/debug detail | Richest retained solver data | Debugging-first; higher memory by design. |
| Custom | Whatever the user last adjusted | Whatever the user last adjusted | Displays when sliders no longer match a preset. |

### Sliders And Controls

All numeric controls should be clamped, display their actual runtime value, and
feed a live memory estimate.

| Control | Type | Range | Default lossless value | Notes |
|---------|------|-------|------------------------|-------|
| Replay retention | Slider | 5-600 seconds | 60 seconds | Existing `--replay-seconds` should remain the CLI equivalent. |
| Memory budget | Slider | 256-8192 MiB, plus Unlimited | 2048 MiB or project default | Advisory estimate until budget enforcement lands; after enforcement, policy can reduce solver detail first when budget is exceeded. |
| Visual sample rate | Slider/stepper | 30, 60, 120 Hz | 120 Hz | 120 Hz is current fixed-tick quality. 60/30 require interpolation. |
| Visual keyframe interval | Slider | 0.25-5.0 seconds | 1.0 second | Shorter intervals improve seek speed, cost more memory. |
| Position sparsity threshold | Slider | 0-20 mm | 0 mm | Zero means emit exact pose changes; non-zero intentionally drops tiny movement deltas. |
| Orientation sparsity threshold | Slider | 0-2 degrees | 0 degrees | Zero means exact orientation changes. |
| Position quantization | Slider | Off, 0.1 mm, 0.5 mm, 1 mm, 5 mm, 10 mm | Off | Off stores float32 position values. |
| Orientation quantization | Slider | Off, 18-bit, 16-bit, 14-bit, 12-bit, 10-bit | Off | Largest-three quaternion encoding is a good compact candidate. |
| Velocity capture | Slider/enum | Off, Changed, Every visual delta, Every visual frame | Changed | Visual scrub only needs pose; solver/debug/prediction may need velocity. |
| Solver keyframe interval | Slider | 0.25-10.0 seconds | 1.0 second | Controls authoritative restore seek distance. |
| Solver delta detail | Slider/enum | Hashes only, summary, restore, contacts, full debug | Restore | Default should preserve branch/restore viability without full debug rows every frame. |
| Contact detail cap | Slider | 0-8 contact rows per active body, plus Unlimited | 2-4 | Lossy only for debug visualization if restore does not depend on capped rows. |
| Pipeline trace retention | Slider/enum | Off, checkpoints, 1/30 frames, 1/10 frames, every frame | checkpoints | Pipeline trace is debug-heavy and should not be always dense. |
| Collision cell key retention | Slider/enum | Off, checkpoints, changed, every frame | changed | Useful for broadphase diagnostics, but can grow with scene complexity. |
| Memory pressure behavior | Segmented control | Warn, degrade solver, degrade visual, stop recording | Warn/degrade solver | Visual loss should never happen silently in default mode. |

Do not hide visual loss behind a vague "quality" slider only. A top-level preset
is fine, but the underlying sliders must make it obvious whether the user is
changing sample rate, quantization, sparse thresholds, or solver detail.

## Runtime Data Model

### ReplayMemoryQualityConfig

Add a new policy/config struct near replay runtime config ownership. Keep it
separate from the existing basic recorder config so old call sites can migrate
gradually.

Suggested shape:

```cpp
enum class ReplayQualityPreset
{
    LosslessLook,
    Balanced,
    MemorySaver,
    DiagnosticsHeavy,
    Custom
};

enum class ReplayVisualQuantization
{
    Off,
    Position01MmQuat18,
    Position05MmQuat16,
    Position1MmQuat14,
    Position5MmQuat12,
    Position10MmQuat10
};

enum class ReplaySolverDetail
{
    HashesOnly,
    Summary,
    Restore,
    Contacts,
    FullDebug
};

enum class ReplayMemoryPressureBehavior
{
    WarnOnly,
    DegradeSolver,
    DegradeVisual,
    StopRecording
};

struct ReplayMemoryQualityConfig
{
    ReplayQualityPreset preset = ReplayQualityPreset::LosslessLook;
    int retentionSeconds = REPLAY_PAST_BUFFER_SECONDS;
    int maxResidentMiB = 2048;

    int visualSampleRateHz = 120;
    int visualKeyframeIntervalFrames = 120;
    float positionSparseThresholdMeters = 0.0f;
    float orientationSparseThresholdDegrees = 0.0f;
    ReplayVisualQuantization visualQuantization = ReplayVisualQuantization::Off;
    bool captureVelocityDeltas = true;

    int solverKeyframeIntervalFrames = 120;
    ReplaySolverDetail solverDetail = ReplaySolverDetail::Restore;
    int contactRowsPerBodyCap = 4;
    int pipelineTraceStrideFrames = 120;
    bool retainCollisionCellKeys = true;

    ReplayMemoryPressureBehavior pressureBehavior = ReplayMemoryPressureBehavior::DegradeSolver;
};
```

The exact field names can change, but the distinction must remain:

```text
visual quality knobs != solver/debug fidelity knobs
```

### Policy Ownership And Precedence

Replay quality needs one durable runtime policy owner. Do not let CLI parsing,
UI widgets, recorder internals, and artifact code each keep a partial version of
the policy.

Recommended ownership:

| Layer | Responsibility |
|-------|----------------|
| `ReplayMemoryQualityConfig` | Full resolved replay policy, including preset and every custom slider value. |
| `ReplayRuntime` | Owns the active policy, validates it, applies it to presentation, solver, event, and artifact paths. |
| `Run` | Passes startup policy into `ReplayRuntime` and applies UI policy-change commands through one runtime entry point. |
| UI command packet | Emits one-frame requests to change replay policy; does not directly mutate recorder state. |
| CLI/config loading | Builds an input policy and lets `ReplayRuntime` resolve defaults/clamps. |
| Replay artifact manifest | Records the policy used for saved data; does not become the live policy owner. |

Precedence should be explicit and tested:

```text
engine defaults < config file < command line < in-game UI session overrides
```

Open implementation decision: whether UI session overrides should persist to a
config file. The default for this plan is no persistence until the user
explicitly saves runtime options.

Changing the active policy while recording must go through one of two explicit
paths:

| Policy change type | Required behavior |
|--------------------|-------------------|
| Pure display/status change | Apply immediately without resetting replay history. |
| Retention, sample rate, keyframe interval, quantization, sparse thresholds, or solver detail | Rebuild recorder storage or apply to the next capture window with a clear UI/status message. |
| Lossy visual change while in Lossless Look | Require preset change or visible lossy indicator before applying. |

### Body Dictionary

Move stable fields out of per-frame body samples into a body dictionary:

| Field | Store in dictionary? | Reason |
|-------|----------------------|--------|
| Replay id | Yes | Stable identity. |
| Model index | Yes, with versioning | Fast lookup, but can change across restores. |
| Name | Yes | Expensive and repeated today. |
| Shape kind | Yes | Stable for normal body lifetime. |
| Mass/inverse mass | Yes for body lifetime, emit metadata update if edited | Repeated every solver body sample today. |
| Inertia/inverse inertia | Yes for body lifetime, emit metadata update if edited | Solver only, but mostly stable. |
| Fixed/static traits | Yes, with metadata update deltas | Mostly stable. |
| Position/orientation | No | Dynamic visual pose. |
| Linear/angular velocity | Delta stream | Needed for solver/debug/prediction. |
| Sleeping/contact summary | Delta stream | Changes often enough to keep separate. |

Body lifecycle events:

| Event | Encoding |
|-------|----------|
| Body appears | Dictionary add plus first pose delta. |
| Body disappears | Dictionary tombstone or visibility delta. |
| Body metadata changes | Dictionary update delta with changed-field mask. |
| Body hidden in replay frame | Visibility delta instead of deleting dictionary entry. |

### Visual Delta Frames

For default lossless look:

1. Keep sample rate at 120 Hz.
2. Store full float32 position and orientation in deltas.
3. Emit a body delta whenever the pose differs from the previous reconstructed
   pose or a visible/contact/sleep flag changes.
4. Carry forward bodies with no delta.
5. Emit a full visual keyframe every second.

Suggested compact record:

```cpp
struct ReplayVisualBodyDelta
{
    uint32_t bodyDictionaryIndex;
    uint16_t fieldMask;
    // Optional payload follows based on fieldMask and quantization mode.
};
```

Potential field mask bits:

| Bit | Payload |
|-----|---------|
| Position | float3 or quantized position. |
| Orientation | float4 or quantized quaternion. |
| Linear velocity | float3 or quantized velocity. |
| Angular velocity | float3 or quantized velocity. |
| Sleeping flags | packed bits. |
| Contact summary | count, max penetration, impulse summary. |
| Visibility | visible/hidden. |
| Metadata version | dictionary version this delta expects. |

Do not use interpolation in the default lossless mode. Interpolation belongs to
sample-rate-reduced modes.

### Quantized Visual Modes

Quantized modes should be deterministic and bounded.

Recommended first implementation:

| Data | Encoding |
|------|----------|
| Position | Absolute position on a configurable meter grid relative to the current visual keyframe or world origin. |
| Orientation | Largest-three normalized quaternion encoding with sign bit and fixed bit depth. |
| Velocity | Optional quantized vector using scene-scale limits. |

Quantization acceptance must report max observed error:

| Metric | Required display/log |
|--------|----------------------|
| Max position error | In millimeters for retained frames. |
| Mean position error | In millimeters for retained frames. |
| Max orientation error | In degrees. |
| Mean orientation error | In degrees. |
| Dropped visual deltas | Count and percent. |

The UI should show these as small status text or a diagnostics overlay when
replay tuning is open.

### Solver Keyframes And Deltas

Solver state should not be copied wholesale every frame by default. Use full
solver snapshots as keyframes, then store compact changes between them.

Break the solver world snapshot into channels:

| Channel | Default encoding |
|---------|------------------|
| Per-body timers | Changed-index float deltas; optionally quantized if not used for exact restore. |
| Per-body flags | Bitset XOR or changed-index byte deltas. |
| Sleep island ids/parents/ranks | Changed-index deltas plus keyframe reset. |
| Persistent contacts | Add/update/remove by contact key. |
| Contact cache | Add/update/remove by cache key. |
| Solver stats | Small summary every frame. |
| Debug contacts | Controlled by solver detail and cap. |
| Pipeline trace | Checkpoints or sampled stride unless full debug mode. |
| Collision cell keys | Changed set or checkpoint-only unless requested. |

For exact restore, distinguish between:

```text
state needed to restore simulation
state useful only for visualization/debug UI
state included in hashes even if not needed for restore
```

If contact rows are needed for exact warm-start restore, they must not be capped
in Restore mode. If they are only needed for UI display, the cap can be lossy and
should be labeled as such.

Before implementing solver deltas, create a field-by-field matrix for every
member copied by `CaptureReplaySolverSnapshot`, restored by
`RestoreReplaySolverSnapshot`, and hashed by `HashSolverWorldSnapshot`. The
matrix is a blocking prerequisite because current hashes include some fields
that may be debug-only for restore.

Initial inventory to verify:

| Snapshot field/channel | Restore-critical? | Hash-critical today? | Default storage direction |
|------------------------|-------------------|----------------------|---------------------------|
| `version`, `modelCount` | Yes | Yes | Keyframe/header; validate on reconstruct. |
| `nextSleepIslandVisualId` | Likely yes | Yes | Keyframe plus scalar delta when changed. |
| `sleepEnabled` | Yes | Yes | Keyframe plus scalar delta when changed. |
| `collisionVisualFrameActive` | Display/debug, maybe restore-adjacent | Yes | Classify before compaction; hash may need separate debug hash. |
| `tornadoConfig` | Yes | Yes | Keyframe plus config-change events. |
| `tornadoSystemConfig` | Yes | Yes | Keyframe plus config-change events. |
| `tornadoSystemElapsedSeconds` | Yes | Yes | Keyframe plus scalar delta. |
| `timeRemaining` | Yes | Yes | Changed-index float deltas. |
| `sleepSupportedThisFrame` | Yes | Yes | Bitset XOR or changed-index bytes. |
| `sleepInhibitedThisFrame` | Yes | Yes | Bitset XOR or changed-index bytes. |
| `sleepState` | Yes | Yes | Bitset XOR or changed-index bytes. |
| `sleepCounter` | Yes | Yes | Changed-index byte deltas. |
| `underwaterSleepLocked` | Yes | Yes | Bitset XOR or changed-index bytes. |
| `tornadoCaptureSeconds` | Yes | Yes | Changed-index float deltas. |
| `tornadoEjectCooldownSeconds` | Yes | Yes | Changed-index float deltas. |
| `collisionVisualContacts` | Visual/debug, maybe restore-adjacent | Yes | Classify; keep exact for hash or move to debug hash. |
| `sleepIslandVisualId` | Visual/debug, maybe restore-adjacent | Yes | Classify before allowing lossy/debug-only treatment. |
| `sleepIslandAssignedVisualId` | Visual/debug, maybe restore-adjacent | Yes | Classify before allowing lossy/debug-only treatment. |
| `sleepSupportEdges` | Likely yes for sleep behavior | Yes | Add/remove edge deltas. |
| `sleepIslandParent` | Yes if restoring islands exactly | Yes | Changed-index int deltas. |
| `sleepIslandRank` | Yes if restoring islands exactly | Yes | Changed-index byte deltas. |
| `sleepIslandHasAwake` | Yes if restoring islands exactly | Yes | Bitset XOR or changed-index bytes. |
| `sleepIslandHasSupportAnchor` | Yes if restoring islands exactly | Yes | Bitset XOR or changed-index bytes. |
| `sleepIslandEligible` | Yes if restoring islands exactly | Yes | Bitset XOR or changed-index bytes. |
| `sleepIslandCanSleep` | Yes if restoring islands exactly | Yes | Bitset XOR or changed-index bytes. |
| `persistentContacts` | Yes for warm-start/contact restore | Yes | Add/update/remove by contact key; uncapped in Restore. |
| `persistentContactCache` | Yes for warm-start restore | Yes | Add/update/remove by cache key; uncapped in Restore. |
| `solverStats` | Debug/status unless solver consumes it later | Yes | Store summaries; decide if hashes split restore/debug. |
| `persistentContactCounts` | Yes if used by sleep/contact behavior | Yes | Changed-index uint16 deltas. |
| `persistentRestingContactCounts` | Yes if used by sleep behavior | Yes | Changed-index uint16 deltas. |
| `debugContacts` | Debug/display | Yes | FullDebug/Contacts only, or split debug hash from restore hash. |
| `pipelineTrace` | Debug/display | Yes | FullDebug/stride/checkpoint only, or split debug hash from restore hash. |
| `collisionCellKeys` | Broadphase/debug; classify before dropping | Yes | Changed set if restore-critical; checkpoint/debug otherwise. |

Hash policy must be settled before `ReplaySolverDetail::Restore` ships:

| Hash kind | Purpose |
|-----------|---------|
| Restore hash | Covers only state required to restore deterministic simulation. |
| Debug hash | Covers debug contacts, pipeline trace, collision visual contacts, and other inspection-only payloads. |
| Full hash | Compatibility hash matching current full snapshot behavior. |

If implementation keeps only one hash, Restore mode must retain every field that
current `HashSolverWorldSnapshot` includes. If implementation splits hashes, the
plan must update replay probes and artifact metadata to report which hash is
being compared.

### Reconstruction

Replay should reconstruct a frame through a small reusable scratch buffer:

1. Find nearest visual keyframe at or before target frame.
2. Copy or reference the keyframe state into scratch.
3. Apply visual delta frames up to target frame.
4. Produce the same render-facing sample shape expected by current scrub code,
   or migrate scrub code to read the reconstructed pose view directly.

For solver restore:

1. Find nearest solver keyframe at or before target frame.
2. Apply solver deltas up to target frame.
3. Verify reconstructed solver/presentation hashes when hash tracking is
   enabled.
4. Restore physics state or report a hash mismatch before branching.

Keep a tiny seek cache:

| Cache | Purpose |
|-------|---------|
| Last reconstructed visual frame | Dragging the scrubber usually moves locally; applying one frame delta is cheaper than reseeking. |
| Last reconstructed solver frame | Branch/restore tools often inspect nearby frames. |
| Keyframe index | Avoid scanning the ring to find seek bases. |

## Memory Budget Model

Add live statistics to `ReplayRecorderStats` or a new `ReplayMemoryStats`:

```cpp
struct ReplayMemoryStats
{
    uint64_t visualBytesResident = 0;
    uint64_t solverBytesResident = 0;
    uint64_t eventBytesResident = 0;
    uint64_t dictionaryBytesResident = 0;
    uint64_t scratchBytesResident = 0;
    uint64_t totalBytesResident = 0;

    uint32_t retainedFrames = 0;
    uint32_t visualKeyframes = 0;
    uint32_t solverKeyframes = 0;
    uint32_t visualBodyDeltas = 0;
    uint32_t solverDeltas = 0;
    uint32_t droppedVisualDeltas = 0;
    uint32_t cappedDebugRows = 0;

    float maxPositionErrorMeters = 0.0f;
    float maxOrientationErrorDegrees = 0.0f;
};
```

Memory pressure should degrade in this order unless the user chooses otherwise:

1. Drop or reduce full debug-only solver rows.
2. Increase solver checkpoint interval within slider limits.
3. Reduce pipeline trace retention.
4. Reduce solver detail from full debug to restore.
5. Warn that visual memory is now dominant.
6. Only reduce visual quality if the user explicitly allows visual degradation.

Default lossless visual mode should never silently lower visual fidelity.

## Expected Memory Shape

For 4000 bodies:

| Track | Rough 60s memory | Notes |
|-------|------------------|-------|
| Current full presentation + solver typical | About 15 GiB estimated; Phase 0 must replace this with measured data | Full body and solver snapshots every tick. |
| Dense 120 Hz visual pose stream | About 0.86 GiB | 4000 bodies * 32-byte pose record * 7200 frames, before metadata/index overhead. |
| Sparse 120 Hz visual, 25% active | About 0.22 GiB | Assumes only 1000 bodies emit pose changes per frame. |
| Sparse 120 Hz visual, 10% active | About 0.09 GiB | Mostly sleeping/static scenes. |
| Full current-style solver retained every frame | Dominant cost | This should be replaced by keyframes/deltas. |

The memory win should come mostly from solver compaction and metadata
deduplication. Visual replay can stay high quality.

## Implementation Phases

### Phase 0: Instrument Current Memory

Goal: measure before changing behavior.

Tasks:

1. Add or extend replay stats to report resident memory by track:
   presentation, solver, events, dictionary, scratch.
2. Count bodies, contacts, cache rows, debug contacts, pipeline records, and
   collision keys per retained frame.
3. Add a measured 4000-object baseline for current full replay memory before
   compact storage changes land. Report resident bytes by track, retained frame
   count, average active bodies, average contacts, vector capacity overhead, and
   allocator-visible total when available.
4. Add the solver snapshot field matrix described above. For every snapshot
   member, classify it as restore-critical, hash-only, debug/display-only, or
   unknown. Unknown fields block Phase 3.
5. Add command-line logging for replay memory at shutdown or at periodic
   intervals.
6. Add debug-only assertions that ring capacity and retained-frame counts match
   config.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| 4000-object replay run | Reports measured per-track replay memory and replaces the rough estimate in this plan or a linked report. |
| Solver-heavy scene | Shows solver snapshot as dominant memory consumer. |
| Replay disabled | Reports near-zero replay resident memory. |
| Solver field matrix | Every solver snapshot field is classified before delta implementation starts. |

Validation:

```bat
tools\validate_fast.bat
```

Use `tools\validate_perf.bat` if instrumentation lands in hot capture paths.

### Phase 1: Split Body Metadata From Visual Pose

Goal: make visual replay compact without any visual quality loss.

Tasks:

1. Add a replay body dictionary owned by the presentation recorder or shared
   replay runtime.
2. Move stable fields out of per-frame visual body samples:
   name, shape kind, fixed/static identity, mass when used for display.
3. Add visual body delta records with field masks.
4. Emit a 120 Hz lossless visual delta stream.
5. Emit full visual keyframes every configured interval.
6. Add reconstruction that returns either:
   - a compatibility `ReplayPresentationSample`, or
   - a render-facing pose view consumed by `ApplyPresentationSampleForRender`.
7. Preserve current scrub behavior and visual output in Lossless Look mode.
8. Add a lossless visual equivalence probe that captures old full samples and
   reconstructed compact samples for the same frames, then compares body ids,
   visibility, position, orientation, and hidden/deleted body behavior.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Scrub visual replay | Reconstructed samples match current full samples before visual judgment is used. |
| Visual equivalence probe | Reports zero body mismatch and zero pose error in Lossless Look, or only documented float copy tolerance. |
| Unchanged sleeping bodies | Do not emit repeated pose deltas between keyframes. |
| Newly spawned/hidden bodies | Appear/disappear correctly through dictionary events. |
| Existing replay artifact save | Still works or is clearly versioned/migrated. |

Validation:

```bat
tools\validate_fast.bat
tools\validate_replay_scrub.bat
```

If render-facing sample application changes enough to affect DX12 output:

```bat
tools\validate_dx12_renderer.bat
```

### Phase 2: Add Replay Quality Config And CLI

Goal: make the policy explicit before adding UI.

Tasks:

1. Add `ReplayMemoryQualityConfig`.
2. Map presets to concrete values.
3. Make `ReplayRuntime` the active policy owner and add a single entry point for
   applying a fully resolved policy.
4. Define policy precedence:
   engine defaults, config file, command line, then in-game UI session
   overrides.
5. Add `UIReplayCommands` or an equivalent command packet field so UI controls
   can request policy changes without mutating replay recorders directly.
6. Extend `ReplayRuntime::ConfigureRecording` or add a new overload that accepts
   the full policy.
7. Add command-line options:
   - `--replay-quality lossless|balanced|memory|diagnostics`
   - `--replay-memory-mib <value>`
   - `--replay-visual-hz 30|60|120`
   - `--replay-visual-keyframe-ms <value>`
   - `--replay-position-epsilon-mm <value>`
   - `--replay-orientation-epsilon-deg <value>`
   - `--replay-position-quant-mm off|0.1|0.5|1|5|10`
   - `--replay-orientation-quant-bits off|18|16|14|12|10`
   - `--replay-solver-keyframe-ms <value>`
   - `--replay-solver-detail hashes|summary|restore|contacts|full`
8. Keep old `--replay` and `--replay-seconds` behavior working.
9. Print the resolved policy at startup.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| No new flags | Lossless Look defaults apply. |
| Balanced flag | Resolved policy shows balanced values. |
| Invalid values | CLI parse fails with a clear message. |
| Old replay flags | Continue to behave as before except with compact storage. |
| Policy precedence | Defaults/config/CLI/UI resolve deterministically and are covered by tests or probes. |
| UI command path | UI emits policy requests through the run loop; recorders are not directly mutated by widgets. |

Validation:

```bat
tools\validate_fast.bat
```

### Phase 3: Compact Solver Keyframes And Deltas

Goal: remove the major resident-memory cost while preserving restore/debug
usefulness.

Tasks:

1. Consume the Phase 0 solver field matrix. No field classified as unknown may be
   omitted, quantized, capped, or moved to debug-only storage.
2. Decide hash policy before implementation: single full hash, or split restore,
   debug, and full hashes.
3. Keep full solver world snapshots only at solver keyframe intervals.
4. Add solver delta channels for:
   - per-body timers,
   - per-body flags,
   - sleep island arrays,
   - contact counts,
   - persistent contact rows,
   - contact cache rows,
   - tornado config/system state,
   - sleep support edges,
   - solver hash-critical fields identified by the matrix,
   - solver stats,
   - collision cell keys when enabled.
5. Encode persistent contacts and contact cache as add/update/remove streams by
   stable key.
6. Preserve full restore data in `ReplaySolverDetail::Restore`.
7. Gate debug-only payloads behind `ReplaySolverDetail::Contacts` and
   `ReplaySolverDetail::FullDebug`.
8. Add hash verification after reconstructing a solver frame.
9. Make mismatch reporting actionable: frame, keyframe base, delta count,
   expected hash, reconstructed hash.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Restore from solver keyframe | Matches current full snapshot restore at keyframe. |
| Restore between keyframes | Reconstructed hash matches recorded hash. |
| Restore detail matrix | Every restore-critical and hash-critical field is represented or intentionally covered by split hash policy. |
| Contact inspector | Shows available contact rows according to solver detail. |
| FullDebug mode | Retains current debug richness as much as possible. |
| Memory Saver mode | Drops debug-only detail without breaking visual scrub. |

Validation:

```bat
tools\validate_replay_scrub.bat
tools\validate_physics.bat
```

Use `tools\validate_full.bat` if replay restore touches broad runtime launch
flow.

### Phase 4: Quantization And Sparse Thresholds

Goal: add optional lossy compression controls without changing default visual
quality.

Tasks:

1. Implement position quantization modes.
2. Implement orientation quantization modes.
3. Implement position and orientation sparse thresholds.
4. Track and expose quantization/dropped-delta error metrics.
5. Add warning states when visual quality is no longer lossless.
6. Ensure all lossy modes are deterministic.
7. Make lossless mode use the exact previous encoding path, not quantization with
   magic zero values.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Lossless Look | Max visual error reports 0. |
| Balanced | Max visual error stays below configured tolerance. |
| Memory Saver | UI clearly marks visual mode as lossy. |
| Fast-moving collision | Reduced sample rate mode may differ, but 120 Hz lossless/sparse remains exact. |

Validation:

```bat
tools\validate_replay_scrub.bat
tools\validate_dx12_renderer.bat
```

Use `tools\validate_perf.bat` because quantization and reconstruction are hot
paths.

### Phase 5: UI Sliders And Presets

Goal: expose the policy in-game with usable controls.

Important: until Phase 7 is implemented, the memory budget slider is advisory
only. The UI must label it as an estimated budget and must not claim a hard cap
or automatic degradation unless budget enforcement is already live.

Tasks:

1. Add a Replay Quality section, initially in Options or as a dedicated Replay
   tab.
2. Add a preset selector.
3. Add sliders for retention, memory budget, visual sample rate, visual keyframe
   interval, position threshold, orientation threshold, quantization, solver
   keyframe interval, solver detail, contact cap, pipeline trace retention, and
   memory pressure behavior.
4. Use existing `UISlider` preview/commit patterns from Options tab.
5. Add `UIReplayCommands` or extend existing UI commands with replay policy
   requests.
6. Apply changes through the run loop into `ReplayRuntime`, not directly from UI
   widgets.
7. Show live estimated and actual memory. If Phase 7 has not landed, label the
   memory budget as advisory:
   - visual memory,
   - solver memory,
   - event memory,
   - total memory,
   - visual error status.
8. Mark lossy settings plainly in the UI.
9. When a slider changes a setting that requires recorder storage rebuild, show
   whether the change applies immediately, after replay reset, or on the next
   capture window.

UI grouping:

| Group | Controls |
|-------|----------|
| Preset | Replay quality preset, memory budget. |
| Visual | Sample rate, keyframe interval, position threshold, orientation threshold, position quantization, orientation quantization. |
| Solver | Solver keyframe interval, solver detail, contact cap. |
| Diagnostics | Pipeline trace retention, collision cell retention, hash logging. |
| Status | Estimated memory, actual memory, retained frames, lossless/lossy indicator. |

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Drag preset/slider | UI updates preview value and commits through commands. |
| Lossless preset | All visual sliders resolve to lossless values. |
| Manual slider edit | Preset changes to Custom. |
| Memory estimate | Updates as sliders move and clearly says whether it is advisory or enforced. |
| Replay recording active | Policy changes either safely rebuild recorder buffers or clearly apply on next capture window. |

Validation:

```bat
tools\validate_fast.bat
```

If UI draw/input behavior changes broadly:

```bat
tools\validate_full.bat
```

### Phase 6: Artifact Compatibility

Goal: keep saved replay export/load coherent with the new in-memory model.

Tasks:

1. Decide whether runtime compact memory becomes the canonical source for V2
   artifact chunks or whether export reconstructs existing artifact frames.
2. Preserve the existing body dictionary/dense pose approach in V2 artifacts.
3. Add manifest fields for:
   - visual sample rate,
   - quantization mode,
   - sparse thresholds,
   - solver checkpoint interval,
   - solver detail,
   - max visual error.
4. Keep old V2 artifacts loadable if format changes.
5. Add query output that reports replay quality policy and memory estimates.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Save lossless replay | Loaded artifact visually matches source replay. |
| Save balanced replay | Manifest records quantization/sparsity policy. |
| Load old replay | Existing V2 load path still works. |
| Query replay | Reports policy and retained-frame stats. |

Validation:

```bat
tools\validate_replay_scrub.bat
```

Use any replay artifact validation script if one exists or is added during this
phase.

### Phase 7: Memory Budget Enforcement

Goal: make the memory slider real.

This phase graduates the advisory Phase 5 memory budget into an enforced policy.
Until this phase lands, memory budget UI must remain clearly labeled as an
estimate.

Tasks:

1. Add budget accounting before accepting a new frame.
2. Estimate worst-case frame bytes under current policy.
3. Add pressure states:
   - normal,
   - warning,
   - degrading solver detail,
   - recording stopped.
4. Drop debug-only solver payloads first under pressure.
5. Never degrade visual lossless mode unless policy allows it.
6. Show pressure status in UI and logs.
7. Add guardrails for large object counts:
   - warn when active bodies exceed policy budget,
   - display retained seconds under current budget.
8. Add telemetry comparing predicted memory to actual resident memory so budget
   estimates can be tuned instead of trusted blindly.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Budget below current need | UI warns or degrades according to selected behavior. |
| Lossless visual protected | Visual fidelity is not silently reduced. |
| Solver debug detail high | Budget pressure reduces optional debug rows before visual data. |
| 4000-object stress scene | Resident memory remains bounded by policy plus documented overhead. |
| Advisory label removed | UI only presents the budget as enforced after this phase is active. |

Validation:

```bat
tools\validate_perf.bat
```

Use `tools\validate_full.bat` if pressure behavior can disable replay during
normal runtime flows.

## Suggested Default Values

### Lossless Look

| Setting | Value |
|---------|-------|
| Retention | 60s |
| Memory budget | 2048 MiB warning budget, no visual degradation |
| Visual sample rate | 120 Hz |
| Visual keyframe interval | 1s |
| Position threshold | 0 mm |
| Orientation threshold | 0 degrees |
| Position quantization | Off |
| Orientation quantization | Off |
| Velocity capture | Changed |
| Solver keyframe interval | 1s |
| Solver detail | Restore |
| Contact cap | Unlimited if needed for restore, otherwise 4/body for debug display |
| Pipeline trace | checkpoints |
| Pressure behavior | Degrade solver |

### Balanced

| Setting | Value |
|---------|-------|
| Retention | 60s |
| Memory budget | 1024 MiB |
| Visual sample rate | 120 Hz |
| Visual keyframe interval | 1s |
| Position threshold | 0.5 mm |
| Orientation threshold | 0.05 degrees |
| Position quantization | 0.5 mm |
| Orientation quantization | 16-bit |
| Velocity capture | Changed |
| Solver keyframe interval | 2s |
| Solver detail | Restore |
| Contact cap | 4/body debug display |
| Pipeline trace | checkpoints |
| Pressure behavior | Degrade solver |

### Memory Saver

| Setting | Value |
|---------|-------|
| Retention | 30-60s |
| Memory budget | 512 MiB |
| Visual sample rate | 60 Hz |
| Visual keyframe interval | 2s |
| Position threshold | 2-5 mm |
| Orientation threshold | 0.25 degrees |
| Position quantization | 1-5 mm |
| Orientation quantization | 12-14 bit |
| Velocity capture | Off or changed |
| Solver keyframe interval | 5s |
| Solver detail | Summary or Restore |
| Contact cap | 1-2/body debug display |
| Pipeline trace | off or checkpoints |
| Pressure behavior | Degrade solver, then stop recording |

### Diagnostics Heavy

| Setting | Value |
|---------|-------|
| Retention | 30-60s |
| Memory budget | 4096+ MiB |
| Visual sample rate | 120 Hz |
| Visual keyframe interval | 0.5s |
| Position threshold | 0 mm |
| Orientation threshold | 0 degrees |
| Position quantization | Off |
| Orientation quantization | Off |
| Velocity capture | Every visual delta or every frame |
| Solver keyframe interval | 0.5s |
| Solver detail | FullDebug |
| Contact cap | Unlimited |
| Pipeline trace | every frame |
| Pressure behavior | WarnOnly |

## Migration Notes

1. Keep old full sample structs during migration.
2. Introduce compact storage behind the recorder API.
3. Reconstruct old sample views where existing tools still expect them.
4. Move tools to read compact views gradually.
5. Keep artifact export/load versioned.
6. Do not change visual scrub UI behavior until compact visual reconstruction is
   proven identical.

Compatibility bridge:

| Existing consumer | First migration path |
|-------------------|----------------------|
| Render scrub | Reconstruct `ReplayPresentationSample` compatibility view. |
| Solver scrub UI | Reconstruct `ReplaySolverFrameSample` compatibility view on demand. |
| Path visualizer | Iterate compact reconstructed frames or provide a frame visitor that reconstructs scratch samples. |
| Replay exporter | Export from reconstructed frames, then later write compact chunks directly. |
| Replay queries | Add query support for compact stats and policy. |

## Testing And Validation Strategy

Use focused tests before broad gates.

| Change type | Validation |
|-------------|------------|
| Plan/docs only | No validation required. |
| Replay data model/refactor only | `tools\validate_fast.bat` |
| Replay visual scrub behavior | `tools\validate_replay_scrub.bat` and `tools\validate_dx12_renderer.bat` if render output changes. |
| Solver restore or physics state reconstruction | `tools\validate_physics.bat` |
| Hot capture/reconstruction paths or memory budget enforcement | `tools\validate_perf.bat` |
| Broad replay/runtime UI flow | `tools\validate_full.bat` |

Add focused debug probes where useful:

| Probe | Purpose |
|-------|---------|
| Lossless visual equivalence probe | Capture old full sample and new reconstructed sample for the same frame; compare body id, model index, visibility/hidden state, position, and orientation exactly or within float copy tolerance. |
| Quantization error probe | Report max/mean visual error over retained frames. |
| Solver field matrix audit | Confirm every solver snapshot field is classified as restore-critical, hash-only, debug/display-only, or intentionally omitted under a split-hash policy. |
| Solver reconstruction hash probe | Reconstruct arbitrary frames between keyframes and compare the selected hash kind: restore, debug, or full. |
| Memory pressure probe | Force a tiny memory budget and verify configured degradation order. |
| 4000-object stress probe | Measure resident replay memory under presets. |

## Risks And Mitigations

| Risk | Mitigation |
|------|------------|
| Lossless mode accidentally becomes lossy | Keep lossless path separate from quantized path; add max-error stats and asserts. |
| Sparse carry-forward mishandles hidden/deleted bodies | Represent visibility/lifecycle as explicit deltas and keyframe full active set. |
| Reconstructing frames during scrub gets too slow | Use 1s visual keyframes, last-frame seek cache, and preallocated scratch buffers. |
| Solver deltas fail to restore exact state | Gate authoritative branch/restore on hash verification; keep full keyframes frequent enough. |
| Hashes include debug-only fields | Split restore/debug/full hashes, or retain all hash-covered fields in Restore mode. |
| Contact caps break restore | Separate restore-required contacts from debug-display caps. |
| UI sliders create unclear quality loss | Presets plus explicit lossy indicators and error metrics. |
| Budget slider overpromises before enforcement exists | Label it advisory until budget enforcement ships; remove advisory language only after Phase 7 acceptance passes. |
| Memory estimates diverge from real heap usage | Track actual vector capacities/resident bytes, not only theoretical payload bytes. |
| Runtime policy change invalidates current ring | Apply changes at next recorder reset, or rebuild ring with clear UI/status message. |
| Artifact compatibility regresses | Keep old reader path and version manifest changes. |

## Open Questions

| Question | Default answer for implementation |
|----------|-----------------------------------|
| Should Replay Quality be a new tab immediately? | Start in Options if faster; graduate to Replay tab when controls crowd the page. |
| Should memory budget be hard or advisory by default? | Advisory plus solver degradation; never silently degrade visual lossless mode. |
| When can the UI call memory budget enforced? | Only after Phase 7 budget accounting and pressure behavior are implemented and validated. |
| Should visual keyframes be dense even in sparse mode? | Yes. Dense keyframes bound seek cost and repair carry-forward drift. |
| Should quantization affect saved artifacts? | Yes, but the manifest must record it and report error metrics. |
| Should 600s retention stay available? | Yes, but UI should show memory estimates and likely warn at high object counts. |
| Should exact solver restore be guaranteed in Balanced? | Prefer yes. If not, label Balanced as visual-first and keep Lossless/Diagnostics as exact restore modes. |

## Definition Of Done

This work is complete when:

1. Lossless Look mode passes objective full-sample versus reconstructed-sample
   equivalence probes before subjective visual review.
2. 4000-object replay memory is measured before and after the change, and the
   compact path is dramatically lower than current full-frame
   presentation-plus-solver storage.
3. Replay UI exposes presets and sliders for visual sample rate, keyframe
   interval, sparsity, quantization, solver detail, and memory budget.
4. Memory budget UI accurately distinguishes advisory estimates from enforced
   caps.
5. Visual quality loss is never silent; lossy settings show estimated or measured
   error.
6. Solver restore/debug fidelity is controlled separately from visual quality.
7. Every solver snapshot field is classified and either encoded, deliberately
   excluded under split hash policy, or retained in full debug/restore modes.
8. Replay memory stats report resident bytes by track.
9. Solver reconstruction verifies hashes when authoritative restore is used.
10. Saved replay artifacts either remain compatible or are clearly versioned with
   migration support.
9. Required validation gates pass for the implementation phases that land.
