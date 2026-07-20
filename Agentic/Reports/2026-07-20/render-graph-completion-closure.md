# Render Graph Completion Closure

Date: 2026-07-20
Branch: `nightrunner-20th-july`
Plan: `render-graph-completion`, G0-G5 complete
Verdict: closed; independent review clear

## Outcome

One production `RenderGraph` now accumulates the complete world and UI frame.
Each owner wrapper appends declarations and executes only its new callback
range, while named external resources retain stable identity and one compiled
transition history across the frame. `RenderPipeline` formats that live graph;
it no longer reconstructs a marker-only diagnostic schedule.

Normal frames close with exactly one declaration-only `Present` row immediately
before swap-chain presentation. Capture-restart frames close through an
explicit zero-declaration contract, poison callback payload borrows, clear
native-resource declarations, and release the command-context borrow before
capture automation can load another scene. Run opens graph ownership before
the text-only branch, so ordinary, text-only, Legacy UI, and ImGui frames share
the same lifecycle.

Actual ImGui vendor draw submission is reachable only through the
callback-owned `ImGuiEditorPass`. The DX12 architecture test pins incremental
graph growth, stable resource identity, range-only callback execution,
Present-to-render-target and render-target-to-Present transitions, normal and
capture-only declaration contracts, disabled callback rejection, and accidental
declaration rejection.

## Retired Paths And Remaining Exceptions

Static reconciliation found zero `callbackOwned`, `CallbackOwned`,
`RenderGraphBarrierPolicy`, or `GraphResult` scaffolding. No direct world/UI
pass-body fallback or marker graph remains. Completed graphs poison their
erased callback records, so accidental whole-graph replay fails before touching
a returned stack payload. The allocation-free graph fingerprint includes
resource/transient shape, pass labels, callback state, access, and subresources.

The remaining explicit backbuffer transitions are bounded edges, not ordinary
pass scheduling:

| Exception | Owner | Bound |
|---|---|---|
| Present | `RenderBackendDX12::Present` / `PresentBackbuffer` | Once per submitted frame after the graph's sole Present declaration. |
| Cold capture/readback | `Dx12BackbufferCapture` | Synchronous CopySource borrow; restores the exact entry access on success and failure. |
| Development viewport copy | `Dx12ImGuiRendererOwner::UpdateGameViewportTexture` | Development-only CopySource borrow; restores exact entry access before ImGui recording. |
| Shutdown/resize reconciliation | `RenderBackendDX12::Shutdown` | Lifecycle epoch only; moves swap-chain images to Present before replacement/release. |

Upload/mip, dynamic-geometry, and acceleration-structure barriers remain with
their concrete resource owners and are not frame-pass escape hatches.

## Focused Evidence

| Evidence | Time | Result |
|---|---:|---|
| Debug DX12 architecture build | 4.18 s | PASS; zero warnings/errors |
| Debug DX12 architecture tests | 22.32 s | PASS; all cases including normal/capture graph contracts |
| Direct Automation build | 18.31 s | PASS; zero warnings/errors |
| Legacy five-frame visible probe | 1.13 s | PASS; exit 0 |
| ImGui five-frame visible probe | 1.25 s | PASS; exit 0, 5 frames / 77 draws |
| First-frame text-only probe | 1.10 s | PASS; exit 0 |
| Screenshot capture/restart probe | 1.26 s | PASS; exit 0; callback-only graph closed without fabricated Present |

## Closure Validation

| Command | Time | Result |
|---|---:|---|
| `tools\validate_replay_visual_fidelity.bat` | 454.96 s | PASS; one invocation/engine generation, 2,401 ticks, 200 moved bricks, all positive and false-pass controls, zero golden refresh |
| `tools\validate_full.bat` (initial) | 13.80 s | Stopped in preflight before tests/runtime: one touched header required the repository format pipeline |
| `tools\validate_format.bat` | 12.78 s | PASS after formatting only `RuntimeRenderer.h` |
| `tools\validate_full.bat` (closure rerun) | 177.73 s | PASS; CPU/coverage, Automation, DX12, and five runtime processes; physics CSV 44,401 lines byte-exact |
| `tools\validate_perf.bat` | 104.76 s | PASS; zero steady-gameplay allocation/reserve violations and no budget/baseline regression |
| `tools\run_graphics_stress.bat 1` | 62.66 s | PASS; PID 27628 completed the bounded minute and closed by the intended PID-scoped timeout without a crash |

The DX12 performance witness recorded 1,940 frames at 0.6852 ms average and
1.2189 ms P99. The scale matrix also passed; the 2,000-body witness recorded
3.1951 ms average frame / 1.9427 ms average physics, and the sleeping 5,000-body
witness recorded 4.5707 ms / 2.4940 ms. The allocation guard reported
`gameplay_violations=0`, reserve `policy_violations=0`, and an explicit clean
PASS. No baseline, screenshot, replay golden, or physics artifact changed.

Validation transcripts:

- `TestOutput/logs/g5_validate_replay_visual_fidelity.log`
- `TestOutput/logs/g5_validate_full_rerun.log`
- `TestOutput/logs/g5_validate_perf.log`
- `TestOutput/logs/g5_run_graphics_stress.log`

## Independent Review

The first end-of-plan review blocked closure on two concrete lifecycle holes:
text-only frames did not begin graph ownership, and capture-restart frames could
continue/load a scene without graph completion. The main agent added the
unconditional begin boundary, explicit capture-only completion, callback-borrow
poisoning, transient/callback fingerprint coverage, focused tests, and four
runtime probes. The same reviewer then returned a clear verdict with no
blocking or material non-blocking findings.

| Plan | Duck run | Reviewer/thread | Reason | Prompt chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|---|---|---|---|---:|---:|---:|---:|---|---|
| `render-graph-completion` | `render-graph-completion-duck-01` | `/root/render_graph_completion_duck_01` | Initial closure review | 866 | 4,173 | n/a | ~15 min | Blocked: text-only and capture lifecycle holes | Required and completed |
| `render-graph-completion` | `render-graph-completion-duck-02` | `/root/render_graph_completion_duck_01` | Follow-up after lifecycle remediation | 1,307 | 1,912 | n/a | ~10 min | Clear; no material findings | None |

## Comment Quality Audit

Checklist: this report section. Checked 10/10 touched source-bearing files;
0 deferred and 0 unchecked:

- `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp`
- `SkullbonezSource/Rendering/RenderGraph.cpp`
- `SkullbonezSource/Rendering/RenderGraph.h`
- `SkullbonezSource/Rendering/RenderPipeline.cpp`
- `SkullbonezSource/Rendering/RenderPipeline.h`
- `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp`
- `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- `SkullbonezSource/Runtime/RunFrame.cpp`

Every file retains a complete learning header. New local comments explain the
one-frame callback/resource lifetime, range-only execution, capture-only
completion, ImGui draw-data lifetime, Present exception, and allocation-free
diagnostic fingerprint. No human-approved wording remains outstanding.

## Ledger Reconciliation

G0-G5 are complete. Under MASTER inventory rule 4, the completed six-task plan
leaves the active/future ledger. Portfolio progress changes from 22/37 (59%) to
17/31 (55%); the apparent percentage drop is the mechanical removal of the
completed plan's six tasks and five previously counted completed rows. Render
HAL modernization is now unblocked and M0 is the next binding task.
