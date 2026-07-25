# UI / Renderer Hard Boundary Closure

Date: 2026-07-25
Branch: `nightrunner-25th-JUL-26`
Plan: completed at 7/7; live TODO deleted under master-plan inventory rule 4
Closure commit: the commit containing this report
Status: **complete**

## Outcome

The Legacy UI package is now a backend-neutral CPU presentation library.
It consumes detached values, performs layout and hit testing, and records one
bounded command stream. It contains no Runtime or Rendering include edge,
renderer owner, backend handle, shader, vertex buffer, GPU timer, or resource
lifecycle operation.

`Runtime/Render` is the sole composition layer that sees both UI draw values
and Rendering capabilities. Its concrete `UiDrawSubmission` translates the
stream into text, geometry, and preview work while owning preview GPU resource
lifetime. Rendering retains measurement and backend state but no
operator-surface layout policy.

A dedicated `SKULLBONEZ_UI` static-library target, a renderer-free production
link probe, and the mandatory data-driven dependency graph make the physical
boundary continuously testable.

The owner approved the two applicable performance-baseline updates after the
memory increase was attributed to retained solar-path storage introduced
before UR0. The refreshed comparisons and the complete `validate_perf.bat`
gate pass.

## Before / After Census

| Evidence | Before UR0 | Final source |
|---|---:|---:|
| Direct `UI -> Rendering` include rows | 25 | 0 |
| UI files containing a Rendering include | 14 | 0 |
| Direct `UI -> Runtime` include rows | 0 | 0 |
| Direct `Rendering -> UI/Runtime` include rows | 0 | 0 |
| Production targets compiling UI implementation directly | 1 application target | 1 dedicated UI library |
| UI implementation duplicated in application/tests | yes | no |
| Mandatory qualitative dependency rules | review-time shell snippets | 25 data rows plus fixtures |

All 25 registered UI-to-Rendering rows were deleted together with their
backend authority:

| Registered responsibility | Final disposition |
|---|---|
| Text measurement in widgets/layout | UI-owned immutable `UIFontMetrics` |
| `UIRenderContext` backend borrows | deleted |
| Renderer pointers in `UIDrawContext` | deleted; the context records values only |
| UI-side flush/callback dispatch | deleted; Runtime/Render iterates a read-only span |
| Immediate text/quad/image submission | `Runtime/Render::UiTextPass` and `UiDrawSubmission` |
| UI-owned preview shader and dynamic vertex buffer | `Runtime/Render::UiDrawSubmission` |
| Raw preview texture handles in UI values | replaced by a UI catalog identity; resolved during submission |
| Renderer diagnostics embedded in UI | projected by `Runtime/UI::RenderDiagnosticsProjection` |
| Operator profiler presentation in Rendering | `UIProfilerOverlayPresenter` |
| Development ImGui renderer | remains explicitly owned by `Runtime/DevelopmentTools` |

Deletion searches found no alias, forwarding context, callback facade, or
renamed compatibility spelling for the removed authority.

## Final Dependency And Project Graph

```text
Runtime owners
  -> Runtime/UI projection
  -> UI detached frame values
  -> UI layout, hit testing, and UIDrawList
  -> Runtime/Render UiDrawSubmission
  -> Rendering/DX12 owners

UI typed commands
  -> Runtime/App arbitration
  -> owning Runtime/domain command application
```

The authoritative dependency validator reported:

```text
SELF_TEST_PASS: 25 include-rule fixtures and 1 project-rule fixtures passed
Dependency graph summary: include_rules=25 project_rules=1 findings=0
```

The self-test exercises the real quoted/angle-bracket include parser,
repository resolver, rule evaluator, and project-membership evaluator. The
`runtime_input` row permits only the exact Window, ReplayEventCommand, and
SceneLifecycle seams rather than broad Runtime package prefixes.

Every standing human-readable direction proof also returned zero rows:

- Core downward direction
- Physics/Rendering upward direction
- Gameplay upward direction
- UI-to-Runtime/Rendering direction
- downward Replay includes
- all 18 Runtime package rows, including top-level frame views

Project/filter validation reported 767 matching items across four production
projects. `SKULLBONEZ_UI.vcxproj` owns exactly 34 UI translation units and 36
UI headers. Neither `SKULLBONEZ_CORE.vcxproj` nor
`SKULLBONEZ_TESTS.vcxproj` compiles a UI implementation file directly.

## Draw Stream, Capacity, And Allocation

`UIDrawList` retains plain fixed-capacity commands and copied text:

| Storage | Capacity | Measured high-water | Headroom |
|---|---:|---:|---:|
| Commands | 2,048 | 289 | 7.0x |
| Text bytes | 16,384 | 1,369 | 11.9x |
| Clip nesting | 32 | below cap in representative captures | bounded |

The high-water run covered the full editor, Render, Targets, Memory,
standalone memory overlay, replay presentation, text-only presentation, and
the complete 21-scene UI suite. Per-frame diagnostic output reported no
command, text, or clip overflow.

Unused fixed-capacity command rows are trivial and untouched. `PushCommand`
value-initializes every committed row, preserving deterministic semantic
fingerprints without faulting every reserved page at owner construction.
Suppressed over-depth clip pushes retain their own logical depth so matching
pops cannot remove a retained outer clip.

The allocation-policy scan reported:

```text
scanned=443 direct_heap_findings=30 dynamic_stl_member_findings=131
stl_growth_findings=648 allowlist_errors=0
```

No runtime growth privilege was added or expanded.

## Resource Ownership And Lifecycle

| Resource or state | Owner | Create / rebuild | Reset / release proof |
|---|---|---|---|
| UI layout, cache, hit-test state | `InGameUI` | CPU construction from detached values | `ResetPresentationState`; no renderer parameter |
| Ordered command/text storage | `UIDrawList` instances in UI and Runtime/Render scratch owners | fixed storage at owner construction | `Clear`; no allocation or GPU lifetime |
| Text shader, atlas, and text batch submission | existing Runtime/Render and Rendering text owners | renderer initialization | renderer teardown order |
| Preview shader and dynamic vertex buffer | `Runtime/Render::UiDrawSubmission` | lazy `EnsurePreviewResources` during a preview submission | `ReleaseGpuResources` before geometry-owner teardown |
| Preview texture handles | renderer-owned frame snapshot | projected each render frame | borrowed synchronously by `SubmitWithPreviews`; never retained |
| Profiler and render diagnostics | Rendering measurement owners | backend/runtime sampling | copied to bounded UI values; no UI owner borrow |

`validate_dx12_renderer.bat`, the 21-case UI suite, replay presentation, and
the bounded graphics-stress run exercised repeated frame submission and
shutdown without stale handles, double release, device-loss diagnostics, or
missing preview content.

## Behavioral And Visual Evidence

Final-source captures were inspected for clipping, overlap, alignment, stale
content, and missing labels:

| Required state | Evidence |
|---|---|
| Full editor | `TestOutput/validation/ui_boundary/editor_full.bmp` |
| Minimized editor | `Profile/ui_dx12_minimized.bmp` |
| Render tab | `TestOutput/validation/ui_boundary/render_tab.bmp` |
| Render-target preview | `TestOutput/validation/ui_boundary/render_targets.bmp` |
| Profiler table/hierarchy | `Profile/ui_dx12_profiler_default.bmp`, `Profile/ui_dx12_profiler_hierarchy.bmp` |
| Profiler timeline/histogram | `Profile/ui_dx12_profiler_timeline.bmp`, `Profile/ui_dx12_performance_histogram.bmp` |
| Memory tab/overlay | `TestOutput/validation/ui_boundary/memory_tab.bmp`, `memory_overlay.bmp` |
| Replay overlay | `TestOutput/interaction/replay_prediction_after_click.bmp` |
| Text-only mode | `TestOutput/validation/ui_boundary/text_only.bmp` |

The manual captures were temporary ignored validation artifacts. The
authoritative UI comparator passed all 21 committed capture cases without a
golden refresh. The replay screenshot shows the Legacy replay control bar,
cause tree, selected path target, and prediction presentation from the real
physics/replay harness.

Production frame-stream fingerprints are committed for all eleven public UI
tabs by calling the real `InGameUI::Draw`; the test does not hand-author a
synthetic substitute. A separate Release executable links only
`SKULLBONEZ_UI` and reproduced all eleven fingerprints without Runtime,
Rendering, a native window, or a graphics device.

## Independent Review

One independent whole-plan rubber-duck review inspected the six implementation
commits, the aggregate boundary diff, projects, tests, validator, allocation
and lifetime paths, and UI capture coverage. No second review loop was run.

| Finding | Disposition |
|---|---|
| Synthetic fingerprint fixture did not call production UI | replaced with real all-tab `InGameUI::Draw` fingerprints |
| No UI-only link proof | added `UiBoundaryUnitTests`, referencing only `SKULLBONEZ_UI` |
| Angle-bracket includes bypassed dependency parsing | parser and end-to-end negative fixtures now cover both forms |
| Validator fixtures bypassed scanner/resolver/project evaluator | self-tests now execute the production path in temporary repositories |
| `runtime_input` allowed broad package prefixes | replaced with exact permitted files |
| Over-depth clip push could pop an outer clip | added suppressed logical depth and regression assertions |
| `UiDrawSubmission.h` carried a non-permanent related path | points to this permanent closure report |
| Missing Memory/Targets/replay/text-only capture evidence | added final-source manual captures plus the real replay harness capture |
| Capacity evidence covered only profiler content | instrumented all representative surfaces and the complete UI suite |
| No focused `UIProfilerOverlayPresenter` exercise | added detached-value doctest with exact command semantics |
| Projection/submission lifecycle evidence was indirect | covered by the CPU projection tests, renderer validation, preview captures, stress shutdown, and static ownership checks |

The reviewer reported no other owner leak. A follow-up found two capacity
evidence defects: early `InGameUI::Draw` returns bypassed telemetry, retained
`UiTextPass` scratch streams were not observed on every return, and a nearby
comment still named the obsolete 8,192-command cap. The final source routes
every UI return through combined frame/histogram/memory telemetry, observes
all four retained pass lists with an exit-path scope, and consistently names
the 2,048-command capacity. The reviewer rechecked those remediations and
reported zero blocking or non-blocking findings.

## Validation

| Gate | Final result |
|---|---|
| Dependency validator self-test/live scan | pass; 25 include fixtures, 1 project fixture, 0 findings |
| Human direction proofs | pass; 23 proof groups, all 0 rows |
| Project/filter validation | pass; 767/767 items across 4 projects |
| Renderer-free UI boundary test | pass; 11 production surfaces |
| `validate_tests.bat` | pass; 391 cases, 2,403,286 assertions |
| `validate_ui.bat` | pass; 21 DX12 UI captures |
| `validate_dx12_renderer.bat` | pass; committed captures within tolerance |
| `run_graphics_stress.bat 1` | pass; bounded crash-free DX12 run |
| Replay visual fidelity | pass; one 6,800-frame process and all controls |
| `validate_full.bat` | pass; mandatory CPU umbrella plus 5 engine processes |
| Allocation-policy repository scan | pass; 443 files, 0 allowlist errors |
| `validate_perf.bat` | pass; allocation policy, absolute budgets, DX12 comparison, and physics-bench comparison are clean |
| Repository formatter and `validate_format.bat` | pass; 552 source files and 306 headers are stable |

Replay visual fidelity produced 2,401 ticks, 200 moved wall bricks, 175
toppled bricks, 200 causal nodes, one presented cascade, and matching
saved/loaded visual packets. Every negative and determinism control detected
its intended mutation. The wrapper command window expired while its single
6,800-frame process was still healthy; the completed report was then checked
by the exact post-run commands without launching a second process.

The performance probe stays within absolute CPU/GPU budgets. After owner
approval, the DX12 baseline records 156.96 / 223.75 / 223.75 MiB and the final
rerun records 156.71 / 223.67 / 223.67 MiB. The physics-bench baseline records
156.63 / 219.64 / 219.52 MiB and the final rerun records
156.73 / 219.36 / 219.24 MiB. Both comparisons pass the 5 MiB tolerance.

An additional same-machine control rebuilt and ran the exact baseline commit
with the same hidden-window launch shape. The historical executable now
measures 151.68 / 214.53 / 214.50 MiB instead of its committed
149.41 / 212.06 / 212.06 MiB sample. The final current executable measures
156.58 / 218.71 / 218.68 MiB under that identical launch shape: a true
same-session delta of +4.90 / +4.18 / +4.18 MiB, within the gate's 5 MiB
tolerance. This proves that roughly 2.3-2.5 MiB of the original formal failure
was historical run/environment drift and supported the subsequent
owner-approved refresh.

A preserved Profile executable built at 08:33 on 2026-07-25, immediately
before UR0, provides a direct plan-attribution control without rebuilding or
changing current source. Two adjacent runs of that pre-UR6 executable against
the same current machine, assets, scene, and launch command measured
158.10 / 221.55 / 221.42 MiB and 158.51 / 221.11 / 220.98 MiB. The final UR6
executable measured 156.95 / 219.71 / 219.58 MiB between those runs: UR6 is
1.15-1.56 MiB lower at start and 1.40-1.84 MiB lower after restart/end. The
formal comparison therefore cannot be attributed to this plan. The owner
approved the attributed baseline refresh, and the final formal gate passes.

The historical bump is attributable to commit `f7a6e4a3` ("Stabilize retained
replay prediction rendering"), not to UR6. The last recorded run before that
commit measured 149.44 / 212.55 / 212.43 MiB. That commit added a
27,000-segment × 19-float compact trajectory arena: 1.957 MiB in
`EditorTracer`, the same 1.957 MiB mapped into each of the two DX12 frame upload
resources (3.914 MiB), two 4,096-row CPU range tables (0.375 MiB), and two
frame-local DX12 cache tables (0.125 MiB). Those explicit retained owners total
about 6.37 MiB before D3D resource, mapping, and allocator overhead, matching
the observed 7.2-7.6 MiB process delta. This capacity is the bounded storage
that keeps all solar paths stable and allocation-free across frames; it was
introduced after the old performance baseline and before UR0.

## Touched-Source Comment Audit

The authoritative UR6 semantic inventory was captured immediately before the
owner-requested repository-wide mechanical formatting pass as the union of:

```text
git diff --name-status 54c22bc8 --
git ls-files --others --exclude-standard
```

filtered to `.cpp`, `.h`, `.hpp`, `.inl`, `.hlsl`, substantial `.py`, and
batch/PowerShell tools.

The later formatter pass expands the raw working-tree file count but changes
only whitespace and comment alignment. It does not change the audited comment
text or ownership meaning. Its two substantial Python owners were inspected
after the compact-call rule was added, and the full formatted repository then
passed `validate_format.bat`, `validate_full.bat`, and the bounded DX12 graphics
stress run.

| Inventory class | Count | Result |
|---|---:|---|
| Existing substantial source-bearing paths | 52 | inspected; learning headers and local ownership/invariant comments present |
| Tiny self-explanatory wrapper | 1 (`tools/validate_dependency_graph.bat`) | inspected; full header intentionally unnecessary |
| Deleted source-bearing paths | 2 | deletion diffs inspected; no replacement authority remains |
| Deferred or unchecked | 0 | none |
| Total | 55 | 55 checked |

The final audit corrected stale Tracy ABI wording, the quoted-only include
description, the production fingerprint-test summary, clip-overflow ownership
comments, fixed-capacity page-touching rationale, and three links that would
otherwise have pointed at the deleted TODO plan. No local term requires
human-approved wording.

## Baseline And Artifact Policy

No visual baseline, replay golden, scene, configuration, shader, replay
artifact, or physics CSV was refreshed by UR6. The owner-approved closure
refresh changed only:

- `TestOutput/baselines/dx12_perf.json`
- `TestOutput/baselines/physics_bench_perf.json`

The separately approved replay visual-fidelity goldens landed earlier in
commit `7a43e83b` and are not part of the UR6 performance-baseline refresh.
