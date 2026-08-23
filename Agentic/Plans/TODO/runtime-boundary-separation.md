# Runtime Boundary Separation And Project Topology Plan

Date: 2026-08-22
Status: Active by owner direction. 2/8 phases complete; RBS2 ready.
Impact area: `SkullbonezSource/Runtime/`, Runtime-facing Rendering/UI seams,
Visual Studio project topology, dependency enforcement, tests, and documentation
Owner: Runtime architecture, with each moved value or behavior retained by its
concrete subsystem owner
Priority: Binding second plan. RBS0 through RBS7 execute in strict internal
order. `RAGDOLL_PHYSICS` receives scarce-slot and fan-in priority, but is not a
predecessor when phase-local leases and mutable validation outputs are disjoint.
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

- [x] Record branch, commit, dirty files, CodeGraph/build freshness, and current
      baseline findings.
- [x] Inventory all resolved Runtime package include edges and parser limits.
- [x] Report SCCs, bidirectional pairs, and every non-App -> App edge through a
      repeatable tool, not a frozen policy count.
- [x] Inventory every `Run` declaration/definition, retained member, and direct
      member accessed by a method outside App.
- [x] Classify every App header consumed outside App as `move value`, `move
      behavior`, `invert event/result`, `retain App-only`, or exact exception.
- [x] Inventory `.vcxproj` source ownership, project references, duplicate
      compilation, PCH/flags, and portable CMake manifests.
- [x] Measure clean/incremental build and link time for Automation, Debug, Profile.
- [x] Ratify the final package DAG and record the approved minimal VC topology:
      retain Maths/Physics/UI, prove the one Rendering library, keep remaining
      production sources in App, and defer every additional project boundary.
- [x] Add pre-change witnesses for timing cadence, resize/input, UI switching,
      stress, scene reload, and replay presentation where coverage is insufficient.

**Acceptance:** Every cycle/reverse edge has an owner and disposition; the target
has no unnamed shared bag; the Rendering boundary has a proved source/config/build
decision; every other new project remains deferred; no owner decision is deferred
into implementation.

### RBS0 Ratification Evidence - 2026-08-23

#### Frozen starting point and repeatable evidence

- The inventory was frozen on branch `nightrunner-22nd-AUG-26`, commit
  `d6578d1511e7d4560b1c09ee6df28ab0404a023d`, with a clean tracked worktree.
  That branch is the head of open PR 159. No integration branch was created.
- `codegraph status .` reported a current index containing 1,183 files, 36,839
  nodes, and 111,064 edges. Source findings below were confirmed against the
  tracked files rather than accepted from the index alone.
- `python tools/check_dependency_graph.py --repo .
  --report-runtime-graph` is the repeatable current-graph witness added in
  commit `f074f9765b59f6b18f73b87eca7990b0f460e858`. Two consecutive runs
  produced identical SHA-256
  `34f407aefc9c679fca8dbff25913129f252444fea4bb306ad00246431162c351`.
- The report resolves ordinary quoted and angle-bracket include operands using
  the checker's documented local-first textual search. Macro-expanded operands
  and backslash-continued directives remain explicit parser residuals and are
  covered by negative fixtures; the report does not claim compiler-equivalent
  preprocessing.
- `python tools/check_build_config_consistency.py --repo .` reported 1,863
  compile rows, 348 source files, 78 shared source files, 156 reviewed divergent
  file/setting pairs, zero dropped-inheritance rows, and zero blocking
  diagnostics. These are current measurements, not budgets.
- No behavioral or visual golden was refreshed. RBS0 is a documentation and
  ownership gate; the existing owner-controlled baselines remain the acceptance
  oracles for the implementation phases.

The earlier isolated measurement files were produced at `3d48ba0...`, not at
the frozen RBS0 base, and are not used below. Fresh measurements ran at
`f0eba07534e81d1e3445d5844b8219f94d8314aa`; its diff from the frozen
`d6578d1511e7d4560b1c09ee6df28ab0404a023d` contains only plan/session
documentation and the renderer-free UI boundary test, so every production and
project input is byte-identical to the frozen base. Each row ran an MSBuild
solution clean followed by `tools\validate_build.bat <Configuration>` and one
no-change rerun. `Link` is the MSBuild Target Performance total for Core plus
Tests, not a separately timed process:

| Configuration | Clean build/link | Clean `Link` | No-change build | No-change `Link` |
|---|---:|---:|---:|---:|
| Automation | 57.369 s | 3.898 s | 1.431 s | 0.075 s |
| Debug | 53.813 s | 4.856 s | 1.408 s | 0.075 s |
| Profile | 55.927 s | 3.726 s | 1.417 s | 0.074 s |

#### Current Runtime graph

The current report resolves 300 Runtime source-bearing files into 21 packages,
144 directed cross-package edges, and 585 concrete include sites. It finds one
19-package cyclic component containing every package except `Runtime/Debug` and
`Runtime/RuntimeFrameViews.h`; those two are singleton components. There are 36
bidirectional pairs and 35 non-App to App include sites.

The bidirectional pairs are, in report order:

```text
App <-> Automation, Camera, Capture, DevelopmentTools, Diagnostics, Editor,
        Input, Render, Scene, Startup, UI
Automation <-> Capture, Diagnostics, Scene
Camera <-> Direction, Input, Interaction, Scene
Capture <-> Diagnostics
Diagnostics <-> Render, Replay, Scene
Editor <-> Replay, Scene, Tools
Input <-> Replay, Scene
Interaction <-> Render, Simulation
Planning <-> Render, UI
Render <-> Replay, Scene
Replay <-> Tools
Scene <-> Simulation, Tools
```

#### Binding target Runtime DAG

The target uses this total rank. A package may depend only on an earlier rank;
`Runtime/App` is therefore the sole composition root. Rank is a direction rule,
not blanket permission to add a new dependency: RBS1 retains closed allow rows
and admits a new downward edge only when a concrete owner and source need are
reviewed.

| Rank | Package | Binding owner contract |
|---:|---|---|
| 0 | `Runtime/Debug` | Leaf debug values and assertions; no Runtime owner reach-back. |
| 1 | `Runtime/RuntimeFrameViews.h` | Value-only frame views and forward declarations; no Runtime include. |
| 2 | `Runtime/Startup` | Native-host ownership, launch parsing, and detached startup values; no Scene or Replay behavior. |
| 3 | `Runtime/Camera` | Camera modes and camera-owned state; scene targets arrive as detached inputs. |
| 4 | `Runtime/Interaction` | Workspace, gesture, pick, and typed interaction command policy; Camera modes are stable lower values. |
| 5 | `Runtime/Input` | Device samples, immutable bindings, and frame-local routing values; no Scene, Replay, or App authority. |
| 6 | `Runtime/Simulation` | Timestep and physics-advance policy consuming detached Interaction values; no Scene lifecycle ownership. |
| 7 | `Runtime/Scene` | Scene lifecycle/world coordination consuming the lower host, camera, input, and simulation policies. |
| 8 | `Runtime/Replay` | Recorded-data infrastructure consuming detached lower-owner snapshots and publishing replay values. |
| 9 | `Runtime/Prediction` | Future-simulation production and publication over Scene/Replay values; no Editor or Tools reach-back. |
| 10 | `Runtime/Planning` | Operator product state over Prediction/Replay; publishes detached views and commands. |
| 11 | `Runtime/Tools` | Generic runtime tool values and presentation helpers; no Editor owner borrow. |
| 12 | `Runtime/Editor` | Editing behavior over lower Scene/Replay/Tools values; emits requests instead of reaching into Capture or Diagnostics. |
| 13 | `Runtime/Render` | Runtime render orchestration and generic submission over immutable lower-owner packets. |
| 14 | `Runtime/Diagnostics` | Aggregates and publishes detached diagnostics from lower owners. |
| 15 | `Runtime/DevelopmentTools` | Process development-tool behavior consuming lower snapshots and commands. |
| 16 | `Runtime/Capture` | Capture and stress policy consuming snapshots and emitting bounded requests/results. |
| 17 | `Runtime/Direction` | Look Lab/demo direction over Capture, Tools, Scene, and Camera values. |
| 18 | `Runtime/Automation` | Input, capture, and direction automation over lower-owner APIs. |
| 19 | `Runtime/UI` | Operator projection/composition over detached facts; emits typed commands only. |
| 20 | `Runtime/App` | Process construction, top-level sequencing, result application, and shutdown. |

This rank partitions every current directed edge arithmetically: 98 edges/467
include sites point to an earlier rank, while 46 edges/118 sites point upward.
Rank alone never grants `retain`: the closed target/header projection below
separately rejects downward concrete-owner and capability-bag borrows that
violate a package's owner contract. The counts identify current evidence only
and are not closure budgets. Reverse App sites use the more precise header
disposition table below.

#### Exact rank-downward header projection

An independent source-confirmed audit reran on integrated commit `029c837e...`.
The Runtime reporter remained 300 files, 144 edges, and 585 sites; the current
CodeGraph index was also current at 1,183 files, 36,911 nodes, and 111,420
edges. The 467 arithmetically downward sites divide semantically into 178 valid
App composition sites, 131 valid non-App seams, and 158 sites across 105
source-package/target-header pairs that require deletion, movement, splitting,
or inversion. This partition is a current measurement, never an allowance or
budget.

App is the sole construction and sequencing root, so its current 178 downward
sites are valid only for these 75 exact target headers. RBS1 encodes exact
files, not package-prefix permission; `Runtime/Debug` has no current App
permission.

| Target package | Exact App target headers |
|---|---|
| Automation | `InteractionAutomationController.h`, `InteractionAutomationRecorder.h`, `InteractionRecordingBrowser.h`, `RuntimeValidationHarness.h` |
| Camera | `AttachedCameraController.h`, `CameraCollection.h`, `CameraControlState.h`, `RuntimeCameraMode.h` |
| Capture | `CaptureSystem.h`, `RuntimeStressController.h` |
| DevelopmentTools | `ImGuiEditorLayoutPolicy.h`, `ImGuiEditorOwner.h` |
| Diagnostics | `DiagnosticsPhysicsUI.h`, `DiagnosticsRuntime.h`, `OverlayDebugState.h`, `RuntimeDiagnostics.h`, `RuntimeOverlayDiagnostics.h`, `SceneMemoryDiagnostics.h` |
| Direction | `DemoDirectorPlayback.h`, `LookLabController.h` |
| Editor | `EditorTools.h` |
| Input | `Input.h`, `InputController.Bindings.h`, `InputController.h`, `InputRouter.h` |
| Interaction | `OperatorCommandTransaction.h`, `RuntimeInteractionCommands.h`, `RuntimeInteractionController.h`, `RuntimePickService.h` |
| Planning | `ContinuousOrbitalForecast.h`, `ReplayOverlayPackets.h`, `ReplayOverlayRenderer.h`, `ReplayPlanningRuntime.h` |
| Prediction | `ReplayPrediction.h`, `ReplayPredictionRetainedGeometry.h`, `ReplayPredictionRetainedMemory.h`, `ReplayPredictionScheduling.h`, `TrajectoryStore.h` |
| Render | `RenderDefaultsStore.h`, `RenderModelFramePublisher.h`, `RuntimeRenderHost.h`, `RuntimeRenderer.h` |
| Replay | `ReplayAuthoring.h`, `ReplayCoordination.h`, `ReplayIdentity.h`, `ReplayOverlayLayout.h`, `ReplayPresentation.h`, `ReplayPresentationPackets.h`, `ReplayProbeState.h`, `ReplayRecorder.h`, `ReplayRestoreService.h`, `ReplayRestoreTransactions.h`, `ReplayRetainedMemory.h`, `ReplayScrubber.h`, `ReplayTimeline.h`, `ReplayV2Artifact.h`, `ReplayVisualPacket.h`, `ReplayVisualPacketFingerprint.h` |
| top-level value | `RuntimeFrameViews.h` |
| Scene | `SceneCinematicPolicy.h`, `SceneController.h`, `SceneGeneratedControlTransaction.h`, `SceneGeneratedSetup.h`, `SceneLifecycle.h`, `SceneLoadTransaction.h`, `SceneSaveOperations.h`, `SceneWorld.h` |
| Simulation | `SimulationSystem.h` |
| Startup | `StartupCommandLine.h`, `StartupCrashLogging.h`, `StartupLaunchResolution.h`, `StartupProbeHarnesses.h` |
| Tools | `RuntimeFileWriter.h`, `RuntimeTools.h` |
| UI | `RuntimeViewModel.h` |

The exact valid non-App projection is below. Counts are current include sites;
RBS1 admits only the named target file for the named source package. The mixed
Tools and Automation cases remain subject to the symbol/site dispositions in
the repair table rather than turning the exact file into a blanket semantic
approval.

| Source package | Exact allowed target headers and current site counts |
|---|---|
| Interaction | `Camera/RuntimeCameraMode.h` (2) |
| Input | `Camera/CameraCollection.h` (1), `Camera/CameraControlState.h` (1), `Interaction/RuntimeInteractionController.h` (2) |
| Scene | `Camera/AttachedCameraController.h` (1), `Camera/CameraCollection.h` (4), `Camera/CameraControlState.h` (3), `Input/Input.h` (1), `Input/InputRouter.h` (1), `Simulation/SimulationSystem.h` (2) |
| Replay | `Camera/RuntimeCameraMode.h` (1) |
| Prediction | `Replay/ReplayIdentity.h` (4), `Replay/ReplayPathPackets.h` (4), `Replay/ReplayRetainedMemory.h` (1), `Replay/ReplayTrajectoryPackets.h` (2), `Replay/ReplayVisualPacket.h` (4) |
| Planning | `Interaction/RuntimePickService.h` (1), `Prediction/ContinuousPredictionProducer.h` (1), `Prediction/ReplayPredictionView.h` (4), `Replay/ReplayAuthoringPackets.h` (2), `Replay/ReplayCapturePackets.h` (2), `Replay/ReplayPathPackets.h` (1), `Replay/ReplayPresentationPackets.h` (1), `Replay/ReplayTimelinePackets.h` (1) |
| Tools | `Camera/CameraCollection.h` (1), `Camera/RuntimeCameraMode.h` (1), `Input/InputRouter.h` (1), `Interaction/RuntimeInteractionCommands.h` (1), `Interaction/RuntimeInteractionController.h` (2), `Replay/ReplayEventCommand.h` (1), `Replay/ReplayToolPackets.h` (1), `Replay/ReplayVisualPacket.h` (1), `Scene/SceneLifecycle.h` (1), `Scene/SceneSessionState.h` (1), `Scene/SceneWorld.h` (1) |
| Editor | `Camera/CameraCollection.h` (3), `Camera/RuntimeCameraMode.h` (1), `Input/InputController.h` (3), `Input/InputRouter.h` (2), `Interaction/RuntimeInteractionCommands.h` (2), `Interaction/RuntimeInteractionController.h` (3), `Interaction/RuntimePickService.h` (2), `Replay/ReplayAuthoringPackets.h` (1), `Scene/SceneAuthoredSetup.h` (2), `Scene/SceneController.h` (4), `Scene/SceneControllerState.h` (2), `Scene/SceneEntityStore.h` (2), `Scene/SceneGeneratedSetup.h` (1), `Scene/SceneSaveOperations.h` (1), `Scene/SceneSessionState.h` (4), `Scene/SceneWorld.h` (4), `Tools/RuntimeFileWriter.h` (1) |
| Render | `Planning/ReplayOverlayPackets.h` (1), `Replay/ReplayVisualPacket.h` (1), `RuntimeFrameViews.h` (1) |
| Diagnostics | `Scene/SceneLifecycle.h` (1) |
| DevelopmentTools | `Planning/ReplayOverlayPackets.h` (1) |
| Capture | `Scene/SceneLoadRequest.h` (2) |
| Direction | `Tools/RuntimeFileWriter.h` (1) |
| Automation | `Camera/RuntimeCameraMode.h` (3), `Capture/CaptureController.h` (1, only `InteractionAutomationController.cpp:62`), `DevelopmentTools/ImGuiEditorOwner.h` (1), `Direction/DemoDirector.h` (1), `Direction/DemoDirectorPlayback.h` (1), `Input/Input.h` (1), `Interaction/RuntimePickService.h` (1), `Prediction/ReplayPrediction.h` (1), `Prediction/ReplayPredictionArchive.h` (1), `Prediction/ReplayPredictionDrawing.h` (1), `Prediction/ReplayPredictionPackets.h` (1), `Replay/ReplayPresentation.h` (1), `Replay/ReplayTimelinePackets.h` (1), `Replay/ReplayV2Artifact.h` (1), `Replay/ReplayVisualPacket.h` (1), `Replay/ReplayVisualPacketFingerprint.h` (1), `RuntimeFrameViews.h` (1), `Scene/SceneAutomationGateConfiguration.h` (1), `Scene/SceneLifecycle.h` (1), `Scene/SceneSleepingDynamicBodyGatePolicy.h` (1), `Tools/RuntimeFileWriter.h` (2) |
| UI | `Planning/ReplayOverlayPackets.h` (1), `RuntimeFrameViews.h` (1) |

The App table contains 75 unique headers and the non-App count terms sum to
131. A `git ls-files SkullbonezSource/Runtime` reconciliation resolves every
one of the 163 distinct header spellings cited across this complete downward
projection; no target is a guessed future path.

`Debug`, `RuntimeFrameViews.h`, `Startup`, and `Camera` have no valid current
non-App downward site. `Simulation` has none after its misplaced
`PhysicsAdvanceState` include is split. The cold Automation proof-only
`ReplayPrediction.h` and `ReplayPresentation.h` uses reconstruct immutable
offline evidence and do not borrow the live process owners.

The remaining 158 downward sites are binding repairs:

| Source | Exact current repair projection | Owner disposition | Phase |
|---|---|---|---|
| Simulation | `Interaction/RuntimeInteractionController.h` from `SimulationSystem.h:34` | The include exists only for `PhysicsAdvanceState`; move that detached value to an Interaction value/command header. | RBS5 |
| Scene | `Debug/CollisionVisualizer.h` from `SceneWorld.cpp:45`; `Debug/PhysicsDebugVisualizer.h` from `SceneWorld.cpp:46` | Debug rank zero permits leaf values/assertions, not cached GPU owners. Scene publishes CPU debug packets; Render owns GPU state and submission. | RBS4 |
| Replay | `Camera/CameraCollection.h` from `ReplayRecorder.cpp:31`, `ReplayRestoreService.h:40`; `Input/InputRouter.h` from `ReplayAuthoringCauseTreeInput.cpp:33`, `ReplayAuthoringVelocity.cpp:39`; `Interaction/RuntimeInteractionController.h` from `ReplayAuthoringCauseTreeInput.cpp:34`, `ReplayOverlayLayout.h:37`, `ReplayPresentation.h:40`, `ReplayScrubber.h:25`; `Interaction/RuntimePickService.h` from `ReplayPresentation.cpp:28`; `Scene/SceneEntityStore.h` from `ReplayAuthoringVelocity.cpp:40`, `ReplayPresentation.cpp:29`, `ReplayRecorder.cpp:38`; `Scene/SceneSessionState.h` from `ReplayRestoreService.h:44`; `Scene/SceneWorld.h` from `ReplayRestoreService.h:43` | Replay consumes detached lower-owner facts. Operator authoring and picking move to Planning; App composes live lower owners. Only `Camera/RuntimeCameraMode.h` is a current lower seam. | RBS4, RBS5 |
| Prediction | `Replay/ReplayAuthoring.h` from `ReplayAuthoringCauseTree.cpp:34`, `ReplayCauseFocusSubmission.cpp:26`, `ReplayPrediction.cpp:51`, `ReplayPredictionDrawing.cpp:39`; `Replay/ReplayCoordination.h` from `ReplayAuthoringCauseTree.cpp:35`; `Replay/ReplayOverlayLayout.h` from `ReplayPrediction.cpp:52`, `ReplayPredictionPublication.cpp:25`, `ReplayPredictionTopologyPublication.cpp:27`; `Replay/ReplayPresentation.h` from `ReplayAuthoringCauseTree.cpp:38`, `ReplayPredictionPresentation.h:32`; `Replay/ReplayPresentationSubmission.h` from `ReplayCauseFocusSubmission.cpp:25`, `ReplayPredictionDrawing.cpp:43`; `Replay/ReplayRecorder.h` from `ReplayPrediction.h:37`; `Replay/ReplayScrubber.h` from `ReplayPrediction.cpp:56`, `ReplayPredictionPublication.cpp:27`, `ReplayPredictionTopologyPublication.cpp:28`; `Scene/SceneEntityStore.h` from `ReplayCauseFocusSubmission.cpp:32`, `ReplayPrediction.cpp:49`, `ReplayPredictionDrawing.cpp:47`, `ReplayPredictionPublication.cpp:23`, `ReplayPredictionTopologyPublication.cpp:25` | These are mutable sibling or Scene owners. App composes siblings through packets. Prediction retains only the exact Replay value headers listed above. | RBS4, RBS5 |
| Planning | `Input/InputRouter.h` from `ReplayPlanningRuntime.cpp:23`; `Interaction/RuntimeInteractionController.h` from unused include `ReplayOverlayPackets.h:35`; `Prediction/ReplayPrediction.h` from `ReplayPlanningRuntime.h:34`; `Prediction/ReplayPredictionSolverEvidenceStore.h` from `ReplayCauseInspection.h:59`; `Replay/ReplayAuthoring.h` from `ReplayOverlayRenderer.h:34`; `Replay/ReplayOverlayLayout.h` from `ReplayCauseInspection.cpp:57`, `ReplayOverlayRenderer.cpp:37`; `Replay/ReplayPresentation.h` from `ReplayOverlayRenderer.h:37`, `ReplayPlanningRuntime.h:35`; `Replay/ReplayRecorder.h` from `ReplayCauseInspection.cpp:58`; `Replay/ReplayScrubber.h` from `ReplayOverlayRenderer.h:38`; `Scene/SceneEntityStore.h` from `ContinuousOrbitalForecast.cpp:33`, `ReplayPlanningRuntime.cpp:25` | `ReplayPlanningRuntime.h:62-88` falsely claims lower values while accepting live Prediction, Input, and Scene owners. Split immutable evidence/view packets; App applies cross-owner effects. | RBS4, RBS5 |
| Tools | Unused `Scene/SceneControllerState.h` from `RuntimeTools.cpp:37`; mixed `RuntimeTools.h:599-725` owns `RunEditorPlacementState`, `EditorTracer`, and editor mutation methods at lines 673-715 | Delete the unused include and move Editor symbols/behavior to Editor. The remaining Camera/Input/Interaction/Scene lower-owner headers are allowed only for launcher/mouse-pickup behavior after that extraction; the whole Tools surface is not retained. | RBS5 |
| Editor | `Tools/RuntimeTools.h` from `EditorGizmoTools.cpp:27`, `EditorHistory.cpp:28`, `EditorInteractionTools.cpp:33`, `EditorObjectPlacement.cpp:33`, `EditorOverlayTools.cpp:25`, `EditorTools.cpp:28`, `EditorTracer.cpp:42`, `LauncherTools.cpp:26`, `MousePickupTools.cpp:29` | The broad class is the misplaced Editor owner/capability bag. Move Editor state/behavior and replace residual generic-tool uses with focused APIs. | RBS5 |
| Render | `Camera/CameraCollection.h` from `RuntimeRenderer.cpp:35`; `Camera/CameraControlState.h` from `RuntimeRenderer.cpp:36`, `UiTextPass.cpp:47`; `Debug/BroadphaseVisualizer.h` from `RuntimeRenderPasses.cpp:42`; `Debug/CollisionVisualizer.h` from `RuntimeRenderPasses.cpp:40`, `RuntimeRenderer.cpp:42`; `Debug/PhysicsDebugVisualizer.h` from `RuntimeRenderPasses.cpp:41`; `Input/InputController.h` from `UiTextPass.cpp:46`; `Interaction/RuntimeInteractionController.h` from `RuntimeRenderPasses.h:51`; `Planning/ReplayOverlayRenderer.h` from `UiTextPass.cpp:60`; `Scene/SceneCinematicPolicy.h` from `RuntimeRenderPasses.cpp:46`, `RuntimeRenderer.cpp:47`; `Scene/SceneController.h` from `RuntimeRenderer.cpp:48`; `Scene/SceneControllerState.h` from `UiTextPass.cpp:52`; `Scene/SceneSessionState.h` from `UiTextPass.cpp:53`; `Scene/SceneTerrain.h` from `RenderResourceLifecycle.cpp:25`, `RuntimeRenderPasses.cpp:45`, `RuntimeRenderer.cpp:43`; `Scene/SceneWorld.h` from `RenderModelFramePublisher.cpp:28`, `UiTextPass.cpp:54`; `Tools/RuntimeTools.h` from `RuntimeRenderHost.h:33`, `RuntimeRenderPasses.cpp:44`, `RuntimeRenderer.cpp:41`, `UiTextPass.cpp:55` | Render may retain only `Planning/ReplayOverlayPackets.h`, `Replay/ReplayVisualPacket.h`, and `RuntimeFrameViews.h`. `RuntimeRenderHost.h:101-121` and `RuntimeRenderer.h:77-206,435-445` expose live owner/capability bags rather than immutable packets. | RBS4; resource residue RBS5 |
| Diagnostics | `Debug/BroadphaseVisualizer.h`, `Debug/CollisionVisualizer.h`, and `Debug/PhysicsDebugVisualizer.h` from `RuntimeOverlayDiagnostics.h:37,39,40`; `Input/InputController.h` from `DiagnosticsRuntime.cpp:36`; `Render/RuntimeRenderFrameValues.h` from `RuntimeOverlayDiagnostics.cpp:36`; `Replay/ReplayRecorder.h` from `RuntimeDiagnostics.cpp:34`; `Scene/SceneController.h` from `DiagnosticsRuntime.cpp:39`; `Scene/SceneEntityStore.h` from `SceneMemoryDiagnostics.cpp:26`; `Scene/SceneSessionState.h` from `DiagnosticsRuntime.cpp:38`, `RuntimeDiagnostics.cpp:35`; `Scene/SceneWorld.h` from `RuntimeOverlayDiagnostics.cpp:38` | Only `Scene/SceneLifecycle.h` is a current value seam. Split the needed render policy from mutable refs at `RuntimeRenderFrameValues.h:128-149`; `DiagnosticsRuntime.cpp:540-590` must stop applying Scene/Capture/UI effects, and GPU visualizers at `RuntimeOverlayDiagnostics.h:83-125` move to Render. | RBS3, RBS4, RBS5 |
| DevelopmentTools | Unused `Input/InputRouter.h` from `ImGuiEditorOwner.cpp:48` | Delete the include. `Planning/ReplayOverlayPackets.h` is the genuine detached seam. | RBS4 |
| Capture | From `RuntimeStressController.cpp`, `Camera/AttachedCameraController.h:26`, `Camera/CameraControlState.h:35`, `Diagnostics/DiagnosticsRuntime.h:29`, `Diagnostics/OverlayDebugState.h:36`, `Diagnostics/RuntimeOverlayDiagnostics.h:24`, `Diagnostics/SceneMemoryDiagnostics.h:30`, `Input/InputRouter.h:28`, `Interaction/RuntimeInteractionController.h:40`, `Render/RenderDefaultsStore.h:34`, `Render/RuntimeRenderHost.h:31`, `Render/RuntimeRenderer.h:32`, `Scene/SceneCinematicPolicy.h:45`, `Scene/SceneController.h:41`, `Scene/SceneGeneratedControlTransaction.h:44`, `Scene/SceneLoadTransaction.h:42`, `Simulation/SimulationSystem.h:49`, `Tools/RuntimeTools.h:46` | `RuntimeStressController.h:77-93` is a one-call capability-slice set despite its no-retention claim. Capture consumes snapshots and emits bounded requests/results. Only `Scene/SceneLoadRequest.h` remains. | RBS5 |
| Direction | `Camera/CameraCollection.h` from `DemoDirectorPlayback.h:33`; `Camera/CameraControlState.h` from `DemoDirectorPlayback.h:31`; `Capture/CaptureController.h` from `LiveStyleController.cpp:23`; `Scene/SceneCinematicPolicy.h` from `DemoDirectorPlayback.cpp:43`, `LiveStyleController.cpp:25`, `LiveStyleController.h:31`; `Scene/SceneController.h` from `DemoDirectorPlayback.cpp:44`, `LiveStyleController.cpp:21`; `Scene/SceneWorld.h` from `DemoDirectorPlayback.cpp:45` | Tick signatures at `DemoDirectorPlayback.h:81-85` and `LiveStyleController.h:51-57` mutate concrete lower owners. Direction emits Camera/Scene/Capture commands/results. Only `Tools/RuntimeFileWriter.h` remains. | RBS5 |
| Automation | Deny `Camera/AttachedCameraController.h` from unused include `InteractionAutomationController.cpp:61`; `Camera/CameraCollection.h` from `InteractionAutomationController.cpp:67`; `Camera/CameraControlState.h` from `InteractionAutomationController.cpp:66`, `InteractionAutomationReportWriter.cpp:51`; unused `Diagnostics/RuntimeOverlayDiagnostics.h` from `InteractionAutomationController.cpp:60`; `Editor/EditorTools.h` from `InteractionAutomationController.cpp:76`, `InteractionAutomationReportWriter.cpp:44`; `Input/InputRouter.h` from `InteractionAutomationController.cpp:63`, `InteractionAutomationRecorder.cpp:24`; `Interaction/RuntimeInteractionController.h` from `InteractionAutomationController.cpp:64`, `InteractionAutomationReportWriter.h:40`; `Planning/ContinuousOrbitalForecast.h` from `InteractionAutomationController.cpp:58`; `Planning/ReplayCauseInspection.h` from `InteractionAutomationController.cpp:79`, `InteractionAutomationReportWriter.cpp:47`; unused `Planning/ReplayOverlayRenderer.h` from `InteractionAutomationReportWriter.cpp:48` and `.h:41`; `Replay/ReplayCoordination.h` from `InteractionAutomationController.h:67`; `Replay/ReplayOverlayLayout.h` from `InteractionAutomationController.cpp:78`; `Replay/ReplayOverlaySurface.h` from `InteractionAutomationController.cpp:77`; `Scene/SceneController.h` from `InteractionAutomationController.cpp:71`; `Scene/SceneSessionState.h` from `InteractionAutomationController.cpp:72`, `InteractionAutomationReportWriter.cpp:53`; `Scene/SceneWorld.h` from `InteractionAutomationReportWriter.cpp:54`; `Tools/RuntimeTools.h` from `InteractionAutomationController.cpp:69`, `InteractionAutomationReportWriter.cpp:55`, `InteractionAutomationReportWriter.h:47`. Also deny cross-owned `Capture/GraphicsStressController.h` from `RuntimeValidationHarness.h:42` and `Direction/LiveStyleController.h` from `RuntimeValidationHarness.h:44`. | Split pure layout/view and `ReplayFrameIntent` packets. Operator presentation moves to Planning. `RuntimeValidationHarness.h:115-151` must stop owning/exposing Capture/Direction controllers. `CaptureController.h` is mixed: only `InteractionAutomationController.cpp:62` is the valid public `SaveScreenshot` command; `RuntimeValidationHarness.cpp:42` is a one-call courier deleted when Direction emits a capture request. ImGuiEditor is valid because Automation uses its public command/status API. Broad Tick signatures at `InteractionAutomationController.h:465-473` become detached Automation views/commands. | RBS4, RBS5 |
| UI | `Automation/RuntimeValidationHarness.h` from `OperatorEditorFrameComposer.cpp:33`; `Capture/CaptureController.h` from `RuntimeViewModel.cpp:22`; `Capture/CaptureSystem.h` from `OperatorEditorFrameComposer.cpp:39`; `Diagnostics/RuntimeOverlayDiagnostics.h` from `OperatorEditorFrameComposer.cpp:32`; `Editor/EditorTools.h` from `OperatorEditorFrameComposer.cpp:40`; `Scene/SceneController.h` from `RuntimeViewModel.cpp:24` | `RuntimeViewModel.cpp:33-57` reads Scene/Capture/Physics owners and `OperatorEditorFrameComposer.cpp:246,289-304` aliases the full App surface. UI consumes detached facts and emits commands; only `Planning/ReplayOverlayPackets.h` and `RuntimeFrameViews.h` remain. | RBS4 |

RBS1 therefore needs closed `allowed_exact_target_files` per source package and
an exact source-file/target-file rule for mixed edges such as Automation to
`CaptureController.h`. Package-prefix permission would re-admit the capability
bags above. Include rules cannot prove the symbol-level mixed Tools edge, so
RBS5 owns the named Editor symbol movement as a binding disposition.

#### Current rank-upward repair projection

| Current source | Forbidden current targets and include-site counts | Owner disposition | Deleting phase |
|---|---|---|---|
| `Startup` | App (3), Replay (2), Scene (3) | Startup parses ordered raw CLI tokens and presence values only. Before owner construction, App asks Replay to resolve/validate retention defaults and asks Prediction to validate/resolve horizon values, then applies Scene/Replay effects. Existing error/status strings, directive order, and registered bounds remain exact; no literal or validator is copied into Startup. | RBS2 |
| `Camera` | App (1), Direction (1), Input (2), Interaction (1), Scene (4) | Camera receives detached targets and input decisions; Direction, Input, Interaction, and Scene call Camera-owned operations. | RBS2, RBS3, RBS5 |
| `Interaction` | Render (2), Scene (4), Simulation (1) | Interaction emits typed commands; the concrete owner applies each command and App sequences cross-owner effects. | RBS4, RBS5 |
| `Input` | App (1), Replay (1), Scene (1) | Input emits binding/action values; App translates Replay commands and Scene activation events. | RBS2, RBS5 |
| `Simulation` | Scene (1) | Scene owns lifecycle generation; App maps the published Scene event/generation into a Simulation-owned lifecycle input and invokes Simulation. Generated-scene reset is therefore App sequencing of a Scene fact, not a Simulation borrow of `SceneLifecycle.h`. | RBS5 |
| `Scene` | App (6), Automation (4), Diagnostics (7), Editor (3), Render (3), Tools (3) | Scene publishes lifecycle, model, and world snapshots; higher consumers cannot be Scene-owned dependencies. | RBS2, RBS3, RBS4, RBS5 |
| `Replay` | Diagnostics (2), Editor (3), Render (1), Tools (5), UI (1) | Replay publishes source-neutral packets and commands; presentation, diagnostics, and tools consume them above Replay. | RBS4, RBS5 |
| `Prediction` | Editor (5), Tools (3) | Prediction owns future-data construction and publishes immutable geometry/facts; Editor and Tools consume them. | RBS4, RBS5 |
| `Planning` | Render (1), UI (1) | Planning publishes product views/commands; Runtime/UI and Render submit them without Planning reaching upward. | RBS4 |
| `Tools` | Editor (4) | Editor-owned behavior moves to Editor; reusable value/algorithm seams remain Tools-owned. | RBS5 |
| `Editor` | App (2), Capture (1), Diagnostics (1) | Editor emits typed requests and detached state; App dispatches them to Capture and Diagnostics. | RBS2, RBS5 |
| `Render` | App (4), DevelopmentTools (1), Diagnostics (6), UI (2) | Render consumes packets and publishes render metrics/results; UI, Diagnostics, and DevelopmentTools remain higher consumers. | RBS2, RBS3, RBS4, RBS5 |
| `Diagnostics` | App (1), Automation (1), Capture (1) | Diagnostics publishes one immutable snapshot; App, Automation, and Capture consume it. | RBS2, RBS3, RBS5 |
| `DevelopmentTools` | App (1) | Development-tool mode is a Startup value and App applies process-level surface choice. | RBS2 |
| `Capture` | App (7), Automation (1) | Capture owns stress/capture policy and emits results; Automation consumes those results and App applies process effects. | RBS2, RBS3, RBS5 |
| `Automation` | App (7) | Automation drives public lower-owner commands and returns a bounded turn result to App. | RBS2, RBS3, RBS5 |
| `UI` | App (2) | UI composes detached views and returns commands; `Run` and native Window remain unavailable. | RBS2, RBS4, RBS5 |

No Runtime package exception is approved. RBS1 encodes this rank and the
closed-world allow projection; RBS2-RBS5 remove the current violations rather
than grandfathering them.

#### Reverse App headers and `Run` surface

The 35 reverse App include sites resolve to nine headers:

| App header | Current sites and consumers | Binding classification and target owner | Phase |
|---|---|---|---|
| `InputFrame.h` | 3: Automation, Capture, Scene | `move value`: frame facts move to Input; sequencing stays App-only. | RBS2, RBS5 |
| `ReplayRuntime.h` | 2: Capture, Scene | `retain App-only`: sibling composition stays App; replace both includes with typed requests/results owned by the caller or Replay. | RBS5 |
| `ReplayRuntimePackets.h` | 2: Automation, Render | `move value`: source-neutral publication packets move to Replay. | RBS2 |
| `Run.h` | 3: Automation, Capture, UI | `move behavior`, then `retain App-only`: UI/Capture own their operations and Automation uses typed App input/results. | RBS4, RBS5 |
| `RunLaunchOptions.h` | 9: Automation, Capture, DevelopmentTools, Diagnostics, Editor, Scene, Startup | `move value`: Startup owns launch policy. | RBS2 |
| `RunLaunchOptions.Renderer.h` | 1: Startup | `move value`: merge the renderer projection into the Startup-owned launch contract. | RBS2 |
| `RunStartupState.h` | 2: Capture, Scene | `move value`: Startup owns capacity/thread defaults and publishes them. | RBS2 |
| `RunTimerState.h` | 6: Automation, Camera, Capture, Render, Scene | `move behavior`: Diagnostics owns cadence/aggregation and publishes an immutable timing snapshot. | RBS3 |
| `Window.h` | 7: Automation, Capture, Editor, Input, Render, Scene, UI | `invert event/result`: Startup owns the native host; App applies typed resize/input/fullscreen events to concrete owners. | RBS2 |

`Run.h` currently declares 46 callable operations: four inline definitions
(`RequireRenderer`, `Renderer` mutable/const, and `BackbufferCapture`) plus 42
out-of-line definitions. Forty definitions live under `Runtime/App`; two violate the target:
`Run::RenderOperatorUiPhase` in
`Runtime/UI/OperatorEditorFrameComposer.cpp` and `Run::RunUIStressActions` in
`Runtime/Capture/RuntimeStressController.cpp`.

The out-of-line inventory is:

```text
Runtime/App/Run.cpp:
  Run, ~Run, BindRenderBackend, ApplyStartupOverrides,
  ApplyStartupPredictionRequest, Initialise, ApplyDevelopmentUiMode,
  SelectDevelopmentUiSurface, LastSceneLoadResult, RunSceneLoadOnly
Runtime/App/InputFrame.cpp:
  ApplyInputCommandsPhase
Runtime/App/InputFrameExecution.cpp:
  PublishLookLabStatusView, ApplyLookLabSeed, BeginLookLabSave,
  CancelPendingLookLabSave, PrepareSceneScopedOwnersForTransition, RunInputPhase
Runtime/App/RunFrame.cpp:
  PumpFrameMessages, BeginFrameTurn, AdvanceInteractionRecordingBoundary,
  CaptureInteractionRecordingTurn, BeginFrameDiagnosticsPhase,
  RunAutomationAndInputPhase, RunSimulationPhase, PrepareRenderPhase,
  PublishRenderModelsPhase, RenderWorldPhase, RunPostDrawDiagnosticsPhase,
  CompleteLookLabPostRenderCaptures, FinishFrameWorkPhase, PresentFramePhase,
  CompleteFramePhase, Execute, TickPhysics, AfterPhysicsStep, TickScreenshots,
  TickAutoCycle, TickSceneAdvance, UpdateLogic
Runtime/App/RunRender.cpp:
  Render
Runtime/UI/OperatorEditorFrameComposer.cpp:
  RenderOperatorUiPhase
Runtime/Capture/RuntimeStressController.cpp:
  RunUIStressActions
```

The maximum-feature build retains 35 members:

```text
m_resultDiagnostics, m_window, m_workerPool, m_config, m_profiler,
m_tracyClientOwner, m_assets, m_sceneController, m_lastSceneLoadResult,
m_skipExecute, m_launchOptions, m_startupPredictionApplied, m_applicationExit,
m_renderDefaults, m_startup, m_diagnosticsRuntime, m_timers, m_inputRouter,
m_interaction, m_interactionRecorder, m_interactionAutomation, m_camera,
m_attachedCamera, m_lookLab, m_simulation, m_replayRuntime,
m_continuousForecast, m_runtimeTools, m_imguiEditor, m_operatorUi,
m_overlayDiagnostics, m_validationHarness, m_backbufferCapture, m_renderer,
m_shaderDevelopment
```

The two non-App definitions demonstrate the capability leak directly.
`RenderOperatorUiPhase` accesses 21 members:
`m_applicationExit`, `m_assets`, `m_attachedCamera`, `m_camera`, `m_config`,
`m_continuousForecast`, `m_diagnosticsRuntime`, `m_imguiEditor`,
`m_inputRouter`, `m_interaction`, `m_launchOptions`, `m_operatorUi`,
`m_overlayDiagnostics`, `m_profiler`, `m_replayRuntime`, `m_runtimeTools`,
`m_sceneController`, `m_timers`, `m_tracyClientOwner`, `m_window`, and
`m_workerPool`. `RunUIStressActions` accesses 16:
`m_attachedCamera`, `m_camera`, `m_config`, `m_diagnosticsRuntime`,
`m_inputRouter`, `m_interaction`, `m_launchOptions`, `m_operatorUi`,
`m_overlayDiagnostics`, `m_profiler`, `m_replayRuntime`, `m_runtimeTools`,
`m_sceneController`, `m_simulation`, `m_timers`, and `m_window`. RBS4 gives UI
one snapshot/command phase owner; RBS5 gives Capture one stress-policy owner;
neither receives a replacement capability bag or retained owner union.

#### Project and Rendering ruling

The solution currently contains five projects. Compile-item and reference
inventory is:

| Project | Compile items | First-party compile items | Project references |
|---|---:|---:|---|
| `SKULLBONEZ_CORE` | 208 | 200 | UI, Maths, Physics |
| `SKULLBONEZ_MATHS` | 7 | 7 | none |
| `SKULLBONEZ_PHYSICS` | 31 | 31 | Maths |
| `SKULLBONEZ_UI` | 34 | 34 | none |
| `SKULLBONEZ_TESTS` | 146 | 146 | UI, Maths, Physics |

Each current filter manifest exactly mirrors its project with zero missing or
stale compile/header items: Core 208/252 across 66 filters, Maths 7/10 across
two, Physics 31/51 across 17, UI 34/36 across two, and Tests 146/7 across 33.
The paired figures are compile/header items; filter counts are current
measurements rather than topology budgets.

There is no duplicate compilation among the four production projects. Tests
deliberately compile four Rendering sources with render-free/test-specific
settings: `DX12/Dx12CachedPsoStore.cpp`, `RenderGpuTimingOwner.cpp`,
`RenderGraph.cpp`, and `RenderInstanceStore.cpp`. They remain test
implementations, not a second production owner.

The future `SKULLBONEZ_RENDERING` production source closure is exactly these 32
tracked files; all 44 tracked Rendering headers remain header items:

```text
SkullbonezSource/Rendering/DrawCallTrace.cpp
SkullbonezSource/Rendering/DX12/BLASDX12.cpp
SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.cpp
SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.cpp
SkullbonezSource/Rendering/DX12/Dx12DeferredReleaseOwner.cpp
SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.cpp
SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp
SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp
SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.cpp
SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.cpp
SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp
SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.cpp
SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp
SkullbonezSource/Rendering/DX12/MeshDX12.cpp
SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp
SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp
SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp
SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp
SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp
SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp
SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp
SkullbonezSource/Rendering/DX12/SBTDX12.cpp
SkullbonezSource/Rendering/DX12/ShaderBytecodeManifest.cpp
SkullbonezSource/Rendering/DX12/ShaderDX12.cpp
SkullbonezSource/Rendering/DX12/TLASDX12.cpp
SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp
SkullbonezSource/Rendering/RenderGpuTimingOwner.cpp
SkullbonezSource/Rendering/RenderGraph.cpp
SkullbonezSource/Rendering/RenderInstanceRenderer.cpp
SkullbonezSource/Rendering/RenderInstanceStore.cpp
SkullbonezSource/Rendering/RenderPipeline.cpp
SkullbonezSource/Rendering/Text.cpp
```

Current Rendering source resolves 105 external include sites to Core, 20 to
Maths, six to Physics, and three to Assets. Assets also includes Rendering, so
the three Rendering-to-Assets sites are a real source cycle and must be removed
before project creation:

- delete the unused `Assets/AssetKeys.h` include from
  `Rendering/RenderDiagnosticsTypes.h`;
- give Assets one synchronous `ResolveShaderBaseName` operation with the exact
  existing resolution order: reject an empty key, prefer a registered logical-
  name or base-name override, then the built-in logical-name map, then accept a
  non-empty direct base name. A returned character view may alias a registry
  string, static built-in value, or the caller input; it is borrowed only until
  the next registry mutation/caller-buffer change and must be consumed in the
  same resource-initialization call;
- delete `PrimitiveRenderContext` rather than merely trimming its AssetSystem
  field, and remove the Assets forward declaration from
  `PrimitiveBatchRenderer.h`. The renderer already retains its one concrete
  resource-builder/texture/geometry owner tuple for the backend epoch, so
  resource initialization reads that tuple from the renderer itself and
  receives only the synchronously borrowed direct base names for
  `shader.lit_textured_instanced` and `shader.shadow_depth_instanced`. Each
  batch/draw operation receives only its focused current-frame diagnostics,
  config, light, geometry, and command values; no scope retains an umbrella
  context or a duplicate reference to the renderer itself. RBS5 owns the
  deletion and focused API witnesses, and RBS6 verifies that the context is
  absent before the project split; and
- remove the AssetSystem parameter and forward declaration from `Text.h`.
  Runtime/Render resolves `shader.text`, `shader.solid_color`, and
  `shader.solid_color_batch`, then lends those three direct base-name views to
  `Text::BuildFont` for the same synchronous call.

Assets retains its source-registry authority and its existing calls into
Rendering. Rendering receives no callback, service bag, registry pointer, or
Assets type and retains none of the borrowed names. Focused RBS5 tests pin all
four resolver paths plus the five shader-name handoffs before the source cycle
is declared closed. This fixes the cycle in RBS5 before RBS6 without moving
Assets or Rendering behavior into Core.

RBS6 then creates one static library with direct project references to
`SKULLBONEZ_PHYSICS` and `SKULLBONEZ_MATHS`. `SKULLBONEZ_CORE` references
Rendering plus its existing UI/Physics/Maths projects. Core infrastructure
symbols used by Rendering are resolved by the final executable link, matching
the established Maths/Physics/UI static-library pattern; Rendering must not add
a reverse reference to the executable project.

The Rendering project has real Debug, Release, Profile, Profile-WPO, and
Automation configurations and matching solution `ActiveCfg`/`Build.0` rows.
Every configuration uses `$(Configuration)\SKULLBONEZ_RENDERING\` as its unique
intermediate directory, precise floating point, C++20, exceptions and RTTI
disabled, `/W4` plus warnings-as-errors, the configuration-correct runtime
library, no PCH, multiprocess compilation, and the forced
`Core/FloatingPointContract.h`. Release and Profile-WPO retain `/GL`; Debug,
Profile, Automation, and Profile-WPO retain `SKULLBONEZ_PROFILE_ENABLED`,
`SKULLBONEZ_PLATFORM_PROFILER_PIX`, and `USE_PIX`; all five configurations
retain `SKULLBONEZ_CAPTURE_EXECUTION`; and Automation additionally retains
`SKULLBONEZ_AUTOMATION_DIAGNOSTICS`. `SkoreBuildTracyClient=true` remains a
project-global property, while only Debug, Profile, and Automation import
`SkoreDevelopmentThirdParty.props`; Tracy/ImGui development sources remain
excluded in Release and Profile-WPO exactly as they are today. Tests retain the
four exact render-free variants and their reviewed build-config rulings.

`SKULLBONEZ_RENDERING` becomes the single owner of the existing incremental
`BakeShippingShaders` inputs/outputs and runs that target before its first
`ClCompile`, so `GeneratedShaderReflection.h` is refreshed before
`ShaderBytecodeManifest.cpp`, `ShaderDX12.cpp`, or any other moved consumer can
compile. Core removes its copy of the target; one project build can execute the
input/output-gated bake at most once. Core retains the executable's baked-DXIL
items and deployment. The WinPix targets import exists in both projects for
different reasons: Rendering needs the `pix3.h` compile path for `USE_PIX`,
while Core retains the final-link library and `WinPixEventRuntime.dll`
deployment. Core also retains final deployment of `dxcompiler.dll` and
`dxil.dll`. RBS6 must prove all three DLLs in every runnable bundle before the
project split is accepted.

Portable CMake intentionally compiles no Runtime or Rendering source and rejects
portable tests that include either package. RBS6 preserves that guard rather
than creating a second Rendering source manifest.

Every other proposed production boundary is `defer`: Core, Assets, Gameplay,
Scene, World, and all Runtime packages remain in `SKULLBONEZ_CORE`; no Runtime
tier, portable-engine, Assets, Scene, World, Gameplay, or DX12-only project is
authorized. RBS6 stops for owner review if a Rendering-to-Assets source edge,
duplicate production compilation, blocking configuration diagnostic, reverse
project reference, or unexplained build/link regression remains.

#### Pre-change behavioral witnesses

Existing coverage is sufficient; RBS0 adds no speculative production or test
surface.

| Behavior | Pre-change witness retained for implementation |
|---|---|
| Timing cadence and lifecycle | `TestSimulationSystem.cpp` pacing/cadence/hitch cases and `TestOwnerRequestQueues.cpp` case `Run timers consume reset and activation once per lifecycle generation`. |
| Resize and input | `TestInputRouter.cpp` native-message routing, focus/cursor, and scene-activation cases; `TestRuntimeContracts.cpp` case `IH5 runtime lifecycle owners preserve valid and unavailable policy`; recorded `SkullbonezData/interaction/causal_inspection_visual_qa.json` begins with resize and UI-surface actions. |
| UI switching | `TestStartup.cpp` development-UI selection/invalid-mode cases, `TestInputRouter.cpp` ImGui event-class arbitration, and `TestUIDrawValues.cpp` case `Production UI frame streams retain committed fingerprints`. |
| Stress | `tools/validate_ui_stress.bat` exercises the current Capture-owned stress path and failure propagation; RBS5 keeps that gate while removing `Run` reach-back. |
| Scene reload | `TestOwnerRequestQueues.cpp` ordered lifecycle generation, repeated-load, phase-cursor, request-batch, and detached-navigation cases. |
| Replay presentation | `TestReplayArtifact.cpp` presentation round-trip, `TestReplayDeterminism.cpp` restore, and `TestReplayVisualPacket.cpp` committed/pending presentation and exact visual fingerprint cases. |

#### Independent review closure

The first independent architecture review found 17 counted ownership and
missing-evidence items. Its fixes replaced blanket downward retention with the
exact header/site projection, corrected Startup/Replay and Asset resolution
ownership, made shader-bake and PIX/DLL topology explicit, reran build/link
measurements from production-identical inputs, and completed configuration,
filter, source, reverse-App, and behavioral evidence.

A separate projection auditor then reconciled all 467 downward sites and found
the 178 App / 131 valid non-App / 158 repair partition above. Review pass two
corrected five-configuration macro parity, the 46-operation `Run` inventory,
explicit Profile-WPO validation, and the residual `PrimitiveRenderContext`
capability bag. Final pass three reported zero findings and zero missing
evidence. No exception, compatibility bag, owner-controlled baseline change,
or speculative production/test surface was approved.

Closure reran the repeatable Runtime reporter at 300 files, 21 packages, 144
edges, 585 sites, and 35 reverse-App sites; build-configuration consistency at
1,863 compile rows, 348 source files, 78 shared files, 156 reviewed
divergences, zero dropped inheritance, and zero blockers; project filters at
836/836 project/filter items with zero errors; and `git diff --check`. RBS0 is
documentation/ownership closure, so no new heavy suite or golden refresh was
required.

## Phase RBS1 - Make Package Direction Executable

**Goal:** Make future Runtime cycles and reverse App edges fail mechanically.

- [x] Add data-driven Runtime DAG/rank rules to `dependency_graph_rules.json`.
- [x] Extend `check_dependency_graph.py` to report edges, SCCs, and forbidden-edge
      traces without hardcoded package names.
- [x] Fail every multi-package SCC; add no ceiling or grandfathered cycle budget.
- [x] Add fixtures for legal downward, reverse App, two-node/long cycles, exact
      exceptions, and parser residuals.
- [x] Add project-DAG and single-production-owner checks to the build-config owner.
- [x] Regenerate the AGENTS dependency proof with rule data in the same commit.

**Acceptance:** The pre-separation tree reports the source-confirmed problems;
negative controls fail for the intended reasons; no frozen debt count exists.

### RBS1 Closure Evidence - 2026-08-23

Integrated source commit `45656b307` makes Runtime direction and project
topology executable without treating current debt as permission. The ordinary
dependency gate admits only 276 exact, source-bound repair rows; delimiter,
separator, line, target, or include-spelling drift invalidates a row. Strict
mode ignores those rows and exits nonzero on the current 276 forbidden sites
and one 19-package SCC, so no count ceiling or grandfathered cycle exists.

The deterministic Runtime report covers 300 files, 21 packages, 144 edges,
585 sites, 36 bidirectional pairs, and 35 reverse-root sites. Its two branch
runs were byte-identical at
`f8df6d72d7c0d90a9f8dee838c024a907f3387ece715a61e476745ff222923e9`;
the generated repair-policy proof digest is
`396a362feb8d7e8a8d9cba94773f843e1c93fce2cc6a438f9d3c19bed4a28815`.
The build owner is closed-world over all nine tracked Visual Studio projects:
five contribute effective metadata, four standalone tests remain explicit
topology-only projects, eight reference edges form no cycle, and all 272
tracked production sources have exactly one production owner. Its two branch
reports were byte-identical at
`cac1df830bc9a07fbdcd50cd507198f26e872767a6a890dcb90a90e34bf48b21`.

Integrated Python compilation, both tool self-tests, JSON parsing, generated
proof checking, the ordinary and strict dependency controls,
`validate_dependency_graph.bat`, the build-config repository scan, governance
inventories, and `git diff --check` pass. Independent review first found and
then verified fixes for normalized include spelling and an open project
universe; the repeat verdict is PASS with zero findings. `validate_fast` still
stops on the three absent inherited Tracy/ImGui `Related:` targets already
present at base `ad94aca1e`; RBS1 changes none of those source files or paths.

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
- [ ] Build Automation, Debug, and Profile through
      `tools\validate_build_all.bat`, then build Release and Profile-WPO
      explicitly through `tools\validate_build.bat`; Profile-WPO is not part of
      the all-build helper.
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
tools\validate_build.bat Profile-WPO
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
