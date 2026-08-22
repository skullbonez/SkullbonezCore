# Recorded Interaction Playback Cursor Plan

Date: 2026-08-22
Status: Active by explicit owner direction. 0/4 phases complete.
Impact area: recorded interaction-manifest playback, detached Runtime/Automation
presentation values, GameUI/ImGui draw ordering, DX12 UI submission, tests, and
documentation
Owner: Runtime/Automation owns recorded-turn evidence; the post-RBS product UI
owner composes the fake cursor; Runtime/Render submits backend-neutral draw values
Priority: Binding after `GAME_UI_COMPONENTS`; RIC0 through RIC3 execute in order
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

- [ ] Record branch, commit, dirty files, CodeGraph status, inherited validation
      findings, and the post-UI6 source/project ownership map.
- [ ] Inventory the recorded-frame capture, semantic-anchor resolution,
      playback publication, input routing, native cursor/capture, GameUI,
      replay-overlay, ImGui, screenshot, and Present paths.
- [ ] Build an explicit fake-cursor visibility matrix for focused/unfocused,
      pointer present/absent, ordinary cursor mode, free-look/mouse-look,
      editor viewport look, replay inspection look, tool capture, playback
      failure, and playback completion.
- [ ] Identify the final draw point that is above both GameUI and ImGui and is
      included in backbuffer screenshots.
- [ ] Add focused pure-policy tests and false-pass controls for the visibility
      matrix.
- [ ] Add a hardware-cursor non-interference seam or observation that proves
      fake-cursor decisions do not change native cursor visibility/capture
      requests or commits.
- [ ] Ratify the exact detached value, source placement, project ownership, and
      draw geometry without a new retained owner or feature-specific Rendering
      vocabulary.

**Acceptance:** Every relevant mode has one expected fake-cursor outcome; the
topmost draw point is identified for both operator surfaces; negative controls
can detect a hardware-cursor mutation; no manifest, Replay, native host, or
renderer feature contract needs to change.

## Phase RIC1 - Publish The Detached Recorded-Cursor Value

**Goal:** Carry the already resolved playback pointer into presentation without
creating another input owner.

- [ ] Define the smallest trivially copyable frame value needed to draw the fake
      cursor, including availability, resolved client position, and only the
      recorded button facts used by approved presentation.
- [ ] Build that value from the current recorded turn after semantic-anchor or
      normalized-coordinate resolution.
- [ ] Evaluate visibility from RIC0's pure logical policy and current replayed
      mode facts without reading or applying native cursor state.
- [ ] Clear the value deterministically on absent pointer, focus loss,
      cursorless mode, failure, completion, scene replacement, and automation
      shutdown.
- [ ] Keep normal `Input::AutomationState`, `InputRouter`, UI hit testing,
      camera, world interaction, and recorded timing unchanged.
- [ ] Extend focused tests for coordinate mapping, semantic-anchor precedence,
      mode transitions, button values, and terminal clearing.
- [ ] Prove no interaction-manifest schema, sidecar digest behavior, Replay
      storage, reserve privilege, or steady-state allocation changed.

**Acceptance:** Presentation receives one exact detached value per published
recorded turn; all clearing and cursorless transitions are deterministic; the
input and artifact paths are byte-for-byte unchanged outside the new value;
native cursor/capture evidence matches the pre-feature path.

## Phase RIC2 - Draw The Topmost Fake Cursor

**Goal:** Render a polished software cursor above every supported operator
surface while leaving the hardware cursor untouched.

- [ ] Implement the approved vector arrow from component-neutral bounded draw
      values, preserving the recorded hot point and viewport-edge safety.
- [ ] Submit the cursor after GameUI, replay overlays, and ImGui according to
      the RIC0 ordering proof.
- [ ] Ensure hidden/minimized GameUI and the selected development surface do not
      accidentally suppress an otherwise visible fake cursor.
- [ ] Keep the cursor out of text-only or capture paths only where RIC0's product
      contract explicitly requires it; ordinary playback screenshots must show
      the cursor.
- [ ] Add deterministic draw-command/fingerprint tests for visible, hidden,
      edge, click-feedback, and operator-surface cases.
- [ ] Add an unchanged recorded-manifest visual witness showing motion across
      world content and UI controls, free-look disappearance, reappearance, and
      simultaneous independent hardware-cursor use.
- [ ] Prove fixed command capacity, no post-start allocation, no new texture or
      descriptor, and no DX12 validation error.

**Acceptance:** The fake cursor is readable and topmost on GameUI and ImGui,
tracks the resolved recorded position, disappears only under the ratified
logical conditions, appears in playback screenshots, and causes no hardware
cursor/capture or input-routing change.

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
