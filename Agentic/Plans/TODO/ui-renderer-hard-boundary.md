# UI / Renderer Hard Boundary

Date: 2026-07-25

Owner: UI + Runtime/Render

State: IN PROGRESS

Ledger tasks: 7 (UR0-UR6)

Branch at registration: `main`

Impact area: UI presentation, Runtime render composition, text/UI GPU
submission, render diagnostics projection, build structure, architecture tests

Priority: High. The existing UI/Runtime ownership boundary is correct, but UI
still compiles against concrete renderer facilities. That makes renewed
presentation/backend convergence easy even though Runtime authority no longer
flows into UI.

Implementation mode: use `Agentic/Skills/orchestrator/SKILL.md`. This plan
requires one independent rubber-duck review at whole-plan closure.

## Registration

This plan is registered in `Agentic/Plans/MASTER-PLAN.md` as a seven-task
active architecture plan. Registration grows the active/future denominator
from 0 to 7. No phase is complete at registration.

Required plan-runner commit first line:

```text
UI Renderer Hard Boundary, TASK <DONE> / 7, <OVERALL_PERCENT>% OVERALL COMPLETE — <ACTION SUMMARY>
```

With the 2026-07-25 registration of `replay-subsystem-partition` (6 tasks)
and `downward-domain-bleed-remediation` (6 tasks), the active/future ledger is
19 tasks and the task percentages after UR0-UR6 are 5%, 11%, 16%, 21%, 26%,
32%, and 37%. Recalculate from the authoritative master ledger if the
portfolio changes before implementation.

Amendment 2026-07-25: the owner-accepted architecture review added the
requirements now embedded below — baked-font-metric single sourcing and exact
text parity (UR1), measured capacity high-water before freezing draw-stream
limits (UR1), preview-unavailable fallback semantics defined with the value
contract (UR1), committed draw-stream fingerprint tests gating tab-by-tab
conversion (UR2), and consolidation of the standing shell-snippet dependency
proofs into the UR5 validator (UR5). The task count is unchanged.

## Problem And Measured Evidence

The completed 2026-07-23 UI/Runtime separation established the correct
business-state boundary:

- UI consumes detached `OperatorEditorFrameView` and `InGameUIFrameData`
  values.
- UI emits typed, fixed-capacity command values.
- Runtime owners arbitrate and apply those commands.
- `SkullbonezSource/UI/` has zero Runtime includes.
- `SkullbonezSource/Rendering/` has zero UI or Runtime includes.

That closure did not claim backend independence. A fresh source census on
2026-07-25 found the following remaining coupling:

| Evidence | Current result |
|---|---:|
| Direct `UI -> Rendering` include rows | 25 |
| UI files with at least one Rendering include | 14 |
| Direct `UI -> Runtime` include rows | 0 |
| Direct `Rendering -> UI/Runtime` include rows | 0 |
| Production projects compiling both UI and renderer/runtime-render sources | 1 (`SKULLBONEZ_CORE.vcxproj`) |

The 14 UI files with direct Rendering includes are:

- `SkullbonezSource/UI/UI.cpp`
- `SkullbonezSource/UI/UI.h`
- `SkullbonezSource/UI/UIButton.cpp`
- `SkullbonezSource/UI/UIComboBox.cpp`
- `SkullbonezSource/UI/UIDraw.cpp`
- `SkullbonezSource/UI/UIDrawWidgets.cpp`
- `SkullbonezSource/UI/UIFrameComposition.cpp`
- `SkullbonezSource/UI/UIFrameComposition.h`
- `SkullbonezSource/UI/UILayout.cpp`
- `SkullbonezSource/UI/UISlider.cpp`
- `SkullbonezSource/UI/UITabBar.cpp`
- `SkullbonezSource/UI/UITabProfiler.cpp`
- `SkullbonezSource/UI/UITabProfilerHistogram.cpp`
- `SkullbonezSource/UI/UIWindowChrome.cpp`

The coupling is substantive rather than header noise:

1. `UIRenderContext` carries `Dx12ResourceBuilder`, `Dx12TextureOwner`,
   `Dx12GeometryOwner`, `Dx12Diagnostics`, `RenderGpuTimingOwner`, and
   `TextBatch` borrows into UI.
2. `InGameUI` owns a `std::unique_ptr<Rendering::ShaderDX12>` plus the dynamic
   vertex-buffer state for render-target previews.
3. `UIDrawContext` can bypass the existing `UIDrawList` and issue immediate
   text/quad flushes through concrete renderer owners.
4. `UIFrameComposition` owns `FlushUIDrawList` and preview GPU
   create/reset/draw operations even though those are backend submission and
   resource-lifetime responsibilities.
5. UI layout calls `Rendering::Text2d::MeasureText`; CPU layout therefore
   depends on the same module that owns font textures, shaders, vertex buffers,
   and DX12 flushes.
6. `Rendering/RenderProfilerPresentation.cpp` still contains operator-facing
   profiler layout and text presentation inside the lower renderer layer.
7. UI consumes some renderer-owned diagnostic value types directly instead of
   a UI-owned detached diagnostics view built by Runtime.
8. UI, Rendering, and Runtime/Render are compiled into the same production
   project, so the linker provides no independent package boundary.
9. `AGENTS.md` carries review-time direction proofs, but the mandatory
   validation chain does not currently fail a change merely because a new
   legal-compiling UI/backend include edge appears.

## Goal

Make UI a backend-neutral CPU presentation package with a hard, continuously
checked boundary:

- UI consumes UI-owned detached views and stable lower-level value types.
- UI performs layout, hit testing, widget state, and command authoring.
- UI emits typed operator commands and one bounded, ordered UI draw stream.
- UI owns no GPU handle, shader, vertex buffer, texture owner, render graph,
  GPU timer, backend diagnostics owner, or resource-lifecycle operation.
- Runtime/Render is the only layer allowed to see both UI draw values and
  renderer capabilities.
- Runtime/Render translates the UI draw stream into text, geometry, preview,
  and diagnostics draw submissions.
- Rendering remains below both Runtime and UI and contains no operator-surface
  layout or UI policy.
- Build and mandatory validation fail on a future forbidden dependency edge.

## Target Dependency Shape

```text
Runtime owners
  -> Runtime/UI projection
  -> UI-owned immutable frame views
  -> UI layout / hit testing / fixed-capacity draw stream
  -> Runtime/Render UI submission
  -> Rendering/DX12 resources and commands

UI typed commands
  -> Runtime/App arbitration
  -> Runtime/Interaction and owning-domain command application
```

The permitted compile-time directions at closure are:

| Source package | Permitted target for this plan | Ruling |
|---|---|---|
| `UI` | UI, Core, Maths, and explicitly ratified passive lower value headers | Must not include Runtime or Rendering |
| `Runtime/UI` | UI plus the Runtime owners allowed by the standing package table | Builds detached UI views; owns no GPU work |
| `Runtime/Render` | UI, Rendering, and allowed Runtime packages | The sole UI-to-renderer composition point |
| `Rendering` | Core and Maths under the standing dependency rule | Must not include UI or Runtime |
| `Runtime/DevelopmentTools` | UI values and concrete development renderer capabilities | ImGui remains an explicit development-only Runtime frontend |

Any passive value believed to require a UI-to-Rendering include is presumed
misplaced until UR0 records its domain owner. Move the value to UI, Core, or
Maths, or construct a UI-owned projection in Runtime. Do not hide the edge
behind an opaque integer, `void*`, forwarding header, compatibility alias,
callback, virtual renderer interface, service bag, or generic context.

## Non-Goals

- No visual redesign, widget restyling, font change, or operator workflow
  change.
- No change to scene simulation, replay prediction, physics, gameplay, camera,
  or editor command semantics.
- No Render HAL rewrite and no new runtime-polymorphic renderer abstraction.
- No attempt to make development-only ImGui backend code live in the UI
  package. It remains in `Runtime/DevelopmentTools`.
- No broad UI/Assets cleanup unless a specific asset dependency exists only to
  create or submit GPU resources.
- No replacement of the existing fixed-capacity draw list with a broad frame
  context or parameter bag.
- No retained-trajectory or generic retained-geometry work. That boundary
  belongs to `downward-domain-bleed-remediation` DB1, which reuses this
  plan's bounded value-stream + Runtime/Render translator pattern.
- No baseline, screenshot, replay artifact, physics CSV, shader artifact,
  scene, or configuration refresh. Any visual or behavioral divergence is a
  defect to investigate.
- No recreation of historical frozen-count, spelling-budget, migration-name,
  or `Run`-size ratchets. The new enforcement is a stable directional
  dependency rule, not a debt counter.

## Permanent Invariants

1. `UI` never includes Runtime or Rendering.
2. `Rendering` never includes UI or Runtime.
3. Only `Runtime/Render` translates UI draw values into renderer calls.
4. Runtime/UI projects values; it does not submit GPU commands.
5. UI draw records are values with fixed capacities and explicit overflow
   diagnostics. They retain no owner pointer and allocate no memory in steady
   runtime.
6. UI hit geometry and draw geometry continue to derive from the same layout
   facts.
7. UI ordering, clipping, text measurement, and preview selection are
   deterministic for identical frame values.
8. UI resource creation, resize, device loss, scene reset, and shutdown are
   renderer-owner transactions.
9. Legacy and ImGui consume the same operator frame values and emit the same
   canonical command queues; this plan does not create competing state owners.
10. A visual match is demonstrated from final-source captures. Golden refresh
    is not an acceptance mechanism.

## Edge-Disposition Table

| Current edge or responsibility | Owner at closure | Required disposition |
|---|---|---|
| `UIRenderContext` concrete backend borrows | `Runtime/Render` | Delete the type; do not rename or wrap it |
| `UIDrawContext` renderer pointers | UI command authoring + Runtime/Render submission | Make UI context record only backend-neutral draw commands |
| `UIDrawList::Flush` backend dispatch | `Runtime/Render` | Expose a bounded read-only command view; Runtime iterates it without callback dispatch |
| Text measurement | UI-owned CPU font metrics/layout | Separate measurement data from renderer font atlas/shader/VB ownership |
| Text/quad/image flush | `Runtime/Render::UiTextPass` or a focused child owner | Translate ordered UI commands to existing Rendering primitives |
| Render-target preview shader/VB | focused Runtime/Render UI-preview owner | Move create/reset/draw lifetime out of `InGameUI` |
| Raw preview texture handle in UI frame data | Runtime/Render resolver | UI carries a domain-specific preview target identity and dimensions, never a backend handle |
| Renderer memory/timing/status types in UI | Runtime/UI projection | Copy into UI-owned bounded diagnostic values |
| Operator profiler panels in Rendering | UI layout values + Runtime/Render submission | Move presentation policy out of Rendering; retain measurement collection below |
| ImGui renderer owner | `Runtime/DevelopmentTools` | Retain as a documented development-only exception outside `UI/` |
| Runtime/Render includes UI | `Runtime/Render` | Retain permanently as the intentional composition direction |

## UR0 Source-Derived Census (2026-07-25)

UR0 was regenerated from branch `nightrunner-25th-JUL-26` after the round-4
registration commit reached `main`. CodeGraph supplied the first-pass symbol
and call-path map; `git ls-files`, direct include inspection, and project-file
inspection supplied the authoritative counts:

| Census item | Result |
|---|---:|
| Tracked UI source-bearing files (`.cpp`, `.h`, `.hpp`, `.inl`, `.hlsl`) | 65 |
| Direct UI-to-Rendering include rows | 25 |
| UI files containing those rows | 14 |
| Direct UI-to-Runtime include rows | 0 |
| Direct Rendering-to-UI/Runtime include rows | 0 |
| Production projects compiling UI source | 1 (`SKULLBONEZ_CORE.vcxproj`) |

The registration baseline therefore reconciles exactly: 25 rows in 14 files.
The following table is the row-level closure contract. A deletion condition
means deletion of the physical include and the associated backend authority;
an alias, forwarding include, opaque token, or renamed context does not close
the row.

| # | Current include row | Current responsibility | Closure owner | Deletion condition |
|---:|---|---|---|---|
| 1 | `UIButton.cpp` -> `Rendering/Text.h` | text measurement/layout | UI font metrics | UR1 exact metric parity removes `Text2d` use and the include |
| 2 | `UI.cpp` -> `Rendering/RenderCommandTypes.h` | renderer visibility values | Runtime/UI projection into UI values | UR4 removes every Rendering value/type from UI |
| 3 | `UI.cpp` -> `Rendering/DX12/Dx12Diagnostics.h` | UI-pass attribution/tracing | Runtime/Render UI pass | UR3 performs timing/tracing only during submission |
| 4 | `UI.cpp` -> `Rendering/DX12/Dx12ResourceBuilder.h` | preview resource creation | Runtime/Render preview owner | UR3 moves create/rebuild/release out of UI |
| 5 | `UI.cpp` -> `Rendering/DX12/RenderBackendDX12.h` | immediate draw/preview submission | Runtime/Render UI pass | UR3 is the sole concrete backend caller |
| 6 | `UI.cpp` -> `Rendering/Text.h` | measurement and immediate text/quad flush | UI metrics/recording plus Runtime/Render submission | UR1-UR3 remove measurement, recording, and flush dependencies |
| 7 | `UIComboBox.cpp` -> `Rendering/Text.h` | stale direct include | UI | UR1 preflight deletes the unused include |
| 8 | `UI.h` -> `Rendering/DX12/ShaderDX12.h` | UI-owned preview shader | Runtime/Render preview owner | UR3 deletes the UI shader field and include |
| 9 | `UI.h` -> `Rendering/DX12/Dx12Diagnostics.h` | concrete render context borrow | Runtime/Render submission | UR3 deletes `UIRenderContext`; UR4 projects diagnostic values |
| 10 | `UIDraw.cpp` -> `Rendering/Text.h` | measurement and immediate text/quad work | UI metrics/recording plus Runtime/Render submission | UR1-UR3 leave `UIDraw` backend-neutral |
| 11 | `UIDraw.cpp` -> `Rendering/DX12/RenderBackendDX12.h` | immediate geometry submission | Runtime/Render UI pass | UR2 records all primitives and UR3 submits them |
| 12 | `UIDrawWidgets.cpp` -> `Rendering/Text.h` | widget text measurement | UI font metrics | UR1 exact metric parity removes `Text2d` use |
| 13 | `UIFrameComposition.cpp` -> `Rendering/DX12/RenderBackendDX12.h` | draw-list flush and preview draw | Runtime/Render UI/preview owners | UR3 deletes GPU-oriented composition helpers |
| 14 | `UIFrameComposition.cpp` -> `Rendering/RenderGpuTimingOwner.h` | UI GPU timing scope | Runtime/Render UI pass | UR3 owns timing around command submission |
| 15 | `UIFrameComposition.h` -> `Rendering/RenderCommandTypes.h` | preview raster state and renderer status values | Runtime/Render preview owner plus Runtime/UI projection | UR3 moves raster policy; UR4 removes renderer status types |
| 16 | `UIFrameComposition.h` -> `Rendering/DX12/Dx12Diagnostics.h` | draw attribution | Runtime/Render UI pass | UR3 removes diagnostics from UI declarations |
| 17 | `UIFrameComposition.h` -> `Rendering/DX12/Dx12ResourceBuilder.h` | preview resource lifetime | Runtime/Render preview owner | UR3 removes resource builders from UI declarations |
| 18 | `UIFrameComposition.h` -> `Rendering/DX12/RenderBackendDX12.h` | concrete backend submission | Runtime/Render UI pass | UR3 removes backend parameters from UI declarations |
| 19 | `UIFrameComposition.h` -> `Rendering/Text.h` | font measurement/batches | UI font metrics plus Runtime/Render submission | UR1 supplies metrics; UR3 owns batches |
| 20 | `UILayout.cpp` -> `Rendering/Text.h` | layout text measurement | UI font metrics | UR1 exact metric parity removes `Text2d` use |
| 21 | `UISlider.cpp` -> `Rendering/Text.h` | control text measurement | UI font metrics | UR1 exact metric parity removes `Text2d` use |
| 22 | `UITabBar.cpp` -> `Rendering/Text.h` | tab text measurement | UI font metrics | UR1 exact metric parity removes `Text2d` use |
| 23 | `UITabProfiler.cpp` -> `Rendering/Text.h` | stale direct include | UI | UR1 preflight deletes the unused include |
| 24 | `UITabProfilerHistogram.cpp` -> `Rendering/Text.h` | chart label measurement | UI font metrics | UR1 exact metric parity removes `Text2d` use |
| 25 | `UIWindowChrome.cpp` -> `Rendering/Text.h` | chrome text measurement | UI font metrics | UR1 exact metric parity removes `Text2d` use |

### Symbol and responsibility disposition

| Symbol/responsibility | Current location/use | Exact closure |
|---|---|---|
| `UIRenderContext` | `UI.h`; carries Assets, DX12 resource, diagnostics, timing, and text owners | UR3 deletes it; no backend/context replacement is allowed |
| `UIDrawContext` | `UIDraw.h/.cpp`; retains texture/geometry/text owners and performs immediate draws | UR2 makes it a UI-only recorder; UR3 confirms no renderer pointer, token, or callback remains |
| `UIDrawList::Flush` | callback-oriented dispatch from UI records | UR1 exposes a bounded read-only command view; UR3 deletes UI-side flush and iterates values in Runtime/Render |
| `Text2d::MeasureText` callers | widget, layout, tab, chrome, mini-palette, and frame composition paths | UR1 moves exact measurement to UI-owned immutable metrics generated from the same baked font asset as the renderer atlas |
| Text/quad/image flush | `UIDraw`, `UI.cpp`, and `UIFrameComposition` | UR2 records one ordered stream; UR3 translates it in the Runtime/Render pass |
| UI-owned `ShaderDX12` and preview VB | `InGameUI` fields and preview helpers | UR3 moves all preview create/reset/draw/release state to one focused Runtime/Render owner and deletes the fields |
| Raw preview `textureHandle` | `UIRenderTargetPreviewResource` and Runtime/Render frame assembly | UR1 defines preview identity/fallback values; UR3 resolves identities to frame-local handles only during submission |
| `RenderVisibilityStats` / `RenderMemoryStats` / GPU timing views | renderer values embedded in UI frame data | UR4 creates bounded UI-owned presentation values populated by Runtime/UI |
| `ProfilerOverlayPresenter` / `RenderProfilerPresentation.cpp` | Rendering-owned labels, columns, legend, bars, panels, and text/quad presentation | UR4 moves operator presentation into UI recording; Rendering retains only measurement collection and backend snapshots |
| ImGui renderer backend | `Runtime/DevelopmentTools` | Permanent development-only exception; it never enters `UI/` |

The complete live `Text2d`/text-batch surface spans 15 files once transitive
header users are included: `UI.cpp`, `UI.h`, `UIButton.cpp`, `UIDraw.cpp`,
`UIDraw.h`, `UIDrawWidgets.cpp`, `UIEditorMiniPalette.cpp`,
`UIEditorMiniPaletteDraw.cpp`, `UIFrameComposition.cpp`,
`UIFrameComposition.h`, `UILayout.cpp`, `UISlider.cpp`, `UITabBar.cpp`,
`UITabProfilerHistogram.cpp`, and `UIWindowChrome.cpp`.
`UIComboBox.cpp` and `UITabProfiler.cpp` are confirmed stale direct includes.

### Reverse composition path and project ownership

The intentional reverse dependency begins in `Runtime/App/RunFrame.cpp`, which
constructs the current `UIRenderContext` and invokes
`OperatorEditorFrameComposer::Render`. `Runtime/Render/UiTextPass.cpp` then
injects GPU timing and text owners, projects live render diagnostics and raw
preview handles into `InGameUIFrameData`, and calls `InGameUI::Draw`.
`RuntimeRenderer::ExecuteUiTextThroughRenderGraph` schedules that pass;
`RuntimeRenderer.cpp` currently resets UI-owned preview resources, and
`RenderResourceLifecycle` owns the pass plus the preview snapshot. UR3 keeps
this Runtime/Render composition point but reverses the authority: UI returns
only a bounded draw view, and Runtime/Render owns all translation and resource
lifetime.

`SKULLBONEZ_CORE.vcxproj` is the sole production project compiling all 65 UI
source-bearing files alongside Runtime and Rendering. `SKULLBONEZ_TESTS.vcxproj`
also compiles `OperatorEditorExchange.cpp` and `UIInput.cpp` directly. UR5
creates one standalone production UI target, removes UI source duplication
from the application and test projects, and makes both consumers link that
target.

### Expected final comment-audit inventory

UR6 must reconcile its authoritative touched-file list from `git diff` and
`git ls-files`; the expected source-bearing scope established here is:

- UI draw/layout/presentation: `UI.h/.cpp`, `UIDraw.h/.cpp`,
  `UIDrawList.h/.cpp`, `UIFrameComposition.h/.cpp`, `UIButton.cpp`,
  `UIComboBox.cpp`, `UIDrawWidgets.cpp`, `UIEditorMiniPalette.cpp`,
  `UIEditorMiniPaletteDraw.cpp`, `UILayout.cpp`, `UISlider.cpp`,
  `UITabBar.cpp`, `UITabMemory.cpp`, `UITabProfiler.cpp`,
  `UITabProfilerHistogram.cpp`, `UIWindowChrome.cpp`, and
  `OperatorEditorExchange.h/.cpp`.
- Runtime composition: `Runtime/App/RunFrame.cpp`,
  `Runtime/UI/RuntimeViewModel.h/.cpp`,
  `Runtime/UI/OperatorEditorFrameComposer.h/.cpp`,
  `Runtime/Render/UiTextPass.cpp`, `Runtime/Render/RuntimeRenderPasses.h`,
  `Runtime/Render/RuntimeRenderer.h/.cpp`, and
  `Runtime/Render/RenderResourceLifecycle.h/.cpp`.
- Rendering ownership: `Rendering/Text.h/.cpp`,
  `Rendering/RenderDiagnosticsTypes.h`,
  `Rendering/ProfilerOverlayPresenter.h`, and
  `Rendering/RenderProfilerPresentation.cpp`.
- Tests/tooling likely to become source-bearing scope: focused UI tests,
  `tools/validate_project_filters.py`, and the UR5 dependency validator plus
  its self-test/validation integration scripts.

Project files and plan/report documentation remain part of the implementation
diff but are not source-bearing comment-audit rows. This inventory is expected,
not a cap: any additional touched source-bearing file is added to UR6's
checklist and audited. The census leaves no unresolved owner decision and
preserves the completed UI value/command boundary as the target architecture.

## Ledger

- [x] **UR0 — Ratify the complete source-derived boundary census.**

  Re-run the census from the implementation tip using CodeGraph plus
  `git ls-files` and direct include/symbol inspection. Classify every
  UI-to-Rendering dependency into layout/measurement, draw recording, draw
  submission, resource lifetime, preview texture presentation, diagnostics,
  profiler presentation, or development-only backend work. Inventory every
  `UIRenderContext`, `UIDrawContext`, `UIDrawList`, `Text2d`, raw renderer
  handle, shader, vertex-buffer, render-target-preview, and render diagnostics
  use in UI. Inventory the reverse Runtime/Render call path and all
  operator-facing presentation still implemented in Rendering.

  Update this plan with the exact file/symbol disposition table before source
  movement begins. Record all expected touched source-bearing files for the
  final comment audit. Confirm the existing UI command/value boundary remains
  the target and that no owner decision is missing.

  Acceptance:

  - Every current edge has one named destination owner and deletion condition.
  - The census reconciles to the 25-row/14-file registration baseline or
    explains source changes since registration.
  - No source behavior changes in UR0.

  Evidence: the 2026-07-25 census above records all 25 physical rows, their
  symbol responsibilities, destination owners, and deletion conditions; maps
  the reverse Runtime/Render path and shared-project ownership; and establishes
  the expected final comment-audit inventory. The registration counts are
  unchanged and this slice changes documentation only.

- [ ] **UR1 — Complete the backend-neutral UI draw and text-layout values.**

  Evolve the existing fixed-capacity `UIDrawList`; do not introduce a parallel
  draw-stream owner. Define public, read-only, bounded draw-command values for
  every Legacy UI primitive actually required: rectangles, rounded
  rectangles, triangles, text, clipping/scissor transitions, and
  render-target preview images. Preserve exact command order. Add a
  domain-specific preview target identity rather than a raw texture handle.

  Split CPU font metrics and `MeasureText` behavior from GPU font resources.
  UI must be able to compute identical layout using UI-owned metric values
  without including `Rendering/Text.h`. The **baked font asset is the single
  source of truth**: the same baked metric data feeds both the UI-owned CPU
  metric table and the renderer-owned atlas, so the two authorities cannot
  drift. Runtime populates the immutable UI metric values from that baked
  data during cold initialization, but UI retains no Runtime or renderer
  owner. Text-width parity against the pre-migration implementation is
  **exact equality over a committed corpus** (every live glyph, all in-use
  pixel sizes, representative mixed strings, DPI/scale rounding cases), not a
  tolerance comparison — hit geometry derives from the same numbers, so a
  one-pixel measurement drift is a behavior change.

  Before freezing draw-stream capacities, **measure the real command/text/clip
  high-water** of the heaviest live surfaces (profiler table and histogram
  tabs, memory tab, full editor with scene browser open, replay overlay) and
  record the measured values plus chosen headroom in this plan. The current
  8,192-command/64KB-text limits predate clip and preview-image commands and
  are not evidence.

  Define **preview-unavailable fallback semantics as part of the value
  contract**: when a preview identity cannot be resolved at submission time
  (scene reload, device reset, retired target), the recorded command must
  specify the exact presented fallback (placeholder fill plus label). The
  fallback is a UR1 value-contract decision, not a UR3 submission-time
  improvisation.

  Acceptance:

  - The draw stream is fixed-capacity, trivially inspectable, deterministic,
    and has explicit command/text/clip overflow reporting.
  - Focused tests cover command ordering, all primitive variants, nested clip
    behavior, preview identity including the unavailable fallback, text
    storage, and overflow.
  - Text measurement parity is proven by exact equality over the committed
    corpus, sourced from the same baked font data the atlas consumes.
  - Capacity values are justified by recorded high-water measurements from
    the heaviest surfaces plus stated headroom.
  - No dynamic allocation or callback dispatch is added to recording or
    iteration.

- [ ] **UR2 — Convert all Legacy UI construction to record-only operation.**

  Remove immediate renderer calls from UI widgets, tabs, window chrome,
  overlays, cached draw lists, and minimized/full editor paths. Make
  `UIDrawContext` a UI-only draw recorder. Every UI path records into the one
  ordered list and returns typed commands separately; no UI function flushes,
  binds, uploads, creates, destroys, or times GPU work.

  **Gate the conversion with committed draw-stream fingerprints.** Before the
  first bypass-site conversion, record a stable hash of the complete command
  stream for a committed set of representative frame states (full editor,
  minimized editor, each tab, replay overlay, text-only mode) as CPU-test
  expectations. Then convert immediate-draw bypass sites **surface-by-surface
  (tab-by-tab)**, keeping the fingerprint suite green between conversions. A
  fingerprint change is legitimate only when the conversion intentionally
  adds commands that were previously immediate; the test update must name the
  commands added and their source surface. This catches reordering and
  dropped draws in unit tests instead of at the DX12 screenshot gate.

  Preserve hit testing, active/hot control identity, clipping, z/order, cached
  list reuse, and deterministic command fingerprints. Capacity exhaustion must
  report through the existing probe/diagnostic lane and must never allocate or
  silently reorder.

  Acceptance:

  - The committed fingerprint suite exists before the first conversion and
    passes from the final UR2 source; every intentional fingerprint update
    documents its cause.

  - UI rendering tests can construct complete representative frames without a
    renderer device or DX12 owner.
  - Legacy UI input/output behavior and command arbitration tests remain
    byte/value equivalent.
  - `UIDrawContext` contains no Rendering type, pointer, reference, callback,
    or opaque backend token.

- [ ] **UR3 — Move UI GPU submission and resource lifetime into Runtime/Render.**

  Make `UiTextPass` or focused child owners consume the bounded UI command
  view and translate it to existing Rendering text/geometry operations.
  Runtime/Render owns font atlas/shader/VB setup, UI text/quad batches,
  preview shader/VB setup, render-target handle resolution, draw attribution,
  resize handling, device-loss/reset handling, and shutdown release.

  Delete `UIRenderContext`, `InGameUI::ResetResources`, UI-owned
  `ShaderDX12`/dynamic-VB fields, and GPU-oriented
  `UIFrameComposition` helpers. Do not replace them with `UIBackend`,
  `UIRendererBridge`, a virtual interface, a callback pack, or a generic
  resource context. The concrete Runtime/Render pass already owns the required
  composition point.

  Acceptance:

  - Every UI GPU resource has one Runtime/Render owner with explicit cold
    create/rebuild/release phases.
  - Preview panels resolve a UI preview identity to a frame-local renderer
    texture only during submission.
  - UI draw-call diagnostics and GPU timing remain attributable to the same
    pass names.
  - Device reset, resize, scene reload, and shutdown probes show no stale
    handle, double release, leak, or missing preview.

- [ ] **UR4 — Remove operator-presentation policy from Rendering.**

  Move operator-facing profiler layout, labels, charts, memory presentation,
  and UI-specific render diagnostic shaping out of `SkullbonezSource/Rendering`.
  Rendering retains measurement collection and backend-owned value snapshots.
  Runtime/UI converts those snapshots into UI-owned bounded frame values; UI
  records the presentation; Runtime/Render submits it.

  Reconcile `RenderProfilerPresentation.cpp`,
  `ProfilerOverlayPresenter`, `RenderMemoryStats`, GPU timing views, render
  target metadata, and any other Rendering-owned type consumed by UI. Keep
  true backend diagnostics and renderer validation below the boundary.

  Acceptance:

  - Rendering contains no operator tab/window/panel/layout policy.
  - UI includes no Rendering header and names no Rendering namespace/type.
  - Rendering still includes no UI or Runtime header.
  - Profiler, memory, render-target preview, and diagnostics surfaces retain
    their prior information, ordering, units, and visual layout.

- [ ] **UR5 — Add a build boundary and mandatory dependency regression gate.**

  Create a dedicated UI production target that compiles the UI package without
  Runtime/Render or DX12 dependencies. Move each UI source file to exactly one
  production project and preserve filters/casing. The application target
  depends on UI; Runtime/Render remains the composition layer that sees both
  UI and Rendering.

  Add a small source-graph dependency validator with self-tests and wire it
  into the mandatory fast/CPU/full validation chain and hosted CI. It must
  parse physical include targets and enforce stable directional rules:

  - UI cannot include Runtime or Rendering.
  - Rendering cannot include Runtime or UI.
  - Core and Gameplay standing dependency rules remain enforced.
  - project membership cannot compile UI source in the renderer/application
    target as a back door.

  This validator must encode a directional package graph, not historical
  counts, migration spellings, line budgets, or a grandfathered include
  allowlist. A new forbidden edge fails immediately; a legitimate architecture
  change requires an explicit owner-approved graph and `AGENTS.md` update in
  the same change.

  **Consolidate the standing shell-snippet proofs into this validator.** The
  repository currently enforces its dependency rules through more than twenty
  hand-maintained `rg` commands in `AGENTS.md` (top-level direction rules,
  the 18-row Runtime package matrix, the Replay boundary rule) that run only
  when a reviewer remembers them. UR5 encodes those same rules as validator
  data with positive/negative fixtures, makes the validator the authoritative
  enforcement through the mandatory validation chain, and updates `AGENTS.md`
  in the same change so the prose names the validator as authority (retained
  `rg` blocks become human-readable mirrors of validator rules, or are
  deleted where redundant). The rule set must be **data-extensible**: the
  registered follow-up plans (`replay-subsystem-partition` RS4,
  `downward-domain-bleed-remediation` DB4) add their package and
  symbol-deletion rules as new rule rows plus fixtures, with no validator
  code rewrite.

  Acceptance:

  - The UI target builds and its CPU tests link without DX12 libraries or
    Runtime objects.
  - Positive and negative fixtures prove every new graph rule.
  - Every standing `AGENTS.md` dependency proof (top-level direction rules,
    Runtime package matrix, Replay boundary) is encoded as validator rules
    with fixtures, and `AGENTS.md` names the validator as the enforcement
    authority in the same change.
  - Adding a new directional rule requires only rule data plus fixtures, not
    validator code changes.
  - `validate_fast`, `validate_all_cpu_tests`, and `validate_full` invoke the
    same dependency validator once through their established call chain.
  - `validate_project_filters` proves exact single-project ownership for UI
    source and headers.

- [ ] **UR6 — Close behavior, performance, ownership, and documentation.**

  Re-run the complete census and every standing direction proof. Audit every
  touched source-bearing file against the comment-style guide. Run one
  independent rubber-duck review across UI, Runtime/UI, Runtime/Render,
  Rendering/Text, profiler presentation, build targets, tests, and the new
  dependency gate. Any credible owner leak, backend edge, context/bridge
  disguise, allocation, or visual regression reopens its owning phase.

  Capture representative Legacy UI states from the final binary: full editor,
  minimized editor, render tab, render-target preview, profiler table/timeline,
  memory overlay, replay overlay, and text-only mode. Compare against committed
  behavior without refreshing visual baselines.

  Acceptance:

  - All permanent invariants and static proofs below pass.
  - One independent review has no unresolved finding.
  - All mapped gates pass from the final source.
  - Closure evidence is written under `Agentic/Reports/<date>/`, this plan is
    deleted under inventory rule 4, `MASTER-PLAN.md` returns the denominator
    from 7 to 0, and `Agentic/SessionState.md` records the handoff.

## Dependencies And Decisions

- UR0 is first and may refine file-level dispositions, but may not weaken the
  target zero-edge rules.
- UR1 precedes UR2 so UI can record every required primitive before immediate
  draws are removed.
- UR2 precedes UR3 so the Runtime submitter consumes one authoritative stream
  rather than supporting old and new UI render paths in parallel.
- UR3 precedes UR4 because profiler/diagnostic presentation must target the
  final draw stream.
- UR1-UR4 must close before UR5 freezes the build/dependency graph.
- UR6 is the sole whole-plan independent review.
- The registered follow-up plans depend on UR5's validator:
  `replay-subsystem-partition` RS4 and `downward-domain-bleed-remediation`
  DB4 register their directional and symbol-deletion rules in it. UR5 must
  therefore land the data-extensible rule format, not a UI-specific
  hardcoding.
- No owner decision is required to start. If implementation discovers a true
  need for UI to include Rendering, stop and request an owner ruling rather
  than adding an exception.

## Static Closure Proofs

The final dependency validator is authoritative. These human-readable review
proofs must also return no rows:

```powershell
rg -n '^#include[[:space:]]+.*(Runtime|Rendering)/' SkullbonezSource/UI
rg -n '^#include[[:space:]]+.*(Runtime|UI)/' SkullbonezSource/Rendering
rg -n '^#include[[:space:]]+.*(Assets|Gameplay|Physics|Rendering|Scene|World|Runtime|UI)/' SkullbonezSource/Core
rg -n '^#include[[:space:]]+.*(Assets|Scene|World|Runtime|UI)/' SkullbonezSource/Gameplay
rg -n '^#include[[:space:]]+.*Runtime/Replay/' SkullbonezSource/Physics SkullbonezSource/Rendering SkullbonezSource/Scene SkullbonezSource/World SkullbonezSource/Core
```

The UR0 census must define symbol-level deletion checks for at least:

- `UIRenderContext`
- UI-owned `ShaderDX12` and dynamic preview vertex buffers
- `InGameUI::ResetResources`
- backend-oriented `FlushUIDrawList`
- renderer pointers inside `UIDrawContext`
- raw render-target texture handles in UI frame values
- operator-facing layout implementation under `SkullbonezSource/Rendering`

Deletion checks may not be satisfied by aliases or renamed compatibility
shapes.

## Validation Map

Repository validation is deferred until implementation begins. Documentation
registration itself requires no repository validation.

| Phase | Iteration evidence | Pre-commit/closure gates |
|---|---|---|
| UR0 | Source census and plan reconciliation | Documentation-only; no repository validation |
| UR1 | Focused draw-list/font-metric unit tests | `tools\validate_tests.bat`; allocation repository scan if runtime storage changes |
| UR2 | Focused UI layout/input/command tests | `tools\validate_ui.bat`, `tools\validate_tests.bat` |
| UR3 | Focused resource lifecycle and preview probes | `tools\validate_dx12_renderer.bat`, `tools\run_graphics_stress.bat 1`, `tools\validate_ui.bat` |
| UR4 | Focused profiler/memory/preview presentation tests | `tools\validate_dx12_renderer.bat`, `tools\run_graphics_stress.bat 1`, `tools\validate_ui.bat` |
| UR5 | Dependency-validator self-test, negative fixtures, UI-only build | `tools\validate_fast.bat`, `tools\validate_all_cpu_tests.bat`; run the changed validator directly |
| UR6 | Final static proofs, comment audit, representative captures, independent review | `tools\validate_ui.bat`, `tools\validate_dx12_renderer.bat`, `tools\run_graphics_stress.bat 1`, `tools\validate_perf.bat`, `tools\validate_full.bat` |

Mapped gates are cumulative. Any change to Runtime/Render or DX12 presentation
also retains the standing replay-visual-fidelity gate when it changes replay
overlay presentation/submission. Invoke that expensive gate once for the
owning phase from final source; do not refresh its artifacts without explicit
owner authorization.

## Closure Evidence Requirements

The closure report must contain:

- before/after include and project-membership census;
- a disposition for every UR0 edge;
- final dependency graph and project graph;
- UI draw command capacities, high-water/overflow evidence, and allocation
  result;
- resource create/reset/release ownership table;
- representative visual captures and comparison result;
- UI-only build/link proof;
- dependency-validator positive/negative fixture proof;
- test and mapped-gate results with process counts where applicable;
- touched-source comment-audit inventory, checked count, deferred count, and
  any unchecked file;
- independent review verdict and remediation, without looping a second review;
- confirmation that no baseline, golden, scene, config, shader, replay
  artifact, or physics CSV was refreshed.
