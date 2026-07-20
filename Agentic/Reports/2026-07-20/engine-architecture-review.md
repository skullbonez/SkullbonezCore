# Engine Architecture Review — 2026-07-20

Scope: owner-requested critical review of engine, physics, rendering, runtime,
and internal-UI architecture at branch tip `claude/engine-architecture-review-cz697b`
(2026-07-20). Test-harness size, agent-file count, and the incomplete ECS/job
system were explicitly out of scope. This report is the dated evidence base for
the eight-plan architecture-review campaign registered the same day.

## Headline verdict

The classic god objects are closed: `GameModel` no longer exists in source,
`Run` is a genuine composition root (`SkullbonezSource/Runtime/Run.h:107`), and
`PhysicsWorld` sequences concrete stage owners. The live debts are structural:
dependency direction is not enforced, three half-finished dual architectures
are being carried, replay is the largest and most privileged subsystem, the
render HAL is a GL-era abstraction, and gameplay content is fused into engine
core modules.

## Findings (ordered by severity)

### A. Dependency direction is not real

- Physics → Runtime: `Physics/PhysicsWorld.h:55`,
  `Physics/Stages/PhysicsContactSolverStage.h:45`,
  `Physics/Stages/PhysicsSleepController.h:52`, and
  `Physics/Stages/PhysicsStepDiagnostics.h:31` include
  `Runtime/Replay/ReplaySolverSnapshot.h`; `Physics/PhysicsWorld.cpp` includes
  `Runtime/Replay/ReplayRetainedMemory.h` and `Runtime/Allocation/*`; roughly a
  dozen physics files include `Runtime/Scene/SceneCapacity.h`.
- `Physics/SimulationSystem.h:38` lives in `Physics/` but is
  `namespace Runtime` and includes `Runtime/RuntimeInteractionController.h`.
- Core → everything: `Core/SkullScope.cpp:33-37` includes five physics
  headers; `Core/Profiler.cpp:35-53` includes `Rendering/Text.h`;
  `Core/Config.h:39` includes `Runtime/Scene/SceneCapacity.h`;
  `Core/WorkerPool.h:34` includes `Assets/AssetKeys.h`.
- Rendering → Runtime: `Rendering/DX12/RenderBackendDX12.h:72`
  (`SceneCapacity.h`), `RenderDeviceDX12.h:49` (`RuntimeAllocationTracker.h`),
  plus `Runtime/WindowConstants.h` in three DX12 TUs and `Rendering/Text.cpp`.

### B. EngineConfig is the surviving (data) god object

`Core/Config.h` is 595 lines, ~266 fields, included by 75 files, and threaded
by reference into the physics hot path: `PhysicsWorld::RunPhysics` takes
`const EngineConfig&`, and solver code reads
`config.persistentContactSolver.iterations` and
`config.terrainContact.maxBaumgarteBias` at solve time
(`Physics/PersistentContactSolver.cpp:165-193`). Eleven stage headers name
`EngineConfig`.

### C. Physics facade duplication

`PhysicsEngine.h` and `PhysicsScene.h` expose near-identical method lists;
`PhysicsEngine.cpp` is 469 lines of 1:1 forwarders. The header admits it is
migration scaffolding ("while preserving the existing PhysicsScene
implementation"). This is the forwarding-owner shape the Migration Cleanup
Review Rule forbids leaving behind.

### D. Run::Execute re-accretion

`Runtime/RunFrame.cpp:949-1558` (~610 lines) contains a full ImGui automation
command interpreter, including raw Win32 window resizing with DPI math
(`RunFrame.cpp:1059-1141`). Command interpretation belongs to the automation
or ImGui owner. `FillOperatorRenderingParameters`/`FillOperatorAudioView`
(`RunFrame.cpp:150-335`) are ~200 lines of per-frame field copying.

### E. Render graph is a half-migration living as double architecture

`Rendering/RenderGraph.h:128-133` — barrier policy is `DiagnosticOnly`;
hand-written backend barriers own execution. `Runtime/Render/RuntimeRenderer.h:184-205`
carries per-pass `callbackOwned` booleans plus parallel direct/graph execution
paths. Pass wrappers are parameter avalanches:
`ExecuteReflectionThroughRenderGraph` takes 10 positional args including five
bools (`RuntimeRenderer.h:220-230`); `RenderUiText` takes 12.

### F. The render HAL is a GL-2-era state machine on DX12

`IRenderCommandContext` exposes `SetDepthTest`/`SetBlendFunc`/
`SetPolygonOffset`/`SetClipPlane`/`BindTexture(slot)`; the backend
reverse-engineers PSOs from accumulated state via `PSOKey12`
(`Rendering/DX12/RenderBackendDX12.h:155-176`). `RenderBackendDX12` implements
seven interfaces in one class (`RenderBackendDX12.h:644-650`), ~6,000 lines
over nine TUs (internal owner split is genuine). The DXR path is single-purpose:
`DispatchReflectionRays` takes eight individual sky texture handles and raw
`float*` matrices (`RenderBackendDX12.h:830-846`).

### G. Gameplay content fused into engine core

`TornadoGameplay`/`TornadoField` are owned and sequenced by `PhysicsWorld`
(`Physics/PhysicsWorld.h:147`), and `TornadoVisualPass` is baked into the
fixed pass list (`RuntimeRenderer.h:319`). Point joints are the only
constraint and live as a "stay-behind" vector on the `PhysicsWorld` facade
(`PhysicsWorld.h:144`) rather than in the stage/store model.

### H. Replay proportion and privilege

`Runtime/Replay/` is 34,594 lines — larger than all of `Physics/` (28,200)
and all of `Rendering/` (30,664), with the largest TU in the repo
(`ReplayPrediction.cpp`, 4,444 lines). It is the only subsystem with a
runtime allocation privilege, and its types have leaked into physics stage
headers (finding A).

### Minor

- Raw Win32 globals `g_rawMouse*` in `Runtime/Input.cpp:62-74`.
- `InGameUI::UpdateInput` takes 14 defaulted positional parameters
  (`UI/UI.h:419-433`).
- Legacy/ImGui dual UI means editable settings are bookkept in up to four
  places while E17 evaluation runs (accepted transitional cost).

## Owner-ratified remediation order and decisions (2026-07-20)

The owner registered eight plans as the next active queue, biggest wins first:

1. `dependency-direction-restoration`
2. `physics-facade-unification` — **decision: PhysicsEngine survives; PhysicsScene is absorbed and deleted.**
3. `physics-settings-snapshot`
4. `run-execute-deaccretion`
5. `render-graph-completion` — **decision: finish the migration; RenderGraph becomes the owner of barrier emission and pass scheduling. Freezing as diagnostics was rejected.**
6. `render-hal-modernization`
7. `gameplay-module-extraction` — **decision: extracted gameplay content lives in a new top-level `SkullbonezSource/Gameplay/` module.**
8. `replay-boundary-containment`

E17 extended hands-on owner acceptance remains a parked owner item and does
not block this campaign.
