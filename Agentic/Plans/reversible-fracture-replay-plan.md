# Reversible GPU Fracture Replay Plan

Date: 2026-06-18
Status: Draft implementation plan
Impact area: DX12 renderer, shaders, replay UI, scene system, physics triggers
Validation for this document-only change: none required

## Goal

Build a playable feature where the user can shoot a breakable ball or object,
watch it burst into many tiny GPU-simulated shards, then drag the replay slider
backward to watch the same shards join back into the original object.

The central replay rule is:

```text
Do not run physics backward.
Record shard presentation samples, then scrub those samples backward.
```

Forward live simulation fractures the object and advances shards. Reassembly in
the first version comes from replay scrubbing earlier recorded frames, not from
a forward-time recall ability.

## User-Facing Shape

The target workflow is:

1. Load a breakable-object test scene.
2. Enter nudge/ray-test mode or use a deterministic projectile scene.
3. Shoot the breakable object.
4. The original object hides and many shards fly outward, keeping in mind the shards 
   are a puzzle that can create the object when pieced together (so frame 0 shards
   would look identical to the original object)
5. Shards bounce off the terrain/floor, lose energy, spin, and settle.
6. Pause replay and drag the timeline backward.
7. The exact recorded shard poses scrub backward until the object visually
   reforms.
8. Scrubbing before the fracture frame shows the intact original object again.

V1 shards are GPU-simulated visual bodies. They are not full CPU `GameModel`
solver bodies. This keeps shard counts high while avoiding model-capacity,
determinism, and contact-solver cost problems.

## Current Repo Fit

Useful existing pieces:

- Nudge mode already lets the user left-click a dynamic object from the camera.
- Bullet/projectile scenes already exercise deterministic high-speed impacts.
- `Replay System Plan` already defines the correct model: scrub presentation
  samples instead of integrating physics backward.
- DX12 already has a compute-shader path for GPU mip generation.
- Terrain exposes CPU height, normal, and plane queries that can seed a GPU
  terrain collision resource.
- Instanced rendering already draws many spheres/boxes efficiently.

Important constraints:

- DX12 is the only runtime renderer.
- Physics-visible behavior must remain deterministic.
- Repository validation scripts are PR/commit gates, not casual iteration
  commands.
- Tiny shards must not fall through the floor; terrain bounce is required even
  when shards are GPU-only.

## Target Architecture

```text
SkullbonezRun
  -> ReplayController
  -> FractureSystem
       -> FractureObjectRegistry
       -> FractureTemplateStore
       -> FractureEventQueue
       -> GpuShardSimulator
       -> GpuShardRenderer
       -> FractureReplaySamples
```

`FractureSystem` owns visual shard state and break/replay presentation. CPU
physics continues to own the original object, bullets, and impact trigger.

### Core Types

Add small stable identifiers and records:

```cpp
using FractureObjectId = uint32_t;
using FractureShardId = uint32_t;

struct FractureEvent
{
    FractureObjectId objectId;
    int modelIndex;
    uint32_t frameIndex;
    uint32_t seed;
    Vector3 impactPoint;
    Vector3 impactNormal;
    Vector3 impulse;
    Matrix4 objectWorld;
    float objectRadius;
};

struct FractureShardRestPose
{
    FractureShardId shardId;
    Vector3 localPosition;
    Quaternion localOrientation;
    Vector3 localScale;
    Vector3 localNormal;
    float mass;
    float radius;
};

struct FractureShardState
{
    FractureShardId shardId;
    Vector3 position;
    Quaternion orientation;
    Vector3 linearVelocity;
    Vector3 angularVelocity;
    float sleepAmount;
    float ageSeconds;
    uint32_t flags;
};
```

Names can shift during implementation, but the data contract should not: every
shard needs stable identity, rest pose, current pose, velocity/spin, size,
material, and active/sleep state.

## Feature Defaults

Use these defaults unless a future implementation discussion changes them:

| Setting | Default |
|---------|---------|
| Supported v1 objects | spheres and boxes |
| Default shards per object | 512 |
| Hard cap per object | 2048 |
| Shard collision | terrain/floor only |
| Shard-to-shard collision | not in v1 |
| Shard-to-object collision | not in v1 |
| Replay retention | use the replay system default once implemented |
| Reassembly | replay scrub backward |
| Forward recall power | out of scope for v1 |

## Implementation Plan

### Phase 1: Fracture Plan Hooks And Scene Authorship

Goal: make breakable objects explicit without changing runtime behavior.

Work:

1. Add scene directives:
   - `breakable <target> on|off`
   - `fracture_shards <target> <count>`
   - `fracture_impulse_threshold <target> <value>`
   - `fracture_restitution <target> <value>`
   - `fracture_friction <target> <value>`
   - `replay on|off`
2. Store parsed fracture options on scene objects or a scene-level fracture
   table.
3. Add a focused scene such as
   `SkullbonezData/scenes/reversible_fracture_replay.scene`.
4. Keep behavior unchanged until the runtime fracture system consumes these
   options.

Acceptance:

- Scene parser accepts the new directives.
- Existing scenes load unchanged.
- The new scene marks one ball as breakable.

Validation at PR gate:

```bat
tools\validate_fast.bat
```

Use `tools\validate_physics.bat` if parser changes alter physics scene loading.

### Phase 2: Fracture Trigger From Shooting

Goal: emit a deterministic fracture event when a breakable object is shot hard
enough.

Work:

1. Extend nudge/ray-test hit handling so a ray impulse above threshold can emit
   a `FractureEvent`.
2. Extend projectile/contact-driven scenes so deterministic bullet impacts can
   also emit the same event.
3. Hide or mark the original object as visually replaced once fractured.
4. Keep CPU physics authoritative for bullets and the original object up to the
   fracture frame.
5. Do not spawn CPU `GameModel` shards in v1.

Acceptance:

- Shooting a breakable object emits exactly one fracture event.
- Non-breakable objects keep current behavior.
- The deterministic fracture scene breaks on the same frame across repeated
  runs.

Validation at PR gate:

```bat
tools\validate_physics.bat
```

### Phase 3: GPU Shard Template And Rendering

Goal: draw many shards in the object's rest shape before adding simulation.

Work:

1. Build procedural shard templates for spheres and boxes.
2. Each template records rest-local transforms and a simple shared shard mesh.
3. Add a GPU instance rendering path that reads shard instance data from a GPU
   buffer instead of CPU-uploading every shard each frame.
4. Render the fractured object as shards still sitting at their rest poses.
5. Match material/tint roughly to the source object.

Acceptance:

- A fractured object can be rendered as 512 shard instances in its original
  shape.
- The original object is not double-rendered while shard rendering is active.
- DX12 validation reports no descriptor, UAV, or resource-state errors.

Validation at PR gate:

```bat
tools\validate_dx12_renderer.bat
```

### Phase 4: GPU Shard Simulation With Terrain Bounce

Goal: make shards fly outward, bounce on the floor, and settle.

Work:

1. Extract reusable DX12 compute support from the generate-mips implementation
   pattern.
2. Add `fracture_emit.hlsl`:
   - initializes current shard state from rest pose,
   - applies impact impulse, radial scatter, and deterministic seeded variation.
3. Add `fracture_sim.hlsl`:
   - integrates gravity,
   - updates orientation from angular velocity,
   - samples terrain height and normal,
   - resolves terrain penetration,
   - reflects normal velocity using restitution,
   - damps tangential velocity using friction,
   - puts low-energy shards to sleep.
4. Build a GPU terrain collision resource at scene load:
   - use `Terrain::GetTerrainHeightAndNormalAt` for sampled heightmap scenes,
   - use analytic constants or the same sampled grid for flat-slope scenes.
5. Keep all shard simulation state on GPU during live play.

Acceptance:

- Shards do not fall through flat terrain or ordinary heightmap terrain.
- Shards visibly bounce and settle.
- No per-frame CPU readback is required for live rendering.
- 512 shards run without obvious frame hitching.

Validation at PR gate:

```bat
tools\validate_dx12_renderer.bat
tools\validate_perf.bat
```

Run `tools\validate_physics.bat` only if trigger/contact behavior changes in
the same PR-bound slice.

### Phase 5: Replay Presentation Samples For Shards

Goal: record enough shard presentation data to scrub backward and visually
reassemble the object.

Work:

1. Extend the replay presentation sample model with fracture data:
   - active fracture sets,
   - intact-object visibility,
   - shard transforms,
   - shard sleep/visibility flags.
2. Capture a sample at the fracture frame before outward shard motion advances.
3. Store samples after each committed simulation tick, matching the existing
   replay-system plan.
4. Avoid recording raw GPU buffers directly. Use a compact CPU-side sample path:
   - for v1, allow an explicit GPU-to-CPU sample copy only when replay
     recording is enabled,
   - keep it bounded by replay retention and shard caps.
5. While scrubbing, present recorded shard samples instead of advancing the live
   GPU simulator.

Acceptance:

- Dragging the replay slider backward shows shards moving back into the original
  object.
- Scrubbing before the fracture frame shows only the intact object.
- Scrubbing after the fracture frame shows only shard presentation.
- Resuming at the live head returns control to live GPU simulation.

Validation at PR gate:

```bat
tools\validate_full.bat
tools\validate_dx12_renderer.bat
tools\validate_perf.bat
```

### Phase 6: Replay UI Slice

Goal: expose just enough UI to use the feature.

Work:

1. Add replay controls to the existing in-game UI:
   - record toggle,
   - pause/scrub toggle,
   - timeline slider,
   - step previous/next,
   - resume live head.
2. Keep the UI compact and use existing slider/button patterns.
3. Disable world shooting/editor clicks while the UI is actively dragging the
   replay slider.
4. Show basic status text such as current replay frame, live head, and fracture
   sample count.

Acceptance:

- The user can shoot, pause, scrub backward, scrub forward, and resume.
- UI interaction does not fire shots or mutate the world.
- Replay slider remains responsive for the shard test scene.

Validation at PR gate:

```bat
tools\validate_full.bat
```

Add `tools\validate_dx12_renderer.bat` if UI or replay changes affect captured
renderer baselines.

### Phase 7: Focused Validation And Documentation

Goal: make the feature reproducible and safe for future work.

Work:

1. Add or update runtime reference docs for:
   - breakable scene directives,
   - replay controls,
   - shooting workflow,
   - known v1 shard collision limits.
2. Add a dated report with screenshots or captures showing:
   - intact object,
   - fracture,
   - settled shards,
   - replay scrub mid-reassembly,
   - fully reassembled scrub state.
3. Record validation output in commit notes or report.

Acceptance:

- A future agent can load the scene and reproduce the workflow.
- Validation commands and visual evidence are documented.

Validation at final PR gate:

```bat
tools\validate_full.bat
tools\validate_dx12_renderer.bat
tools\validate_perf.bat
```

Run `tools\validate_physics.bat` if the final branch includes physics trigger
or deterministic scene behavior changes.

## First Useful Slice

The first practical slice should be:

1. Add a breakable test scene and fracture directives.
2. Emit a fracture event from nudge/ray-test shooting.
3. Replace the source object visually with GPU-rendered shards in rest pose.
4. Add GPU shard simulation with terrain bounce.
5. Add minimal replay recording/scrubbing for shard presentation samples.

Do not start with saved replay files, branch-from-frame, forward recall, or
full shard physics.

## Risks And Mitigations

| Risk | Mitigation |
|------|------------|
| Shards fall through terrain | Require terrain height/normal sampling in `fracture_sim.hlsl` before accepting v1. |
| GPU shard state breaks determinism | Treat shards as visual presentation; CPU physics baselines ignore shard motion. |
| Replay readback is too expensive | Read shard samples only when replay is enabled, cap shard count, and run perf validation. |
| Scene model capacity is exceeded | Do not create CPU `GameModel` shards in v1. |
| Reassembly looks wrong | Record a sample at the fracture frame while shards are still in rest pose. |
| Object double-renders | Replay samples must explicitly store intact-object visibility and shard-set visibility. |
| DX12 barriers regress | Route UAV/SRV/vertex buffer state changes through graph-owned DX12 helpers. |
| UI clicks fire shots while scrubbing | Existing UI input capture must block world actions during replay slider interaction. |

## Out Of Scope For V1

- Forward-time recall power where shards fly back together while time advances.
- Shard-to-shard collision.
- Every shard as a full CPU physics body.
- Exact destructible mesh cutting for arbitrary convex hulls.
- Saved `.skreplay` playback unless the underlying replay system reaches that
  phase first.
- Gameplay damage, scoring, inventory, or networking behavior.

## Definition Of Done

The feature is done when:

1. A user can shoot a breakable object and see it fracture into many shards.
2. Shards bounce off terrain/floor and settle instead of falling through.
3. The replay slider can scrub backward through the fracture and visually
   reassemble the object.
4. The intact object and shard set have correct visibility before, during, and
   after the fracture frame.
5. The deterministic fracture scene reproduces the fracture frame.
6. DX12 validation reports zero errors.
7. Performance validation shows bounded overhead at the default shard count.
8. Runtime documentation explains the scene directives and user workflow.
