# Render HAL Modernization Closure

Date: 2026-07-21  
Branch: `nightrunner-20th-july`  
Plan: `render-hal-modernization`, M0-M5 complete  
Verdict: closed; independent review clear after one stack-balance remediation

## Outcome

The DX12-only render HAL now receives typed, declared operation values instead
of reconstructing raster intent from mutable command-context setters. Raster
state is carried by `RasterStateDesc`/draw descriptions, the live cache identity
is populated from those values, and pass precompile shares the same fixed PSO
owner. The global raster setters and backend desired-state fields are deleted.

The DXR facet now accepts typed setup, reflection-ray, environment-texture, and
TLAS span descriptions. Dead clear/clip interface rows and ignored positional
parameters are gone. The M4 capability witness reported DXR tier 11 on this
machine.

M5 closes the remaining dependency-direction exception. `Core::Profiler` owns
only fixed marker/counter/worker history and accepts completed
`GpuTimingSample` values by Core marker hash. It stores no renderer borrow and
its public header contains no Rendering or Text declaration. Rendering owns one
concrete, non-polymorphic `RenderGpuTimingOwner` with fixed open-scope and
completed-sample arrays, plus one stateless `ProfilerOverlayPresenter` that
consumes `ProfilerFrameView` at the synchronous UI draw site. Runtime wiring
invalidates backend queries before teardown and hands Core only values.

GPU record ranges, backend timestamps, platform events, Core CPU nesting, and
Tracy render counters retain their immediate frame positions. Registry resets
advance a Core marker epoch; the render owner invalidates old backend slots
before reading the new identity generation. Release retains the no-op surface
without a hidden dependency.

## Closure Proofs

`TestOutput/agent_logs/render_hal_m5_closure_grep.txt` records zero rows for:

- mutable raster setter declarations on `IRenderCommandContext`;
- raster setter calls in runtime render passes;
- raw matrix pointers on the migrated command/DXR interfaces; and
- Rendering/Text types, backend borrows, callbacks, or global/service lookups
  in `Core/Profiler.h`.

The DX12 architecture target pins the concrete timing owner, absence of
polymorphism, empty presenter state, and fixed read-only marker span. A Profile
build after the final review fix completed in 10.21 seconds with zero warnings
or errors. Earlier focused evidence also includes a 32.10-second clean Release
build and a passing Debug DX12 architecture build/test.

## PSO And Performance Evidence

The same one-minute suite and seed show the cache remains warm and bounded:

| Evidence | M0 | M1 pilot | M5 closure |
|---|---:|---:|---:|
| In-process PSO entries/misses | 24 entries; exact hits not yet instrumented | 23 | 19 |
| Cache hits at frame 10,800 | not instrumented | 150,746 | 150,750 |
| Pass-precompiled PSOs | not instrumented | 1 | 1 |

M5's miss count remains fixed at 19 while hits grow. The four-entry reduction
relative to M0 is recorded without assigning a causal split among M1-M4.

| DX12 performance witness | M1 | M5 closure | Change |
|---|---:|---:|---:|
| Frame average | 0.7511 ms | 0.7245 ms | -3.54% |
| Frame P99 | 1.3535 ms | 1.2042 ms | -11.03% |

The ratified comparison gate reports no regression. Allocation evidence reports
`gameplay_violations=0` and reserve `policy_violations=0`.

## Closure Validation

| Command | Time | Result |
|---|---:|---|
| `tools\validate_full.bat` (first preflight) | 13.02 s | Correctly stopped on the incomplete header format pipeline; no runtime validation ran |
| `tools\validate_full.bat` (second preflight) | 15.62 s | Correctly stopped on two missing project-filter policy prefixes |
| `python tools\validate_project_filters.py --repo .` | 3.00 s | PASS; 722 project/filter items, zero errors |
| `tools\validate_full.bat` (pre-review source) | 174.54 s | PASS; 329/329 tests, 61,354 assertions, coverage, Automation, DX12, physics |
| `tools\validate_perf.bat` (pre-review source) | 104.58 s | PASS; no DX12/physics regression |
| `tools\run_graphics_stress.bat 1` (pre-review source) | 61.63 s | PASS; 12,784 frames, 351 scene loads, empty stderr |
| `tools\validate_full.bat` (final repaired tip) | 149.05 s | PASS; zero warnings/errors, CPU/coverage, Automation, zero DX12 errors, accepted images, 44,401 physics lines byte-exact |
| `tools\validate_perf.bat` (final repaired tip) | 106.07 s | PASS; 0.7245 ms average / 1.2042 ms P99, zero policy violations |
| `tools\run_graphics_stress.bat 1` (final repaired tip) | 61.99 s | PASS; 12,239 frames, 336 scene loads, graceful PID-scoped stop, empty stderr; 19 misses / 150,750 hits at frame 10,800 |
| `Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --platform-profiler-markers --frames 10 --replay off` | 1.28 s | PASS; exit 0, marker emission requested/enabled, empty stderr |

Final transcripts:

- `TestOutput/agent_logs/render_hal_m5_validate_full_final.log`
- `TestOutput/agent_logs/render_hal_m5_validate_perf_final.log`
- `TestOutput/agent_logs/render_hal_m5_graphics_stress_final.log`
- `TestOutput/agent_logs/render_hal_m5_platform_profiler_final_stdout.log`
- `TestOutput/agent_logs/render_hal_m5_platform_profiler_final_stderr.log`

No screenshot, replay golden, physics baseline, shader, or authored-data
artifact changed.

## Independent Review

The single end-of-plan reviewer found one blocking early-return defect and one
non-blocking presentation timing difference. `ReflectionPass::Render` opened
the outer reflection GPU range before a recoverable null-target return; the
fix converts that range to `PROFILE_GPU_SCOPED`, so DXR texture-resolution and
raster-target early returns both unwind the Core and render-owner stacks.
`UiTextPass` now obtains `FrameView()` at each presentation site, restoring
first-frame/reset marker visibility.

The same reviewer inspected the remediation and returned a clear verdict: no
other material M5 ownership, stack-balance, or borrow-lifetime issue remains.
The formal gates above were rerun after that verdict. A direct null-framebuffer
unit fixture is not practical because the pass receives the target through
DX12 resource creation rather than an injectable test value; the lexical RAII
shape, independent source review, architecture target, full gate, and bounded
stress are the recorded regression protection.

## Comment Quality Audit

Checklist: this report section. Checked 19/19 touched source-bearing files;
0 deferred and 0 unchecked:

- `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp`
- `SkullbonezSource/Core/Profiler.h`
- `SkullbonezSource/Rendering/ProfilerImplementation.cpp`
- `SkullbonezSource/Rendering/ProfilerOverlayPresenter.h`
- `SkullbonezSource/Rendering/RenderGpuTimingOwner.h`
- `SkullbonezSource/Runtime/Init.cpp`
- `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`
- `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp`
- `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- `SkullbonezSource/Runtime/RunFrame.cpp`
- `SkullbonezSource/Runtime/RuntimeDiagnostics.cpp`
- `SkullbonezSource/Runtime/RuntimeDiagnostics.h`
- `SkullbonezSource/Runtime/UiTextPass.cpp`
- `SkullbonezSource/UI/UI.cpp`
- `SkullbonezSource/UI/UI.h`
- `SkullbonezSource/UI/UIFrameComposition.cpp`
- `SkullbonezSource/UI/UIFrameComposition.h`
- `tools/validate_project_filters.py`

Every file retains the required learning-header sections. New local comments
name the value boundary, marker epoch, fixed-capacity/lifetime invariants,
teardown order, and recoverable early-return stack hazard.

## Ledger Reconciliation

M0-M5 are complete. Under MASTER inventory rule 4, the completed six-task plan
leaves the active/future ledger. Portfolio progress changes from 23/31 after M5
completion to 17/25 (68%) after mechanical removal of the completed plan's six
tasks. `gameplay-module-extraction` T0 is the next binding task.
