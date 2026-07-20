# Run::Execute De-accretion Closure

Date: 2026-07-20  
Branch: `nightrunner-20th-july`  
Status: X1 complete; X2 independent review and closure gate pending

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

Neither move introduces retained references, callbacks, forwarding facades, or
a multi-domain state bag.

## X1 `Run::Execute` Disposition

The rows below cover the complete current body at
`SkullbonezSource/Runtime/RunFrame.cpp:713-1232`. “Stays” means composition-root
sequencing allowed by the God-Object Closure Rule; domain policy remains in the
named concrete owner or in a typed value returned by that owner.

| Current lines | Block | Disposition | Ownership reason |
|---|---|---|---|
| 713-722 | Skip/previous-exit preflight | Stays | Run owns whether the process frame loop starts and resolves the final application result. |
| 723-755 | Bounded Win32 message pump and `WM_QUIT` translation | Stays | Operating-system message pumping and process exit translation are explicitly Run-owned. |
| 757-809 | Steady-gameplay allocation scope, timers/profiler, renderer facet checks, frame borrow-map construction | Stays | Allocation phase, top-level bookkeeping, startup-owner wiring, and stack-only frame views are composition-root duties. |
| 811-868 | Before-input automation tick and typed result application | Stays after X0 | `InteractionAutomationController` interprets commands; Run only sequences camera/replay/input-owner commands, records failures, and posts quit. |
| 869-942 | Development-surface input facts, `ProcessInputFrame`, typed surface request, explicit surface reassertion, live-style tick | Stays after X1 move | Input interpretation is in `InputFrameExecution`; ImGui/Legacy owners publish facts. Run retains only exclusive process-surface selection and ordering. |
| 944-969 | Collision-visual frame start and deterministic-presentation pin aggregation | Stays | Each capture/automation owner publishes whether it needs the next presentation; Run combines those frame-order facts before physics, without retaining capture state. |
| 970-1004 | Physics allocation/tick, replay prediction, post-physics diagnostics, graphics-stress tick | Stays | These are ordered calls into Simulation, ReplayRuntime, overlay diagnostics, and validation owners under their correct allocation phases. |
| 1006-1035 | Optional render drain, model-frame view build, world render | Stays | Run owns top-level render order and failure-to-process-result conversion; renderer owners decide synchronization and rendering behavior. |
| 1037-1123 | ImGui viewport capture, immutable UI/replay view assembly, Legacy text pass, ImGui begin/build/end | Stays | Frame-local value assembly and exclusive frontend ordering are composition. ImGui and replay owners retain their own state and publish typed results. |
| 1125-1169 | Live-style capture and after-render interaction automation | Stays | Capture and automation owners perform the work; Run supplies the required order, allocation phase, failure latch, and quit boundary. |
| 1171-1182 | Screenshot and auto-cycle helper calls | Stays | The helpers below apply typed owner results across capture, scene, camera, and process boundaries. |
| 1184-1223 | CPU-work timing, Present, submitted-frame marker, profiler sampling, perf-log tick | Stays | Timer/profiler bookkeeping and successful-Present process sequencing are explicitly allowed Run duties. |
| 1225-1232 | Scene-advance helper, restart-frame loop edge, final result | Stays | Run orders the scene transaction at the frame boundary and resolves the process result after loop exit. |

## X1 Named Helper Disposition

| Helper | Current lines | Disposition | Complete block classification |
|---|---:|---|---|
| `TickPhysics` | 1235-1360 | Stays as cohesive frame-step sequencing | Replay scrub selects the zero-simulation camera tick; `RuntimeInteractionController` publishes the complete physics/camera policy; Simulation publishes a deterministic tick count; the helper orders presentation capture, manipulator work, SceneWorld physics, bounded contact presentation, replay capture, tools, camera, and director playback. X1 moved the only post-publication policy mutation into `BuildFramePolicy`. No physics state is retained in Run. |
| `AfterPhysicsStep` | 1363-1430 | Stays as post-step transaction composition | Restores tool-owned pickup velocity, asks ReplayRuntime whether capture is active, then assembles stack-only replay restore/probe participants and applies typed probe outcomes to scene/process owners. ReplayRuntime owns probe sequencing and failure state. |
| `TickScreenshots` | 1433-1553 | Stays as capture-result composition | CaptureController owns scheduling, readback, completion, and automation result values. The helper supplies scene facts and applies typed Quit/Advance/Hold outcomes across process, SceneController, diagnostics, and camera owners; scene loads remain explicit composition transactions. |
| `TickAutoCycle` | 1556-1604 | Stays as capture-result composition | CaptureController owns auto-cycle/capture decisions. The helper applies its failure/completion result to the process latch, diagnostics, SceneController, and camera owners without retaining capture state. |
| `TickSceneAdvance` | 1607-1684 | Stays as scene-transaction composition | SceneController owns frame-advance policy and emits `SceneFrameAdvanceResult`; Run supplies stack-only load participants, applies consumer outputs, restarts the timer when requested, and posts quit at the process boundary. |

## Validation Evidence

| Command | Time | Result |
|---|---:|---|
| direct Automation build at X1 tip | 10.46 s | PASS; zero warnings/errors |
| direct Release build at X1 tip | 25.93 s | PASS; zero warnings/errors |
| focused runtime interaction policy test | 2.44 s | PASS; 1 case, 25 assertions |
| `tools\validate_ui_stress.bat` | 87.85 s | PASS; typed Ctrl+0 hot swap, clean logs, zero DX12 errors |
| `tools\validate_full.bat` | 148.65 s | PASS; all CPU/coverage and five runtime lanes, byte-exact physics |

Final X2 evidence will be appended at the closure tip. No baseline or golden
refresh is authorized by this plan.
