# Draw Call Trace Tree Plan

Status: proposed
Created: 2026-06-15
Scope: renderer diagnostics, DX12 draw submission, profiler/debug UI
Validation when implemented: `tools\validate_dx12_renderer.bat`; add `tools\validate_perf.bat` if the trace is enabled in normal Profile runs or touches hot-path allocation behavior.

## Goal

Make climbing draw-call count easy to inspect in-engine.

The debug screen should show a tree of actual GPU draw submissions, grouped by render pass and logical batch, so the total count can be explained without opening an external capture. The first useful view should live underneath the existing Profiler tab because that tab already owns expandable timing rows and frame diagnostics.

Also rename the existing `NoteDrawCall()` hook. The name is too vague for a diagnostic contract. Use `RecordDrawCall()` for the counter/trace hook because it describes bookkeeping around a draw that has already been emitted or is about to be emitted, without implying that the helper itself submits GPU work.

## Current Read

Relevant existing pieces:

- `SkullbonezSource/SkullbonezIRenderBackend.h`
  - `ResetFrameDrawCallCount()`
  - `NoteDrawCall()`
  - `GetFrameDrawCallCount()`
- `SkullbonezSource/SkullbonezRenderBackendDX12.h`
  - stores `m_frameDrawCallCount`
  - increments the count through `NoteDrawCall()`
- Draw submission sites:
  - `SkullbonezSource/SkullbonezMeshDX12.cpp`
    - `MeshDX12::Draw()`
    - `MeshDX12::DrawInstanced()`
  - `SkullbonezSource/SkullbonezRenderBackendDX12.DynamicGeometry.cpp`
    - `UploadAndDrawDynamicVB()`
    - `DrawLinesColored()`
    - `DrawInstancedMesh()`
- High-level render pass scopes already exist:
  - `Frame/Render/Skybox`
  - `Frame/Render/Reflection`
  - `Frame/Render/Reflection/Skybox`
  - `Frame/Render/Reflection/Balls`
  - `Frame/Render/Balls`
  - `Frame/Render/Terrain`
  - `Frame/Render/Water`
  - `Frame/Shadows/ShadowMap`
  - `Frame/UI/Draw`
- The Profiler tab already renders expandable tree rows from a marker hierarchy.
- The top UI already reports total draw calls and UI draw calls, but not why the count exists.

## Main Problems

### 1. The counter has no attribution

`GetFrameDrawCallCount()` can say that the number went up, but not whether the extra calls came from shadows, reflection, debug overlays, UI, text, terrain, water, or object batches.

### 2. `NoteDrawCall()` is not a good contract name

The call is not a casual note. It is the central hook for draw-call diagnostics. Rename it before expanding it so the public renderer interface stays readable.

Preferred names:

- `RecordDrawCall()` for the actual hook.
- `ResetFrameDrawCalls()` if renaming the reset function in the same pass.
- Keep `GetFrameDrawCallCount()` unless the implementation also adds a richer `GetFrameDrawCallTrace()` accessor.

Avoid names like `SubmitDrawCall()` because the hook records diagnostics; the actual DX12 command-list method submits the draw.

### 3. Per-draw profiler markers would be the wrong tool

Do not add a CPU/GPU profiler marker or GPU timer around every individual draw. The profiler has a fixed marker budget, GPU timestamp queries add overhead, and per-draw timers would distort the count investigation.

Use a cheap CPU-side draw trace for count attribution. Keep GPU timers at pass scope.

## Target Model

Add a renderer-neutral draw-call trace that is reset each frame, records actual GPU draw submissions, and exposes an aggregate tree to the debug UI.

Suggested data shape:

```cpp
enum class DrawCallKind
{
    Mesh,
    InstancedMesh,
    DynamicVertexBuffer,
    DebugLines,
    Unknown
};

struct DrawCallRecord
{
    DrawCallKind kind = DrawCallKind::Unknown;
    const char* label = nullptr;      // string literal or stable static name
    int vertexCount = 0;
    int instanceCount = 1;
};

struct DrawCallTraceNode
{
    const char* name = nullptr;       // full path
    const char* leafName = nullptr;
    uint32_t hash = 0;
    int parentIndex = -1;
    int depth = 0;
    int drawCallCount = 0;
    int vertexCount = 0;
    int instanceCount = 0;
};
```

Keep the runtime trace fixed-capacity and allocation-free during frame rendering. If capacity is exceeded, increment an overflow counter and show that in the UI instead of allocating in the hot path.

Initial caps can match existing diagnostics:

- `MAX_DRAW_TRACE_NODES = 128`
- `MAX_DRAW_TRACE_EVENTS = 512`

The UI only needs aggregate nodes at first. Per-draw event rows can be added later if they prove useful.

## Implementation Phases

### Phase 1: Rename the existing hook

Mechanical rename:

- `IRenderBackend::NoteDrawCall()` -> `IRenderBackend::RecordDrawCall()`
- `RenderBackendDX12::NoteDrawCall()` -> `RenderBackendDX12::RecordDrawCall()`
- update all call sites in:
  - `SkullbonezMeshDX12.cpp`
  - `SkullbonezRenderBackendDX12.DynamicGeometry.cpp`

Optional same-slice cleanup:

- `ResetFrameDrawCallCount()` -> `ResetFrameDrawCalls()`

Keep behavior identical in this phase: one increment per actual DX12 draw call.

### Phase 2: Add frame-local draw trace storage

Add a small renderer diagnostics module, for example:

- `SkullbonezSource/SkullbonezDrawCallTrace.h`
- `SkullbonezSource/SkullbonezDrawCallTrace.cpp`

Responsibilities:

- `BeginFrame()`
- `PushScope(const char* fullPath, uint32_t hash)`
- `PopScope(uint32_t hash)`
- `RecordDrawCall(const DrawCallRecord& record)`
- aggregate draw, vertex, and instance totals into the current scope and all ancestors
- expose immutable frame data to UI after the frame has rendered

The draw trace should be independent from GPU timers. It may share marker path names with the profiler, but it should not consume profiler marker slots.

### Phase 3: Scope the existing render paths

Start with existing high-level pass scopes, then add batch scopes where count attribution matters.

Useful initial tree:

```text
Frame
  Shadows
    ShadowMap
      TerrainCasters
      ObjectCasters
        Spheres
        Boxes
        Pines
  Render
    Skybox
    Reflection
      Skybox
      Balls
        Spheres
        Boxes
        Pines
    Balls
      Spheres
      Boxes
      Pines
    Terrain
    Water
    DebugOverlay
      Broadphase
      TornadoField
      PhysicsDebug
  UI
    Draw
      BackdropBlur
      Widgets
      Text
```

Where possible, reuse the existing profiler names for high-level branches so the timing tree and draw tree are mentally aligned.

For lower-level object batches, add explicit draw scopes around the batch draw points:

- sphere batch end
- box batch end
- pine batch end
- shadow sphere batch end
- shadow box batch end
- shadow pine batch end
- collision visualizer instanced batches
- debug line visualizers

The trace should distinguish actual GPU draw calls from UI draw-list commands. A UI panel may emit many shape commands, but the draw tree should report only the final GPU draw submissions.

### Phase 4: Extend `RecordDrawCall()` metadata

After the tree exists, change the hook from a bare increment to a metadata call:

```cpp
virtual void RecordDrawCall(const DrawCallRecord& record);
```

Keep a convenience overload:

```cpp
void RecordDrawCall()
{
    RecordDrawCall({});
}
```

Suggested metadata at each backend draw site:

- `MeshDX12::Draw()`: `Mesh`, vertex count, one instance
- `MeshDX12::DrawInstanced()`: `Mesh`, vertex count, instance count
- `DrawInstancedMesh()`: `InstancedMesh`, static vertex count, instance count
- `UploadAndDrawDynamicVB()`: `DynamicVertexBuffer`, vertex count, one instance
- `DrawLinesColored()`: `DebugLines`, vertex count, one instance

Do not stringify dynamic details every draw. Labels should be stable string literals supplied by the surrounding scope or known draw site.

### Phase 5: Add the Profiler tab tree view

Place a `Draw Calls` section underneath the existing profiler table in `SkullbonezSource/UI/UITabProfiler.cpp`.

Recommended columns:

- `Scope`
- `Draws`
- `Instances`
- `Vertices`

Behavior:

- Collapsible tree rows, reusing the profiler tab's expander pattern.
- Hide zero-draw branches by default.
- Default-expanded roots:
  - `Frame/Render`
  - `Frame/Shadows`
  - `Frame/UI`
- Show an overflow indicator if trace node/event capacity was exceeded.
- Keep the existing profiler table first; draw attribution is supporting detail beneath it.

State can live beside the profiler tab state:

```cpp
uint32_t drawExpandedHashes[MAX_MARKERS];
int drawExpandedHashCount;
bool drawDefaultExpansionApplied;
```

The existing scroll area should include both the timing table and the draw-call section in `ContentHeight()`.

### Phase 6: Optional platform profiler annotation

Do not emit per-draw PIX markers by default.

Optional future mode:

- `--draw-call-markers`
- only active when platform profiler markers are enabled
- emits `PlatformProfilerGpuMarker()` for draw leaves with compact labels

This can help external GPU captures, but it should stay opt-in because one marker per draw can create noisy captures and measurable CPU overhead.

## UI Acceptance Criteria

- The Profiler tab shows a `Draw Calls` section underneath the existing timing table.
- Total draw count in the tree matches `GetFrameDrawCallCount()`.
- UI subtree count matches the existing measured UI draw-call subtotal.
- Expanding object rendering shows separate sphere, box, and pine batch counts.
- Shadows and reflection have separate branches, so off-screen passes no longer look like mysterious extra world draws.
- Debug line overlays appear under a debug branch when enabled.
- If trace capacity is exceeded, the UI says so and still shows partial totals.

## Engineering Constraints

- No heap allocation inside per-draw recording.
- No per-draw GPU timestamp queries.
- No dynamic profiler marker registration per draw.
- No new runtime dependency on retired GL/DX11 paths.
- Keep DX12 validation at zero errors.
- Keep the trace disabled or very cheap in normal non-debug display modes.
- Preserve the existing total draw count behavior throughout the rename.

## Suggested Validation For Implementation

Do not run validation while iterating unless a specific question requires it.

Before PR-bound commit:

- `tools\validate_dx12_renderer.bat`

Also run:

- `tools\validate_perf.bat`

when the implementation records richer metadata in normal Profile builds, changes the draw hot path beyond a simple increment, or leaves the draw tree active outside the visible debug/profiler UI mode.

## Open Decisions

- Whether draw scopes should be explicit `DRAW_CALL_SCOPE()` calls or piggyback on existing `PROFILE_GPU_*` scopes for high-level branches.
- Whether `ResetFrameDrawCallCount()` should be renamed in the same slice as `NoteDrawCall()`.
- Whether to expose the trace through `IRenderBackend` or a standalone diagnostics singleton.
- Whether per-draw event rows are worth keeping after the aggregate tree ships.
