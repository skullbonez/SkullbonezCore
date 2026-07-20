# Run::Execute De-accretion Closure

Date: 2026-07-20  
Branch: `nightrunner-20th-july`  
Status: Complete; independent closure review clear and final full gate passed

## Ownership Result

`Run::Execute` is a composition-root frame loop. It pumps Win32 messages,
establishes frame scopes and borrow maps, orders concrete-owner operations,
applies typed cross-owner results, and resolves the process result. X0 removed
the development-UI automation interpreter. X1 removed the two remaining cheap
policy leaks found by the complete sweep:

1. `ProcessInputFrame` now returns `InputFrameExecutionResult` when the input
   owner interprets Ctrl+0. Run applies only the process-wide surface selection;
   it no longer rescans `InputRouter::Actions()`.
2. `RuntimeInteractionController::BuildFramePolicy` now owns cross-scene pause
   precedence. `TickPhysics` supplies the fact and consumes the returned policy
   without mutation. The value-seam test proves the lock outranks a live
   launcher owner and Space remains its step-level release.

The initial X2 review found two remaining logical-surface leaks. Both are now
closed: `SceneController` publishes one `SceneFrameProceedPolicy` for physics,
screenshots, auto-cycle, and scene completion, and the complete operator-editor
projection/traversal moved from `RunFrame.cpp` into the concrete stateless
`Runtime/UI/OperatorEditorFrameComposer`. Run keeps one ordered presentation
call. The review's non-blocking command-collapse concern is also closed: the
automation producer rejects a second same-frame surface selection.

None of these moves introduces retained references, callbacks, forwarding
facades, or a multi-domain state bag. The composer performs the complete UI
projection and draw preparation synchronously; it is not a relay back into Run.

## X1 `Run::Execute` Disposition

The rows below cover the complete current body at
`SkullbonezSource/Runtime/RunFrame.cpp:178-705`. “Stays” means composition-root
sequencing allowed by the God-Object Closure Rule; domain policy remains in the
named concrete owner or in a typed value returned by that owner.

| Current lines | Block | Disposition | Ownership reason |
|---|---|---|---|
| 178-187 | Skip/previous-exit preflight | Stays | Run owns whether the process frame loop starts and resolves the final application result. |
| 188-220 | Bounded Win32 message pump and `WM_QUIT` translation | Stays | Operating-system message pumping and process exit translation are explicitly Run-owned. |
| 222-274 | Steady-gameplay allocation scope, timers/profiler, renderer facet checks, frame borrow-map construction | Stays | Allocation phase, top-level bookkeeping, startup-owner wiring, and stack-only frame views are composition-root duties. |
| 276-333 | Before-input automation tick and typed result application | Stays after X0 | `InteractionAutomationController` interprets commands; Run only sequences camera/replay/input-owner commands, records failures, and posts quit. |
| 334-407 | Development-surface input facts, `ProcessInputFrame`, typed surface request, explicit surface reassertion, shared scene proceed policy, live-style tick | Stays after X1/X2 moves | Input interpretation is in `InputFrameExecution`; SceneController publishes one frame policy. Run retains only exclusive process-surface selection and ordering. |
| 409-438 | Collision-visual frame start and deterministic-presentation pin aggregation | Stays | Each capture/automation owner publishes whether it needs the next presentation; Run combines those frame-order facts before physics, without retaining capture state. |
| 439-469 | Physics allocation/tick, replay prediction, post-physics diagnostics, graphics-stress tick | Stays | These are ordered calls into Simulation, ReplayRuntime, overlay diagnostics, and validation owners under their correct allocation phases. |
| 471-500 | Optional render drain, model-frame view build, world render | Stays | Run owns top-level render order and failure-to-process-result conversion; renderer owners decide synchronization and rendering behavior. |
| 502-588 | ImGui viewport capture, frame-owned editor-view storage, replay snapshot, composer call, ImGui begin/build/end | Stays after X2 extraction | `OperatorEditorFrameComposer` owns runtime-to-UI traversal and Legacy draw preparation. Run supplies storage and exclusive frontend order; ImGui/replay owners retain their state. |
| 590-634 | Live-style capture and after-render interaction automation | Stays | Capture and automation owners perform the work; Run supplies the required order, allocation phase, failure latch, and quit boundary. |
| 636-647 | Screenshot and auto-cycle helper calls | Stays | The helpers below consume one scene proceed policy and apply typed owner results across capture, scene, camera, and process boundaries. |
| 649-688 | CPU-work timing, Present, submitted-frame marker, profiler sampling, perf-log tick | Stays | Timer/profiler bookkeeping and successful-Present process sequencing are explicitly allowed Run duties. |
| 690-705 | Scene-advance helper, restart-frame loop edge, final result | Stays | Run orders the scene transaction at the frame boundary and resolves the process result after loop exit. |

## X1 Named Helper Disposition

| Helper | Current lines | Disposition | Complete block classification |
|---|---:|---|---|
| `TickPhysics` | 709-835 | Stays as cohesive frame-step sequencing | Replay scrub selects the zero-simulation camera tick; `RuntimeInteractionController` publishes the complete physics/camera policy; Simulation publishes a deterministic tick count; the helper orders presentation capture, manipulator work, SceneWorld physics, bounded contact presentation, replay capture, tools, camera, and director playback. The shared `SceneFrameProceedPolicy` supplies sampled step/lock facts. No physics state is retained in Run. |
| `AfterPhysicsStep` | 838-905 | Stays as post-step transaction composition | Restores tool-owned pickup velocity, asks ReplayRuntime whether capture is active, then assembles stack-only replay restore/probe participants and applies typed probe outcomes to scene/process owners. ReplayRuntime owns probe sequencing and failure state. |
| `TickScreenshots` | 908-1028 | Stays as capture-result composition | CaptureController owns scheduling, readback, completion, and automation result values. The helper consumes the shared proceed decision, supplies scene facts, and applies typed Quit/Advance/Hold outcomes across process, SceneController, diagnostics, and camera owners. |
| `TickAutoCycle` | 1031-1079 | Stays as capture-result composition | CaptureController owns auto-cycle/capture decisions. The helper consumes the shared proceed decision and applies its failure/completion result without retaining capture state. |
| `TickSceneAdvance` | 1082-1157 | Stays as scene-transaction composition | SceneController owns and receives the frame proceed policy and emits `SceneFrameAdvanceResult`; Run supplies stack-only load participants, applies consumer outputs, restarts the timer when requested, and posts quit at the process boundary. |

## Validation Evidence

| Command | Time | Result |
|---|---:|---|
| direct Automation build at X1 tip | 10.46 s | PASS; zero warnings/errors |
| direct Release build at X1 tip | 25.93 s | PASS; zero warnings/errors |
| focused runtime interaction policy test | 2.44 s | PASS; 1 case, 25 assertions |
| `tools\validate_ui_stress.bat` | 87.85 s | PASS; direct surface-command matrix, clean logs, zero DX12 errors |
| `tools\validate_full.bat` | 148.65 s | PASS; all CPU/coverage and five runtime lanes, byte-exact physics |
| direct Automation build after X2 remediation | 19.72 s | PASS; zero warnings/errors |
| direct Release build after X2 remediation | 49.56 s | PASS; production macro path, zero warnings/errors |
| focused scene proceed-policy test | 2.05 s | PASS; 1 case, 9 assertions |
| `tools\validate_automation.bat` after X2 remediation | 40.46 s | PASS; Profile negative boundary and one combined replay/prediction/development-UI/Ctrl+0 process |
| `python tools\validate_project_filters.py` | 2.58 s | PASS; 720 project items, 720 filter items, zero errors |
| `tools\validate_full.bat` at X2 closure tip | 166.92 s | PASS; all CPU/coverage and five runtime lanes, accepted DX12 images, zero DX12 errors, byte-exact 44,401-line physics |

Touched-file comment audit: 10/10 source/tool files inspected, 0 deferred,
against `Agentic/Skills/comment-style-audit/skill.md`; formatting was rechecked
after the repository pipeline normalized touched declarations. The independent
follow-up review is clear with no blocking findings. It recorded two residual
non-blocking risks: duplicate-surface rejection has source-level but no focused
negative fixture, and the composer retains a broad include with no retained
authority. No baseline or golden refresh occurred.
