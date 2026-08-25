# Recorded Interaction Playback Cursor Plan

Date: 2026-08-22
Status: Owner-parked by explicit owner direction on 2026-08-25. 3/4 phases
complete; RIC3 is not selectable until the owner reactivates this plan.
Impact area: recorded interaction-manifest playback, detached Runtime/Automation
presentation values, GameUI/ImGui draw ordering, DX12 UI submission, tests, and
documentation
Owner: Runtime/Automation owns recorded-turn evidence; the post-RBS product UI
owner composes the fake cursor; Runtime/Render submits backend-neutral draw values
Priority: Parked and excluded from active master-plan ordering. If reactivated,
RIC3 consumes the completed RIC0-RIC2 path.
Commit name: `RECORDED_CURSOR`

## Owner Direction

When a recorded interaction manifest is played back, show a clearly visible
software-drawn cursor at the recorded pointer position whenever that replayed
turn logically has a cursor. Hide only this fake cursor during free-look,
mouse-look, focus loss, absent-pointer turns, or another cursorless interaction
state.

The hardware cursor is explicitly outside this feature. It remains the live
operator's cursor and must never be hidden, moved, captured, released, restyled,
or otherwise changed on behalf of recorded playback. The fake cursor is an
additive replay-viewing overlay, so both cursors may be visible simultaneously.

This plan applies to complete recorded interaction manifests consumed through
`--interaction-script`. It does not add pointer trajectories to ordinary
`.skreplay` simulation artifacts or reinterpret the Replay timeline's event
stream as input evidence.

---

## Dated Baseline Evidence

Evidence recorded on 2026-08-22 at `5e0f78279` on `main`, with the existing
dirty Physics/Ragdoll work and ledger edits preserved as user-owned:

- CodeGraph is current at 1,179 files, 36,661 nodes, and 110,198 edges.
- `RecordedInputFrame` already retains normalized absolute pointer coordinates,
  pointer availability, focus, buttons, wheel, raw mouse deltas, and an optional
  semantic UI anchor for every recorded turn.
- `InteractionAutomationRecorder::CapturePendingTurn` captures that complete
  device value without moving the operating-system pointer.
- `InteractionAutomationController` resolves semantic anchors and publishes the
  selected recorded turn through
  `InteractionAutomationInputDriver::PublishRecordedFrame`.
- `PublishRecordedFrame` already scales normalized coordinates into the current
  client area and forwards them through `Input::AutomationState`; no new
  interaction-manifest field or schema version is required for the basic cursor.
- `InputRouter::EvaluatePointerPresentation` already resolves frame-local
  mouse-look/editor/replay-look cursor policy. RIC0 must ratify which of those
  logical facts govern only the fake cursor without changing native presentation.
- Native cursor visibility/capture belongs to `Input`, `InputRouter`, and
  `Window`. Those owners and their `ShowCursor`, `SetCursor`, capture, and cursor
  request paths are protected non-targets of this plan.
- Backend-neutral triangles and rectangles already flow through `UIDrawList`,
  `UIDrawContext`, `UiDrawSubmission`, and `UiTextPass`. A vector cursor therefore
  needs no texture asset, descriptor, or new renderer capability.
- GameUI/replay overlays and the development ImGui surface currently have
  distinct late draw paths. RIC2 must prove one final cursor layer appears above
  the selected operator surface rather than assuming GameUI ordering covers
  ImGui.

Historical counts describe the starting point; they are not budgets or closure
ratchets.

## Goals

- Publish one detached, frame-local recorded-cursor presentation value from the
  already selected recorded turn.
- Draw a recognizable resolution-independent fake cursor at the exact resolved
  playback position.
- Make fake-cursor visibility follow replayed logical cursor availability,
  including pointer absence, focus, free-look/mouse-look, capture, and playback
  completion.
- Keep the hardware cursor completely independent and unchanged.
- Draw above world content, GameUI, replay overlays, and ImGui without adding a
  second retained input or interaction owner.
- Preserve interaction-manifest compatibility, semantic-anchor resolution,
  deterministic playback, fixed-capacity/no-growth behavior, and existing input
  routing.
- Provide focused behavioral, native-cursor non-interference, draw-order, and
  visual evidence.

## Non-Goals

- No hardware cursor hiding, movement, capture, release, clipping, replacement,
  styling, or position synchronization.
- No `SetCursorPos`, `ShowCursor`, `SetCursor`, `SetCapture`, `ReleaseCapture`,
  `Input::SetSystemCursorVisible`, `InputRouter::RequestCursorVisible`, or native
  capture request made on behalf of the fake cursor.
- No pointer samples added to `.skreplay`, Replay event commands, solver
  checkpoints, prediction archives, or replay reserve storage.
- No synthetic pointer movement fed back from the drawn cursor into input, UI
  hit testing, camera control, or world interaction.
- No retained fake-cursor state owner, callback pack, service/context bag,
  forwarding facade, or Runtime-to-Rendering feature contract.
- No texture asset or animated cursor framework unless RIC0 proves vector draw
  values cannot meet the visibility requirement.
- No redesign of cursor policy, camera modes, editor interaction, GameUI,
  replay controls, ImGui, or recording format.
- No golden refresh merely to accept the new overlay.

## Dependencies And Decisions

- `RUNTIME_BOUNDARIES` RBS0-RBS7 and `GAME_UI_COMPONENTS` UI0-UI6 execute first.
  RIC work consumes their final product-UI, submission, package, and project
  ownership instead of introducing a temporary pre-separation path.
- Runtime/Automation owns recorded-turn selection and may publish a detached
  value. It must not become a renderer, UI, or native-pointer owner.
- The post-UI composition owner decides whether the fake cursor is visible and
  appends component-neutral geometry. Runtime/Render remains only the printer.
- The fake cursor's visual position is the already resolved playback position:
  semantic anchor when available, otherwise normalized current-client mapping.
- Cursorless state is judged from replayed frame/mode facts, not from the live
  operator's hardware cursor position or foreground movement.
- Implementing this plan follows `Agentic/Skills/orchestrator/SKILL.md`,
  including fresh worker delegation, independent review, commits, pushes, and
  terminal handoff.

---

## Target Behavior Contract

```text
Recorded manifest turn
    -> InteractionAutomationInputDriver resolves current client position
    -> normal Input/InputRouter processing remains unchanged
    -> detached recorded-cursor presentation value
    -> product UI composes vector cursor geometry
    -> final UI submission draws above the selected operator surface

Live hardware cursor
    -> existing Window/Input/InputRouter native policy only
    -> never reads or reacts to the recorded-cursor presentation value
```

### Visibility contract

The fake cursor is visible only when all applicable facts are true:

- recorded-manifest playback is active and has published a real recorded turn;
- the selected turn has a resolved client pointer position;
- the recorded application-focus value permits pointer presentation;
- the replayed interaction/camera state is cursor-bearing rather than free-look,
  mouse-look, viewport-look, tool capture, or another existing cursorless state;
- the position resolves inside the active client viewport; and
- playback has not failed, completed, or cleared its synthetic frame.

RIC0 owns the exact mode/policy matrix. It must use existing authoritative
logical facts or a narrow pure predicate; it may not query or mutate hardware
cursor state to decide fake-cursor visibility.

### Hardware-cursor non-interference contract

For every playback turn, enabling or disabling the fake cursor must leave all
native cursor/capture requests and commits exactly as they would be without the
feature. Tests must cover visible fake cursor, cursorless replayed mode, absent
pointer, focus loss, playback completion, and overlapping live/fake positions.
The feature may observe detached logical policy values; it may not apply native
policy.

### Visual contract

- Use a compact high-contrast vector arrow with a dark outline and light fill,
  clamped only for safe drawing at the viewport edge without changing the
  recorded input coordinate.
- Preserve the recorded hot point at the arrow tip.
- Optional button feedback may change only fake-cursor presentation and must be
  derived from the same recorded turn.
- Draw after the selected GameUI or ImGui surface and after replay overlays so
  the cursor cannot disappear behind the action it is demonstrating.
- The draw path is fixed-capacity and allocation-free in steady playback.

## Exception Table

No exception is approved at registration. A temporary exception must name the
exact edge/type, owner, reason, preserved behavior, deletion condition, and
deleting phase. No exception may survive RIC3.

| Exception | Owner | Reason | Behavioral constraint | Deletion condition | Phase |
|---|---|---|---|---|---|
| (none) | - | - | - | - | - |

---

## Phase RIC0 - Ratify Visibility, Ordering, And Non-Interference

**Goal:** Freeze the precise product contract and negative controls before
adding presentation code.

- [x] Record branch, commit, dirty files, CodeGraph status, inherited validation
      findings, and the post-UI6 source/project ownership map.
- [x] Inventory the recorded-frame capture, semantic-anchor resolution,
      playback publication, input routing, native cursor/capture, GameUI,
      replay-overlay, ImGui, screenshot, and Present paths.
- [x] Build an explicit fake-cursor visibility matrix for focused/unfocused,
      pointer present/absent, ordinary cursor mode, free-look/mouse-look,
      editor viewport look, replay inspection look, tool capture, playback
      failure, and playback completion.
- [x] Identify the final draw point that is above both GameUI and ImGui and is
      included in backbuffer screenshots.
- [x] Add focused pure-policy tests and false-pass controls for the visibility
      matrix.
- [x] Add a hardware-cursor non-interference seam or observation that proves
      fake-cursor decisions do not change native cursor visibility/capture
      requests or commits.
- [x] Ratify the exact detached value, source placement, project ownership, and
      draw geometry without a new retained owner or feature-specific Rendering
      vocabulary.

### RIC0 Baseline And Ownership Evidence

RIC0 started from pushed integration commit `3269f3f9026974186a50ea358994ea76e07174ff`
on isolated branch `codex/ric0-contract`. The worktree was clean. The shared
repository CodeGraph index was current at 1,216 files, 37,937 nodes, and 114,637
edges; the isolated worktree intentionally had no second ignored `.codegraph/`
copy. The inherited causal-depth oracle and historical Physics performance
baseline remain external recorded failures. RIC0 neither owns nor refreshes
either baseline.

The post-UI6 ownership map is:

| Responsibility | Current owner | Project | RIC constraint |
|---|---|---|---|
| Recorded device-frame capture and manifest tape | `Runtime/Automation` | `SKULLBONEZ_CORE` | Preserve schema, reserve, and sidecar behavior. |
| Semantic-anchor resolution and selected-turn publication | Automation plus App composition | `SKULLBONEZ_CORE` | Reuse the exact resolved client position published to normal input. |
| Retained input, logical pointer policy, and native desired/committed state | `Runtime/Input/InputRouter` | `SKULLBONEZ_CORE` | Observe detached logical facts only; add no second retained owner. |
| Product fake-cursor visibility and bounded geometry | `Runtime/UI` | `SKULLBONEZ_CORE` | Pure frame policy and component-neutral draw values only. |
| Generic UI draw submission and frame-graph order | `Runtime/Render` | `SKULLBONEZ_CORE` | Print an existing `UIDrawList`; add no feature contract below Runtime. |
| Backend-neutral fixed-capacity draw commands | `UI/UIDrawList` | `SKULLBONEZ_UI` | Reuse unchanged generic triangles; no cursor vocabulary enters UI foundation. |
| DX12 graph execution, backbuffer capture, and Present | `Rendering/DX12` | `SKULLBONEZ_RENDERING` | No RIC source, descriptor, texture, or backend API enters Rendering. |
| Focused behavioral and negative-control coverage | `SkullbonezTests` | `SKULLBONEZ_TESTS` | CPU-only policy/non-interference tests in RIC0. |

### RIC0 Source-Path Inventory

The selected-turn path is fixed as follows:

1. `Run::CaptureInteractionRecordingTurn` copies the single
   `InputRouter::DeviceFrame()` after normal routing and asks GameUI for an
   optional semantic anchor only when a real client pointer exists.
2. `InteractionAutomationRecorder::CapturePendingTurn` stores normalized
   coordinates, pointer availability, recorded focus, buttons, wheel, raw
   deltas, complete key words, and the optional anchor. An anchor cannot create
   a pointer sample.
3. `TickInteractionAutomationBeforeInput` selects exactly one recorded turn,
   resolves its anchor through `InGameUI::ResolveInteractionAnchor`, and passes
   that point to `InteractionAutomationInputDriver::PublishRecordedFrame`.
4. `PublishRecordedFrame` gives the semantic point precedence; otherwise it
   maps normalized coordinates into the inclusive current-client pixel domain
   `[0,width-1] x [0,height-1]`. It publishes only `Input::AutomationState` and
   never moves the operating-system pointer.
5. Normal `Input`/`InputRouter` routing publishes device, UI, runtime-mode, and
   `PointerPresentationPolicy` facts. `InputRouter` remains the sole owner of
   native capture/cursor requests and commits; App remains the sole caller of
   `Input::SetNativeMouseCapture` and `Input::SetSystemCursorVisible`.

The late presentation order is world, GameUI operator draw list, generic UI
overlay, Planning replay overlay, UI finalization, and then the selected ImGui
surface. The approved RIC2 insertion point is one generic `UIDrawList` graph
pass immediately after `RuntimeRenderer::RenderDevelopmentUi` returns and
before `RunPostDrawDiagnosticsPhase`, `TickScreenshots`, frame-graph
finalization, and Present. It is therefore above both operator surfaces and is
already part of every backbuffer screenshot path without a second capture or
Present.

### RIC0 Visibility Matrix

`PublishedRealTurn` means a selected manifest turn exists; the neutral
zero-turn baseline publication is not a real turn. `CursorBearing` is a pure
classification of existing runtime mode, pointer-presentation, and interaction
capture facts. It never reads live hardware state.

| Playback/focus/pointer/mode row | Fake cursor | Rationale |
|---|---:|---|
| Published real turn, focused, resolved in-client pointer, ordinary cursor-bearing mode | Visible | Canonical playback-viewing state. |
| Published real turn with live hardware cursor at the same point | Visible | Live and fake cursors are independent additive surfaces. |
| Published real turn while UI owns an ordinary click/drag | Visible | The cursor must remain visible over the action it demonstrates. |
| Passive replay inspection with no inspection-look gesture | Visible | Inspection alone is cursor-bearing. |
| Playback inactive or not a recorded manifest | Hidden | No recorded-turn evidence exists. |
| Neutral zero-turn baseline publication | Hidden | Baseline application is not a recorded pointer turn. |
| Playback failed | Hidden | Terminal failure clears presentation. |
| Playback completed | Hidden | Completion clears the synthetic frame. |
| Selected turn has no pointer | Hidden | An anchor cannot fabricate pointer availability. |
| Semantic/fallback position is unresolved or outside the active client | Hidden | Drawing cannot reinterpret the input coordinate. |
| Recorded application focus is false | Hidden | Fake visibility follows recorded focus, not desktop focus. |
| Ordinary right-held free-look/mouse-look | Hidden | Camera look owns the replayed pointer. |
| Fly-camera mouse-look | Hidden | Fly look is cursorless even when a client point is present. |
| Editor viewport look | Hidden | The editor viewport owns mouse-look presentation. |
| Replay inspection look | Hidden | Replay camera look owns the pointer for that turn. |
| Placement-preview native-cursor suppression | Hidden | Existing logical pointer policy classifies the turn cursorless. |
| `CameraLook` interaction capture | Hidden | Camera capture is cursorless. |
| `ToolGesture` interaction capture | Hidden | A tool-captured pointer is not a demonstrative free cursor. |

RIC0 approves no button-dependent styling. The later detached value therefore
needs no recorded button fields: it is a trivially copyable frame value carrying
only real-turn publication, resolved client `x/y`, pointer availability, and
recorded focus. Automation produces it from the same mapping used by
`PublishRecordedFrame`; App joins existing mode/capture facts; Runtime/UI alone
decides visibility.

The approved geometry is two fixed-capacity triangles: a dark outer arrow and a
smaller light fill, with the recorded hot point at their shared tip. Near the
right or bottom edge the arrow body flips horizontally or vertically so it
stays inside the viewport; the hot point never moves. The command high water is
exactly two, with no text, texture, descriptor, animation, or click-feedback
state.

RIC2 treats the exact 14-by-21 outer body, 10-by-16 inner body, and dark/light
color values as a provisional, reversible presentation decision. The durable
contract is the exact recorded hot point, independent axis flipping or
contraction at viewport edges, outer-then-inner order, and fixed 0-or-2 command
shape with no text, resource, allocation, or native-pointer consequence.

RIC2 also carries inherited formatter-only cleanup in
`InteractionAutomationInputDriver.cpp`, `InteractionAutomationInputDriver.h`,
and `RecordedCursorFrame.h`. The repository formatter changes layout only in
those three RIC1-owned files; no token, behavior, ownership, or RIC1 contract
changes under this task.

### RIC0 Validation Evidence

- `clang-format 21.1.8 --dry-run --Werror` passed for the new policy header and
  focused test; `git diff --check` passed; exact write scope was 8/8 approved
  paths.
- `tools\validate_project_filters.bat` passed with 0 errors across 862
  production project and filter items. The partial `SKULLBONEZ_TESTS` project
  check passed with 0 errors across 174 project and filter items.
- `tools\validate_dependency_graph.bat` passed with the generated proof current,
  0 repair-plan debt, and 0 findings.
- The Profile `SKULLBONEZ_TESTS.vcxproj` build passed with the configured v145
  toolset, 0 warnings, and 0 errors.
- `Profile\SKULLBONEZ_TESTS.exe "--test-case=Recorded cursor presentation*"
  --no-skip --no-colors` passed 3 test cases and 27 assertions, including
  false-pass controls for both native capture and visibility observations.
- No baseline, golden, manifest/schema, Replay storage/reserve, Rendering/UI
  foundation, native cursor API, hardware capture policy, or GPU artifact was
  changed. The registered ImGui and Tracy submodules were initialized at their
  pinned commits for the local build with zero superproject gitlink diff.

**Acceptance:** Every relevant mode has one expected fake-cursor outcome; the
topmost draw point is identified for both operator surfaces; negative controls
can detect a hardware-cursor mutation; no manifest, Replay, native host, or
renderer feature contract needs to change.

## Phase RIC1 - Publish The Detached Recorded-Cursor Value

**Goal:** Carry the already resolved playback pointer into presentation without
creating another input owner.

- [x] Define the smallest trivially copyable frame value needed to draw the fake
      cursor: real-turn publication, pointer availability, resolved client
      position, and recorded focus, with no button or styling facts.
- [x] Build that value from the current recorded turn after semantic-anchor or
      normalized-coordinate resolution.
- [x] Evaluate visibility from RIC0's pure logical policy and current replayed
      mode facts without reading or applying native cursor state.
- [x] Clear the value deterministically on absent pointer, focus loss,
      cursorless mode, failure, completion, scene replacement, and automation
      shutdown.
- [x] Keep normal `Input::AutomationState`, `InputRouter`, UI hit testing,
      camera, world interaction, and recorded timing unchanged.
- [x] Extend focused tests for coordinate mapping, semantic-anchor precedence,
      mode transitions, exact normal-input equivalence, native-state byte
      equality, and terminal clearing.
- [x] Prove no interaction-manifest schema, sidecar digest behavior, Replay
      storage, reserve privilege, or steady-state allocation changed.

**Acceptance:** Presentation receives one exact detached value per published
recorded turn; all clearing and cursorless transitions are deterministic; the
input and artifact paths are byte-for-byte unchanged outside the new value;
native cursor/capture evidence matches the pre-feature path.

### RIC1 Publication Evidence

Automation owns `RecordedCursorFrame`, a trivially copyable five-field value
projected from the same `Input::AutomationState` assembled by
`PublishRecordedFrame`. Semantic positions override mapped coordinates only
when the selected turn has a real pointer. `RunFrame` keeps one stack-local
copy, joins replayed logical capture/input/editor facts, filters through the
RIC0 policy, and retains no generation or pointer owner. RIC1 deliberately
terminates at that final-draw-seam local; RIC2 consumes it after development UI
and before screenshots/Present, then removes the temporary non-consumption
cast.

Focused Profile tests passed 6/6 cases and 91/91 assertions for production
mapping, semantic precedence, exact normal-input equivalence, policy priority,
all clear paths, and native desired/committed byte equality. Profile and
Automation builds passed with zero warnings and errors. Dependency
proof/fixtures/repository scan, allocation self-test/repository scan, project
filters, build-configuration consistency, and affected ownership inventories
passed. No manifest/schema/sidecar/Replay/native API, baseline, golden, or
tracked artifact changed; ImGui and Tracy were initialized locally at their
pinned commits with zero gitlink diff.

## Phase RIC2 - Draw The Topmost Fake Cursor

**Goal:** Render a polished software cursor above every supported operator
surface while leaving the hardware cursor untouched.

- [x] Implement the approved vector arrow from component-neutral bounded draw
      values, preserving the recorded hot point and viewport-edge safety.
- [x] Submit the cursor after GameUI, replay overlays, and ImGui according to
      the RIC0 ordering proof.
- [x] Ensure hidden/minimized GameUI and the selected development surface do not
      accidentally suppress an otherwise visible fake cursor.
- [x] Keep the cursor out of text-only or capture paths only where RIC0's product
      contract explicitly requires it; ordinary playback screenshots must show
      the cursor.
- [x] Add deterministic draw-command/fingerprint tests for visible, hidden,
      edge, explicit no-click-feedback, and operator-surface cases.
- [x] Add an unchanged recorded-manifest visual witness showing motion across
      world content and UI controls, free-look disappearance, reappearance, and
      pair it with explicit native cursor/capture non-interference tests.
- [x] Prove fixed command capacity, no post-start allocation, no new texture or
      descriptor, and no DX12 validation error.

**Acceptance:** The fake cursor is readable and topmost on GameUI and ImGui,
tracks the resolved recorded position, disappears only under the ratified
logical conditions, appears in playback screenshots, and causes no hardware
cursor/capture or input-routing change.

### RIC2 Validation Evidence

The pre-edit and post-edit replays both used the exact unchanged
`TestOutput/recordings/20260822T004519Z/interaction.json` manifest. Its SHA-256
remained `17C2A6B8D0BCF6E952BC35F39BE728A571ABB4DAAA1FA38CB4EE14FA429ECCF1`,
the adjacent scene remained
`9EF1CE4A4EFB84C8F26C6CD473C2E9D76B8E884F1AF1FD8111B1D3FCDA376880`, and all
16 original manifest-directory files rehashed identically to the pre-edit
inventory. The pre-edit executable SHA-256 was
`CCD8C25E8B43C025BA919315A966E4B39AB0E525664198B833E1DE88B650AEE2`; the
post-edit executable SHA-256 was
`CE3A6264CCA0FAAD5A1EAD4BD1724186E8E29785AC631E30D5626F6E0523E1C7`, built
from base `e3e346d462698d1af2ad933a2fc7f16792f3122b` plus this exact leased diff.
The canonical post-edit report recorded `ok=true` and 412 frames, while the
fresh trace contained 414 JSONL rows.

Native renderer captures under
`TestOutput/validation/RECORDED_CURSOR_RIC2/post_edit_visual/` show the light
inner/dark outer arrow before right-look, no fake cursor while right-look is
active, and the arrow again at the later recorded point after release. The
matching trace rows report `uiMinimized=true` in all three states, proving that
the minimized GameUI did not suppress it. Exact crop inspection followed the
repository visual-QA workflow after an earlier capture landed on an ambiguous
turn. The native input non-interference test independently snapshots requested
and committed native mouse-capture state before cursor composition and proves
both remain byte-identical afterward; the draw owner contains no native API
access.

Focused Profile tests passed 9/9 cases and 204/204 assertions, including the
fixed `0x5DA1E2565D540004` draw fingerprint, exact outer-then-inner command
shape, independent right/bottom flips, tiny-viewport contraction, fixed 0-or-2
capacity, zero text/overflow, surface neutrality, and native-state false-pass
controls. `validate_automation`, `validate_ui_stress`, and
`validate_dx12_renderer` passed; both graphics gates reported zero DX12
InfoQueue errors, and the standalone renderer matched every committed baseline.
`run_graphics_stress.bat 1` completed its bounded minute and stopped through
the expected PID timeout. Formatting, dependency proof/repository scan,
project-filter validation (864/864), build-configuration consistency (zero
blocking diagnostics), `git diff --check`, and the exact 12-path scope audit
passed. No baseline, golden, manifest, schema, sidecar, Replay store/reserve,
FP4 path, or tracked validation artifact changed. Independent review remains
owned by RIC3 because no RIC2 finding changed the ratified risk.

## Phase RIC3 - Terminal Behavioral And Ownership Closure

**Goal:** Prove replay-viewing behavior, architecture, native non-interference,
and visual ordering end to end.

- [ ] Replay the exact unchanged RIC2 manifest and preserve the command, report,
      trace, screenshot, process result, and observed visibility sequence.
- [ ] Verify the trace proves recorded positions/actions remain unchanged while
      the presentation observation reports the expected fake-cursor state.
- [ ] Run focused tests for visibility, clearing, coordinate mapping, draw
      fingerprints, command capacity, and native cursor/capture
      non-interference.
- [ ] Exercise GameUI, ImGui, hidden/minimized UI, free-look/mouse-look, focus
      loss, viewport edges, playback completion, and overlapping live/fake
      cursors.
- [ ] Audit every touched source-bearing file with the comment-style audit and
      reconcile the exact touched-file checklist.
- [ ] Run affected dependency, aggregate, signature, complexity, reachability,
      glossary, allocation, build-configuration, Automation, UI stress, DX12,
      screenshot, and graphics-stress gates.
- [ ] Review the diff for any native cursor API call, Replay schema/storage
      change, new retained input owner, broad context, callback bridge, reverse
      dependency, or feature vocabulary below Runtime.
- [ ] Obtain independent interaction-ownership and visual-ordering review after
      fixes, then run `tools\agent_validate.bat --plan-completion` once.
- [ ] Record final validation, review fixes, draw capacity/high water,
      allocation result, baseline disposition, source placement, and the empty
      exception table.

**Terminal acceptance:** Recorded interaction playback displays one accurate,
topmost fake cursor in cursor-bearing turns and none in cursorless turns; the
hardware cursor remains independently visible/controlled under exactly the
same native policy as before; input routing and interaction artifacts are
unchanged; both operator surfaces and screenshots show the required result;
no new owner, forbidden edge, runtime allocation, schema change, native cursor
side effect, or unauthorized golden refresh exists; all required gates and
independent review pass.

---

## Validation Map

Heavy validation remains concentrated in RIC3.

| Phase | Focused iteration evidence | Pre-commit/closure gates |
|---|---|---|
| RIC0 | visibility matrix, draw-order map, native non-interference false-pass controls | documentation and focused CPU policy tests |
| RIC1 | coordinate/anchor, transitions, clearing, unchanged input/artifact tests | focused unit tests, dependency and allocation scans, Automation build |
| RIC2 | draw fingerprints, GameUI/ImGui ordering, unchanged-manifest screenshot witness | unit tests, Automation, UI stress, DX12, bounded graphics stress |
| RIC3 | exact manifest replay, trace/screenshot evidence, audits and independent review | all focused rows plus fast/CPU, Automation, UI stress, DX12, graphics stress, full/plan-completion |

## Mandatory Review Questions

1. Can any fake-cursor code call, request, commit, infer, or alter hardware
   cursor visibility, position, style, clipping, or capture?
2. Is the cursor value detached and frame-local, with Runtime/Automation still
   owning recorded-turn evidence and InputRouter still the sole retained input
   owner?
3. Does visibility follow the replayed logical mode/focus/pointer state rather
   than the live hardware cursor?
4. Is the hot point exactly the position used by normal recorded input routing,
   including semantic-anchor precedence?
5. Is the cursor topmost for GameUI, replay overlays, ImGui, and screenshots?
6. Does playback completion or failure clear presentation without changing the
   native cursor path?
7. Are drawing and policy fixed-capacity, allocation-free, and free of Replay
   schema/storage or renderer feature vocabulary?
8. Which negative control fails if hardware cursor behavior, input actions,
   draw order, coordinate mapping, visibility, or clearing regresses?

## Stop Conditions

Stop for owner review if RBS7 or UI6 is incomplete/stale at execution time; the
feature appears to require a hardware cursor or capture mutation; a recorded
position cannot be obtained without changing the manifest schema; GameUI and
ImGui cannot share a topmost draw boundary without a new reverse edge; a second
retained input/pointer owner appears necessary; a Runtime feature contract would
enter Rendering or UI foundation; steady-state allocation grows; or a visual,
Replay, interaction, or Physics golden refresh appears necessary.

## Completion Reporting

The closing handoff reports the exact visibility matrix, detached value and
owners, final draw ordering for GameUI/ImGui/screenshots, unchanged-manifest
command/report/trace evidence, hardware cursor/capture before-and-after proof,
draw-command capacity/high water, allocation result, tests and focused gates,
touched-source checklist counts, independent-review fixes, baseline
disposition, and empty exception table.
