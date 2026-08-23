# Runtime Boundary Separation And Project Topology Plan

Date: 2026-08-22
Status: Active by owner direction. 0/8 phases complete.
Impact area: `SkullbonezSource/Runtime/`, Runtime-facing Rendering/UI seams,
Visual Studio project topology, dependency enforcement, tests, and documentation
Owner: Runtime architecture, with each moved value or behavior retained by its
concrete subsystem owner
Priority: Binding third plan under the superseding 2026-08-23 owner direction;
RBS0 through RBS7 execute in strict internal order. `ERROR_OBSERVABILITY` and
`RAGDOLL_PHYSICS` receive scarce-slot and fan-in priority, but are not
predecessors when phase-local leases and mutable validation outputs are
disjoint.
Commit name: `RUNTIME_BOUNDARIES`

## Owner Direction

Make Runtime's physical package structure express a real dependency direction.
`Runtime/App` remains the sole process composition root: it may construct owners,
sequence startup/shutdown and frame phases, pump platform messages, and apply
typed cross-owner results. No lower Runtime package may include App merely to
borrow retained state, define an App method, or reuse a value placed in the wrong
directory.

Adequate separation requires all of the following:

1. The Runtime package graph is acyclic, with App at the top.
2. Concrete subsystem owners retain their own business state and publish detached
   values or typed commands across package boundaries.
3. No operation regains the whole application surface through capability slices,
   callback records, or differently named context bags.
4. Rendering consumes frame values and draw/submission packets; it does not
   mutate App-owned timers, UI state, Replay state, or process policy.
5. Visual Studio projects are split where a project/link boundary makes the
   dependency direction mechanically stronger. Project count is not a goal.

The owner approved a conservative Visual Studio topology on 2026-08-22. The
source/package DAG remains mandatory, but source separation does not imply one
project per package or Runtime tier. Existing Maths, Physics, and UI static
libraries remain. One dedicated Rendering static library is approved in
principle because Rendering is a cohesive subsystem with an existing
feature-neutral dependency contract; RBS0 must still prove its exact source
closure, project references, configuration parity, and build/link cost before
RBS6 creates it.

Core, Assets, Scene, World, Gameplay, and Runtime sources remain in
`SKULLBONEZ_CORE` by default. Do not create a broad portable-engine library,
per-tier Runtime libraries, or other production projects during this plan.
Another project boundary requires a separate explicit owner decision supported
by the RBS0 dependency and build evidence. A thin App is thin in authority and
responsibility, not in source-file count. Do not create projects merely to
reorganize Solution Explorer, empty the executable project, or absorb a source
cycle into a linkable library.

---

## Dated Baseline Evidence

Evidence recorded on 2026-08-22 at `98605e036` on
`codex/cause-hierarchy-ui-first`:

- The worktree was clean and CodeGraph was current at 1,175 files, 36,520 nodes,
  and 109,730 edges.
- The configured dependency proof and repository scan passed with zero findings.
  That gate checks allowed edges; it does not reject package cycles.
- A resolved quoted-include scan found 35 direct bidirectional Runtime package
  pairs. This is an observation, not a count allowance or ratchet.
- There are 28 non-App Runtime include sites that include `Runtime/App/*`.
- `Run::RenderOperatorUiPhase` is defined in Runtime/UI and
  `Run::RunUIStressActions` is defined in Runtime/Capture: the two current `Run`
  definitions outside Runtime/App.
- `Run::RenderOperatorUiPhase` spans lines 246-809 and projects domain views,
  assembles seven sibling UI graph invocation records, submits GameUI and ImGui,
  applies surface switching, starts Tracy, and reinitializes WorkerPool.
- `RuntimeRenderer::RenderUiText` receives all seven invocation records in one
  call. Each may satisfy a callback lifetime locally, but together they expose
  the complete UI capability surface.
- `RunTimerState` states that Run owns its timers, while
  `UiTextPass::UpdateFrameMetrics` mutates those timers from Runtime/Render.
- The solution has five projects: App, Maths, Physics, Tests, and UI. Maths,
  Physics, and UI are production static-library boundaries; Core, Assets,
  Gameplay, Scene, World, Rendering, and most Runtime source remain in App.

RBS0 refreshes every inventory. Historical numbers explain the plan; they do
not define completion thresholds.

## Goals

- Make Runtime/App a true composition root with no reverse dependency.
- Give every Runtime package one responsibility and an acyclic target set.
- Move App-located values to honest owners without forwarding headers or aliases.
- Separate native host ownership, frame metrics, operator-view projection,
  renderer submission, and process command application.
- Remove `Run` definitions from non-App packages and close the logical `Run`
  surface under the God-Object Closure Rule.
- Add executable package-cycle proof to the dependency gate.
- Add the approved Rendering VC project boundary if RBS0 proves that it
  reinforces ownership without configuration drift or material build cost.
- Preserve zero-allocation runtime, determinism, replay compatibility, UI
  behavior, DX12 validation, and all owner-controlled baselines.

## Non-Goals

- Do not redesign Physics, Replay, Prediction, Planning, RenderGraph, GameUI,
  ImGui, Tracy, or the editor as products.
- Do not remove features, pursue LOC reduction, or target a package/project count.
- Do not replace dependencies with `void*`, callbacks, service bags, capability
  unions, broad contexts, forwarding facades, globals, or owner reach-back.
- Do not move Runtime feature vocabulary/state into Core, Rendering, Physics,
  UI, Scene, or World.
- Do not create a monolithic `RuntimeFoundation`, `RuntimeServices`, or similar
  library to make cyclic sources link.
- Do not create per-package or per-tier Runtime projects, a broad portable-engine
  project, or additional production libraries without a new explicit owner
  decision.
- Do not measure App closure by how few source files remain in
  `SKULLBONEZ_CORE`; composition-root closure is an authority rule.
- Do not refresh any golden baseline merely to make the separation pass.
- Do not treat smaller functions or signatures as proof that authority moved.

## Dependencies And Decisions

- Core Engine Evidence-Driven Code Reduction CR0-CR5 is complete; RBS work starts
  from its closing source rather than reopening its candidate ledger.
- RBS0 is a binding design gate. Later phases may not invent package placement
  that the refreshed DAG did not ratify or project placement outside the
  conservative owner ruling above.
- Source-package cycles are removed before RBS6 creates library boundaries. A
  linker-friendly grouping is not evidence that source direction is correct.
- Existing Maths, Physics, and UI projects remain authoritative. RBS0 may refine
  their dependency evidence but may not replace, merge, or subdivide them. No
  phase may duplicate their source into App.
- Rendering is the only new production project authorized by this plan. If RBS0
  cannot form that boundary without reverse references, duplicate compilation,
  configuration drift, or unjustified build cost, stop for owner review instead
  of creating substitute libraries.
- Owner-controlled baseline mismatches are preserved and reported. This plan has
  no standing authority to refresh a behavioral oracle.
- Implementing this plan follows `Agentic/Skills/orchestrator/SKILL.md`, including
  fresh worker delegation, independent review, commits, pushes, and handoff.

---

## Target Architecture Contract

RBS0 may refine package names, but must preserve these properties:

```text
Runtime/App                         process composition and top-level sequencing
    -> product/runtime features     Replay, Prediction, Planning, Scene, Render
    -> operator/tool features       UI composition, Editor, Capture, Automation
    -> runtime policies             Input, Interaction, Camera, Simulation
    -> host/startup values          native host, startup options, frame values

No arrow points back to Runtime/App.
No multi-package strongly connected component is permitted.
```

The final graph may use more precise packages. It may not use a broad shared
package to absorb unrelated values.

### Value and command boundaries

- Lower owners publish detached immutable snapshots with explicit lifetime and
  identity semantics.
- UI consumes snapshots and returns typed commands; it does not receive mutable
  Physics, Replay, Renderer, Window, WorkerPool, or App owners.
- Rendering consumes immutable frame facts and bounded draw/submission packets.
  It mutates only Rendering- or Runtime/Render-owned state.
- Platform input produces typed native/window events. App applies their effects
  without storing a callback pack in the window owner.
- Timing aggregation occurs at an explicit frame boundary. UI visibility and
  render-pass selection cannot decide whether metrics advance.

### Composition-root closure

At closure `Run` may construct/destroy process owners, sequence lifecycle/frame
phases, pump messages, apply typed results, and return the application result. It
may not build domain UI rows, own stress policy, calculate rolling diagnostics,
decide feature business transitions, or expose members through methods defined
in sibling packages.

### Project topology

The final Visual Studio project graph is a DAG. Each first-party production
source belongs to exactly one production project. Shared compilation across
production projects is rejected unless a configuration-specific implementation
need is proved.

The approved target is intentionally small:

- retain `SKULLBONEZ_MATHS`, `SKULLBONEZ_PHYSICS`, and `SKULLBONEZ_UI`;
- create one `SKULLBONEZ_RENDERING` static library after RBS0 proves its exact
  closure and RBS6 can move each Rendering source into it exactly once;
- retain the executable `SKULLBONEZ_CORE` project as the owner of App and the
  remaining Core, Assets, Scene, World, Gameplay, and Runtime sources; and
- preserve Tests and portable CMake reuse without a second drifting source
  manifest.

RBS0 records the Rendering project's source closure, dependency edges,
configuration/PCH/FP contract, compile/link impact, and any stop condition. All
other proposed production project boundaries receive `defer`; RBS0 may not
promote one to `create` without a new explicit owner decision. Runtime packages
still follow the ratified acyclic source DAG while sharing the App project.

---

## Initial Reverse-Boundary Worklist

RBS0 refreshes this table. A row is not complete merely because a header moved.

| Current App seam | Reverse consumers | Target decision to ratify | Required proof |
|---|---|---|---|
| `RunLaunchOptions*` | Startup, Scene, Editor, Diagnostics, DevelopmentTools, Capture | Startup-owned launch policy | CLI/defaults/UI modes/reload overrides |
| `Window` | Input, Editor, Render, Capture, UI | Host owner emits typed events; App applies effects | resize/input/fullscreen/GameUI/ImGui/Automation |
| `RunTimerState` | Camera, Render, Capture, Scene | Frame-metrics owner plus immutable snapshot | cadence/lifecycle/HUD in hidden/GameUI/ImGui modes |
| `ReplayRuntimePackets` | Render/presentation | Lowest source-neutral publication owner | replay/prediction/scrub/archive behavior |
| `InputFrame` | Scene, Capture | Input values or App-only sequencing | input order/scene transition/stress |
| `RunStartupState` | Scene, Capture | Startup-owned capacity/thread defaults | generated reset/reload/stress |
| `ReplayRuntime` | Scene, Capture | App-only sibling composition; typed requests/results | reset/restore/load/stress/visual fidelity |
| `Run` | UI, Capture translation units | Definitions return to App; UI/Capture own operations | UI frame/stress/failure propagation |

## Exception Table

No exception is approved at registration. A temporary row must name exact edge,
owner, reason, behavioral constraint, deletion condition, and deleting phase. No
exception may survive RBS7.

| Edge | Owner | Reason | Behavioral constraint | Deletion condition | Phase |
|---|---|---|---|---|---|
| (none) | - | - | - | - | - |

---

## Phase RBS0 - Ratify The Package And Project DAG

**Goal:** Produce the complete current graph, rule every reverse edge, and approve
the target topology before implementation.

- [ ] Record branch, commit, dirty files, CodeGraph/build freshness, and current
      baseline findings.
- [ ] Inventory all resolved Runtime package include edges and parser limits.
- [ ] Report SCCs, bidirectional pairs, and every non-App -> App edge through a
      repeatable tool, not a frozen policy count.
- [ ] Inventory every `Run` declaration/definition, retained member, and direct
      member accessed by a method outside App.
- [ ] Classify every App header consumed outside App as `move value`, `move
      behavior`, `invert event/result`, `retain App-only`, or exact exception.
- [ ] Inventory `.vcxproj` source ownership, project references, duplicate
      compilation, PCH/flags, and portable CMake manifests.
- [ ] Measure clean/incremental build and link time for Automation, Debug, Profile.
- [ ] Ratify the final package DAG and record the approved minimal VC topology:
      retain Maths/Physics/UI, prove the one Rendering library, keep remaining
      production sources in App, and defer every additional project boundary.
- [ ] Add pre-change witnesses for timing cadence, resize/input, UI switching,
      stress, scene reload, and replay presentation where coverage is insufficient.

**Acceptance:** Every cycle/reverse edge has an owner and disposition; the target
has no unnamed shared bag; the Rendering boundary has a proved source/config/build
decision; every other new project remains deferred; no owner decision is deferred
into implementation.

## Phase RBS1 - Make Package Direction Executable

**Goal:** Make future Runtime cycles and reverse App edges fail mechanically.

- [ ] Add data-driven Runtime DAG/rank rules to `dependency_graph_rules.json`.
- [ ] Extend `check_dependency_graph.py` to report edges, SCCs, and forbidden-edge
      traces without hardcoded package names.
- [ ] Fail every multi-package SCC; add no ceiling or grandfathered cycle budget.
- [ ] Add fixtures for legal downward, reverse App, two-node/long cycles, exact
      exceptions, and parser residuals.
- [ ] Add project-DAG and single-production-owner checks to the build-config owner.
- [ ] Regenerate the AGENTS dependency proof with rule data in the same commit.

**Acceptance:** The pre-separation tree reports the source-confirmed problems;
negative controls fail for the intended reasons; no frozen debt count exists.

## Phase RBS2 - Separate Startup Values And Native Host Ownership

**Goal:** Remove the broad low-level reasons packages include App.

- [ ] Move launch options and startup state to the ratified Startup/value owner,
      updating consumers without App forwarding headers.
- [ ] Split native window handles/dimensions/fullscreen/message interpretation
      from renderer resize and ImGui routing.
- [ ] Emit bounded typed native events; App applies them synchronously to owners.
- [ ] Remove retained `Dx12FrameOwner` and `ImGuiEditorOwner` window borrows after
      the event path is proven.
- [ ] Move source-neutral replay/frame packets to their lowest honest owner.
- [ ] Delete obsolete includes, friends, exceptions, project items, and comments.

**Acceptance:** Startup/Input/Editor/DevelopmentTools/Render no longer include App
for launch/window values; behavior matches across resize, fullscreen, UI surfaces,
and Automation; no callback pack or second window state exists.

## Phase RBS3 - Give Frame Metrics One Owner

**Goal:** Move aggregation out of UI rendering and publish one immutable snapshot.

- [ ] Introduce the ratified App/Diagnostics timing owner with explicit startup,
      frame sample, scene lifecycle, and publication operations.
- [ ] Move only metrics sharing that invariant; leave unrelated timers honest.
- [ ] Update metrics at a fixed boundary independent of UI/pass visibility.
- [ ] Publish an immutable snapshot to UI, Automation, Capture, and Render.
- [ ] Delete Render -> App timing includes and repair ownership comments.
- [ ] Test cadence, half-second aggregation, first sample, scene reset/activation,
      hidden UI, GameUI, and ImGui.

**Acceptance:** Render cannot mutate/include the owner; identical frame sequences
publish identical facts across UI modes; the snapshot contains no owner borrow.

## Phase RBS4 - Separate Operator Projection, Commands, And GPU Submission

**Goal:** Remove the complete UI capability surface from Run and RenderUiText.

- [ ] Classify view projection, UI composition, command application, GPU
      submission, development UI, and process command application separately.
- [ ] Give Runtime/UI a phase owner for
      `snapshot -> compose -> submit -> emit commands -> complete`; retain values
      and cursor only, never subsystem pointers.
- [ ] Have domain owners publish specific detached operator views.
- [ ] Make GameUI and ImGui consume the same immutable facts where applicable.
- [ ] Return typed commands for surface/editor/replay/Tracy/operator actions.
- [ ] Keep Tracy startup, WorkerPool changes, application failure, and top-level
      surface choice in App after commands return.
- [ ] Replace the seven-sibling RenderUiText surface with focused submission
      values; callback ABI records remain renderer-local if required.
- [ ] Answer all five ownership questions and correct false capability claims.
- [ ] Return `Run::RenderOperatorUiPhase` to App once it only sequences/applies.

**Acceptance:** No operation receives every UI capability slice; UI, Render, and
App have separate owners; UI/Render cannot mutate process/domain owners; UI
switching, editor, Replay, Tracy, and failure behavior remain covered.

## Phase RBS5 - Remove Remaining Reverse App Edges

**Goal:** Close Scene, Capture, Automation, Camera, Diagnostics, and other cycles.

- [ ] Process remaining edges in owner-aligned batches from RBS0.
- [ ] Replace Scene access to InputFrame/ReplayRuntime/timers/startup/Window with
      typed load requests/results and App-applied reactions.
- [ ] Move stress policy to a Capture/Diagnostics owner receiving explicit values
      and emitting typed actions; it may not retain/access Run.
- [ ] Keep Replay/Prediction/Planning siblings composed by App through values.
- [ ] Remove all Run definitions outside Runtime/App.
- [ ] Delete exceptions as each edge closes; invert non-App cycles similarly.

**Acceptance:** No non-App Runtime file includes Runtime/App; every SCC has one
package; the exception table is empty; no replacement god owner exists.

## Phase RBS6 - Enforce The Architecture With Minimal VC Topology

**Goal:** Add the one approved Rendering boundary so link topology reinforces the
DAG without turning source packages into a proliferation of projects.

- [ ] Create `SKULLBONEZ_RENDERING` with the exact source closure and acyclic
      project references proved by RBS0; create no other production project.
- [ ] Keep App thin in authority: entry, native startup, composition, application
      orchestration, plus remaining sources whose ownership does not justify a
      separate library. Do not use source-file count as an acceptance measure.
- [ ] Assign every production `.cpp` to exactly one production project.
- [ ] Reconcile filters, configurations, PCH, forced FP contract, warnings,
      feature macros, and reference order in all configurations.
- [ ] Update build-config rulings only for reviewed intentional differences.
- [ ] Reconcile Tests and CMake reuse without drifting source manifests.
- [ ] Compare build/link timings to RBS0; review material regressions.
- [ ] Put project-DAG/single-owner checks into fast and hosted CPU gates.

**Acceptance:** The production graph adds only the approved Rendering library;
project references match package direction; every source has one production
owner; all configurations share required contracts; no library hides a source
cycle; App thinness is demonstrated by authority rather than project size.

## Phase RBS7 - Composition-Root And Terminal Closure

**Goal:** Prove the separation is behavioral, physical, and build-enforced.

- [ ] Re-run package/SCC inventory and reconcile the ratified DAG.
- [ ] Review Run.h, every Run definition, internal headers, transactions, and
      forwarding surfaces as one logical owner.
- [ ] Answer the five ownership questions for every touched aggregate/slice set.
- [ ] Run all seven governance inventories and adjudicate current findings.
- [ ] Run dependency, project/filter, allocation, determinism, related-path, and
      portable-build checks.
- [ ] Run focused Startup, Window/Input, interaction, Scene, UI/UI-stress,
      Capture/Automation, Replay family, renderer, and build tests.
- [ ] Run fast, CPU umbrella, Automation, DX12, one-minute graphics stress,
      replay visual, and relevant performance gates.
- [ ] Obtain independent ownership and project-topology reviews after fixes.
- [ ] Run `tools\agent_validate.bat --plan-completion` once at terminal closure.
- [ ] Record commands, timings, results, project/package graphs, and baselines.

**Terminal acceptance:** App is the sole composition root; package/project graphs
are acyclic; the project topology contains no unapproved production split; no Run
method exists outside App; Rendering mutates no App state;
UI projection, GPU submission, and process commands have separate owners; no bag,
slice union, facade, friend reach-back, or renamed replacement remains; required
gates are green; the exception table is empty; no unauthorized baseline changes.

---

## Validation Map

Rows are cumulative; heavy validation remains in RBS7.

| Area | Focused validation |
|---|---|
| Dependency rules/tooling | self-test, proof, repo scan, negative fixtures, fast |
| Startup/launch | parsing tests, startup probes, UI-mode matrix |
| Host/window | Window/Input tests, Automation, resize/fullscreen, DX12 |
| Frame metrics | owner/lifecycle tests, hidden/GameUI/ImGui, diagnostics |
| Operator UI | UI tests, interaction policy, UI stress, Automation, replay visual, DX12 |
| Scene/Capture | scene load, Capture/Automation, Replay reset/restore |
| Replay family | unit, allocation, spike, artifact, visual gates |
| VC topology | build-config, filters, all configurations, portable CMake |
| Terminal | all focused rows plus `agent_validate --plan-completion` |

Every DX12 source/project move still requires zero warnings, zero validation
errors, and bounded graphics stress. Project movement does not waive launch proof.

### Terminal command set

RBS0 may add focused commands when the final ownership map is broader, but RBS7
must run at least this cumulative set in the repository-prescribed order:

```bat
python tools\check_dependency_graph.py --self-test
python tools\check_dependency_graph.py --check-proof AGENTS.md
python tools\check_dependency_graph.py --repo .
python tools\check_build_config_consistency.py --self-test
python tools\check_build_config_consistency.py --repo .
tools\validate_project_filters.bat
tools\validate_build_all.bat
tools\validate_build.bat Release
tools\validate_runtime_interaction_policy.bat
tools\validate_ui_stress.bat
tools\validate_all_cpu_tests.bat
tools\validate_fast.bat
tools\validate_automation.bat
tools\validate_dx12_renderer.bat
tools\run_graphics_stress.bat 1
tools\validate_replay_visual_fidelity.bat
tools\validate_perf.bat
tools\agent_validate.bat --plan-completion
```

The portable CMake configure/build/test commands from `README.md` are also
mandatory after RBS6 project/source ownership settles.

## Mandatory Review Questions

1. Which concrete owner enforces the moved invariant?
2. Did the batch remove a reverse edge without adding another cycle?
3. Can one operation recover the whole surface through narrow-looking records?
4. Are pointer/span/view/event lifetimes synchronous and explicit?
5. Which focused witness fails if ordering, routing, identity, cadence,
   allocation, or failure behavior changes?
6. Does every moved source compile once under the same FP/exception/RTTI/warning
   contract?
7. Which old header, exception, forwarder, ruling, project item, or comment is
   now deletion-ready?

## Stop Conditions

Stop for owner review if the target owner requires a broad shared package; an
extraction needs stored pointers/callbacks/friends/services; a cycle is merely
moved or absorbed; a value cannot state identity/lifetime/units/completeness;
UI/Render retains mutable process/domain authority; source ownership differs by
configuration without need; a golden refresh is required; or graph/build evidence
is stale.

## Completion Reporting

The closing handoff reports the final package and minimal project graphs, SCC
result, former App reverse edges and owners, Run surface, the Rendering
project/source move, all explicitly deferred project candidates, build timings,
validation, review fixes, baseline disposition, and an empty exception table.
