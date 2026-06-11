# PIX Profiling Integration Plan

## Goal

Add a PIX integration that lets existing Skullbonez profiler markers appear in:

- PIX CPU timing captures.
- PIX GPU captures/timelines for DirectX 12.
- Direct3D 11 GPU annotations where supported by tooling, using the D3D11 annotation interface.

The important constraint is that engine code should keep using the existing `PROFILE_*` and `PROFILE_GPU_*` marker vocabulary. Those markers should fan out to the in-engine profiler and, when explicitly enabled, to PIX-compatible APIs.

## Sources Checked

- Microsoft PIX WinPixEventRuntime docs: `pix3.h`, `PIXBeginEvent`, `PIXEndEvent`, and `PIXSetMarker` are provided by the `WinPixEventRuntime` package, with CPU overloads and D3D12 command-list/command-queue overloads.
- Microsoft PixEvents repository: PIX events are intended for CPU and GPU annotations, ETW timing captures, and GPU captures.
- Microsoft D3D11 `ID3DUserDefinedAnnotation` docs: D3D11 contexts expose `BeginEvent`, `EndEvent`, `SetMarker`, and `GetStatus`.

## Current Repo Fit

Relevant existing pieces:

- `SkullbonezSource/SkullbonezProfiler.h/.cpp`
  - Owns marker registration, CPU timing, GPU timing, CSV output, and UI.
  - Current macros:
    - `PROFILE_BEGIN`, `PROFILE_END`, `PROFILE_SCOPED`
    - `PROFILE_GPU_BEGIN`, `PROFILE_GPU_END`, `PROFILE_GPU_SCOPED`
- `SkullbonezSource/SkullbonezIRenderBackend.h`
  - Already provides backend-specific GPU timer hooks.
  - Good place to add backend-specific PIX GPU marker hooks.
- `SkullbonezSource/SkullbonezRenderBackendDX12.*`
  - Has one direct `ID3D12GraphicsCommandList*` and one `ID3D12CommandQueue*`.
  - Can call `PIXBeginEvent`, `PIXEndEvent`, and `PIXSetMarker` with D3D12 contexts.
- `SkullbonezSource/SkullbonezRenderBackendDX11.*`
  - Has `ID3D11DeviceContext*`.
  - Can query `ID3DUserDefinedAnnotation` and call its marker APIs.
- `SkullbonezSource/SkullbonezRunFrame.cpp` and `SkullbonezSource/SkullbonezRunRender.cpp`
  - Already wrap frame render/UI and render passes in `PROFILE_GPU_*` scopes.

## Design

### 1. Add a PIX adapter layer

Add small files:

- `SkullbonezSource/SkullbonezPixMarkers.h`
- `SkullbonezSource/SkullbonezPixMarkers.cpp`

Responsibilities:

- Hide `pix3.h` and D3D11 annotation details from most engine code.
- Compile to no-ops unless both profiler support and PIX support are enabled.
- Apply a runtime gate so validation/perf runs are unchanged by default.
- Convert profiler marker names to PIX colors.
- Keep stack discipline simple and assert/log mismatches in debug/profile builds.

Suggested API:

```cpp
namespace SkullbonezCore::Basics::PixMarkers
{
    void SetEnabled( bool enabled );
    bool IsEnabled();

    void CpuBegin( const char* name, uint32_t hash );
    void CpuEnd();
    void CpuMarker( const char* name, uint32_t hash );
}
```

Backend-specific GPU emission should stay on `IRenderBackend`, because only each backend knows the right command context.

### 2. Add backend hooks to `IRenderBackend`

Add no-op virtuals:

```cpp
virtual void PixGpuBegin( const char* name, uint32_t hash ) {}
virtual void PixGpuEnd() {}
virtual void PixGpuMarker( const char* name, uint32_t hash ) {}
```

The profiler calls these only for `PROFILE_GPU_*` scopes. This avoids leaking DX12/DX11 types into `SkullbonezProfiler`.

### 3. Avoid duplicate CPU scopes for GPU markers

Do not blindly call `PIXBeginEvent(color, name)` inside every `Profiler::Begin`.

Reason: `PROFILE_GPU_BEGIN` currently calls both CPU timing and GPU timing. The D3D12 `PIXBeginEvent(commandList/commandQueue, ...)` overloads also create associated CPU-side timing/marker information in PIX. If the engine also emits a separate CPU PIX event for the same GPU scope, captures will show duplicate CPU rows.

Implementation approach:

- Keep in-engine CPU timing for all markers.
- Update profiler entry points so marker kind is explicit:
  - CPU-only profiler scopes emit PIX CPU events.
  - GPU profiler scopes emit PIX GPU events through `Gfx().PixGpuBegin/End`.
- Update macros so:
  - `PROFILE_BEGIN/END/SCOPED` call CPU-kind methods.
  - `PROFILE_GPU_BEGIN/END/SCOPED` call GPU-kind methods that still record CPU elapsed time internally.

This is a small signature change inside `SkullbonezProfiler`; call sites should remain unchanged.

### 4. DX12 GPU marker implementation

In `RenderBackendDX12`:

- Include `pix3.h` only behind the PIX compile flag.
- Implement:
  - `PixGpuBegin(name, hash)`
  - `PixGpuEnd()`
  - `PixGpuMarker(name, hash)`
- Use the D3D12 PIX overloads:
  - `PIXBeginEvent(m_commandList, color, name)`
  - `PIXEndEvent(m_commandList)`
  - `PIXSetMarker(m_commandList, color, name)`

Important DX12 detail:

- Audit paths that close or execute `m_commandList` while a GPU PIX scope is open, especially `FlushUploadBuffer`, `Finish`, `FlushGPU`, and any mid-frame upload recovery.
- If command-list submission can happen inside an open marker, either:
  - split/reopen the active PIX event stack around the command-list close/reset, or
  - route top-level long-lived markers through `m_commandQueue` and keep pass-level markers on `m_commandList`.

Start conservative:

- Use command-list events for pass-level `PROFILE_GPU_*` markers.
- Add an internal debug counter/stack in `RenderBackendDX12` and assert if `ExecuteCommandLists` happens with an open command-list PIX stack before the split/reopen support exists.

### 5. DX11 GPU annotation implementation

The WinPixEventRuntime D3D-context overloads are D3D12-oriented. For DX11, use the official D3D11 annotation interface:

- Add `ID3DUserDefinedAnnotation* m_annotation = nullptr;`.
- During init, call `m_context->QueryInterface(__uuidof(ID3DUserDefinedAnnotation), ...)`.
- During shutdown, release it.
- Implement backend PIX hooks with:
  - `m_annotation->BeginEvent(wideName)`
  - `m_annotation->EndEvent()`
  - `m_annotation->SetMarker(wideName)`

Use stack-allocated UTF-8-to-wide conversion for marker names. Current marker names are ASCII path literals, so a simple bounded widening helper is enough.

### 6. Runtime and build gating

Add a runtime flag so normal validation and perf baselines are unaffected:

- Command line: `--pix-markers` / `--pix`.
- Optional scene directive later: `pix_markers on`.
- Optional environment fallback: `SKULLBONEZ_PIX_MARKERS=1`.

Build setup:

- Add `WinPixEventRuntime` as a project dependency.
- Define `USE_PIX` or a project-local `SKULLBONEZ_PIX_ENABLED` in Debug and Profile only.
- Do not enable PIX in Release.
- Ensure `WinPixEventRuntime.dll` is copied next to:
  - `Debug/SKULLBONEZ_CORE.exe`
  - `Profile/SKULLBONEZ_CORE.exe`
- Keep PIX headers/libs out of non-PIX builds or compile them to no-ops.

Because `Profile` already defines `SKULLBONEZ_PROFILE_ENABLED`, it is the main PIX capture target.

### 7. Marker policy

Initial permanent marker set should be the existing profiler tree:

- CPU:
  - `Frame`
  - `Frame/Input`
  - `Frame/Physics`
  - physics submarkers already present under broadphase/narrowphase/terrain/integrate
  - `Frame/VsyncWait`
- GPU:
  - `Frame/Render`
  - `Frame/Render/Skybox`
  - `Frame/Render/Reflection`
  - `Frame/Render/Reflection/Skybox`
  - `Frame/Render/Reflection/Balls`
  - `Frame/Render/CinematicSky`
  - `Frame/Render/Balls`
  - `Frame/Render/Terrain`
  - `Frame/Render/Shadows`
  - `Frame/Render/Water`
  - `Frame/UI`
  - `Frame/UI/Quads`
  - `Frame/UI/Text`

Avoid per-draw or per-object PIX markers initially. They will make captures noisy and can perturb hot paths.

### 8. Validation plan for implementation

Planning/documentation only requires no validation.

For the actual implementation, the impact area is renderer backends, profiler
infrastructure, runtime flags, and performance-sensitive hot paths. At the
pre-commit/PR gate, required validation should be:

```bat
tools\validate_full.bat
```

Additionally perform manual PIX smoke checks:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --pix-markers --profiler --scene SkullbonezData\scenes\perf_test.scene
Profile\SKULLBONEZ_CORE.exe --renderer dx11 --vsync off --pix-markers --profiler --scene SkullbonezData\scenes\perf_test.scene
```

Manual acceptance:

- In a PIX CPU Timing Capture, CPU markers appear with the same `Frame/...` hierarchy.
- In a PIX GPU Capture for DX12, render/UI GPU scopes appear around the expected command ranges.
- In DX11 captures/tooling, D3D11 annotation ranges appear where the tool supports them.
- With `--pix-markers` omitted, validation/perf output remains comparable to current baselines.
- DX12 validation output remains zero-error.
- `/W4` remains zero-warning.

### 9. Implementation order

1. Add build dependency and copy rules for `WinPixEventRuntime`.
2. Add `SkullbonezPixMarkers` no-op adapter and runtime enabled flag.
3. Add command-line parsing for `--pix-markers` / `--pix`.
4. Refactor profiler methods/macros to distinguish CPU-only and GPU marker emission while preserving existing call sites.
5. Add `IRenderBackend` PIX GPU virtuals.
6. Implement DX12 `pix3.h` GPU marker emission.
7. Add DX12 command-list close/execute stack assertions, then handle split/reopen if the assertions fire.
8. Implement DX11 `ID3DUserDefinedAnnotation` emission.
9. Update `Agentic/Reference/runtime-reference.md` with the new flag.
10. Run validation and manual PIX captures.

### 10. Risks

- Duplicate CPU markers if CPU and GPU PIX events are emitted for the same `PROFILE_GPU_*` scope.
- D3D12 PIX scope mismatch if a command list is closed while an event is open.
- Runtime dependency failures if `WinPixEventRuntime.dll` is not copied beside the executable.
- Perf baseline drift if PIX markers are enabled during standard validation.
- Too many markers in captures if temporary profiling markers become permanent without review.

## Recommended First Patch Scope

Keep the first patch narrow:

- PIX package/build wiring.
- Runtime flag.
- CPU PIX markers through existing CPU profiler scopes.
- DX12 GPU PIX markers for existing `PROFILE_GPU_*` scopes.
- DX11 annotation support if it stays small.

Do not add new render-pass markers until the bridge is working and captures are readable.
