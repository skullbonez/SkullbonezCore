# Render Interface And WorkerPool Measurement

Date: 2026-07-12
Plan: `render-interface-and-workerpool-slimming`
Branch: `nightrunner-12th-july`

## WorkerPool Call Inventory

The production inventory covered `Submit`, `ParallelFor`,
`ParallelForChunks`, `MakeChunks`, and their fixed/no-allocation variants.

| Caller | Phase | API before | Verdict |
|---|---|---|---|
| `AmortizedTask::SubmitTick` | Per-frame while replay prediction builds | `Submit(std::function)` | Migrated to typed `SubmitNoAlloc(task)` over a fixed private ring |
| `RunWorkerSystemSelfTest` | Validation | `ParallelFor(std::function)` | Migrated to `ParallelForNoAlloc` |
| `PhysicsWorld` (4 sites) | Physics hot path | `ParallelForNoAlloc` | Keep |
| `TornadoGameplay` | Physics hot path | `ParallelForNoAlloc` | Keep |
| `GameModelRenderer` (5 sites) | Render hot path | fixed chunk build + `ParallelForChunksNoAlloc` | Keep |
| replay prediction tools (2 sites) | Replay hot path | `ParallelForNoAlloc` | Keep |

No caller used allocating `ParallelForChunks` or `MakeChunks`. Those overloads,
the three `std::function` task aliases, and the dynamic general task deque were
deleted. The replacement general-task ring contains 256 private type-erased
records behind a typed public API and fails fatally with
owner/phase/count/capacity/high-water diagnostics on exhaustion. Its self-test
submits 10 fenced rounds of 32 tasks, forcing head/tail wraparound without
depending on worker scheduling.
The allocation-policy checker then exposed the obsolete `std::deque` allowlist
pattern; removing it left zero allowlist errors.

## Representative Render Measurement

Command:

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --no-contact-audio --frames 180 --scene SkullbonezData/scenes/perf_1000.scene.json
```

The PID-scoped visible launch completed with exit 0 in 9.51s. The added
architecture diagnostic measured `draw_call_high_water=20`; its bounded raw
evidence was copied to
`TestOutput/validation/r1_perf1000_runtime_events.log` during the run.

The representative principal-dispatch model is 20 draws multiplied by three
draw-facing role categories—shader activation, mesh/dynamic submission, and
command-context submission—for at most 60 principal virtual category hops in
the busiest frame. Constant/texture setters add pass-scoped interface calls.
The binding `tools\validate_perf.bat` gate passed both absolute DX12 budgets
and the baseline comparison with no regressions across 1,940 frames. This
dispatch volume does not
justify replacing narrow role seams with full-backend access or forwarding
wrappers.

## Interface Inventory And Verdicts

| Interface | Consumers / call frequency | Verdict and reason |
|---|---|---|
| `IRenderDeviceLifecycle` | 19 files; startup/frame lifecycle; DX12 architecture unit test | Keep: narrow device/reset/drain authority and direct test consumer |
| `IRenderResourceFactory` | 35 files; cold resource creation/rebuild; two test files | Keep: `TestRenderResourceDoubles` and determinism tests construct null resources without a device |
| `IRenderCommandContext` | 38 files; per-pass/per-draw | Keep: capability narrowing prevents draw code from reaching lifecycle/capture/backend state; measured dispatch is negligible |
| `IRenderDiagnostics` | 30 files; per-frame counters/traces | Keep: read/write diagnostic capability without command or resource authority |
| `IRenderCaptureBackend` | 14 files; cold capture/readback; owner-request test | Keep: explicit recoverable external-output seam and test consumer |
| `IRenderRayTracing` | 10 files; optional reflection setup/dispatch | Keep: nullable capability boundary prevents raster code from assuming DXR support |
| `IRenderShaderDevelopment` | 4 files; manual cold reload | Keep: cold developer capability isolated from runtime command authority |
| `IShader` | 24 files; pass/draw constants; `NullShader` test double | Keep: real no-device test implementation. A devirtualization probe failed 13 override contracts and was restored |
| `IMesh` | 12 files; draw/setup queries; `NullMesh` test double | Keep: real no-device test implementation; value wrapping would add allocation/forwarding |
| `IFramebuffer` | 10 files; target lifecycle; null framebuffer test double | Keep: testable resource lifetime seam |

## R2 Decision

Collapse list: empty.

Every interface has either a direct test/optional capability consumer or a
cohesive authority-narrowing role. The one attempted shader/mesh collapse was
rejected by concrete compilation evidence from the complete null-resource test
doubles, and the final perf gate found no DX12 regression. No `*Adapter`,
`*Bridge`, callback interface, or full-backend reach was introduced. R2
therefore closes as the plan-authorized evidence-recorded keep-everything
outcome.
