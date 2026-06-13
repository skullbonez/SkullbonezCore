# Roadmap Item Report: dx12-only-engine-architecture

## What Changed, In Plain English

The DX12 renderer now has the first real pieces of a cleaner architecture around it. The work did not replace the whole renderer in one jump. Instead, it pulled several fragile DX12 responsibilities into named helper systems, added very detailed comments for engineers without GPU background, and introduced a render graph contract that can describe frame passes and resource transitions before those transitions are allowed to replace the current hand-written barriers.

The validation workflow was also made less brittle. The renderer validation batch now fails fast when the build fails, instead of continuing into renderer launches that can make agents appear stuck. Every implementation slice was committed and pushed in small atomic commits, with renderer parity checks run repeatedly against GL, DX11, and DX12 as requested.

This report closes the current orchestrator queue item. It does not mean the entire long-term DX12-only architecture vision is finished. Remaining follow-up work includes making the graph drive live barriers, moving passes into graph callbacks, replacing name-based shader setters, and building the later material/resource systems described by the source plan.

## At A Glance

- Source plan: `Agentic/Plans/dx12-only-engine-architecture-plan.md`
- Archived plan: `Agentic/Plans/Done/dx12-only-engine-architecture-plan.md`
- Branch: `codex/dx12-only-engine-architecture`
- Implementation commits:
  - `7ff83dad` fix renderer validation fail-fast behavior
  - `8bbca94e` extract DX12 descriptor and upload allocators
  - `a7d119f0` add DX12 diagnostics naming and DRED setup
  - `fc073fb5` centralize DX12 fence timeline
  - `77b212ac` name DX12 pipeline diagnostics
  - `612ae9ff` add render graph contract
  - `2b57b4e1` compile render graph transitions
  - `7fcdc527` dump DX12 frame graph skeleton
  - `23618b64` require orchestrator reports before completion
- Queue/status commit: `ce381d07`
- Report commit: this report-only commit; exact SHA reported in final response
- Report web URL: https://github.com/skullbonez/SkullbonezCore/blob/codex/dx12-only-engine-architecture/Agentic/Reports/2026-06-14/dx12-only-engine-architecture/report.md
- PR: none opened
- Merge SHA: none
- Final status: `done`
- Queue status: `done`
- Started: `2026-06-13T21:54:57+10:00`
- Finished: `2026-06-14T08:59:48+10:00`
- Elapsed: about 11h 5m wall clock

## Progress Timeline

- Queued `dx12-only-engine-architecture` as the only orchestrator queue item and enabled the policy.
- Fixed `tools\validate_renderers.bat` so build failures stop the script before GL, DX11, or DX12 launches.
- Extracted DX12 descriptor allocation and upload arena policy from loose backend counters.
- Added DX12 DRED setup and human-readable names for major DX12 objects.
- Centralized frame fence signaling and waits in `Dx12FenceTimeline`.
- Named PSOs, root signatures, raytracing objects, readback resources, and other diagnostic objects.
- Added a heavily commented API-neutral `RenderGraph` contract.
- Added a first graph compile step that emits resource transition records.
- Added a diagnostic DX12 frame graph skeleton dump for current major passes and resources.
- Updated orchestrator documents so future runs require a committed report plus terminal queue status before finalizing.
- Archived the source plan and marked the queue item `done`.

## Timings

- `tools\validate_renderers.bat` after fail-fast batch fix: 35.8s, passed.
- `tools\validate_renderers.bat` after DX12 diagnostics/comment expansion: 58.9s, passed.
- `tools\validate_build.bat Profile` after fence timeline: 23.5s, passed.
- `tools\validate_renderers.bat` after fence timeline: 36.4s, passed.
- `tools\validate_build.bat Profile` after pipeline diagnostic names: 3.9s, passed.
- `tools\validate_renderers.bat` after pipeline diagnostic names: 19.1s, passed.
- `tools\validate_build.bat Profile` after render graph contract/comments: 24.3s, passed.
- `tools\validate_renderers.bat` after render graph contract/comments: 37.3s, passed.
- `tools\validate_build.bat Profile` after render graph transition compiler: 2.7s, passed.
- First transition-compiler renderer validation attempt: 2.8s, failed fast on formatting before renderer launches.
- `tools\format_fix.bat` after formatting failure: 2.9s, ran; unrelated formatter churn was reverted before commit.
- `tools\validate_renderers.bat` transition-compiler rerun: 63.4s, passed.
- `tools\validate_build.bat Profile` after frame graph skeleton: 22.8s, passed.
- `tools\validate_renderers.bat` after frame graph skeleton: 36.1s, passed.
- Orchestrator safeguard documentation: not validated by script because it was documentation/control metadata only.

## Implementation

### Validation Batch

`tools\validate_renderers.bat` now builds Profile first and fails immediately if the build fails. Renderer launches are guarded by PID-scoped timeout handling, so a compile failure no longer cascades into repeated renderer launch attempts.

### DX12 Device-Adjacent Helpers

`SkullbonezRenderDeviceDX12.h/.cpp` now contains:

- `Dx12DescriptorAllocator`, which explains and manages static descriptor slots and per-frame transient descriptor rows.
- `Dx12UploadArena`, which explains and manages CPU-written upload memory with fence-safe per-frame reset.
- `Dx12FenceTimeline`, which centralizes fence signaling and waiting.
- DRED and object-naming helpers so validation and device removal reports point to human-readable objects.

### Backend Integration

`SkullbonezRenderBackendDX12.cpp/.h` now uses those helpers for descriptor allocation, upload suballocation, frame allocator waits, full GPU drains, timer readback waits, and architecture usage stats. The backend now also includes a diagnostic-only frame graph skeleton dump.

### Render Graph

`SkullbonezRenderGraph.h/.cpp` adds:

- API-neutral graph resource declarations.
- Pass declarations with queue type, reads, and writes.
- Resource access declarations such as `RenderTarget`, `DepthWrite`, `PixelShaderResource`, `UnorderedAccess`, and `Present`.
- `RenderGraphCompileResult`, which lists transition records before passes.
- `DumpText()`, which prints resources, passes, and computed transitions.

This is scaffolding only. The current renderer still records live DX12 commands and barriers through the existing backend path.

### Comments For Non-GPU Engineers

The render architecture comments now explain descriptor heaps, descriptor allocators, SRVs, UAVs, RTVs, DSVs, upload arenas, fences, root signatures, root descriptor tables, PSOs, resource barriers, and render graph transitions in plain C++ engineering terms.

### Orchestrator Safeguards

The orchestrator documents now explicitly forbid successful finalization after code push alone. A terminal queue status and a committed report are required.

## Changed Files

Important implementation files:

- `tools\validate_renderers.bat`
- `AGENTS.md`
- `SKULLBONEZ_CORE.vcxproj`
- `SKULLBONEZ_CORE.vcxproj.filters`
- `SkullbonezSource\SkullbonezRenderBackendDX12.cpp`
- `SkullbonezSource\SkullbonezRenderBackendDX12.h`
- `SkullbonezSource\SkullbonezRenderDeviceDX12.cpp`
- `SkullbonezSource\SkullbonezRenderDeviceDX12.h`
- `SkullbonezSource\SkullbonezRenderGraph.cpp`
- `SkullbonezSource\SkullbonezRenderGraph.h`

Orchestrator/reporting files:

- `Agentic\Orchestrator\README.md`
- `Agentic\Orchestrator\queue.json`
- `Agentic\Orchestrator\runbook.md`
- `Agentic\Orchestrator\templates\report.md`
- `Agentic\Orchestrator\templates\worker-prompt.md`
- `Agentic\Plans\Done\dx12-only-engine-architecture-plan.md`
- `Agentic\Reports\2026-06-14\dx12-only-engine-architecture\report.md`

## Validation

- Required gate: `tools\validate_renderers.bat`
- Commands run:

```text
tools\validate_build.bat Profile
tools\validate_renderers.bat
tools\format_fix.bat
tools\validate_renderers.bat
```

- Final renderer result:

```text
tools\validate_renderers.bat: PASS
Formatting: PASS
Profile build: PASS
Debug build: PASS
DX12 InfoQueue validation errors: 0
Cross-renderer parity: PASS
water_ball_test GL vs DX12 avg_diff=2.9023
solver_smoke GL vs DX12 avg_diff=1.2131
```

Representative validation log:

```text
Agentic/Runs/2026-06-13/dx12-only-engine-architecture/validate_renderers_after_frame_graph_skeleton.log
```

## Screenshots And Artifacts

No report images were committed for this architecture/control report.

Renderer validation artifacts were generated under:

```text
TestOutput/validation/renderers/20260613T130434Z/
```

Useful generated files from the final renderer run include:

```text
TestOutput/validation/renderers/20260613T130434Z/manifest.json
TestOutput/validation/renderers/20260613T130434Z/summary.json
TestOutput/validation/renderers/20260613T130434Z/water_ball_test_gl_vs_dx12_side_by_side.png
TestOutput/validation/renderers/20260613T130434Z/water_ball_test_gl_vs_dx12_heatmap.png
TestOutput/validation/renderers/20260613T130434Z/solver_smoke_gl_vs_dx12_side_by_side.png
TestOutput/validation/renderers/20260613T130434Z/solver_smoke_gl_vs_dx12_heatmap.png
```

## Phone-Readable Images

No phone-readable images are embedded in this report. The main deliverable was render architecture code and validation behavior, not a visible scene change. Renderer comparison images remain in `TestOutput/validation/renderers/20260613T130434Z/`.

## Interesting Code Snippets

Descriptor allocator explanation:

```cpp
// A descriptor allocator does not allocate textures or GPU memory. It only
// hands out unused row numbers in that descriptor table and converts those row
// numbers into the CPU/GPU handles DX12 wants.
```

Render graph transition compiler:

```cpp
RenderGraphCompileResult RenderGraph::Compile() const
```

Diagnostic frame graph skeleton:

```cpp
void RenderBackendDX12::DumpFrameGraphSkeleton() const
```

Fail-fast renderer validation behavior:

```text
Build Profile first. If it fails, print the build-log tail and exit before renderer launches.
```

## PR Status

No PR was opened as part of this report. The branch was pushed to:

```text
codex/dx12-only-engine-architecture
```

## Merge Status

Not merged. Merge automation remains disallowed by repository policy and `AGENTS.md`.

## Conflicts

No merge conflicts were encountered.

One formatting issue was encountered during validation:

- `tools\validate_renderers.bat` failed fast at formatting for `SkullbonezRenderGraph.cpp`.
- `tools\format_fix.bat` was run.
- Unrelated formatter line-wrap churn in other headers was reverted before commit.
- The renderer validation gate then passed.

## Residual Risk

- The render graph is not yet the live barrier owner.
- The diagnostic frame graph skeleton is a superset of possible paths, not a per-frame branch-specific graph.
- The material system, shader reflection, resource allocator, DX12-only validation scripts, and live pass modules remain future work.
- GL and DX11 are still in the renderer parity suite as reference backends.
- No PR was opened, so this branch has not gone through GitHub review.

## Sub-Agent Result Summary

No sub-agent was spawned. The implementation worker role was performed locally in this session.

Run-state path:

```text
Agentic/Runs/2026-06-13/dx12-only-engine-architecture/
```

## Next Queue Action

The queue item is now terminal:

```text
dx12-only-engine-architecture: done
```

Recommended follow-up is to create a new queue item for the next live DX12 architecture phase, most likely:

```text
Move current DX12 pass/resource barriers into RenderGraph-owned diagnostics and then into live barrier emission.
```
