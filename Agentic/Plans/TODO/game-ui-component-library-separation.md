# Game UI Component Library Separation Plan

Date: 2026-08-22
Status: Active by owner direction. 0/7 phases complete; binding after
`RUNTIME_BOUNDARIES` RBS7.
Impact area: `SkullbonezSource/UI/`, game-facing composition under
`SkullbonezSource/Runtime/`, `SKULLBONEZ_UI.vcxproj`, focused UI tests,
dependency/build enforcement, and documentation
Owner: UI foundation plus the concrete Runtime product owners that compose it
Priority: Third in the binding order, after `RAGDOLL_PHYSICS` and
`RUNTIME_BOUNDARIES`
Commit name: `GAME_UI_COMPONENTS`

## Owner Direction

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

`Agentic/Plans/TODO/runtime-boundary-separation.md` is being implemented by
another agent and must not be edited by this plan. This plan begins only after
RBS7 closes, then measures the resulting source rather than assuming the
registration-era layout still exists.

The following work is deliberately not duplicated:

| Existing RBS ownership | Potential duplication | This plan's disposition |
|---|---|---|
| RBS0 package/project DAG and build-cost census | Re-inventorying or independently ratifying Runtime/package topology | Consume the RBS0/RBS7 graph and timings; UI0 inventories only the component-versus-product boundary left inside/above UI. |
| RBS1 dependency-cycle enforcement | Building another include/SCC checker | Extend existing rule data or project ownership fixtures only where the narrowed UI boundary needs proof. Do not add a second graph tool. |
| RBS3 immutable frame-metrics owner | Moving profiler/frame timing ownership while splitting UI views | Treat the RBS3 snapshot as a product input. Do not move, recalculate, or republish timing state. |
| RBS4 operator projection, command application, and GPU submission separation | Reworking `Run::RenderOperatorUiPhase`, the seven-slice submission surface, process commands, or renderer ownership | RBS4 is a prerequisite. This plan consumes its detached views, command path, Runtime/UI owner, and Runtime/Render submission seam. |
| RBS5 reverse-App and cycle removal | Moving UI files in a way that recreates App edges | Preserve the closed RBS package DAG; any proposed move that needs a reverse edge stops for owner review. |
| RBS6 minimal VC topology | Creating another UI/product library or independently changing project policy | Retain the one existing `SKULLBONEZ_UI` library, move game-specific sources to the RBS-approved `SKULLBONEZ_CORE` package owner, and create no additional production project. |
| RBS7 terminal architecture validation | Claiming those same Runtime ownership changes as GameUI closure | Record RBS7 as inherited baseline evidence. Re-run only gates affected by later GameUI source/API changes, then perform this plan's own terminal closure. |

If RBS4 already moves or decomposes a type listed in this plan's initial
worklist, UI0 marks that row `satisfied by RBS` and tests the resulting seam. It
must not recreate, rename, or move the type a second time merely to match this
registration document.

---

## Dated Baseline Evidence

Evidence measured on 2026-08-22 at `a00654873` on `main`. Concurrent Physics
and Replay working-tree changes were user-owned and are outside this plan.
CodeGraph was current during the initial UI analysis, then reported unrelated
pending source changes during registration; UI0 must refresh its own baseline
after RBS7 rather than treating these observations as closure counts.

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

UI0 replaces this seed with an exact tracked-file inventory after RBS7. A file
is ruled by responsibility, not name or desired line count.

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

## Phase UI0 - Ratify The Post-RBS Component/Product Boundary

**Goal:** Measure the source after RBS7, classify every UI file and consumer,
and freeze an owner-approved migration map before implementation.

- [ ] Record branch, commit, dirty files, CodeGraph status, RBS7 handoff, and
      inherited validation/baseline findings.
- [ ] Inventory every tracked `SkullbonezSource/UI` source-bearing file with
      `git ls-files` and classify it as foundation, product composition,
      product value/command, backend submission, or exact exception.
- [ ] Inventory every UI component use and direct Runtime draw-authoring site,
      grouped by repeated presentation responsibility rather than raw count.
- [ ] Compare retained widget state against `RuntimeUiSurface` and other
      disposable surface values; rule each state field to one owner.
- [ ] Inventory public include dependencies, project membership, tests,
      CMake/source manifests, allocation behavior, draw capacities, and current
      visual fingerprints.
- [ ] Mark every registration worklist row already completed by RBS as
      `satisfied by RBS` with its resulting source path and test evidence.
- [ ] Ratify the exact target paths and file move order without creating a new
      project or changing the RBS DAG.
- [ ] Add focused pre-change witnesses for component states and repeated Runtime
      controls that lack behavioral coverage.

**Acceptance:** Every tracked UI file and repeated product presentation path has
one owner/disposition; all RBS overlap is resolved without duplicate work; the
component API and source move order require no reverse edge, project addition,
or broad context.

## Phase UI1 - Establish Stateless Component Value Contracts

**Goal:** Make the existing component foundation expressive enough for both the
main GameUI shell and owner-local Runtime surfaces.

- [ ] Add the smallest component-neutral geometry and visual-state values
      required by the UI0 inventory.
- [ ] Add stateless draw/hit/value helpers for the proved shared component
      families, beginning with panel, button, toggle, slider, tab, scrollbar,
      and label/value row.
- [ ] Make disabled, hovered, focused, active, selected, and checked behavior
      explicit where the existing product surfaces already use it.
- [ ] Keep text measurement in `UIFontMetrics` and style in immutable
      foundation values; eliminate renderer-side measurement from product
      presentation touched by this phase.
- [ ] Preserve fixed capacities, ordered commands, copied text, clipping,
      fingerprints, and overflow reporting.
- [ ] Add focused tests for every component state, hit/draw geometry equality,
      quantization, clipping, disabled behavior, and deterministic fingerprints.

**Acceptance:** Runtime control state can be rendered without mouse-coordinate
re-derivation or domain callbacks; component tests link only the UI foundation;
no product vocabulary, Runtime include, renderer handle, retained owner pointer,
or steady-state growth enters the new contracts.

## Phase UI2 - Converge The Existing GameUI Widgets

**Goal:** Make the current GameUI use the authoritative component functions
before moving product composition across the project boundary.

- [ ] Route existing retained widget wrappers through UI1 component contracts.
- [ ] Keep persistent state only where a component owns a real interaction
      invariant such as popup open state or drag capture.
- [ ] Delete wrapper-local duplicated drawing, hit testing, text measurement,
      and style decisions once all callers use the shared contract.
- [ ] Convert common window chrome, footer controls, rows, tables, and tab
      presentation proven reusable by UI0.
- [ ] Preserve existing layout, pointer capture, automation anchors, draw order,
      cache signatures, and public visual fingerprints.
- [ ] Add focused false-pass controls proving hit geometry cannot diverge from
      draw geometry and product command values are unchanged.

**Acceptance:** The current GameUI draws through the shared component
foundation; retained wrappers have explicit ownership reasons; component
conversion causes no visual fingerprint, interaction, allocation, or command
semantic change.

## Phase UI3 - Adopt Components In Owner-Local Runtime Surfaces

**Goal:** Remove repeated raw component presentation from Runtime while leaving
domain state, layout decisions, and action routing with their current owners.

- [ ] Convert the UI0-proved repeated Replay/Planning surfaces first, using the
      existing surface rows and geometry as component inputs.
- [ ] Convert remaining product badges/panels only when the same component
      contract fits without domain flags or callbacks.
- [ ] Keep `RuntimeUiControlId`, `RuntimeUiActionId`, z-order, pointer blocking,
      selection identity, and typed command routing above the foundation.
- [ ] Delete local button/toggle/tab/scrollbar/panel styling and renderer text
      measurement made redundant by shared components.
- [ ] Preserve domain-specific charts, scientific visualizations, trajectories,
      tables, and authored layouts as owner-local presentation; do not force
      them through an unsuitable generic widget.
- [ ] Test pointer ownership, disabled click blocking, action identity, draw
      ordering, Replay interaction, and exact detached-state rendering.

**Acceptance:** Repeated controls use one component implementation; domain
surfaces retain their semantic owners; no central UI god presenter, Runtime
cycle, callback bridge, or product-aware foundation API is introduced.

## Phase UI4 - Move Product Composition Above The Foundation

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

Stop for owner review if RBS7 is incomplete or its graph/evidence is stale; a
component needs product vocabulary, Runtime/Rendering includes, callbacks,
owner pointers, or backend handles; moving product UI creates a reverse edge;
one interaction state would gain two owners; a new production project appears
necessary; genericization would obscure a domain visualization; steady-state
allocation grows; or a visual/behavioral golden refresh appears necessary.

## Completion Reporting

The closing handoff reports the RBS overlap dispositions, final foundation and
product source inventories, component API and migrated consumers, retained
owner-local presentations, source/project graphs, UI-only link proof, build
timings, draw capacities/high water, allocation result, visual/fingerprint
comparison, validation, touched-source checklist counts, independent-review
fixes, baseline disposition, and an empty exception table.
