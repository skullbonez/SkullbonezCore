# DX12 rectification and shader profiling plan

> Execution plan: [Render and Shader Audit Remediation](../../Plans/TODO/render-and-shader-audit-remediation.md), queued at 0/6 on 2026-09-18. This document retains the original audit proposal and profiling detail; the TODO plan owns future phase acceptance and ledger updates.

18 September 2026 · Owner: Rendering, with Runtime/Render and UI owners · **0/6 phases complete**

This is the follow-up to the [audit report](report.md). It is an implementation proposal, not an executed remediation. Apply it to a reconciled branch containing the intended current hull, grass and UI work; do not overwrite the existing dirty working tree. Before implementation, follow [AGENTS.md](C:/SkullbonezCore/AGENTS.md) and the repository's [plan orchestrator](C:/SkullbonezCore/Agentic/Skills/orchestrator/SKILL.md). The current deliverables do not change the shared master plan or claim phase acceptance.

## 1. Establish trustworthy evidence — R1 and R9

- [ ] Repair GPU timestamps: per-frame query/readback ownership, per-occurrence pairs, resolved-validity masks, marker epoch/frame identity, nonblocking consumption, sample age and dropped/overflow counts. Aggregate repeated disjoint scopes without double-counting nested scopes.
- [ ] Add tests for two same-name scopes in one frame; a marker present then absent; marker reset/reindex; delayed fences; frame-slot wrap; overflow; and device recreation. An absent sample must be unavailable rather than a stale zero or old measurement.
- [ ] Add per-pass counters for actual draw/dispatch/indirect calls, logical instances, primitives, upload bytes, CB upload bytes, PSO/root/descriptor changes and barrier calls. Separate these from UI command counts. Add UI flush reasons: capacity, clip, image, layer and finalization.
- [ ] Add an isolated optimized profiling shader output mode, including matching source/symbols and a matching manifest/reflection set. Keep existing shipping output and hashes intact. Confirm source correlation before collecting captures.

**Acceptance:** repeated marker totals agree with externally captured disjoint ranges within measured run-to-run tolerance; stale identities never publish; no new GPU waits for telemetry; diagnostic overhead recorded with markers/counters enabled and disabled. Store captures and counter exports with commit, dirty-diff hash, executable hash and shader identities.

## 2. Establish the baseline matrix

Use the installed RTX 3080 first. Hold camera, scene tick, content, lighting, resolution, quality and prediction/replay state constant. Warm shaders, PSOs and assets before steady-state measurements; measure cold start/hot reload separately. Collect at least three comparable 30-second runs per selected case, then repeat longer if noise is material. Report median, p95 and p99 CPU frame, CPU submission and GPU frame/pass time, plus sample count and missing samples. Do not add CPU and GPU frame durations together.

| Case | Variations | Main question |
|---|---|---|
| Ordinary scene | UI off; shell; Tools; profiler/memory; Targets | Incremental UI and diagnostics cost |
| Hull-heavy scene | repeated hulls vs distinct meshes; shadows off/on | Draw and upload scaling |
| Split Future/cinematic | bloom/shafts/shadows/SMAA one at a time | Dominant full-screen and receiver costs |
| Water reflection | planar/DXR, paused/moving, low/high object count | Extra view work vs TLAS/trace cost |
| Grass scene | off/on; visible and shadow paths | Preserve recent grass work; avoid misattribution |
| Dense replay/forecast | short/long paths, many ghosts, scrubbing | Retained geometry and overlay scaling |

Run representative cases at 1920×1080 and 3840×2160 where supported. UI correctness additionally covers 640×480, 480×360 and 320×240. Use uncapped vsync-off runs for bottleneck isolation and the normal presentation settings for user-visible pacing. Exclude loading, screenshots and file IO from steady-state samples, but retain those events in a separate hitch report. Do not benchmark alongside another GPU-heavy session.

**Acceptance:** baseline evidence contains actual native frame/draw counts, view-specific counts, UI geometry/upload totals and externally verified GPU timing. Unknown values remain blank; no guessed budgets.

## 3. Repair inexpensive UI and binding waste — R3 to R6

- [ ] Batch the eight crosshair quads into one compatible sequence while preserving its layer.
- [ ] Reject fully clipped/transparent UI content before tessellation, using conservative glyph/rotated-text/halo bounds; preserve all stack transitions. Avoid flushes on unchanged effective scissor and fully invisible images when ordering allows.
- [ ] Prototype analytic rounded rectangles versus reusable geometry; include border and halo coverage. Compare CPU build time, geometry bytes and GPU blend cost.
- [ ] Reuse unchanged constant uploads within their valid allocation generation. Avoid same-shader rebinding when command-list state is still valid. Explicitly invalidate on reset, arena reuse, reload and device recreation.

**Acceptance:** crosshair 8→1 local draw count when present; rejected content creates no upload/draw; long lists scale with visible rows; no alpha, clipping, popup or text-order regression. Unchanged constants reuse an address only while its allocation remains valid. Show measured CPU/GPU and memory deltas for each change. Retain an optimization only if its benefit exceeds the measured noise or it establishes a needed correctness invariant.

## 4. Repair scene geometry submission — R2

- [ ] Build immutable hull render geometry at load/edit time with stable geometry identity and explicit invalidation. Preserve normals/UV seams and face silhouette.
- [ ] Pack per-view instance lists by mesh and compatible state; retain frustum/shadow culling and the existing separate transparency rules.
- [ ] Use compact shadow inputs and avoid re-expanding hull geometry for each shadow map. Distinguish CPU mesh-cache hits from actual GPU instance batching.
- [ ] Compare repeated-hull and unique-hull fixtures at increasing object counts. Count main, planar reflection and each shadow view independently.

**Acceptance:** ordinary frames do not triangulate or reupload immutable hull meshes. Repeated compatible hulls scale by mesh groups/capacity chunks rather than object count. Unique meshes remain correctly handled without false batching claims. Physics/collider data and replay identity remain unchanged; scene mutation/device reset cannot expose stale mesh resources.

## 5. Run shader and DXR experiments — R7 to R11

Change one variable at a time, using the procedure below. Prioritize by captured frame contribution.

1. Reuse tonemap's original center scene sample in bloom, before fog modifies the color. Check compiled sample sites and image equivalence.
2. Compare full-resolution bloom against a downsample/prefilter/blur/composite chain, accounting for extra draws, targets, transitions, taps and bandwidth. Thresholding before versus after filtering changes appearance; treat that as a quality decision.
3. Compare 24/32/48 volumetric steps at the existing half-size target. Check sun off-screen, depth boundaries, camera motion and temporal stability. Do not introduce a full-resolution duplicate march.
4. Profile active material styles and shadow receiver cost. Consider a small bounded variant set and reduced shadow kernels only where measured. Warm every new PSO and report cache/bytecode growth. Preserve implicit-derivative correctness when altering branch structure.
5. Compare unchanged-TLAS reuse, full rebuild and supported refit on paused/moving DXR cases. Include traversal time and topology changes in acceptance.

**Acceptance:** report per-pass and total-frame changes, CPU overhead, VRAM/upload deltas and visual comparisons. No replacement based only on smaller HLSL, fewer source operations or reduced draw count. Keep unchanged shipping quality as the default unless an explicit quality change is accepted.

## 6. Close with cumulative validation

Use the repository's current validation mapping. At the implementation boundary, typical required commands include:

```powershell
tools\validate_dx12_renderer.bat
tools\run_graphics_stress.bat 1
tools\validate_perf.bat
```

Add `validate_fast` for affected Runtime/tool changes, focused unit/source-design checks, and `validate_replay_visual_fidelity.bat` when touching replay presentation. UI changes need native Skarness interaction and inspected screenshots. Upload/frame-allocator changes require the renderer validation repeated three times under repository policy. Profiling-marker changes also require the `--platform-profiler-markers` runtime check. Full `agent_validate.bat --plan-completion` belongs only at final closure of the implemented plan, not this documentation audit.

**Acceptance:** zero DX12 validation errors; clean bounded stress exit with measured duration; no runtime allocation/overflow/memory growth; correct UI ordering; no unintended visual/replay/physics changes; no automatic golden refresh. Record every command, result and artifact. Finish independent review and required gates, leave Profile built in the working workspace, then update the owning plan/ledger with evidence. An unchanged second build must do no compile/link work before claiming F5-ready.

## Practical shader profiling procedure

### A. Prepare an optimized, identifiable build

Use Profile/x64, fixed content and a known camera. Enable existing markers with `--platform-profiler-markers`. Performance captures should have GPU validation disabled; run correctness validation separately. Keep the same settings across A/B runs.

Current baking strips debug information. Add the isolated profiling output mode from phase 1. Retain `-O3`, the entry point, shader model 6.6, matrix packing, includes and macros. Generate symbols using `-Zi` with `-Qembed_debug`, or separate symbols via `-Fd` with correctly configured source paths. Microsoft documents the relevant source-debug data and naming model in the [DXC source debugging guide](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SourceLevelDebuggingHLSL.rst).

For example, this **standalone diagnostic compile** writes outside shipping assets:

```powershell
$auditDxc = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe'
New-Item -ItemType Directory -Force TestOutput\shader-profile | Out-Null
& $auditDxc -T ps_6_6 -E main_ps -WX -Ges -O3 -Zpc -Zi -Qembed_debug `
  -Fo TestOutput\shader-profile\post_tonemap.ps.dxil `
  SkullbonezData\shaders\post_tonemap.hlsl
```

This command does not make the engine load that shader. Integrate the complete profiling asset set through its manifest/reflection contract before capture; do not overwrite one shipping DXIL file and bypass freshness checks. Do not use `-Od` for performance comparisons.

### B. Establish where the frame is limited

Take a PIX **Timing Capture** with GPU timing enabled and CPU samples when needed. Inspect frame pacing, render-thread work, submission gaps, waits and GPU busy time. This determines whether shader work, CPU submission or synchronization is the useful next target. Follow the [PIX timing capture guidance](https://devblogs.microsoft.com/pix/timing-captures/).

Take a separate PIX **GPU Capture** of the same warmed representative frame. Start analysis and collect timing data. Group by existing pass markers, inspect actual draw/dispatch parameters, shader identity and bound targets, and run warnings analysis. Check UI overdraw/pixel history, small draws, redundant state updates and barriers. Use timing captures to validate real queue overlap; GPU-capture replay does not preserve cross-queue concurrency. See [PIX GPU captures](https://devblogs.microsoft.com/pix/gpu-captures/).

### C. Profile the costly shaders

On this RTX 3080, collect an Nsight Graphics GPU Trace for the same workload. In Shader Profiler, rank active shaders and correlate costly sampled instructions to HLSL. Inspect register pressure/occupancy, texture dependency stalls and available memory/throughput counters. Low occupancy alone does not establish a problem; identify the limiting resource and its frame contribution first. Feature availability depends on installed Nsight version and hardware. See [NVIDIA Shader Profiler](https://docs.nvidia.com/nsight-graphics/UserGuide/shader-profiler.html).

Prototype one change, recompile with the same optimized settings, and compare both pass time and full-frame time. Shader debugging explains correctness; it is not a performance measurement. Record quality differences and reject optimizations that merely move cost into another pass or make the image worse without an accepted tradeoff.

### D. Drive repeatable UI/scene states

Use the repository's [Skarness procedure](C:/SkullbonezCore/Agentic/Skills/skarness/SKILL.md) for Automation state reproduction and screenshots. Its control availability must be discovered before selecting commands:

```powershell
python tools\skarness.py launch --session TestOutput\skarness\dx12-audit-baseline --exe Automation\SKULLBONEZ_CORE.exe --detail full
python tools\skarness.py capabilities TestOutput\skarness\dx12-audit-baseline
python tools\skarness.py query TestOutput\skarness\dx12-audit-baseline render-submission
python tools\skarness.py command TestOutput\skarness\dx12-audit-baseline capture.screenshot path=C:\SkullbonezCore\TestOutput\skarness\dx12-audit-baseline\baseline.png
python tools\skarness.py command TestOutput\skarness\dx12-audit-baseline session.stop
```

These commands are a future reproduction skeleton, not executed evidence. Query resulting identity/state after controls; an acknowledged command alone does not prove the requested state rendered. Keep Automation correctness evidence separate from Profile performance numbers and record build/instrumentation differences. Add narrowly missing counters/controls under the repository's existing authorization rather than substituting uncontrolled desktop input.

### E. Store a compact experiment record

For each candidate save: build/driver/GPU; scene/camera/tick; resolution/quality; warmup and sample window; capture path; active shader hash; draw/dispatch count; CPU submission and GPU pass/frame median/p95/p99; upload/VRAM; image comparison; profiler overhead; and retain/reject decision. Name the source of each number. The [static inventory](shader-static-inventory.csv) is useful for disassembly comparison, not a substitute for these measurements.
