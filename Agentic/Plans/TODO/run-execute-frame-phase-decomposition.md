# Run::Execute Frame-Phase Decomposition

Date: 2026-07-22
Owner: skullbonez
State: In progress; RX0 census complete
Ledger tasks: 4 (RX0-RX3)

## Problem And Evidence (2026-07-22, main tip 0c5263e1)

`Run`'s state decomposition is complete (`run-execute-deaccretion` closed
X0-X2 on 2026-07-20; `run-member-and-include-shrink` closed earlier), but the
control flow did not follow: `Run::Execute` in
`SkullbonezSource/Runtime/RunFrame.cpp:177` runs to roughly line 748 — a
~570-line single function containing the Win32 message pump, automation
pre-input, development-UI capture intent assembly, input turn, simulation
tick, capture/screenshot/auto-cycle/scene-advance policy application, render
entry, and present bookkeeping, threaded with a
`SKULLBONEZ_AUTOMATION_DIAGNOSTICS` × `SKULLBONEZ_DEVELOPMENT_TOOLS` `#ifdef`
lattice. The four frame-view structs already exist as phase boundaries
(`RuntimeFrameHostView`, `RuntimeFrameInteractionView`,
`RuntimeFrameSceneView`, `RuntimeFramePresentationView`), but the phases
themselves are inline prose inside one function body.

This is a god *function*: every frame-order invariant the repo's validation
depends on lives in one place that must be read top-to-bottom to change
safely.

## Goal

`Run::Execute` reads as a short, stable sequence of named frame phases, each
a private `Run` method (or existing-owner call) taking the established frame
views. The frame order contract becomes visible in the phase call sequence
instead of in 570 lines of interleaved prose. Conditional-build branching
collapses toward the owners that already exist (`InteractionAutomationController`,
`ImGuiEditorOwner`) so the `#ifdef` lattice in `Execute` shrinks to phase-call
guards.

## Non-Goals

- No behavior, frame-order, or timing change of any kind. This is move-only
  sequencing extraction; validation and replay comparisons depend on the
  stable order.
- No new owner types, no context bag, no callback pack, no `*Internal`
  aggregation. The existing frame views are the calling convention
  (`frame-view-calling-convention` closure stands).
- No change to the message-pump drain policy (`win32-message-pump-drain`
  closure stands, including the 256-message cap).
- No `Run` state additions.

## RX0 Frame-Order Census (2026-07-22, source commit d5f6fc24)

The line ranges below name the unchanged source in
`SkullbonezSource/Runtime/RunFrame.cpp`. "Writes" includes calls that mutate a
named owner, platform state, or the current render graph; stack-only locals are
called out when they carry sequencing information into a later span.

| Order | Exact lines | Current phase | State read | State written / observable effect | RX1 extraction target |
|---:|---:|---|---|---|---|
| 0 | 177-190 | Execute prologue and exit latch | `m_skipExecute`, `m_applicationExit` | Immediate success/latched-result return; initializes `messageExitCode`; fixes the 256-message cap | Retain inline in `Execute` |
| 1 | 191-219 | Bounded Win32 message drain | thread message queue, `m_sceneController.State().currentFrame`, validation harness | Dispatches at most 256 FIFO messages; on `WM_QUIT` publishes the graphics-stress summary, latches normal exit, copies `wParam`, and breaks before frame work | `PumpFrameMessages`, returning a small value result (`quitRequested`, exit code), not retained state |
| 2 | 221-279 | Frame scope, timing begin, backend borrow validation, and frame-view construction | frame/work timers, all four frame-view members, render-backend facets | Enters steady-gameplay allocation scope; starts timers/profiler; resets DX12 draw-call diagnostics; constructs stack-only host/interaction/scene/presentation views and UI render borrows | `BeginFrameTurn`; keep views stack-owned by the caller |
| 3 | 280-337 | Automation before input | replay automation view, interaction automation program, window, scene/input/replay state | Advances automation; may select development UI, apply camera mode/replay intent/world-input owner, latch owned failure, or post quit | `RunAutomationBeforeInputPhase` |
| 4 | 338-416 | Development capture intent, input turn, surface reconciliation, proceed policy, and live style | prior ImGui input frame/command queues when enabled; input/router/interaction/camera/tool/UI/scene owners; launch options; scene/config/render defaults | Builds stack capture/command values; `ProcessInputFrame` mutates the established owners; may change the selected UI surface; builds `SceneFrameProceedPolicy`; advances live-style policy | `RunInputPhase`, returning the proceed policy plus frame-local UI facts needed later |
| 5 | 418-436 | Collision-visual begin and deterministic-presentation decision | current scene path/state/frame, simulation timer, capture policy, camera auto-cycle, live-style and automation capture predicates | Resets the scene collision-visual frame; builds stack `RuntimeCaptureSceneContext`; fixes `capturePresentationPinned` before simulation | `PrepareSimulationPhase` |
| 6 | 437-465 | Physics, replay prediction, and post-physics overlays | seconds-per-frame, pinned-presentation flag, proceed policy, scene physics/tornado/entities/environment, config, worker pool, simulation timers | Advances simulation/physics; publishes replay prediction; updates post-physics overlay diagnostics; returns interpolation alpha | `RunSimulationPhase` |
| 7 | 467-501 | Graphics stress, presentation alpha, optional pipeline synchronization, and render-model publication | all four frame views, replay/runtime state, render sync setting, config/scene/worker pool | Advances graphics stress; may finish/reopen the DX12 frame; on failure stops frame timing and resolves exit; publishes stack render-model view | `PrepareRenderPhase` |
| 8 | 503-518 | World render graph recording | render model view, presentation alpha, render graph facet | Opens the frame graph and records `Render`; the graph remains open for UI and post-draw work | `RenderWorldPhase` |
| 9 | 520-633 | Development viewport capture and both operator UI surfaces | rendered backbuffer, scene/camera/tool/interaction state, replay overlay state, Tracy status, window metrics | May capture the game viewport; composes Legacy UI; builds/renders ImGui; may swap surfaces or start Tracy and reinitialize/bind workers; UI failures resolve exit | `RenderOperatorUiPhase` |
| 10 | 635-679 | Post-draw live-style capture and automation assertions | capture owner/backbuffer, replay/interaction/scene state, development UI status | Saves pending live-style capture; advances after-render automation; may latch failure or post quit | `RunPostDrawDiagnosticsPhase` |
| 11 | 681-688 | Scheduled screenshot/capture completion | proceed policy, capture owner and rendered backbuffer through `TickScreenshots` | May mutate capture/scene policy and `continue` the outer loop **before** auto-cycle, work-time sampling, graph finalization, Present, profiler sampling, perf log, or scene advance | `RunScreenshotPhase`, preserving the early-continue edge |
| 12 | 690-696 | Auto-cycle and CPU work-time sample | proceed policy, work timer | Advances auto-cycle (which may post quit); stops work timer and publishes clamped `cpuFrameWorkMs` | `FinishFrameWorkPhase` |
| 13 | 698-723 | Graph finalization, Present, and submitted-frame marker | open render graph, DX12 frame/diagnostics facets | Finalizes graph; presents; on failure stops timing and resolves exit; on success marks one Tracy frame, stops frame timer, and ends profiler frame | `PresentFramePhase` |
| 14 | 725-742 | Profile sampling, perf log, and scene advance | profile samples when enabled, scene perf pass/frame, proceed policy | Publishes physics/render/GPU timing, advances perf log, then may advance/load a scene and `continue` only **after** successful Present/bookkeeping | `CompleteFramePhase` |
| 15 | 743-745 | Loop exit resolution | `messageExitCode`, first-failure latch | Resolves the latched application result, preserving an owned failure over the platform code | Retain inline in `Execute` |

### Conditional-build ownership map

| Exact lines | Guard | True owner / boundary | RX1-RX2 ruling |
|---:|---|---|---|
| 281-337 | `SKULLBONEZ_AUTOMATION_DIAGNOSTICS` | `InteractionAutomationController` owns automation progress/results; `ReplayRuntime`, `InputRouter`, `ApplicationExitState`, and the platform queue apply its typed intent | RX1 moves the island intact behind one phase call; RX2 pushes assembly behind the existing automation owner without giving it direct owner reach-back |
| 290-301 | nested `SKULLBONEZ_DEVELOPMENT_TOOLS` | `ImGuiEditorOwner` applies its commands; `Run` retains atomic surface sequencing and exit-latch application | Keep the nested build seam inside the automation phase |
| 341-372 | `SKULLBONEZ_DEVELOPMENT_TOOLS` | `ImGuiEditorOwner` owns the completed-frame capture snapshot, command queues, and selected surface | Move intact into the input phase; RX2 makes the owner-facing value seam whole |
| 359-370 | nested `SKULLBONEZ_AUTOMATION_DIAGNOSTICS` | automation owns the replay command value; `UI::SubmitOperatorEditorCommand` owns bounded queue insertion | Preserve submit failure routing to `ApplicationExitState` |
| 382-402 | `SKULLBONEZ_DEVELOPMENT_TOOLS` | `Run` sequences atomic surface selection around input; `ImGuiEditorOwner` owns selected/activated state | Keep immediately after `ProcessInputFrame`; it must not be folded into a later render phase |
| 432-435 | `SKULLBONEZ_AUTOMATION_DIAGNOSTICS` | `InteractionAutomationController` owns the scheduled after-render capture predicate | Keep in the pre-simulation pinning decision |
| 520-535, 538-540 | `SKULLBONEZ_DEVELOPMENT_TOOLS` | `ImGuiEditorOwner` owns viewport capture and secondary-surface visibility | Keep after world render and before operator composition |
| 576-633 | `SKULLBONEZ_DEVELOPMENT_TOOLS` | `ImGuiEditorOwner` owns frame construction/results; `Run` retains surface sequencing | Move as one operator-UI phase island |
| 613-629 | nested `TRACY_ENABLE` | `TracyClientOwner` owns capture startup; `WorkerPool` is restarted/rebound only after successful start | Preserve this explicit cold diagnostics action and its exact order |
| 644-679 | `SKULLBONEZ_AUTOMATION_DIAGNOSTICS` | `InteractionAutomationController` owns after-render assertions/results | Keep after completed UI recording and live-style capture, before screenshots |
| 647-661 | nested `SKULLBONEZ_DEVELOPMENT_TOOLS` | `ImGuiEditorOwner` owns the copied status; automation receives a value-only view | Preserve the value boundary; no automation-to-editor owner borrow |
| 725-732 | `SKULLBONEZ_PROFILE_ENABLED` | `DiagnosticsRuntime` owns sampled profiler values; `RunTimerState` owns published frame timings | Keep after successful Present and profiler-frame end |

### Extraction order oracle

RX1 is move-only and must preserve these control facts exactly:

1. Message dispatch precedes allocation/timing/frame-view construction; the
   256-message cap and FIFO deferral remain unchanged.
2. Automation-before-input precedes development capture consumption and the
   single `ProcessInputFrame` call. Surface reconciliation remains immediately
   after input.
3. The proceed policy and deterministic-presentation pin are fixed before
   physics. Physics precedes replay prediction, overlays, stress mutation, and
   render-model publication.
4. World render opens the graph; Legacy/ImGui recording and post-draw capture
   occur while it remains open; only Present finalizes it.
5. A posted quit does not truncate the current frame. The next bounded message
   drain observes `WM_QUIT`.
6. Screenshot completion may restart before Present. Scene advance may restart
   only after successful Present, frame/profiler end, and perf logging.
7. Pipeline-sync, viewport, ImGui, and Present failures retain their current
   stop-timer/end-profiler/owned-failure result paths and exact error strings.

## Phases

- [x] RX0 — Frame-order census. Document the current `Execute` body as an
  ordered phase list with exact line ranges, the state each span reads and
  writes, and every `#ifdef` region's true owner. This census is the
  extraction map and the review oracle for "nothing moved out of order".
- [ ] RX1 — Extract the frame turn. Pull the per-frame body into named
  private phase methods per the RX0 map (e.g. pump/drain, frame-begin +
  view construction, automation-before-input, input turn, simulation,
  capture-and-advance, render, present/frame-end), each taking existing
  frame views by reference. `Execute` retains the loop, exit latch
  resolution, and phase sequence only. Move-only: identical call order,
  identical strings, identical exit codes.
- [ ] RX2 — Conditional-build consolidation. Relocate automation and
  development-UI intent assembly currently inlined in `Execute` into the
  owners that already hold that authority, so conditional builds guard whole
  phase calls rather than interleaved statement islands. No behavior change;
  Automation and Release builds compile identically in effect.
- [ ] RX3 — Closure. Independent ownership review over the logical `Run`
  frame surface (`Run.h`, `RunFrame.cpp`, `RunInput.cpp`, `RunRender.cpp`,
  sibling Run TUs) per the god-object closure rule: phases are sequencing
  only, no phase method grew business authority, no reach-back appeared.
  Final gates below.

## Dependencies And Decisions

- Second in the round-2 campaign binding order (after
  `physics-standalone-world-unification`); runs before the renderer
  decomposition so that plan rebases on the final frame-phase shape.
- Owner decision ratified at registration: phase methods live on `Run`
  itself — this is sequencing, not new ownership; extracting a "FrameDriver"
  owner is explicitly rejected as a forwarding shape.
- `runtime-renderer-decomposition` RR-tasks touching `RunRender.cpp` rebase
  on RX1's extraction; do not run the two plans' overlapping tasks
  concurrently.

## Acceptance

- `Run::Execute` body is under ~80 lines: loop, exit resolution, phase calls.
- The RX0 census maps one-to-one onto the extracted phase methods with no
  reordered read/write.
- Zero new members on `Run`; zero new owner types; `#ifdef` regions inside
  phase bodies reduced to phase-call guards or moved behind existing owners.
- Independent review records no sequencing-order deviation and no authority
  accretion.

## Validation

- Per task: focused Profile/Automation builds and the targeted interaction
  or lifecycle doctest answering that task's question.
- RX1/RX2/RX3 pre-commit: `tools\validate_full.bat` (Run*/Runtime mapped
  gate). RX2 additionally builds the Automation configuration directly.
- Zero behavioral baseline, golden, screenshot, or replay refresh; byte-exact
  physics CSV and unchanged DX12 images prove the move-only claim.
