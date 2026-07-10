# Runtime Shell Decomposition

Date: 2026-07-10 (reconciled)
Status: In progress — 17/27 checklist items complete; earlier
foundation work is summarized separately and is not mixed into this count
Impact area: runtime architecture, scene lifecycle, input routing, render host
Owner: application composition root

## Goal

`Run` is a thin application shell: process lifetime, startup/shutdown, Win32
message pumping, top-level frame order, final owner wiring, and exit reporting.
A scene, replay, render, input, tool, or diagnostics feature lands inside its
owning subsystem without adding a `Run::*` method or a callback into `Run`.

This goal applies to the logical `Run` object: `Run.h`, every `Run*.cpp`, shared
headers, helper/context types, and forwarding facades are one review surface.
A short `Run.cpp` does not satisfy the goal while authority survives in those
other files.

Measured reality (2026-07-10, tracked engine/shader source-bearing files):
`Runtime/` is 82,359 lines — about 48% of 172,036 lines. The earlier split
produced 16+ `Run*.cpp` translation units sharing private state through
`RunInternal.h`; 27 files now include that header. This is partial-class
emulation, not ownership transfer. Current mega-TUs include `RunInput.cpp`
3,140, `Init.cpp` 3,495, `RunPasses.cpp` 2,333, and `RunRender.cpp` 2,323.
Cross-plan mega-files include `RunReplayTools.cpp` 4,779,
`TestSceneParser.cpp` 3,651, and `UI.cpp` 3,381.

## Design Rules

1. Preserve behavior; extraction commits are small and revertible.
2. Split by ownership of state and invariants, not file length.
3. No stored `Run&`/`Run*`, `void*` user hooks, or broad host bags in extracted
   systems.
4. Use concrete owners and value records; do not add callback or inheritance
   seams to hide the same coupling.
5. Each phase deletes a named `Run` method/state surface and adds behavioral
   evidence for the moved boundary.

## Five Concrete Ownership Extractions From `Run`

A mechanical file move does not complete a row. State, invariants, and command
authority must move to the named owner, and the deletion proof must pass.

| # | Ownership extraction | Move out of `Run` | Durable API and state owner | Deletion proof | Required evidence |
|---|---|---|---|---|---|
| 1 | **Input routing → `InputRouter`** | `TakeInput`, blocked/unfocused handling, post-UI keyboard dispatch, pointer-camera routing, repeated hardware polls, and large callback/context construction | `BeginFrame(const DeviceInputFrame&, const PreUiInputFacts&, InputActions&)` before UI and `CompleteFrame(const UiInputHitSnapshot&, const RuntimeInteractionFramePolicy&, InputActions&)` after UI; it owns fixed edge/presentation state, while `RuntimeInteractionController` remains gesture/workspace authority | `Run.h` has no `TakeInput`, `Dispatch*Keyboard*`, or pointer-routing methods; no input callback receives `void*`/`Run*`; no later frame phase polls hardware directly | CPU router/interaction-policy tests, interaction-click automation, perf, then full gate |
| 2 | **Command authority → owner-specific queues** | `DrainRuntimeCommands` and the mixed scene/capture/defaults/quit switch | Fixed bounded queues owned by `SceneController`, `CaptureController`, and `RenderDefaultsStore`, plus value-only `ApplicationExitState`; each owner returns a typed batch result and replay receives only accepted events with explicit wire codes | No central switch names scene, screenshot, defaults, and replay logging together; dead `AdvanceScene`/`Quit` types and generic runtime-command vocabulary are deleted | CPU exit/queue/order/overflow tests plus interaction and full gates |
| 3 | **Scene lifecycle → promoted `SceneController`** | `LoadScene`, reset/preserve-state orchestration, browser refresh, defaults, adjacent/deck movement, and lifecycle callback lambdas | `SceneController` owns queue, browser, lifecycle state, explicit `BeforeSceneUnload`…`AfterSceneActivated` events, and `Load(const SceneLoadRequest&) -> SbResult` | `Run.h` has no scene-load/reset/default business methods; `SceneController.cpp` is no longer a pass-through facade | Parser/round-trip tests, full gate, physics determinism |
| 4 | **Replay workspace → existing `ReplayRuntime`** | `TickReplayScrubberInput`, cause-tree/velocity/prediction input, inspection-camera decisions, replay overlays, restore/hash/probe coordination | `ReplayRuntime::TickWorkspace(const ReplayWorkspaceInput&, ReplayWorkspaceOutput&)` consumes typed UI actions and emits camera requests, owner commands, and fixed-capacity draw records | `Run.h` has no `TickReplay*`, `RenderReplay*`, replay restore/hash business method, or replay camera-transition method | CPU replay tests, replay scrub, interaction proofs, allocation evidence |
| 5 | **Render composition → existing `RuntimeRenderer`** | `BuildRuntimeRendererBindings`, backend-resource release/rebuild logging, editor/replay overlay hook lambdas, and pass-level texture callbacks | `RuntimeRenderer` receives immutable `RenderWorldView`, `RenderSceneView`, `RenderReplayOverlayView`, `RenderToolOverlayView`, and `RenderUiView`; owners build draw records before submission | `Run.cpp` contains no C-style render hook, `void*` user pointer, or callback reading `Run` private members | DX12 architecture tests, renderer gate, then full gate |

These are durable domain boundaries, not migration wrappers. `InputRouter` owns
input orchestration because raw/semantic/UI ordering is one invariant; it is not
a compatibility forwarding layer. Its deletion condition is replacement by an
equivalent input owner, not completion of this migration. The existing scene,
replay, and render owners graduate only when their old `Run` surfaces are
deleted and the named behavioral evidence passes.

### Extraction Sequence

1. Land `validation-gate-integrity.md` V1/V2.
2. Land the input snapshot/router core and application-exit result first, then
   migrate keyboard actions into owner-specific queues without recreating a
   central switch.
3. Promote `SceneController`; physics creation/reset work consumes this boundary.
4. Move replay workspace behavior with the replay and UI plans.
5. Finish render composition after overlay producers no longer call `Run`.

### Landed Foundation (not checklist completion)

The 2026-07-10 foundation commit adds allocation-free `InputRouter` and
`ApplicationExitState` owners plus direct CPU coverage for key ordering,
press/hold/release, inactive contexts, focus cancellation/resynchronization,
bounded diagnostics, nonzero platform exit codes, and first-failure
precedence. `tools\validate_fast.bat`, the project-filter validator, and
`tools\validate_full.bat` passed from that source with zero warnings, zero DX12
validation errors, matching screenshots, and byte-exact physics output.

No B1/B2 checkbox was closed by the foundation alone. Production wiring and
the named deletion/evidence proofs remain authoritative for each row.

### Application Exit Wiring Evidence

B2a is complete. `Run::Execute` now resolves the real `WM_QUIT` code through
`ApplicationExitState`; nonzero platform exits return a synthetic Lane R
failure instead of success. Capture, input-resource rebuild, interaction
automation, replay-probe, renderer finish, and Present failures latch their
owned result before loop shutdown. The first owned result therefore survives a
later renderer failure, nonzero message, or normal exit. Win32 display-mode and
resize failures publish nonzero quit codes when no richer Run-owned result can
cross the window-procedure boundary.

Evidence from the final source: the 13 platform-neutral exit-state cases passed
inside `tools\validate_tests.bat` (99/99 total doctest cases), a Profile x64
build completed with zero warnings/errors, and `tools\validate_full.bat` passed
format/metadata/CPU tests, the DX12 lane with zero InfoQueue errors and matching
screenshots, and the 20,001-line byte-exact physics baseline.

### Owner Queue Wiring Evidence

B2b-B2e are complete. `CaptureController` owns a fixed 16-slot request ring,
rejects empty, truncating, and non-BMP paths before enqueue, and returns only
complete screenshot writes in its accepted batch. The post-render automation
sink remains direct so its validation timing did not move accidentally.
`RenderDefaultsStore` owns a fixed 16-slot ordinary/cinematic save ring and
samples the final live values at the end-of-input checkpoint; both config
writers now return `SbResult`, retain the first failure, and exclude failed
writes from accepted events. `SceneController` owns the fixed 64-slot scene
request ring and all UI/keyboard/probe submission vocabulary. Its batch keeps
ordered non-transition work, accepts only the first same-frame transition, and
reports additional transitions as rejected while `Run::DrainSceneRequests`
remains the explicit C1 execution seam.

Deletion and wire proof: `RuntimeCommandQueue.*`, `RuntimeCommandType`, the
mixed drain/switch, and the zero-producer `AdvanceScene`/`Quit` cases are absent
from source and projects. Replay uses the new `OwnerAction = 10` wire lane plus
explicit 1001-3002 owner codes; only successful capture, persistence, or scene
work records an event, and raw domain enum ordinals are never serialized.

The final source passed `tools\validate_fast.bat`, the production and test
project-filter checks, `tools\validate_tests.bat` (114/114 cases, 2,096
assertions), both `tools\validate_interaction_clicks.bat` scripts,
`tools\validate_perf.bat`, and `tools\validate_full.bat` with zero build
warnings, zero DX12 InfoQueue errors, matching screenshots, and the 20,001-line
byte-exact physics baseline. The touched-file comment audit covered 25
source-bearing files with no deferrals.

### RuntimeRenderer Composition Evidence

A1-A2 are complete. `RuntimeRenderer` receives the five named owner views,
stores explicit render/world owners rather than `RunSubsystemState`, owns
resource lifecycle logging and pass submission, and sequences tool/replay
record producers after replay overrides. The old binding aggregate, two Run
C hooks, `void*` callback user, pass-level texture callback path, and raw sky
alias are deleted. The first adversarial pass found and corrected overlay-order
and disguised-host defects; the required repeat pass was clean. Architecture,
renderer, full, fast, allocation-policy, project/filter, and comment gates pass.
Detailed evidence is in
`Agentic/Reports/runtime_renderer_composition_20260710.md`.

### Keyboard Router Wiring Evidence

B1a and B1c are complete. The CPU suite now characterizes ordered
press/hold/release/repress, simultaneous actions, immutable all-of contexts,
pre-/after-UI/capture phase order, UI refusal, skipped capture, focus
cancellation/resynchronization, and action-owned quick-repeat timing.
`Input::CaptureDeviceInputFrame` captures focus plus the complete 256-key
level snapshot once; automation augments that same value. `InputRouter` alone
owns semantic action edges, context eligibility, delivery memory, and quick-tap
presentation timing. `RuntimeInputContext`/`InputController` no longer expose
semantic keyboard edge storage or polling helpers.

Deletion proof: `MappedKeyboardDispatchContext`, its 18-owner bag, the
13-callback `DispatchMappedKeyboardActions` pack, blocked-key memory advances,
consumer-side `CaptureKeyboardActionPress`, and the editor/diagnostics duplicate
key latches are absent from source.

Evidence from the final source: `tools\validate_tests.bat` passed 103/103 cases
and 2,009 assertions; `tools\validate_interaction_clicks.bat` passed both
inspect and replay scripts; the F6 automation report returned `ok=1` through
the production snapshot/router/diagnostics path; `tools\validate_perf.bat`
passed allocation guard, DX12 and physics performance thresholds; and
`tools\validate_full.bat` passed with zero warnings, zero DX12 InfoQueue errors,
matching screenshots, and the 20,001-line byte-exact physics baseline. The
comment-style audit covered all 14 touched source-bearing files.

### Pointer Snapshot And Native Ownership Evidence

B1b, B1d, and B1e are complete. `CaptureDeviceInputFrame` is the only steady
hardware read and fills keys, three buttons, client position, wheel, raw delta,
focus, and automation overrides before any consumer runs. `InputRouter` retains
that value, owns the only left/right edge memory, and publishes one copied
`UiInputHitSnapshot` after hit testing. UI, editor, replay scrubber/cause-tree/
velocity tools, camera, physics stepping, render camera selection, and frame
code all read those owner values.

`InputRouter` also owns desired native capture and cursor visibility. UI emits a
typed capture request, tool/camera owners request the router directly, focus loss
cancels both states, and the composition root applies only changed presentation
values through the private Win32 seam. Direct `SetCapture`/`ReleaseCapture`
calls exist only inside `Input::SetNativeMouseCapture`; direct runtime/UI
`GetKeyState`, `GetAsyncKeyState`, cursor-position, wheel, raw-delta, and
`Input::Is*` APIs/calls are deleted. The later editor input `void*` callback pack
was deleted as well. B1f remains open only for extraction 1's final `Run.h`
method/state deletion proof (`TakeInput` and pointer-routing composition), not
for hardware polling.

The next deletion seam is also complete: mouse-pickup cancellation is a
`RuntimeTools` owner operation that atomically releases `InputRouter` native
capture intent, ends the interaction gesture, and clears the stored body
handle. The `Run::CancelMousePickup` forwarding method is deleted and scene,
focus, camera-mode, editor, and physics-failure paths call the owner directly.
The remaining B1f proof is still the full `TakeInput`/pointer-routing method
surface; this seam is not counted as B1f closure by itself.

Evidence from this owner seam: `tools\validate_all_cpu_tests.bat` passed all
four CPU lanes with 125/125 doctest cases and 2,708 assertions in 14.4s;
`tools\validate_interaction_clicks.bat` passed inspect-gizmo and replay-
prediction scripts in 16.8s; and `tools\validate_full.bat` passed in 60.8s with
zero warnings, zero DX12 InfoQueue errors, matching screenshots, standalone
physics smoke, and the 20,001-line byte-exact physics baseline. The comment
audit covered all six touched source-bearing files.

Evidence from the final source: `tools\validate_tests.bat` passed 105/105 cases
and 2,023 assertions; `tools\validate_interaction_clicks.bat` passed inspect
gizmo and replay prediction click scripts; `tools\validate_perf.bat` passed the
allocation guard and DX12/physics thresholds in 48.50s; and
`tools\validate_full.bat` passed in 56.40s with zero warnings, zero DX12
InfoQueue errors, matching screenshots, and the 20,001-line byte-exact physics
baseline. The comment-style audit covered all 35 touched source-bearing files.

The next B1f cut deletes the Run-capturing pointer/camera callback pack and the
`Run::BuildRuntimeInputSnapshot` method. `InputRouter::BuildRuntimeSnapshot`
now joins its one device frame and one published UI snapshot with a value-only
`RuntimeInteractionFrameInput`; focused CPU coverage proves pointer coordinates,
button edges, modifier state, UI refusal, suppression, and frame-policy values.
The final world/camera phase is direct composition code, so no input helper
receives callback lambdas that recover `Run`. B1f remains open for deletion of
`TakeInput`, `RouteRuntimePointerInput`, and the remaining pointer/camera
composition methods in `Run.h`.

Evidence: final fast passed in 33.3s with five candidates, 128/128 doctest
cases, and 2,749 assertions; the CPU umbrella passed all four lanes in 10.7s;
both interaction reports passed in 8.3s; perf completed in 32.2s; and full
passed in 52.4s with zero warnings, zero DX12 InfoQueue errors, matching
screenshots, standalone topology smoke, and the 20,001-line byte-exact physics
baseline. The first fast attempt stopped at formatting before build/runtime
work; formatting only `RunInput.cpp` produced the clean rerun. Comment audit:
5/5 touched source-bearing files.

Camera-look gesture lifetime is now a `RuntimeInteractionController` decision.
Its typed sync operation begins camera capture only from an idle gesture owner,
uses the immutable runtime snapshot for button/position facts, and cancels on
focus or frame-policy exit. The two Run camera-look gesture methods are deleted,
and the standalone interaction policy test now exercises the owner API rather
than manually reproducing it. B1f remains open for the larger world-pointer and
camera-mode/presentation composition surface.

Evidence: fast passed in 41.0s with five candidates, 128/128 doctest cases, and
2,749 assertions; the CPU umbrella including Debug/Release interaction policy
passed in 16.4s; both interaction reports passed in 8.3s; and full passed in
52.5s with zero warnings, zero DX12 InfoQueue errors, matching screenshots,
standalone topology smoke, and the 20,001-line byte-exact physics baseline.
Comment audit: 5/5 touched source-bearing files.

Pointer-presentation policy now joins inside `InputRouter`. Editor and replay
contribute six scalar facts; the router combines them with its focused device
frame and UI hit snapshot to decide mouse-look ownership and cursor visibility.
The two Run query methods are deleted. Render-side generated-camera cycling
reads `RuntimeInteractionController`'s committed CameraLook capture instead of
recomputing input policy after the input phase. Focus, UI refusal, RMB look, and
editor placement-preview visibility have direct CPU coverage. B1f remains open
for the mutating presentation helpers, world-pointer route, and `TakeInput`.

Evidence: fast passed in 37.9s with six candidates, 129/129 doctest cases, and
2,755 assertions; the CPU umbrella passed in 11.0s; both interaction reports
passed in 8.3s; perf completed in 32.1s; and full passed in 52.7s with zero
warnings, zero DX12 InfoQueue errors, matching screenshots, standalone topology
smoke, and the 20,001-line byte-exact physics baseline. The first Debug build
found the deleted Run query's render caller; it was corrected to consume the
interaction owner before formal gates. Comment audit: 6/6 touched source files.

The two mutating Run pointer-presentation wrappers are deleted. Composition
sites request router cursor visibility directly from the router-owned policy;
mouse release changes router capture intent and explicitly resets camera deltas
only when mouse-look does not own the cursor. The editor viewport/placement
path publishes the same six scalar facts without recovering a Run method.
B1f remains open for world-pointer routing, focus cleanup, keyboard dispatch,
camera mode helpers, and final `TakeInput` deletion.

Evidence: fast passed in 33.7s with three candidates, 129/129 doctest cases, and
2,755 assertions; both interaction reports passed in 8.5s; and full passed in
51.9s with zero warnings, zero DX12 InfoQueue errors, matching screenshots,
standalone topology smoke, and the 20,001-line byte-exact physics baseline. The
first Debug build identified the editor TU's remaining wrapper caller; it was
replaced with direct scalar-fact publication before formal gates. Comment
audit: 3/3 touched source-bearing files.

Attach-camera target, orbit, return-pose, and follow state is no longer a Run
value. `AttachedCameraController` physically owns `AttachedCameraState`; frame,
input, render, stress, and scene-load composition borrow `State()` explicitly.
The controller retains no scene/model/camera pointer, so ownership moves without
creating a cross-domain service bag. B1f remains open while attached-camera
behavior methods and the wider pointer route still live on Run.

Evidence: fast passed in 37.5s with nine candidates, 129/129 doctest cases, and
2,755 assertions; the CPU umbrella passed in 10.8s; both interaction reports
passed in 8.9s; perf completed in 32.1s; and full passed in 53.2s with zero
warnings, zero DX12 InfoQueue errors, matching screenshots, standalone topology
smoke, and the 20,001-line byte-exact physics baseline. Compile feedback caught
the mechanical replacement touching the SceneController definition parameter;
the parameter was renamed and kept as the intended borrowed state before formal
gates. Comment audit: 9/9 touched source-bearing files.

Attach return-transition authority is also controller-owned. The logical
pre-Attach mode moved out of `RunCameraState`, and `AttachedCameraController`
captures the visible render pose and restores/tweens it through the borrowed
SceneController camera owner. Run's capture/restore methods are deleted. B1f
still owns target selection, submode/pin/orbit/follow behavior and the outer
world-pointer route.

Evidence: fast passed in 34.0s with five candidates, 129/129 doctest cases, and
2,755 assertions; both interaction reports passed in 8.9s; and full passed in
52.4s with zero warnings, zero DX12 InfoQueue errors, matching screenshots,
standalone topology smoke, and the 20,001-line byte-exact physics baseline.
The first Debug build exposed non-const legacy camera getters; the borrowed
camera owner was made mutable before formal gates. Comment audit: 5/5 touched
source-bearing files.

Attach target recovery and per-frame follow application are now controller
operations. `ResolveTargetIdentity` owns stale-target clearing, while
`TickFollow` resolves physics identity, builds the pose, and applies the first-
solve tween or live retarget through a synchronous camera borrow. Run's resolve
and follow-tick methods are deleted; frame/render composition call the owner
directly. Remaining Attach input work is selection, submode/pin/orbit commands.

Evidence: fast passed in 33.8s; both interaction reports passed in 8.5s; perf
completed in 32.1s; and full passed in 52.6s with zero warnings, zero DX12
InfoQueue errors, matching screenshots, standalone topology smoke, and the
20,001-line byte-exact physics baseline. Comment audit: 6/6 touched source-
bearing files.

Attach submode cycling, pin state transitions, and wheel-orbit mutation are now
`AttachedCameraController` operations. The controller borrows models/cameras
only for each synchronous command, while the caller supplies the outer camera-
mode and UI-blocking facts. Run's three command methods and the orbit callback
slot in the UI frame pack are deleted. B1f remains open for Attach target
selection, the outer world-pointer route, focus/keyboard composition, and final
`TakeInput` deletion.

Evidence: fast completed in 33.9s, the CPU umbrella (129/129 doctest cases and
2,755 assertions), both interaction reports, perf, and full passed. Final-source
full completed in 53.1s with
zero warnings, zero DX12 InfoQueue errors, matching screenshots, standalone
topology smoke, and the 20,001-line byte-exact physics baseline. Comment audit:
4/4 touched source-bearing files.

Attach target selection is now controller-owned. `AttachedCameraController`
performs stable target reuse, replay/editor seed acceptance, shared-service ray
picking, identity capture, and initial camera-relative offset capture through
synchronous store/camera borrows. It returns an exact body/collider/model
selection receipt for interaction composition. Run's target setter, seed,
mouse-pick, and world-click methods plus three obsolete pose helper functions
are deleted. A third bounded interaction script proves an Attach object click
retains Attach mode and publishes the exact selected object. B1f remains open
for the wider editor/manipulator/replay/launcher pointer route, focus/keyboard
composition, camera-mode helpers, and final `TakeInput` deletion.

Evidence: the three-scenario interaction gate passed in 18.9s; fast passed in
26.6s; the CPU umbrella passed in 11.0s with 129/129 doctest cases and 2,755
assertions; perf completed in 32.4s; and full passed in 52.2s with zero warnings,
zero DX12 InfoQueue errors, matching screenshots, standalone topology smoke,
and the 20,001-line byte-exact physics baseline. Comment audit: 4/4 touched
source-bearing files; the touched batch wrapper retains its complete local
contract header.

Manipulator pointer routing is now `RuntimeTools` behavior. Input composition
supplies one value snapshot containing mode/UI facts, button edges, client
position, camera pose, and ordinary/clamped rays. The tool owner performs the
pick, validates the body handle, owns native capture and gesture begin/cancel,
updates the camera-facing drag plane, and returns only consumed/interactive
receipts. `Run::TickMousePickupInput` is deleted. The interaction harness now
asserts pickup active on a dynamic-body press and inactive after release, and
reports the final pickup state. B1f remains open for editor pointer behavior,
the replay/launcher outer route, focus/keyboard composition, camera helpers,
and final `RouteRuntimePointerInput`/`TakeInput` deletion.

Evidence: the four-scenario interaction gate passed in 12.1s after the first
probe correctly exposed that the inspect fixture's fixed body is canceled by
the physics owner; the proof was moved to the existing dynamic-body fixture.
Fast passed in 29.6s; the CPU umbrella passed in 11.4s with 129/129 doctest cases
and 2,755 assertions; perf completed in 32.1s; and full passed in 53.2s with
zero warnings, zero DX12 InfoQueue errors, matching screenshots, standalone
topology smoke, and the 20,001-line byte-exact physics baseline. Comment audit:
6/6 touched source-bearing files; the batch wrapper retains its local contract
header.

Editor/Inspect selection command authority is now `RuntimeTools` behavior.
Commands first validate model bounds and the exact body/collider pairing into a
value-only selection plan. Composition applies an optional interaction-owner
transition between prepare and commit, preserving cleanup-before-mutation
ordering without a callback. RuntimeTools commits the selected handles/row and
publishes the accepted selection event. Run's generic interaction command
executor and event publisher are deleted, along with their local naming helpers.
B1f remains open for the rest of editor pointer behavior, replay/launcher outer
routing, focus/keyboard composition, camera helpers, and final pointer-route/
`TakeInput` deletion.

Evidence: all four interaction scenarios passed in 23.7s; fast passed in 21.4s;
the CPU umbrella passed in 11.0s with 129/129 doctest cases and 2,755 assertions;
perf completed in 32.4s; and full passed in 52.3s with zero warnings, zero DX12
InfoQueue errors, matching screenshots, standalone topology smoke, and the
20,001-line byte-exact physics baseline. The first Debug build found an
unqualified legacy `RunInternal` selection resolver; RuntimeTools now performs
the small handle/row repair locally and the retry passed. Comment audit: 6/6
touched source-bearing files.

The remaining editor interaction-state queries are now RuntimeTools-owned.
`HasActiveEditorInteractionState` reads only durable editor state;
`InspectGizmoInteractionActive` joins that state with explicit camera-mode and
replay-inspection facts. Editor input, automation assertions/reports, transition
cleanup, and render overlay composition call the owner directly. Both Run query
methods are deleted. B1f remains open for mutating editor transition/pointer
behavior, replay/launcher routing, focus/keyboard composition, camera helpers,
and final pointer-route/`TakeInput` deletion.

Evidence: fast passed in 31.5s; all four interaction scenarios passed in 12.9s;
and full passed in 52.5s with 129/129 doctest cases, 2,755 assertions, zero
warnings, zero DX12 InfoQueue errors, matching screenshots, standalone topology
smoke, and the 20,001-line byte-exact physics baseline. Comment audit: 7/7
touched source-bearing files.

Editor transition cleanup is now RuntimeTools-owned. The owner clears placement
preview/scale, gizmo gesture state, viewport look, placement mode, hot axes, and
optionally the validated selection through its existing command boundary. Run's
editor-transition cleanup method is deleted; the outer transition composition
retains only mouse-release and cursor reconciliation across input/replay owners.
B1f remains open for that cross-owner transition coordinator, the rest of editor
pointer behavior, replay/launcher routing, keyboard composition, camera helpers,
and final pointer-route/`TakeInput` deletion.

Evidence: fast passed in 31.1s; all four interaction scenarios passed in 12.8s;
and full passed in 52.3s with 129/129 doctest cases, 2,755 assertions, zero
warnings, zero DX12 InfoQueue errors, matching screenshots, standalone topology
smoke, and the 20,001-line byte-exact physics baseline. Comment audit: 4/4
touched source-bearing files.

Pointer presentation mutation is now direct `InputRouter` behavior.
`ApplyPointerPresentation` commits cursor visibility from the evaluated policy;
`ReleasePointerToUi` refuses to steal camera-look capture and returns the exact
camera-reset effect when it releases native capture. All four free runtime
cursor/capture query and mutation wrappers are deleted, and every input/camera/
editor call site consumes the router-owned policy directly. B1f remains open
for cross-owner transition sequencing, editor/replay/launcher routing, keyboard
composition, camera helpers, and final pointer-route/`TakeInput` deletion.

Evidence: fast passed in 38.5s; the CPU umbrella passed in 11.0s with 129/129
doctest cases and 2,755 assertions; all four interaction scenarios passed in
13.4s; perf completed in 32.5s; and full passed in 52.6s with zero warnings,
zero DX12 InfoQueue errors, matching screenshots, standalone topology smoke,
and the 20,001-line byte-exact physics baseline. Comment audit: 3/3 touched
source-bearing files.

Launcher pointer routing is now one RuntimeTools-owned command. The owner
performs mode/UI/edge gating, camera-ray construction, accepted replay-event
publication, cold topology repair, laser/projectile execution, and scene-count
commit. Run consumes only the command's consumed/interactive receipt and input-
mode bookkeeping. A fifth interaction scenario enters Launcher, clicks a live
body, proves owner-owned laser feedback, and retains Launcher mode. B1f remains
open for editor and replay route composition, cross-owner transition sequencing,
keyboard/camera helpers, and final pointer-route/`TakeInput` deletion.

Evidence: the five-scenario interaction gate passed in 22.2s after its first
assertion correctly showed that optional debug ray lines stay inactive when
visualization is disabled; the final proof observes the always-recorded launcher
laser owner instead. Fast passed in 27.4s; the CPU umbrella passed in 10.8s with
129/129 doctest cases and 2,755 assertions; perf completed in 32.5s; and full
passed in 52.9s with zero warnings, zero DX12 InfoQueue errors, matching
screenshots, standalone topology smoke, and the 20,001-line byte-exact physics
baseline. Comment audit: 5/5 touched source-bearing files; the batch wrapper
retains its complete local contract header.

Replay world-pointer routing is now ReplayRuntime-owned. One synchronous,
frame-scoped input value carries gating facts, the prebuilt ray, stable entity/
store views, and camera-exit owners. ReplayRuntime decides eligibility, updates
the stable path target (including additive/clear-on-miss behavior), and exits
inspection camera when the accepted pick requires it. Run's replay branch now
only constructs the borrowed value and observes `consumed`. The Attach click
regression additionally asserts selection on the press frame and release frame.
B1f remains open for editor routing, cross-owner transition sequencing,
keyboard/camera helpers, and final pointer-route/`TakeInput` deletion.

Evidence: the five-scenario interaction gate passed in 14.4s after one initial
Attach assertion failure did not reproduce; same-frame and release-frame
assertions were added, the isolated Attach run passed, and the complete gate
then passed. Fast passed in 21.6s; the CPU umbrella passed in 10.9s with 129/129
doctest cases and 2,755 assertions; perf completed in 32.5s; and full passed in
52.7s with zero warnings, zero DX12 InfoQueue errors, matching screenshots,
standalone topology smoke, and the 20,001-line byte-exact physics baseline.
Comment audit: 3/3 touched source-bearing files.

Cross-owner interaction-transition cancellation now sequences through
`InputRouter::ApplyInteractionTransitionCleanup`. The router interprets the
transition record, asks ReplayRuntime and RuntimeTools to cancel only their own
state, reconciles router-owned capture/cursor policy, and resets camera deltas
only from the explicit capture-release result. All owner references are
synchronous borrows and are never stored. Run's broad
`ClearRuntimeInteractionStateForTransition` method is deleted. B1f remains open
for the final transition wrapper, editor pointer route, keyboard/camera helpers,
and pointer-route/`TakeInput` deletion.

Evidence: fast passed in 39.2s; the CPU umbrella passed in 11.3s with 129/129
doctest cases and 2,755 assertions; all five interaction scenarios passed in
15.0s; perf completed in 32.2s; and full passed in 52.9s with zero warnings,
zero DX12 InfoQueue errors, matching screenshots, standalone topology smoke,
and the 20,001-line byte-exact physics baseline. The first Debug build exposed
a missing `RunCameraMode` forward declaration in the expanded router contract;
the declaration was added and the retry passed. Comment audit: 3/3 touched
source-bearing files.

Transition finalization is now InputRouter-owned as well. The router first runs
the proven cancellation sequence, then re-establishes the authoritative
Launcher, Manipulator, Edit, Replay, Inspect, or Live controller state from the
transition value. Camera-mode and editor-mode composition call this owner API
directly. Run's `ApplyRuntimeInteractionTransitionCleanup` wrapper is deleted.
B1f remains open for the world-owner transition wrapper, editor pointer route,
keyboard/camera helpers, and final pointer-route/`TakeInput` deletion.

Evidence: fast passed in 30.8s; the CPU umbrella passed in 10.9s with 129/129
doctest cases and 2,755 assertions; all five interaction scenarios passed in
14.9s; perf completed in 32.4s; and full passed in 52.6s with zero warnings,
zero DX12 InfoQueue errors, matching screenshots, standalone topology smoke,
and the 20,001-line byte-exact physics baseline. Comment audit: 3/3 touched
source-bearing files.

World-owner selection now enters through `InputRouter::SetWorldInteractionOwner`.
The router derives the exact workspace, applies cross-owner cancellation, and
reasserts the requested owner after cleanup; Run no longer owns or forwards this
transition. All editor, replay-automation, and placement-mode callers invoke the
router directly. B1f remains open for the editor pointer route,
keyboard/camera helpers, and final pointer-route/`TakeInput` deletion.

Evidence: the first two Debug builds exposed missing `WorldInteractionOwner`
and `InteractionExitReason` forward declarations; both were added and the final
Debug build passed in 9.8s with zero warnings. Fast passed in 31.0s; the CPU
umbrella passed in 11.0s with 129/129 doctest cases and 2,755 assertions; all
five interaction scenarios passed in 15.7s; perf completed in 32.7s; and full
passed in 53.8s with zero warnings, zero DX12 InfoQueue errors, matching
screenshots, standalone topology smoke, and the 20,001-line byte-exact physics
baseline. Comment audit: 5/5 touched source-bearing files.

Editor hover/placement preview, invalid-selection repair, and pointer-selection
planning now execute inside `RuntimeTools`. The owner consumes value-only ray
records, borrows model/physics/terrain/asset owners synchronously, and returns a
prepared selection plus exact interaction transition so cleanup still occurs
before selection commit. Run no longer decides pointer picking, selection scope,
or invalid-selection gizmo cancellation. B1f remains open for placement and
transform gesture routing, then outer pointer/`TakeInput` deletion.

Evidence: after one Debug compile exposed an unqualified zero-vector constant,
the corrected Debug build passed in 10.2s with zero warnings. Fast passed in
31.8s; the CPU umbrella passed in 11.4s with 129/129 doctest cases and 2,755
assertions; all five interaction scenarios passed in 15.9s; perf completed in
32.8s; and full passed in 53.2s with zero warnings, zero DX12 InfoQueue errors,
matching screenshots, standalone topology smoke, and the 20,001-line byte-exact
physics baseline. Comment audit: 2/2 touched source-bearing files.

Placement-scale release now executes inside `RuntimeTools`: the tool owns
preflight, placement commit, replay event emission, new-object selection, and
durable gesture teardown. A three-boolean result tells composition whether the
pointer was consumed, interactive automation must be disabled, and the input
mode edge must be published; no Run callback or broad context is retained. B1f
remains open for active transform gestures and new-gesture routing.

Evidence: the zero-warning Debug build passed; all five interaction scenarios
passed in 26.1s; and full passed in 54.3s with 129/129 doctest cases, 2,755
assertions, zero warnings, zero DX12 InfoQueue errors, matching screenshots,
standalone topology smoke, and the 20,001-line byte-exact physics baseline.
Comment audit: 2/2 touched source-bearing files.

Active editor transform drags now execute entirely inside `RuntimeTools`.
Value-only pointer/ray input drives per-frame translate, rotate, or scale
mutation; release computes single- or bounded-group replay deltas and tears down
the gesture before returning an explicit input-mode edge. Run no longer reads
gizmo start state or records editor transform events. B1f remains open for new
transform gesture starts and final editor/outer route deletion.

Evidence: the zero-warning Debug build passed in 10.0s; all five interaction
scenarios passed in 25.1s; and full passed in 54.3s with 129/129 doctest cases,
2,755 assertions, zero warnings, zero DX12 InfoQueue errors, matching
screenshots, standalone topology smoke, and the 20,001-line byte-exact physics
baseline. Comment audit: 2/2 touched source-bearing files.

New editor transform and placement-scale gestures now start through
`RuntimeTools`. Transform preparation captures immutable scale/rotate/translate
geometry before InputRouter cleanup; commit owns pointer capture, durable gizmo
state, and bounded group-start snapshots afterward. Placement-scale start is
owner-local and returns only consumed/began facts. Run no longer computes gizmo
drag planes, axis parameters, rotation angles, or start snapshots. B1f remains
open for final editor-route composition and outer pointer/`TakeInput` deletion.

Evidence: one Debug build caught the now-unused Run collider-store local; after
deletion the zero-warning Debug build passed. All five interaction scenarios
passed in 25.3s; and full passed in 54.0s with 129/129 doctest cases, 2,755
assertions, zero warnings, zero DX12 InfoQueue errors, matching screenshots,
standalone topology smoke, and the 20,001-line byte-exact physics baseline.
Comment audit: 2/2 touched source-bearing files.

Editor world-pointer composition now belongs to `InputRouter::RouteEditorPointer`.
The router consumes one immutable post-UI pointer/ray frame, sequences
RuntimeTools preview/active/start/selection operations around its own transition
cleanup, and returns a bounded ordered semantic-action result. Run's
`TickEditorWorldClick` method and declaration are deleted; Run only samples the
ray and applies composition-root interactive/input-mode results. B1f remains
open for the outer attached/manipulator/replay/launcher pointer route and final
`TakeInput`/camera helper deletion.

Evidence: the first Debug compile found one bool return left from the former Run
method; conversion to the typed route result restored the boundary and the
zero-warning retry passed. Fast passed in 31.3s; the CPU umbrella passed in
11.1s with 129/129 doctest cases and 2,755 assertions; all five interaction
scenarios passed in 15.5s; perf completed in 32.5s; and full passed in 52.2s
with zero warnings, zero DX12 InfoQueue errors, matching screenshots,
standalone topology smoke, and the 20,001-line byte-exact physics baseline.
Comment audit: 4/4 touched source-bearing files.

The complete runtime pointer priority chain now belongs to
`InputRouter::RouteRuntimePointer`. A single immutable normal/clamped ray frame
routes editor, manipulator, attached-camera selection, replay, then launcher;
the router preserves first-consumer priority and returns bounded semantic mode
actions. Run's `RouteRuntimePointerInput` method and declaration are deleted.
B1f remains open for `TakeInput`, viewport/camera helpers, and any remaining
late hardware/state reads.

Evidence: the zero-warning Debug build passed. Fast passed in 30.7s; the CPU
umbrella passed in 11.1s with 129/129 doctest cases and 2,755 assertions; all
five interaction scenarios passed in 15.1s; perf completed in 32.3s; and full
passed in 52.5s with zero warnings, zero DX12 InfoQueue errors, matching
screenshots, standalone topology smoke, and the 20,001-line byte-exact physics
baseline. Comment audit: 4/4 touched source-bearing files.

Editor viewport-look and placement wheel/drag state now execute inside
`RuntimeTools::RouteEditorViewportPlacement` from one post-UI device value.
The UI-frame coordinator applies explicit reset/mode/interactive results and
InputRouter cursor policy directly; the Run-capturing callback, Run method, and
declaration are deleted. B1f remains open for the remaining `TakeInput` UI and
keyboard callback packs, camera movement/ray helpers, and late snapshot reads.

Evidence: the zero-warning Debug build passed. Fast passed in 30.8s; the CPU
umbrella passed in 10.8s with 129/129 doctest cases and 2,755 assertions; all
five interaction scenarios passed in 15.3s; perf completed in 32.2s; and full
passed in 52.3s with zero warnings, zero DX12 InfoQueue errors, matching
screenshots, standalone topology smoke, and the 20,001-line byte-exact physics
baseline. Comment audit: 4/4 touched source-bearing files.

World-ray projection now belongs to `InputRouter`. Device-pointer and explicit
automation-point APIs borrow immutable camera/window views, apply the existing
viewport-clamp invariant, and publish value rays to pointer owners. Run's two
ray helpers and declarations are deleted. B1f remains open for the TakeInput
UI/keyboard callback packs, camera movement helper, and late frame/render
snapshot reads.

Evidence: the zero-warning Debug build passed. Fast passed in 30.5s; the CPU
umbrella passed in 11.2s with 129/129 doctest cases and 2,755 assertions; all
five interaction scenarios passed in 15.0s; perf completed in 32.3s; and full
passed in 52.7s with zero warnings, zero DX12 InfoQueue errors, matching
screenshots, standalone topology smoke, and the 20,001-line byte-exact physics
baseline. Comment audit: 5/5 touched source-bearing files.

## Remaining Work

### A. Narrow the render host

- [x] A1. Implement ownership extraction 5: replace `RuntimeRenderHostBindings`
  with the five immutable views and move replay/tool overlay production to owners.
- [x] A2. Put texture lookup/select behind the asset/render view; no pass-level
  call back into `Run` for texture handles.

### B. Move input, command, tool, and replay decisions

- [x] B1a. Characterize press/hold/release/repress, simultaneous keys, binding
  contexts, pre/post-UI phases, UI refusal, focus loss, and quick-tap behavior
  in pure CPU tests before moving producers.
- [x] B1b. Capture one immutable, fixed-size `DeviceInputFrame` per frame. It
  owns the 256-key bitset, buttons, client pointer, wheel, raw mouse delta, and
  focus state; automation mutates that value rather than a second key path.
- [x] B1c. Move key-edge memory and binding-context enforcement into the
  two-phase `InputRouter`; emit fixed ordered action records and delete
  `MappedKeyboardDispatchContext` plus its callback pack.
- [x] B1d. Publish one immutable post-UI hit/pointer snapshot. Delete duplicate
  UI/replay/editor button memories and make all consumers observe the same
  position and edge values for a frame.
- [x] B1e. Give one owner focus cancellation, cursor requests, and native mouse
  capture. Reconcile UI, replay, editor, and camera capture on focus loss.
- [ ] B1f. Delete direct `Input::Is*`/mouse-position polling from later frame,
  physics, render, editor, and replay phases; complete extraction 1's `Run`
  method/state deletion proof.
- [x] B2a. Add value-only `ApplicationExitState`. Preserve the first owned
  Lane R failure, translate nonzero OS quit codes into failure, and prevent a
  later normal quit from overwriting failure evidence.
- [x] B2b. Move input-triggered capture into a fixed `CaptureController` queue;
  validate bounded paths before enqueue and preserve post-render automation
  timing until its direct sink is retired deliberately.
- [x] B2c. Move ordinary/cinematic persistence into `RenderDefaultsStore`,
  convert writers to `SbResult`, and observe final frame-mutated values at its
  named checkpoint. Defaults are not application commands.
- [x] B2d. Move scene queue storage and submission vocabulary into
  `SceneController`; temporarily retain a scene-only execution drain in `Run`
  until C1 supplies concrete load/save authority.
- [x] B2e. Replay records only accepted owner events with explicit stable event
  codes. Failed/rejected work and raw domain-enum ordinals are never serialized.
- [x] B2f. Close extraction 2 with C1: move scene execution to
  `SceneController`, then delete `DrainSceneRequests`, scene `void*` callbacks,
  and all remaining owner bypasses. The generic queue/type and dead
  zero-producer cases were already deleted under B2b-B2e; the scene `void*`
  callback pack and generic action dispatcher are now deleted as well.
- [x] B3. Complete ownership extraction 4. `ReplayRuntime::TickWorkspace`
  consumes one typed frame view; replay owns scrub/cause/velocity/prediction,
  inspection-camera, overlay, restore/hash, startup, and probe decisions. `Run.h`
  and all `Run*.cpp` files expose no replay business method, callback pack,
  backpointer, or forwarding facade. Evidence and gates are recorded in
  `Agentic/Reports/replay_r2_workspace_20260710.md`.

### C. Scene lifecycle ownership

The first C1 dependency landed on 2026-07-11: `SceneController` physically owns
the scene-lifetime `PhysicsEngine`, `GameModelCollection` retains only a
required borrow, and all runtime/editor/replay/render consumers receive that
owner explicitly. Browser, adjacent-scene, cinematic-deck, reset, and advance
selection policy also moved into `SceneController`; the forwarding-only
`SceneRuntimeCoordinator` object/member is deleted. C1 remains open until the
load/save execution and lifecycle-event deletion proofs below are complete.

The next C1 ownership edge is local: `SceneController` now physically owns the
`GameModelCollection` beside its physics and entity stores. Run's collection
field and all 114 member accesses are deleted; consumers borrow
`SceneController::Models()`, save no longer passes the controller's own models
back through `SceneDefaultsSaveView`, required-contact updates use the owned
collection internally, and replay topology trimming no longer accepts duplicate
model/physics arguments. C1 remains open for world/terrain/camera population
and the final Load orchestration boundary.

`SceneController` now also physically owns the active `WorldEnvironment`.
The Run field and every direct member access are deleted; runtime, editor,
render, replay, save, and scene-population paths borrow `SceneController::World()`.
Save and replay restore contexts no longer duplicate the controller-owned world,
and the Debug replay probe view no longer republishes either controller-owned
models or world state. C1 remains open for terrain/camera ownership and the final
load orchestration boundary.

Evidence from the world-owner move: the final staged fast gate passed in 39.3s
with 17 candidates and no size violations; the CPU umbrella passed all four
lanes with 127/127 doctest cases and 2,730 assertions in 11.0s; replay scrub and
v2 artifact gates passed in 73.6s and 25.9s; focused physics passed in 13.1s;
and full passed in 51.2s with zero warnings, zero DX12 InfoQueue errors,
matching screenshots, standalone topology smoke, and the 20,001-line byte-exact
baseline. The first fast attempt stopped at four formatting findings before
build work; only those files were formatted before the clean rerun. Comment
audit: 15/15 touched source/test files.

The next C1 edge moves the fixed `CameraCollection` into `SceneController`.
`RunSubsystemState` no longer owns the collection or republishes a nullable
camera alias; frame, input, editor, replay, render, automation, and Director
paths borrow `SceneController::Cameras()` directly. Director helpers now accept
the concrete camera owner instead of the broad subsystem shelf, while save and
replay restore contexts derive controller-owned cameras rather than duplicating
mutable authority. C1 remains open for terrain ownership and final load
orchestration.

Evidence from the camera-owner move: the final staged fast gate passed in
42.0s with 24 candidates and no size violations; the CPU umbrella passed all
four lanes with 127/127 doctest cases and 2,730 assertions in 10.8s;
interaction-click camera paths passed in 8.5s; replay scrub passed in 74.9s;
and full passed in 50.7s with zero warnings, zero DX12 InfoQueue errors,
matching screenshots, standalone topology smoke, and the 20,001-line byte-exact
baseline. Focused Debug compiles exposed and corrected context-initializer and
const-reader integration errors before the gates. The first two fast attempts
stopped at four implementation and two header formatting findings before build
work; only the named files were formatted before the clean rerun. Comment audit:
22/22 touched source/test files.

Terrain ownership now follows the same boundary. `SceneController` owns a
`SceneTerrain` that publishes the active terrain and its flat-slope
classification atomically; `RunSubsystemState` no longer owns either field.
Runtime, editor, replay, scene-population, and render consumers resolve the
terrain through that owner. Render passes retain a stable `SceneTerrain&`
rather than mutable `unique_ptr` storage, so scene replacement cannot invalidate
their owner binding. C1 is now open only for promotion of the remaining load
orchestration and deletion of its Run execution seam.

Evidence from the terrain-owner move: the final staged fast gate passed in
40.2s with 23 candidates and no size violations; the project-filter validator
covered 593/593 production items in 1.3s; allocation policy scanned 306 files
with zero allowlist errors in 7.2s; and the CPU umbrella passed all four lanes
with 127/127 doctest cases and 2,730 assertions in 11.2s. A one-minute graphics
stress run completed 8,371 frames and 233 scene loads with empty stderr; all 135
authored scenes passed load-only DX12 activation with 135 empty stderr files in
250.7s; focused physics passed in 16.1s; and full passed in 50.7s with zero
warnings, zero DX12 InfoQueue errors, matching screenshots, standalone topology
smoke, and the 20,001-line byte-exact baseline. The first two fast attempts
stopped at implementation/header formatting; the third identified the missing
project-filter classifier for the new domain header. Each was corrected before
the clean gate. Comment audit: 18/18 touched C++ source/header files.

The full cold load transaction is now implemented by `SceneController::Load`.
Run's `LoadScene` method is deleted; startup, automation, frame advance,
graphics stress, and request execution wire concrete owners directly. The load
boundary retains no Run pointer/reference, callback, or mutable multi-domain
context. `SceneRuntimeResetContext` and `SceneRuntimeLoadBeginContext` are also
deleted; preserve/restore and checked GPU-drain inputs are explicit. C1/C3 and
B2f were closed by moving the scene-only pending-request switch into the
controller and deleting `DrainSceneRequests`.

Evidence from the load-owner promotion: the final staged fast gate passed in
39.7s with 13 candidates and no size violations; the CPU umbrella passed all
four lanes with 127/127 doctest cases and 2,730 assertions in 10.8s;
interaction clicks passed in 8.3s; and replay scrub passed in 75.1s. A
one-minute graphics stress run completed 8,517 frames and 237 scene loads with
empty stderr; all 135 authored scenes activated with 135 empty stderr files in
247.7s; focused physics passed in 13.3s; and full passed in 50.2s with zero
warnings, zero DX12 InfoQueue errors, matching screenshots, standalone topology
smoke, and the 20,001-line byte-exact baseline. Compile feedback corrected the
remaining callers and formerly implicit RunInput helpers before gates. The first
two fast attempts stopped at four implementation and one header formatting
finding before build work; only those files were formatted. Comment audit:
11/11 touched C++ source/header files.

Evidence from the collection-owner move: the final staged fast gate passed in
33.6s with 17 candidates and no size violations; the CPU umbrella passed all
four lanes with 127/127 doctest cases and 2,730 assertions in 11.2s; replay
scrub and v2 artifact gates passed in 74.9s and 26.2s; focused physics passed in
13.2s; and full passed in 52.5s with zero warnings, zero DX12 InfoQueue errors,
matching screenshots, standalone topology smoke, and the 20,001-line byte-
exact baseline. The first fast attempt stopped at formatting; the second found
one Debug replay-probe use of the removed duplicate field. Both were corrected
before the clean rerun. Comment audit: 15/15 touched source-bearing files.

The save half of C3 landed on 2026-07-11. `SceneController::SaveCurrentDefaults`
owns editable snapshots and non-editable defaults rewrites through a synchronous
`SceneDefaultsSaveView`; `Run::SaveCurrentSceneDefaults` is deleted, filesystem
and JSON failures carry Lane R `SbResult` evidence, and replay observes only a
successful write. Load orchestration and the remaining collection business
commands keep C3 open.

The temporary scene execution callback pack is deleted. Browser/demo/reset/
create/advance navigation now returns a value-only `SceneLoadRequest` with an
explicit accepted/no-load result for selecting the already-active scene;
cinematic deck selection is a separate browser-index query. Run sequencing no
longer hands `void*`, load/interactive function pointers, mutable scene state,
capture state, and style owners to a generic action dispatcher, and graphics
stress consumes the same value request. The final owner move deleted
`DrainSceneRequests`; pending scene work now executes inside `SceneController`
and records replay only after an accepted operation succeeds.

Evidence from the callback-pack deletion: the final staged fast gate passed in
41.2s with 11 candidates and no size violations; the CPU umbrella passed all
four lanes with 127/127 doctest cases and 2,730 assertions in 11.1s; a bounded
one-minute graphics stress run completed by PID timeout with empty stderr after
8,309 frames and 231 scene loads; and the full gate passed in 53.4s with zero
warnings, zero DX12 InfoQueue errors, matching screenshots, standalone physics
smoke, and the 20,001-line byte-exact baseline. Two earlier fast attempts
stopped at formatting before build/runtime work; formatting only the touched
files/signature resolved them. Comment audit: 9/9 touched source/test files.

Scene lifecycle markers now enforce transaction order and scene-owner topology
instead of merely remembering the last label. A recoverable failed attempt may
restart at `BeforeSceneUnload`, but an in-attempt phase skip is fatal;
`AfterSceneCleared`/`BeforeScenePopulate` require empty metadata/body/collider
stores, while populated/activated phases require matching counts. The unused
`LastLifecycleEvent` forwarding API is deleted and the transition contract has
direct CPU coverage. The callback pack was subsequently deleted, and the
milestone adversarial pass below added enforced concrete-owner receipts for the
remaining reset/activation phases.

Evidence from this lifecycle-transaction slice: the CPU umbrella passed all
four lanes with 126/126 doctest cases and 2,717 assertions in 14.7s; the
load-only sweep activated all 135 authored scenes with empty stderr in 274.7s;
the focused physics gate passed in 16.2s; and the corrected full gate passed in
74.1s with zero warnings, zero DX12 InfoQueue errors, matching screenshots,
standalone topology smoke, and the 20,001-line byte-exact physics baseline. The
first full attempt stopped at formatting before build/runtime work; formatting
the touched helper signature resolved it. Comment audit: 5/5 touched source
and test files.

The scene-owner milestone is closed. `SceneController::ExecutePending` owns the
fixed pending batch and its scene-only operation switch, calls the controller's
load/create/save decisions directly, and publishes replay only after success.
The caller supplies explicit cold-operation owners for the duration of the
call; the controller retains no `Run` pointer/reference, callback pack, broad
context, or authority over those consumers. The ordered lifecycle phases are
enforced around the concrete-owner reset/populate/activation work inside
`SceneController::Load`, so consumers cannot observe a phase label detached
from its corresponding mutation. Deletion proof finds no `Run::LoadScene`,
`DrainSceneRequests`, scene callback/context pack, or collection scene wrapper.

Evidence from the pending-owner closure: the staged fast gate passed with
three candidates, no size violations, 127/127 doctest cases, and 2,730
assertions; the CPU umbrella passed all four lanes in 11.3s; interaction clicks
passed on the native batch retry in 8.5s after the first PowerShell-hosted
launch was denied by Device Guard; and full passed in 53.0s with zero warnings,
zero DX12 InfoQueue errors, matching screenshots, standalone topology smoke,
and the 20,001-line byte-exact physics baseline. Comment audit: 3/3 touched
source-bearing files.

The required scene-milestone adversarial pass found two blocking C2 defects.
Lifecycle labels were validated but did not prove concrete-owner consumption,
and diagnostics plus interactive/manual-reset state could mutate before a
failed GPU drain returned. The correction splits preparation from commit:
queue validation, reset snapshot capture, and the checked GPU drain are
read-only; only a successful preparation may publish `BeforeSceneUnload` and
commit controller bookkeeping. Diagnostics consumes the old-scene unload edge
through a typed Debug/Release boundary, reset owners accumulate receipts beside
their concrete calls, replay acknowledges activation after timeline reset, and
`SceneRuntime` fatally rejects missing or extra receipts. Manual-reset and
interactive state now commit only with a successful load; no-load navigation
retains its intentional interactive transition. The stale C1/Run comments were
also removed.

Final-source evidence after the adversarial correction: fast passed in 33.9s
with 13 candidates, 127/127 doctest cases, and 2,736 assertions; the CPU
umbrella passed all four lanes in 10.7s; all 135 authored scenes activated in
257.4s with zero-warning builds and no lifecycle/topology/receipt fatal; and
full passed in 52.4s with zero warnings, zero DX12 InfoQueue errors, matching
screenshots, standalone topology smoke, and the 20,001-line byte-exact physics
baseline. The final touched-source comment audit is 13/13. An intermediate
interaction run also passed both reports in 7.6s. The first final fast attempt
found a Release declaration inside a Debug guard; moving the typed diagnostics
boundary outside that guard produced the clean rerun.

The one permitted repeat adversarial pass found the same pre-drain interactive
mutation duplicated in the pre-UI adjacent-navigation helper and the graphics-
stress helper. Both now delegate every accepted request, including accepted
no-load navigation, to `SceneController::Load`; neither mutates interactive
state or short-circuits the owner boundary. The final source search finds no
`request.enterInteractiveSceneRun` branch calling `EnterInteractiveSceneRun`
outside the controller. Fast passed in 26.7s with two candidates; a one-minute
graphics stress run exercised the corrected helper for 8,533 frames and 238
scene loads with empty stderr; both interaction reports passed in 7.5s; and
full passed in 52.3s with zero warnings, zero DX12 InfoQueue errors, matching
screenshots, standalone topology smoke, and the 20,001-line byte-exact physics
baseline. Comment audit: 2/2. The scene-milestone adversarial loop is closed.

- [x] C1. Implement ownership extraction 3: `SceneController` owns the
  preallocated scene/entity metadata store, scene-lifetime `PhysicsScene`,
  load/reset state, browser selection, adjacent load, and deck movement.
- [x] C2. Replace `Run` scene callbacks with explicit `BeforeSceneUnload`
  through `AfterSceneActivated` lifecycle events consumed by concrete owners.
- [x] C3. Move scene save/load orchestration behind `SceneController` and delete
  `Run`/`GameModelCollection` scene business wrappers. The writer consumes a
  borrowed owner view, never `Run` callbacks or collection-order identity.

Coordinate C with `physics-authority-and-identity.md` C0-C5 and
`Agentic/Reports/scene_asset_roundtrip_design_20260710.md`.

### D. Mega-TU decomposition

- [ ] D1. Split `RunInput.cpp` only as ownership extraction 1 lands; do not move
  the same code twice.
- [ ] D2. Split `TestSceneParser.cpp` by schema domain
  (bodies/assets/groups/water/cameras). It already uses nlohmann JSON; this is
  schema/ownership decomposition, not a JSON-library replacement.
- [ ] D3. Convert shared `.inl` composition to real translation units as owners
  move; pair hot-path conversions with perf validation.

### E. Collapse compatibility surfaces

- [ ] E1. Retire `RunInternal.h`; constants/helpers move to a single owner and
  no sibling `Internal` header replaces it.
- [ ] E2. Delete `Run::*` wrappers that only forward to a subsystem.
- [ ] E3. Slim `Core/Common.h`: remove the stale config compatibility include
  and alias includes according to current consumers.

### F. Prove the god object is gone

- [ ] F1. Rebuild the final `Run` ownership inventory from current source. Map
  every remaining `Run` method and mutable field to one of the five permitted
  shell responsibilities (owner construction/wiring, startup/shutdown, OS
  message pump, top-level frame order, final exit reporting), or move it to a
  concrete domain owner. Inspect all `Run*.cpp`, not only `Run.cpp`.
- [ ] F2. Audit the extracted owners and their boundary records for sideways
  migration. `InputRouter`, command owners, `SceneController`, `ReplayRuntime`,
  and `RuntimeRenderer` must not retain `Run*`/`Run&`, callback bags, `void*`
  contexts, friend backdoors, broad mutable contexts, forwarding-only APIs, or
  authority over unrelated domains.
- [ ] F3. After every other runtime-shell item and required gate passes, run one
  independent read-only adversarial ownership review. Record concrete evidence
  for zero remaining god-object or disguised shared-state-hub findings in
  `Agentic/Reports/<date>/runtime-shell-final-ownership-review.md`, including
  the final method/field inventory and inspected substitute-hub surfaces. Any
  credible finding reopens its owning item, is fixed in this plan, and blocks
  closure rather than becoming optional follow-up debt.

## Binding And Open Decisions

| Decision | Binding answer or remaining question |
|---|---|
| Input/UI ordering | **Binding:** sample `DeviceInputFrame` once; run `InputRouter::BeginFrame`; UI then publishes one immutable `UiInputHitSnapshot`; run `CompleteFrame`; later phases consume values only. |
| Command ordering | **Binding:** at the unconditional end-of-input checkpoint, persist render defaults from final UI-mutated values, process input-triggered capture, then accept at most the first scene transition while preserving ordered non-transition scene work. Application exit never enters a queue; `ApplicationExitState` resolves it at the message-loop boundary. Requests produced after the checkpoint run on frame N+1. |
| Scene load contract | Which state survives reset/load and which lifecycle event clears interaction, replay, diagnostics, and camera state? |
| Fixed-step ownership | `SimulationSystem` remains timestep owner; do not recreate a generic simulation facade. Decide only which frame coordinator calls it. |

## Mapping Evidence And Defects To Preserve

The 2026-07-10 call-site audit found 11 independent pointer-position reads,
repeated late key polling in frame/render code, binding contexts recorded but
not generically enforced, and four competing native-capture authorities. The
input extraction is incomplete until those direct consumers are deleted; a
file move around `TakeInput` is not acceptance.

The same audit found that nonzero `WM_QUIT` values are currently discarded:
capture/window failures post exit code 1, `Run::Execute` breaks, then returns
success. It also found zero producers for generic `AdvanceScene` and `Quit`
commands, swallowed file-write results, replay recording attempted rather than
accepted work, and queue overflow without owner/high-water/phase diagnostics.
B2 tests must prove each corrected behavior before the omnibus vocabulary is
deleted.

## Acceptance

- [ ] All five ownership-extraction deletion proofs pass.
- [ ] The complete logical `Run` surface exposes only owner construction/wiring,
  startup/shutdown, OS message pumping, top-level frame order, and final exit
  reporting; it owns no mutable subsystem business state.
- [ ] `RuntimeRenderHost` is removed or a small immutable context.
- [ ] `RunInternal.h` is deleted without a replacement shared-state header.
- [ ] No `*Internal`, `*Context`, `*Services`, `*Bindings`, callback pack,
  forwarding facade, stored host pointer/reference, friend access, or renamed
  compatibility surface recreates `Run` authority.
- [ ] Each extracted owner is cohesive and does not combine unrelated input,
  scene, replay, render, UI, physics, tool, capture, defaults, or diagnostics
  authority.
- [ ] No runtime source file in the reconciled inventory exceeds 1,500 lines
  without a cohesion justification and named follow-up owner.
- [ ] A new feature can enter input, scene, replay, or render through its owner
  without adding a `Run::*` method.
- [ ] The final independent adversarial review reports zero credible god-object
  findings and its evidence report is committed. Any credible finding reopens
  this plan and blocks completion.

## Validation

| Slice | Required gate |
|---|---|
| Render host narrowing | CPU DX12 architecture tests + renderer gate; add full gate if behavior shifts |
| Input/interaction ownership | CPU-test umbrella + interaction policy + interaction clicks + full gate |
| Replay workspace | CPU-test umbrella + replay scrub + focused interaction proof + allocation gate when capacity changes |
| Scene ownership | parser/round-trip tests + full gate + physics gate when creation/reset changes |
| Mechanical file splits | owning file-to-validation gate |
| Hot `.inl` → TU conversion | owning area gate + perf gate |

`validate_full` is now the broad CPU/runtime superset after
`validation-gate-integrity.md` V2; keep the owning focused gates alongside it.
