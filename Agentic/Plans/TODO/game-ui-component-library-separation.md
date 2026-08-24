# Game UI Component Library Separation Plan

Date: 2026-08-22
Status: Active by owner direction. 4/7 phases complete; phase-local Runtime
Boundary Separation prerequisites apply.
Impact area: `SkullbonezSource/UI/`, game-facing composition under
`SkullbonezSource/Runtime/`, `SKULLBONEZ_UI.vcxproj`, focused UI tests,
dependency/build enforcement, and documentation
Owner: UI foundation plus the concrete Runtime product owners that compose it
Priority: Fourth for scarce-slot allocation and fan-in. `RAGDOLL_PHYSICS` is
not a predecessor; UI phases may run beside Physics when their phase-local
Runtime prerequisites and leases are satisfied.
Commit name: `GAME_UI_COMPONENTS`

## Owner Direction

On 2026-08-23 the owner directed the hardened parallel orchestrator to run UI
and Physics plans concurrently by subsystem. This replaces the registration-
time whole-plan RBS7 stop with phase-local prerequisites: UI0-UI2 have no RBS
or Physics predecessor; UI3, UI4, UI5, and UI6 consume RBS4, RBS5, RBS6, and
RBS7 respectively. Internal UI0-UI6 order remains binding.

Separate the reusable, backend-neutral game UI component foundation from the
Skullbonez-specific use of those components. Retain `SKULLBONEZ_UI` as the
dedicated static-library boundary and narrow it to geometry, layout, style,
font metrics, bounded draw values, generic component presentation, and generic
interaction values. Product tabs, operator shells, domain view models, and
typed domain commands remain above that foundation with their Runtime owners.

This plan does not create `SKULLBONEZ_GAME_UI`, a second UI library, or another
production project. The existing UI project is the component-library project;
game-specific composition remains in `SKULLBONEZ_CORE` under the package owner
ratified by `RUNTIME_BOUNDARIES`. A future product-UI project requires a new
explicit owner decision backed by multiple-consumer, independent-link, and
build-cost evidence.

Development-only UI and profiling integrations are outside this plan. Do not
redesign, replace, migrate, or use them as acceptance evidence. Shared operator
contracts placed by RBS4 are consumed as-is; this plan owns only the GameUI
component/product boundary.

---

## Coordination With Runtime Boundary Separation

`Agentic/Plans/TODO/runtime-boundary-separation.md` is implemented by another
plan and must not be edited by this one. Dependencies are phase-local: UI0-UI2
own the UI-foundation inventory and component contracts and may run while
Physics and RBS continue; UI3 consumes the frozen RBS4 projection/submission
seam, UI4 consumes the RBS5 package-owner result, UI5 consumes the RBS6 project
topology, and UI6 consumes RBS7 terminal evidence. A phase never edits an RBS
owner before its named output exists.

`RAGDOLL_PHYSICS` has no direct dependency edge into this plan. Run ready UI
and Physics phases concurrently in separate worktrees. Physics normally leases
`Physics`, adding `path-owner:Runtime/Tools` only while changing instrumentation;
UI normally leases `UI Library` plus the exact Runtime product packages its
phase touches. Linking both libraries or displaying a detached Physics value is
not a production lease collision. Expand and serialize before editing the
shared UI/diagnostics Physics contract or common project/test manifests, and
serialize shared GPU, baseline, performance, and terminal gates after fan-in.

The following work is deliberately not duplicated:

| Existing RBS ownership | Potential duplication | This plan's disposition |
|---|---|---|
| RBS0 package/project DAG and build-cost census | Re-inventorying or independently ratifying Runtime/package topology | UI0 inventories the component foundation without ratifying Runtime topology. Product-placement rows record the latest RBS output and remain explicitly pending until their named RBS phase closes. |
| RBS1 dependency-cycle enforcement | Building another include/SCC checker | Extend existing rule data or project ownership fixtures only where the narrowed UI boundary needs proof. Do not add a second graph tool. |
| RBS3 immutable frame-metrics owner | Moving profiler/frame timing ownership while splitting UI views | Treat the RBS3 snapshot as a product input. Do not move, recalculate, or republish timing state. |
| RBS4 operator projection, command application, and GPU submission separation | Reworking `Run::RenderOperatorUiPhase`, the seven-slice submission surface, process commands, or renderer ownership | RBS4 is a prerequisite for UI3 and later product-surface adoption. UI0-UI2 do not edit that seam. UI3 consumes its detached views, command path, Runtime/UI owner, and Runtime/Render submission seam. |
| RBS5 reverse-App and cycle removal | Moving UI files in a way that recreates App edges | RBS5 is a prerequisite for UI4 product moves. Preserve its closed package DAG; any proposed move that needs a reverse edge stops for owner review. |
| RBS6 minimal VC topology | Creating another UI/product library or independently changing project policy | RBS6 is a prerequisite for UI5 project edits. Retain the one existing `SKULLBONEZ_UI` library, move game-specific sources to the approved `SKULLBONEZ_CORE` package owner, and create no additional production project. |
| RBS7 terminal architecture validation | Claiming those same Runtime ownership changes as GameUI closure | RBS7 is a prerequisite for UI6 only. Record it as inherited baseline evidence, re-run gates affected by later GameUI changes, then perform this plan's terminal closure. |

If RBS4 already moved or decomposed a type when UI0 inventories it, UI0 marks
that row `satisfied by RBS` and tests the resulting seam. If RBS4 is still
active, UI0 marks the row `pending RBS4`; UI3 refreshes it before the first
product-surface edit. Neither phase recreates, renames, or moves the type a
second time merely to match this registration document.

---

## Dated Baseline Evidence

Evidence measured on 2026-08-22 at `a00654873` on `main`. Concurrent Physics
and Replay working-tree changes were user-owned and are outside this plan.
CodeGraph was current during the initial UI analysis, then reported unrelated
pending source changes during registration. UI0 refreshes the foundation
baseline at its own frozen commit; UI3-UI6 refresh affected product/package,
project, and terminal evidence after their named RBS prerequisites rather than
treating these observations as closure counts.

- `SKULLBONEZ_UI.vcxproj` is already a production static library, and
  `SKULLBONEZ_CORE.vcxproj` references it.
- The dependency/project rule requires every tracked `SkullbonezSource/UI`
  source-bearing file to belong to `SKULLBONEZ_UI`, not Core or Tests.
- `UiBoundaryUnitTests` links the real UI library without Runtime, Rendering,
  or a graphics backend and renders all eleven current GameUI tabs from
  detached values.
- `SkullbonezSource/UI/` currently contains 70 `.cpp`/`.h` files and 21,688
  lines. A filename/responsibility classification found roughly 32 component
  files / 3,210 lines and 38 product-or-shell files / 18,478 lines. These are
  observations for planning, never budgets or completion thresholds.
- The component set already includes draw values/context, style, font metrics,
  layout, button, checkbox, combo, icon button, slider, scrollbar, tab bar,
  backdrop, and caching primitives.
- Product policy remains in the same library: `InGameUI`, its broad
  `InGameUIFrameData`, `UICommands`, `UIWindowInteractionOwner`, editor mini
  palette, scene navigation, render authoring, profiler/memory presentation,
  and the `UITab*` family.
- `UIButton::Draw` derives hover from mouse coordinates and cannot express
  disabled, focused, active, or checked presentation, while
  `RuntimeUiControl` already carries those explicit states. That mismatch is a
  concrete reason Runtime overlays cannot reuse the current component directly.
- Excluding development-only sources, Runtime contains 168 direct UI primitive
  authoring calls in the current scan: 146 in `ReplayOverlayRenderer.cpp` and
  22 in `UiTextPass.cpp`. Replay also owns disposable `RuntimeUiSurface`
  control tables and shared geometry, but manually repeats button, toggle,
  panel, scrollbar, tab, and text presentation.

UI0 refreshes every measurement. Historical values explain why the plan exists;
they do not establish a ratchet, count allowance, deletion quota, or LOC goal.

## Problem Statement

The renderer boundary is sound, but the package called UI currently means two
different things:

1. A reusable CPU-side component and draw-recording foundation.
2. The concrete Skullbonez operator/game interface built with that foundation.

Because those responsibilities share one project and public surface, a new
domain control tends either to enlarge `InGameUIFrameData`/`UICommands` or to
bypass the component layer and author styled primitives directly in Runtime.
The two paths already use different interaction state shapes. Project
separation alone has therefore not produced component reuse or product change
locality.

The repair is not a generic retained widget tree, a virtual backend, or a broad
UI context. It is a small value-oriented component vocabulary that both the
main GameUI shell and owner-local Runtime overlays can consume while product
semantics remain above it.

## Goals

- Make `SKULLBONEZ_UI` a clearly reusable backend-neutral component foundation.
- Keep one source of truth for component geometry, visual state, style, text
  measurement, hit testing, and bounded draw recording.
- Let owner-local product presenters use buttons, toggles, sliders, tabs,
  scrollbars, panels, labels, and rows without repeating raw styling code.
- Keep scene, physics, replay, planning, editor, profiler, render-target, and
  other product vocabulary out of the component foundation.
- Replace the broad product frame/command surface with focused detached views
  and typed commands at honest product owners, reusing RBS4 results.
- Preserve the existing Runtime/Render draw submission boundary and all
  fixed-capacity/no-steady-growth guarantees.
- Retain exact draw/hit geometry agreement and deterministic command streams.
- Make source and project ownership mechanically enforce the final boundary.

## Non-Goals

- No redesign of the GameUI's appearance, navigation model, information
  architecture, or product workflows.
- No work on development-only UI, external profilers, or their backends.
- No renderer abstraction, virtual widget backend, callback pack, service bag,
  retained DOM/tree, reflection system, data-binding framework, or scripting
  layer.
- No keyboard/controller navigation, localization, accessibility, animation,
  theme editor, or text-input feature unless a pre-existing behavior must be
  preserved during migration.
- No new production project. In particular, do not create
  `SKULLBONEZ_GAME_UI`, split one project per panel, or move Runtime/domain
  implementations into the UI library.
- No duplication of RBS package-DAG, App-closure, frame-metrics, operator-phase,
  command-application, or GPU-submission work.
- No LOC target, generic-widget quota, raw-draw count ceiling, or spelling
  ratchet.
- No golden refresh merely to accept a visual or interaction change.

---

## Target Architecture Contract

```text
Concrete Runtime/domain owners
    -> detached product views and typed product commands
    -> owner-local presenters or Runtime/UI GameUI composition
    -> SKULLBONEZ_UI component values/functions
    -> bounded UIDrawList
    -> RBS-owned Runtime/Render submission
    -> Rendering backend

Pointer/input snapshot
    -> product surface control table and routing owner
    -> generic component hit/visual state
    -> typed product command
    -> concrete Runtime/domain owner applies it
```

### Component-foundation ownership

`SKULLBONEZ_UI` may own:

- screen-space geometry and clipping values;
- immutable palette, typography, spacing, radii, and component style values;
- CPU font metrics;
- fixed-capacity draw records, copied draw text, fingerprints, and overflow
  diagnostics;
- generic layout helpers;
- generic component descriptions and drawing for button, toggle/checkbox,
  slider, combo/list popup, tab, scrollbar, panel/window chrome, icon button,
  label/value row, and repeated table/list presentation proven by UI0;
- component-neutral visual state such as visible, enabled, hovered, focused,
  active, selected, and checked; and
- stateless hit/value helpers whose geometry is the same value used to draw.

It may not own:

- scene, physics, replay, prediction, planning, editor, profiler, renderer,
  render-target, capture, automation, or application process semantics;
- product tab selection, product command enums, domain queues, domain view
  models, scene navigation state, or operator workflow policy;
- mutable Runtime owners, callbacks, backend handles, texture identities,
  renderer resources, application failure policy, or process commands; or
- a broad control/context object from which a caller can recover unrelated
  product authority.

### Product-composition ownership

- The main GameUI shell and common operator presentation live under the
  RBS-ratified Runtime/UI product owner in `SKULLBONEZ_CORE`.
- Domain overlays remain beside their real owners when that preserves the
  package DAG; for example, Planning/Replay presentation does not move into a
  miscellaneous central UI domain merely to reuse a button.
- Product presenters consume foundation components through values and emit
  typed commands. They may use domain vocabulary because they are above the
  foundation.
- Runtime/Render remains the sole UI draw-value-to-backend translator.

### Component API direction

The preferred seam is stateless composition from explicit values, for example a
button description containing bounds, label, and component-neutral visual
state. Drawing appends to an explicit `UIDrawContext`; hit testing reads the
same bounds. Existing retained widget wrappers may temporarily delegate to the
new functions, but a wrapper survives only when it owns a real persistent
component invariant.

Do not pass mouse coordinates into a draw operation merely so it can rediscover
state already resolved by the product surface. Do not move `RuntimeUiActionId`
or domain action interpretation into UI. If `RuntimeUiSurface` can share a
component-neutral geometry/visual value, extract only that value and leave
control IDs, z-order, pointer blocking, and product action routing with their
current owner.

---

## Initial Source-Disposition Worklist

UI0 replaces this seed with an exact tracked-file inventory at its frozen base.
Foundation rulings close in UI0; product-placement rows whose RBS output is not
yet frozen remain explicitly `pending RBS<n>` and are refreshed by UI3-UI6. A
file is ruled by responsibility, not name or desired line count.

| Current family | Expected closure owner | Initial disposition |
|---|---|---|
| `UIDraw`, `UIDrawList`, `UIFontMetrics`, `UIStyle`, component-neutral layout/cache/input values | `SKULLBONEZ_UI` | Retain and narrow public contracts where needed. |
| `UIButton`, `UICheckBox`, `UIComboBox`, `UIIconButton`, `UISlider`, `UIScrollBar`, `UITabBar`, generic backdrop/chrome | `SKULLBONEZ_UI` | Convert shared rendering/hit logic to explicit component values; retained wrappers delegate or are deleted by ownership evidence. |
| `UI.h/.cpp`, `UIWindowInteractionOwner`, `UIFrameComposition`, editor mini palette | RBS-ratified Runtime/UI GameUI owner | Move product shell/composition above the foundation without restoring Runtime/Render authority. |
| `UICommands`, `OperatorEditorExchange`, `UIRenderAuthoringCatalog`, `UIRenderDiagnostics`, `UISceneNavigationModel` | RBS4 output or concrete Runtime product owner | Consume RBS placement; remove from foundation when product-specific. Do not move shared command application or metrics ownership a second time. |
| `UITab*`, profiler/memory overlay presenters | Runtime/UI or concrete domain presenter owner | Move as product presentation; split focused view inputs instead of retaining one broad `InGameUIFrameData`. |
| Runtime Planning/Replay overlays and `RuntimeUiSurface` users | Existing domain owners | Retain placement; adopt foundation components while keeping domain state/action routing local. |
| `UiTextPass` product badges/panels | RBS-ratified product presenter plus Runtime/Render submitter | Move presentation policy above submission only where RBS has not already done so; do not reopen GPU ownership. |

## Exception Table

No exception is approved at registration. A temporary exception must name the
exact edge/type, concrete owner, reason, preserved behavior, deletion condition,
and deleting phase. No exception may survive UI6.

| Edge or product type retained in foundation | Owner | Reason | Behavioral constraint | Deletion condition | Phase |
|---|---|---|---|---|---|
| (none) | - | - | - | - | - |

---

## Phase UI0 - Ratify The Component Foundation And Staged Product Map

**Goal:** Classify every UI file and consumer, freeze the component-foundation
contract used by UI1-UI2, and stage product-placement rows against their exact
RBS prerequisites without waiting for unrelated Physics work.

- [x] Record branch, commit, dirty files, CodeGraph status, available RBS
      handoffs, pending RBS outputs, and inherited validation/baseline findings.
- [x] Inventory every tracked `SkullbonezSource/UI` source-bearing file with
      `git ls-files` and classify it as foundation, product composition,
      product value/command, backend submission, or exact exception.
- [x] Inventory every UI component use and direct Runtime draw-authoring site,
      grouped by repeated presentation responsibility rather than raw count.
- [x] Compare retained widget state against `RuntimeUiSurface` and other
      disposable surface values; rule each state field to one owner.
- [x] Inventory public include dependencies, project membership, tests,
      CMake/source manifests, allocation behavior, draw capacities, and current
      visual fingerprints.
- [x] Mark every registration worklist row already completed by RBS as
      `satisfied by RBS` with its resulting source path and test evidence; mark
      unresolved product rows `pending RBS4`, `pending RBS5`, or `pending RBS6`.
- [x] Ratify the exact foundation target paths and UI1-UI2 order. Record the
      product move owner and prerequisite without guessing its post-RBS path.
- [x] Add focused pre-change witnesses for component states and repeated Runtime
      controls that lack behavioral coverage.

**Acceptance:** Every tracked UI file and repeated product presentation path has
one owner/disposition; the foundation contract and UI1-UI2 order are final; each
unfrozen product-placement row names one exact RBS prerequisite and later UI
phase; no duplicate work, reverse edge, project addition, or broad context is
introduced.

### UI0 Ratification Evidence - 2026-08-23

#### Frozen source and complete disposition

UI0 was inventoried from clean frozen base
`d6578d1511e7d4560b1c09ee6df28ab0404a023d` on PR 159 branch
`nightrunner-22nd-AUG-26`. CodeGraph was current at 1,183 files, 36,839 nodes,
and 111,064 edges. `git ls-files SkullbonezSource/UI` reconciled 70/70 tracked
source-bearing files: 34 implementations, 36 headers, 21,690 lines, no missing
or duplicate classification.

| Disposition | Exact current files under `SkullbonezSource/UI/` |
|---|---|
| Foundation (30) | `UIBackdropBlur.cpp/.h`, `UIButton.cpp/.h`, `UICache.cpp/.h`, `UICheckBox.cpp/.h`, `UIComboBox.cpp/.h`, `UIDraw.cpp/.h`, `UIDrawList.cpp/.h`, `UIDrawWidgets.cpp/.h`, `UIFontMetrics.cpp/.h`, `UIIconButton.cpp/.h`, `UIInput.cpp/.h`, `UIScrollBar.cpp/.h`, `UISlider.cpp/.h`, `UIStyle.cpp/.h`, `UITabBar.cpp/.h` |
| Product value/command (6) | `OperatorEditorExchange.cpp/.h`, `UICommands.h`, `UIRenderAuthoringCatalog.h`, `UIRenderDiagnostics.h`, `UISceneNavigationModel.h` |
| Product composition (29) | `UI.cpp/.h`, `UIEditorMiniPalette.cpp`, `UIEditorMiniPaletteDraw.cpp`, `UIFrameComposition.cpp/.h`, `UIProfilerOverlayPresenter.cpp/.h`, `UITabCinematic.cpp/.h`, `UITabControls.cpp/.h`, `UITabEditor.cpp/.h`, `UITabMemory.cpp/.h`, `UITabOptions.cpp/.h`, `UITabPhysics.cpp/.h`, `UITabProfiler.cpp/.h`, `UITabProfilerHistogram.cpp`, `UITabScene.cpp/.h`, `UITabSky.cpp/.h`, `UIWindowInteractionOwner.cpp/.h` |
| Exact mixed exception (5) | `UILayout.cpp/.h`, `UIState.h`, `UIWindowChrome.cpp/.h` |
| Backend submission | None; `Runtime/Render/UiDrawSubmission.cpp` remains the sole translator. |

The mixed files are responsibility-bounded, not blanket exceptions.
`UILayout` retains generic screen geometry/interpolation/conversion while its
scene, physics, tornado, pipeline, and footer policy moves in UI4 after RBS5.
`UIState` retains generic placement, animation, and drag/resize anchors while
`blocksCameraMouse` moves as Runtime interaction policy in UI4 after RBS5.
`UIWindowChrome` retains rectangles, clamping, animation, title geometry, and
generic chrome drawing; `BuildWindowTitle(InGameUIFrameData&)` and caller hover
policy move with the product shell in UI4 after RBS5. No exception may survive
UI6.

#### Component and state ownership

| Component/family | Current product consumers | Genuine retained foundation state | Caller/product values |
|---|---|---|---|
| `UIButton` | Main shell/window owner; Sky, Scene, Memory tabs | Bounds | Pointer/hover, enabled/active, command |
| `UICheckBox` | Main shell/window owner, draw widgets, and Cinematic/Editor/Options/Physics/Profiler/Scene/Sky tabs | Bounds | Checked, enabled, hover, accent, command |
| `UIComboBox` | Main shell, frame composition, mini palette, Cinematic/Editor/Scene tabs, window owner | Bounds, popup openness, drop direction, label visibility | Selection, disabled mask, hover, action |
| `UIIconButton` | Profiler tab | Bounds | Expanded/selected |
| `UISlider` | Main shell, mini palette, frame composition, Controls/Cinematic/Memory/Options/Physics/Profiler/Scene/Sky tabs, window owner | Bounds and quantization geometry | Value/range/text, hover/focus/active, command |
| `UIScrollBar` | Main shell/window owner | Track bounds | Content, viewport, offset, visibility time |
| `UITabBar` | Mini palette, frame composition, window owner | Bounds | Labels/count/selection |
| `UIBackdropBlur` | Main shell/window owner | Last screen/bounds and invalidation reason | Enabled and current-frame inputs |
| `UICacheState` | Main shell/window owner | One draw list, semantic key, dirty/frame-valid flags | Product signatures supplied each frame |
| `UIDrawWidgets` | Shell, composition, chrome, window owner, and product tabs | None | Explicit panel/row/label/toggle inputs |
| Window interaction | `UIWindowInteractionOwner` and main shell | Placement/animation and real drag/resize capture | Camera blocking and product window/tab/widget policy |

`RuntimeUiControl` remains Runtime-owned with `id`, `kind`, `action`, draw/hit
rectangles, visibility, enabled, hover, focus, active, checked, and reveal
request values. The bounded `RuntimeUiSurface<N>` retains ordered controls,
identity, pointer/active state, hit ordering, and pointer consumption; `Reset`
clears frame-disposable state. Existing tests cover every control kind,
capacity rejection, identity, visibility/enabled policy, ordered hits,
draw/hit geometry, pointer blocking, active/checked values, and reset.
`focused` is currently unused and `requestsReveal` lacks a direct focused test;
both remain owner-local UI3 work after RBS4 rather than expanding UI0's lease.

#### Runtime presentation and physical-boundary inventory

Only two Runtime sources directly construct `UIDrawContext` presentation:

| Current owner | Sites | Binding disposition |
|---|---:|---|
| `Runtime/Planning/ReplayOverlayRenderer.cpp` | 153 | UI3 adopts shared panels, buttons, toggles, tabs, labels, rows, tables, scrollbars, drawers, and window chrome after RBS4. Replay tracks/markers, cause hierarchy/evidence, intercept facts, trip planning, and porkchop visualization remain with their domain owners. |
| `Runtime/Render/UiTextPass.cpp` | 22 | Eight validation primitives remain submitter-local; fourteen chrome/status-badge calls move to the product presenter after RBS4 in UI3. |
| `Runtime/Render/UiDrawSubmission.cpp` | Backend translator only | Retain in Runtime/Render. |

The 153 Planning sites group into cause summary 15, raw table 14, iterations
table 17, solver drawer 16, replay scrubber/transport 44, intercept readout 2,
trip controls 5, porkchop scientific grid 13, and cause-tree window 27.
Owner-local bounded surfaces remain `ReplayScrubberSurface<13>`,
`ReplayCauseWindowSurface<9>`, and `ReplayTripPlannerSurface<6>`.

Foundation sources include only local UI or standard-library headers. UI has no
Runtime or Rendering include. Current product dependencies are exact: `UI.h`
uses Core Common, Config, MainMemoryStats, and RuntimeReserveAllocator;
`OperatorEditorExchange` uses SbResult/SbDiagnosticStore; the main shell,
profiler presenter, and window owner use Core Profiler; `UITabMemory` uses Core
allocation/memory; `UITabProfiler.cpp` uses `Assets/AssetKeys.h`; and
`UITabScene.cpp` uses Core PlatformWin32. These dependencies move with their
product rows rather than being hidden behind a foundation forwarding seam.

`SKULLBONEZ_UI.vcxproj` and filters own all 70 files exactly; Core and Tests
compile none of those production files. Portable CMake owns all 34 UI
implementations exactly and includes `TestUIDrawValues.cpp`.
`UiBoundaryUnitTests.vcxproj` references only the UI library and compiles only
the Core FatalError/Log/SbResult support required by that boundary executable.

#### Boundedness and behavioral baseline

`UIDrawList` remains fixed at 2,048 commands, 16,384 copied-text bytes, and 32
clip levels. The heaviest frozen product fixture records 289 commands and 1,369
text bytes without overflow. Other current bounded values are 512 operator
hierarchy rows, 12 render-target previews, 192 profiler markers, 128 workers,
120 histogram samples, 64 reserve-growth rows, 160 reserve-capacity rows, 256
input key bits, 120 memory samples, and 64 pinned events. `UICacheState` performs
one construction-time draw-list allocation and does not grow in steady state;
`UISceneNavigationModel` owns its product browsing vector/string state outside
the foundation. The allocation scan has zero UI findings.

The eleven ordered product-surface fingerprints, Profiler through Memory, are:

```text
17282268762934632125, 2399826200700883422, 643319089294822447,
9774020997193876338, 16562541090565446015, 13838569643518502325,
1186693958027131891, 5057719176066529734, 3243788985155815295,
15645422141942934428, 14809053394253860312
```

Profile uses the alternate Profiler fixture fingerprint
`16424379413615724563`; the other ten match. Commit
`b2395d83f` added the missing direct component geometry/state witnesses with
resting fingerprint `9585956286470253977` and engaged fingerprint
`15522795272601894673`, preserving all eleven product fingerprints and bounded
overflow assertions. `tools\validate_ui_boundary_tests.bat` passed on the
integrated PR tree in 6.4 seconds; the isolated worker also passed the complete
six-lane `tools\validate_all_cpu_tests.bat` in 281.8 seconds. No baseline was
refreshed.

#### RBS dispositions and binding implementation order

The RBS0 repeatable graph reporter capability is available from
`f074f9765b59f6b18f73b87eca7990b0f460e858`; it does not satisfy a product move.
Product commands/views, operator projection, Planning/Replay adoption, and
UiText presentation remain `pending RBS4`, then UI3. The main shell, tabs,
frame composition, window owner, and reverse Runtime/UI to App edges remain
`pending RBS5`, then UI4. Project/filter/CMake narrowing remains `pending RBS6`,
then UI5. No product-placement row is falsely marked satisfied, and the
post-RBS product path is intentionally not guessed.

UI1 first adds component-neutral visible/enabled/hovered/focused/active/
selected/checked state beside `UIRect`; then stateless panel, label/value-row,
and repeated-row presentation; button, toggle, slider, tab, and scrollbar;
combo/list-popup and icon contracts; and only generic sub-symbols of window
chrome. UI2 routes existing wrappers through those functions, removes duplicate
draw/hit/style/measurement logic, and retains only popup openness, cache
validity, backdrop invalidation, and genuine drag/resize capture. Both phases
leave every shell, command, frame-composition, product presenter, and
Runtime/Render row untouched until its named RBS prerequisite closes.

## Phase UI1 - Establish Stateless Component Value Contracts

**Prerequisite:** UI0. RBS and Physics may remain active because this phase is
confined to rows UI0 ruled final component foundation and their focused tests.
Do not edit a shell, command, frame-composition, product-presenter, or
Runtime/Render row marked `pending RBS<n>`.

**Goal:** Make the existing component foundation expressive enough for both the
main GameUI shell and owner-local Runtime surfaces.

- [x] Add the smallest component-neutral geometry and visual-state values
      required by the UI0 inventory.
- [x] Add stateless draw/hit/value helpers for the proved shared component
      families, beginning with panel, button, toggle, slider, tab, scrollbar,
      and label/value row.
- [x] Make disabled, hovered, focused, active, selected, and checked behavior
      explicit where the existing product surfaces already use it.
- [x] Keep text measurement in `UIFontMetrics` and style in immutable
      foundation values. Eliminate renderer-side measurement only in final
      foundation rows or stable caller adaptations not marked `pending RBS<n>`;
      defer pending product presentation to UI3/UI4.
- [x] Preserve fixed capacities, ordered commands, copied text, clipping,
      fingerprints, and overflow reporting.
- [x] Add focused tests for every component state, hit/draw geometry equality,
      quantization, clipping, disabled behavior, and deterministic fingerprints.

**Acceptance:** Runtime control state can be rendered without mouse-coordinate
re-derivation or domain callbacks; component tests link only the UI foundation;
no product vocabulary, Runtime include, renderer handle, retained owner pointer,
or steady-state growth enters the new contracts.

### UI1 Closure Evidence - 2026-08-23

Commit `69eaccee2` changes only `UIDraw.h/.cpp`, `UIDrawWidgets.h/.cpp`,
and the renderer-free `UiBoundaryUnitTests.cpp`. `UIVisualState` is one
component-neutral bit value beside `UIRect`; its visible, enabled, hovered,
focused, active, selected, and checked facts are resolved by the caller before
draw. It carries no pointer input, action id, callback, product value, Runtime
owner, renderer handle, or retained pointer.

`UIDrawWidgets` now publishes stateless geometry, hit/value, and draw operations
for panel, clipped label/value row, button, toggle, slider track/thumb and
quantization, exact tab bounds/hit/draw, scrollbar thumb/draw, combo field/
popup/option geometry plus disabled-mask policy, and generic icon buttons.
Disabled components keep their geometric pointer-blocking result but cannot
activate; hidden components append no command. `UIDrawContext` exposes balanced
screen-space clip recording while `UIDrawList` retains the sole bounded clip
state. Text measurement remains in `UIFontMetrics`, styling remains immutable
`UIStyle` data, and all output remains copied into the existing fixed-capacity
ordered command/text/clip buffers.

The focused boundary fixture proves shared geometry and quantization values,
enabled versus disabled activation, hidden zero-command behavior, all seven
visual-state flags, balanced clipping, copied text, deterministic ordering,
and overflow reporting. It pins eight state fingerprints:

```text
9138081368605736749, 49288575029089482, 211756514920079498,
6302452228787434232, 2904659679807374515, 4645013559851399903,
13061036390687143437, 5558979605539197941
```

The comprehensive stateless fixture is `9020569520314488178`. Both UI0 direct
component fingerprints and all eleven product-surface fingerprints remain
unchanged; no visual baseline or golden was refreshed.

Coordinator fan-in evidence on the PR tree:

- `tools\validate_ui_boundary_tests.bat`: pass in 6.6 seconds, real UI library
  plus all eleven detached product surfaces;
- `python tools\check_dependency_graph.py --repo .`: pass, zero findings;
- allocation-policy scan: the inherited 40 repository findings remain, with
  zero finding under `SkullbonezSource/UI`;
- header, brace, formatting, and diff checks: pass; and
- the conservative source pre-commit Physics gate built and ran successfully,
  then reproduced the registered owner-controlled Physics CSV mismatch (6,166
  differing lines, first at line 4,998). UI1 did not touch Physics, scene,
  project, or golden inputs; the oracle was not refreshed.

The new out-of-line component operations deliberately have only the focused
boundary consumer until UI2 routes the retained wrappers through them. UI2
must complete that phase-local reachability transition without adding a ruling
for the temporary test-only state.

## Phase UI2 - Converge The Existing GameUI Widgets

**Prerequisite:** UI1. RBS and Physics may remain active; expand the lease before
touching a product package, shared project manifest, or Physics presentation
contract.

**Goal:** Make the current GameUI use the authoritative component functions
before moving product composition across the project boundary.

- [x] Route existing retained widget wrappers through UI1 component contracts.
- [x] Limit writes to UI0-final foundation rows and stable caller adaptations
      not marked `pending RBS<n>`. Leave pending shell, command, frame-
      composition, product-presenter, and Runtime/Render rows to UI3/UI4 even
      when they still reside physically under `SkullbonezSource/UI`.
- [x] Keep persistent state only where a component owns a real interaction
      invariant such as popup open state or drag capture.
- [x] Delete wrapper-local duplicated drawing, hit testing, text measurement,
      and style decisions once all callers use the shared contract.
- [x] Convert common window chrome, footer controls, rows, tables, and tab
      presentation proven reusable by UI0.
- [x] Preserve existing layout, pointer capture, automation anchors, draw order,
      cache signatures, and public visual fingerprints.
- [x] Add focused false-pass controls proving hit geometry cannot diverge from
      draw geometry and product command values are unchanged.

**Acceptance:** The current GameUI draws through the shared component
foundation; retained wrappers have explicit ownership reasons; component
conversion causes no visual fingerprint, interaction, allocation, or command
semantic change.

UI2 closure evidence on the PR tree:

- the seven retained wrappers now resolve explicit stateless component state,
  geometry, and presentation through the UI1 contracts; popup-open and drag-
  capture state remain with their interaction owners;
- `TabLayout` and `ComboLayout` name the established compatibility split
  between interaction and visual bounds. The combo deletion condition records
  all 18 production `HitBox` callers across the window owner and three product
  tab owners;
- disabled footer and established-combo rendering have command-level negative
  witnesses, while wrapper/direct streams and all eleven detached product-
  surface fingerprints remain unchanged;
- the three phase-local test-only operations (`DrawPanel`,
  `DrawLabelValueRow`, and `TabBounds`) were deleted instead of receiving
  reachability rulings; and
- `tools\validate_ui_boundary_tests.bat`, the dependency and ownership
  inventories, all-config build validation, refreshed strict symbol
  reachability, and `tools\validate_ui_stress.bat` pass. The allocation scan
  retains the inherited 40 non-UI findings and reports zero UI rows. No visual
  or allocation baseline was refreshed.

## Phase UI3 - Adopt Components In Owner-Local Runtime Surfaces

**Prerequisites:** UI2 and RBS4. Refresh every `pending RBS4` row against the
frozen projection, command-application, and submission owners before editing.

**Goal:** Remove repeated raw component presentation from Runtime while leaving
domain state, layout decisions, and action routing with their current owners.

- [x] Convert the UI0-proved repeated Replay/Planning surfaces first, using the
      existing surface rows and geometry as component inputs.
- [x] Convert remaining product badges/panels only when the same component
      contract fits without domain flags or callbacks.
- [x] Keep `RuntimeUiControlId`, `RuntimeUiActionId`, z-order, pointer blocking,
      selection identity, and typed command routing above the foundation.
- [x] Delete local button/toggle/tab/scrollbar/panel styling and renderer text
      measurement made redundant by shared components.
- [x] Preserve domain-specific charts, scientific visualizations, trajectories,
      tables, and authored layouts as owner-local presentation; do not force
      them through an unsuitable generic widget.
- [x] Test pointer ownership, disabled click blocking, action identity, draw
      ordering, Replay interaction, and exact detached-state rendering.

**Acceptance:** Repeated controls use one component implementation; domain
surfaces retain their semantic owners; no central UI god presenter, Runtime
cycle, callback bridge, or product-aware foundation API is introduced.

### UI3 Closure Evidence - 2026-08-24

Replay/Planning intercept, trip-planner, and porkchop panel shells now use the
shared stateless compact panel, and all five trip actions use the shared compact
button. Planning retains rectangles, action/control identity, enabled state,
pointer routing, and specialized scientific presentation. Runtime scene-pause,
camera-mode, and interaction badges share the panel implementation while their
detached chrome facts and status accents remain owner-local.

Profile builds with zero warnings/errors. The renderer-free UI boundary passes;
focused Runtime pointer/action and Planning detached-state/order witnesses pass
9/9 and 25/25 assertions, with deterministic Planning fingerprint
`309035145945859501`. Replay overlay, trip-planner, and composition-order suites
pass, dependency enforcement reports zero findings, project filters pass
856/856, and the deterministic Physics hook remains byte-exact. No baseline was
refreshed.

## Phase UI4 - Move Product Composition Above The Foundation

**Prerequisites:** UI3 and RBS5. Consume the closed package DAG and refresh
every `pending RBS5` owner/path before moving source.

**Exclusive edit resource:** Acquire the exact project/filter/CMake/portable-
manifest single-writer lease before changing or deleting any source membership,
then release it after the coherent membership edit. This command window may not
overlap RBS6 project-topology edits.

**Goal:** Make physical source/project ownership match the component/product
boundary.

- [ ] Move the GameUI shell, window interaction owner, frame composition, tabs,
      product presenters, and product commands/views according to UI0 and RBS4.
- [ ] Replace broad product frame-data access with focused per-surface detached
      views where the move would otherwise preserve a parameter bag.
- [ ] Keep typed commands with the product/domain owner that interprets them;
      foundation controls emit only generic interaction facts.
- [ ] Preserve one scene-navigation, popup, pointer-capture, tab-selection, and
      window-placement owner; do not duplicate state during migration.
- [ ] Delete old UI-project files, forwarding headers, aliases, compatibility
      namespaces, duplicate project items, and stale ownership comments in the
      same batch that removes their last caller.
- [ ] Keep domain-specific owner-local presenters in their existing packages
      when moving them centrally would weaken the RBS DAG.

**Acceptance:** `SKULLBONEZ_UI` contains only component-foundation
responsibilities; game-specific composition and command semantics live above
it; every source compiles once; the UI project still links independently
without Runtime, Rendering, or backend objects.

## Phase UI5 - Enforce Project, Dependency, And Test Boundaries

**Prerequisites:** UI4 and RBS6. Project and portable-manifest files are
single-writer resources during this phase.

**Goal:** Make the narrowed UI foundation and product placement mechanically
durable.

- [ ] Update `SKULLBONEZ_UI.vcxproj` and filters to the exact foundation source
      inventory; update Core/Test/CMake manifests for moved product sources.
- [ ] Extend the existing dependency/project rule data and fixtures for exact
      source ownership and legal downward use; do not add a second checker or
      count budget.
- [ ] Replace the current all-product-tab UI boundary fixture with a true
      foundation-only component/link probe.
- [ ] Retain product UI fingerprints and interaction tests in the owning Core
      test lane so moving them out of the foundation does not weaken behavior
      coverage.
- [ ] Add negative fixtures proving foundation-to-Runtime/Rendering and
      duplicate project ownership fail.
- [ ] Reconcile build flags, PCH, FP contract, warnings, include paths, filters,
      portable build manifests, and build-config rulings.
- [ ] Compare clean/incremental UI and application build/link timings to UI0;
      review material regressions without changing the project count.

**Acceptance:** The existing UI project is a standalone component library;
product composition belongs to the RBS-approved application package; project
and include graphs enforce the direction; component and product behavior tests
remain independently meaningful.

## Phase UI6 - Terminal Behavioral And Ownership Closure

**Prerequisites:** UI5 and RBS7. Terminal GPU, performance, baseline, and full
validation resources serialize with Physics closure on one frozen integration
commit; read-only review and inventory may overlap Physics implementation.

**Goal:** Prove the separation is reusable, behavioral, physical, and
build-enforced.

- [ ] Re-run the UI0 tracked-file and consumer inventories and reconcile every
      disposition and exception.
- [ ] Review all foundation public types for product vocabulary, owner reach-
      back, callbacks, resource handles, broad contexts, and avoidable retained
      state.
- [ ] Review product presenters for duplicated component implementation and
      component APIs for leaked product semantics.
- [ ] Audit every touched source-bearing file with the comment-style audit and
      reconcile the exact `git ls-files` checklist.
- [ ] Run all governance inventories affected by moved signatures, bodies,
      aggregates, reachability, build configuration, and glossary terms.
- [ ] Run dependency/project/filter, UI foundation, product UI, interaction,
      allocation, portable build, Automation, Replay visual, DX12, graphics
      stress, performance, and cumulative repository gates.
- [ ] Compare representative final GameUI and Replay surfaces against the
      pre-change captures/fingerprints without refreshing visual baselines.
- [ ] Obtain independent component-boundary and product-ownership review after
      fixes, then run `tools\agent_validate.bat --plan-completion` once.
- [ ] Record final source/project graphs, build timings, component migrations,
      validation, review fixes, baseline disposition, and the empty exception
      table.

**Terminal acceptance:** `SKULLBONEZ_UI` is the sole reusable component
foundation project; it contains no product workflow or Runtime/Rendering
authority; product presenters use the shared components without losing their
domain owners; component and product source compile exactly once; hit and draw
geometry remain identical; fixed-capacity/no-growth behavior is preserved; no
new project, callback bag, facade, duplicate state owner, or forbidden edge
exists; all required gates pass without an unauthorized golden refresh.

---

## Validation Map

Heavy validation remains concentrated in UI6.

| Phase | Focused iteration evidence | Pre-commit/closure gates |
|---|---|---|
| UI0 | Source/project/consumer inventories, RBS overlap map, pre-change fingerprints | Documentation and focused witnesses only |
| UI1 | Component state, hit/draw, quantization, clipping, overflow, fingerprint tests | UI boundary test, dependency scan, focused unit tests |
| UI2 | Existing GameUI component adoption and false-pass controls | product UI unit tests, UI stress, allocation scan |
| UI3 | Replay/Planning pointer, action, draw-order, and detached-state tests | interaction policy, Replay-focused tests, UI stress |
| UI4 | Product source moves and focused view/command tests | build all configurations, filters, dependency, UI/product tests |
| UI5 | Foundation link probe, product fingerprints, negative project/include fixtures | build config, portable build, fast/CPU gates |
| UI6 | Final inventories, visual comparison, audit, independent review | all focused rows plus Automation, DX12, graphics stress, Replay visual, performance, full/plan-completion |

## Mandatory Review Questions

1. Is this type/component reusable without knowing a Skullbonez product domain?
2. Which owner retains interaction state, and is there exactly one?
3. Do hit testing and drawing consume the same geometry and explicit state?
4. Does reuse remove duplicated implementation without centralizing domain
   policy?
5. Did a source move preserve the post-RBS package DAG and single project owner?
6. Can any value, callback, pointer, or aggregate recover unrelated Runtime or
   renderer authority?
7. Which focused witness fails if visual state, command identity, pointer
   blocking, draw order, capacity, or allocation behavior changes?
8. Which old wrapper, raw draw implementation, forwarding header, project item,
   ruling, or comment is now deletion-ready?

## Stop Conditions

Stop only the affected phase for a missing or stale named RBS prerequisite:
RBS4 for UI3, RBS5 for UI4, RBS6 for UI5, or RBS7 for UI6. Also stop if a
component needs product vocabulary, Runtime/Rendering includes, callbacks,
owner pointers, or backend handles; moving product UI creates a reverse edge;
one interaction state would gain two owners; a new production project appears
necessary; genericization would obscure a domain visualization; steady-state
allocation grows; or a visual/behavioral golden refresh appears necessary.
Unrelated ready UI-foundation or Physics work continues in its own worktree.

## Completion Reporting

The closing handoff reports the RBS overlap dispositions, final foundation and
product source inventories, component API and migrated consumers, retained
owner-local presentations, source/project graphs, UI-only link proof, build
timings, draw capacities/high water, allocation result, visual/fingerprint
comparison, validation, touched-source checklist counts, independent-review
fixes, baseline disposition, and an empty exception table.
