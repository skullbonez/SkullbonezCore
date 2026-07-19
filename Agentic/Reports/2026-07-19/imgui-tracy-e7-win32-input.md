# ImGui + Tracy E7 Win32 Input Evidence

Date: 2026-07-19

Branch: `nightrunner-18th-july`

Plan task: E7 — route Win32 input and focus without stealing gameplay controls

## Outcome

E7 is complete. The pinned Dear ImGui Win32 backend now observes native
messages at the `Window` boundary before engine dispatch. A value-only policy
classifies mouse, keyboard, text, and platform events and chooses exactly one
application consumer for captured input. Platform lifecycle messages continue
through `Window` and `DefWindowProc`. The existing `InputRouter` remains the
sole owner of gameplay input state, action ordering, cursor visibility, and
native mouse-capture intent.

The policy consumes the previous completed ImGui frame's capture flags. Tool
widgets can retain their own mouse, keyboard, and text events; the future E10
central game viewport publishes hover/focus through a typed value seam that
returns mouse and keyboard authority to camera, selection, gizmo, game, and
replay paths. Neither the backend nor `Window` reaches through `Run` or stores
input business state.

## Event And Focus Contract

- Mouse move/buttons/wheel/raw input/cursor messages are classified as mouse.
  The pinned backend observes them first. Captured tool mouse input is neutral
  in the engine frame, including button levels, wheel, and relative/raw delta.
- Key messages are keyboard input. Escape follows keyboard capture: it stays in
  a focused tool, but passes to engine shortcuts when the viewport owns focus.
- Character, system-character, and IME composition messages are text input.
  Text intent is independent of mouse capture and remains captured for a
  focused field even if the central viewport is hovered.
- Alt+Tab and Alt+F4 are platform navigation and cannot be swallowed by editor
  keyboard capture. Focus, resize, DPI, display/device, and other lifecycle
  messages likewise continue through the engine and OS paths.
- Clipboard integration uses Dear ImGui 1.92's pinned Win32/core default path;
  it introduces no parallel engine clipboard owner or captured message class.
- Legacy mode forwards platform synchronization only. Hidden ImGui never sees
  Legacy mouse/key/text traffic and therefore cannot perturb its established
  cursor or capture behavior.
- Both mode still selects one application consumer for each captured event.
  The legacy and gameplay paths receive neutral class state when the tool owns
  it, rather than independently executing a second command.

## Native Pointer And Ghost-Input Safety

`imgui_impl_win32` and the engine both touch HWND-scoped capture and cursor
state. The editor records when the backend handled a mouse message or changed
its cursor selection. `InputRouter::DeferPointerPresentationCommit()` then
forces the engine to republish its complete native pointer intent when gameplay
authority returns. While a tool drag owns mouse capture, the engine deliberately
does not issue a competing release.

Keyboard and button classes are neutral while captured. When capture ends, the
router synchronizes the current physical levels and waits for release/repress.
Thus a key or mouse button held while typing/dragging cannot become a ghost
gameplay press, shortcut, camera movement, or selection action on the first
returning viewport frame. Focus loss retains the established reset behavior.

## Acceptance Evidence

- A table-driven Legacy / ImGui / Both matrix covers tool typing and dragging,
  viewport camera/replay input, focused text over the viewport, platform
  navigation, and exactly one application consumer. The focused policy run
  passed 29 assertions.
- The captured-input regression passed 11 assertions, including neutral
  captured state, held-input resynchronization, physical release, and the next
  genuine repress.
- A pointer-presentation regression proves a vendor/native state touch republishes
  unchanged engine capture/cursor values.
- Final Profile probes for Legacy, ImGui, and Both all exited 0. Legacy completed
  no ImGui frames and allocated no descriptor; ImGui and Both completed four
  frames each with one descriptor high-water and zero live descriptors at
  shutdown. All three reported zero DX12 validation errors. Evidence:
  `TestOutput/validation/e7_mode_legacy_stdout.txt`,
  `TestOutput/validation/e7_mode_imgui_stdout.txt`, and
  `TestOutput/validation/e7_mode_both_stdout.txt`.
- A visible native-message probe delivered all 15 focus, keyboard, text, IME,
  DPI, resize, and mouse cases to the final Profile executable. It exited 0 in
  2.426s after 120 frames and reported 31 backend messages, 2 captured keyboard,
  4 captured text, 2 focus, 1 DPI, and 3 IME messages, with zero DX12 errors and
  clean descriptor shutdown. Evidence:
  `TestOutput/validation/e7_win32_message_stdout.txt`.
- The first hidden-window probe produced a 0 x 0 DX12 startup failure because
  the probe harness hid the native window before device initialization. This
  was a harness setup failure, not an engine regression. The final visible
  probe used synchronous delivery plus a valid client rectangle and passed all
  15 messages.

## Required Gates

| Gate | Result |
|---|---|
| `tools\validate_ui.bat` | PASS in 63.708s; UI suite and Profile/Debug readiness builds passed with zero warnings/errors. |
| `tools\validate_ui_stress.bat` | PASS in 52.764s; deterministic UI stress exited cleanly with zero DX12 validation errors. |
| `tools\validate_fast.bat` | PASS in 55.636s; formatting, 757/757 project filters, tests, and Profile/Debug builds passed. |
| `python tools\check_allocation_policy.py --repo .` | PASS in 9.192s; 402 files scanned and zero allowlist errors. |
| `tools\validate_full.bat` | PASS in 144.928s; CPU umbrella, Automation/replay, zero-error DX12 baselines, and byte-exact 44,401-line physics regression passed. |
| `tools\validate_build.bat Release` | PASS in 47.400s; zero warnings and zero errors. |
| Release EXE/PDB scan | PASS in 0.068s; no `imgui`, `SKULLBONEZ_DEVELOPMENT_TOOLS`, `ImGuiEditorOwner`, or `ImGuiEditorInputPolicy` strings. |

No baseline, screenshot golden, replay golden, physics CSV, query golden, or
authored data was changed.

## Comment Audit

The 13 touched source-bearing files were inspected against
`Agentic/Skills/comment-style-audit/skill.md` and the repository comment guide:

- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorInputPolicy.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h`
- [x] `SkullbonezSource/Runtime/InputFrame.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.h`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/InputRouter.cpp`
- [x] `SkullbonezSource/Runtime/InputRouter.h`
- [x] `SkullbonezSource/Runtime/Run.cpp`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/Window.cpp`
- [x] `SkullbonezSource/Runtime/Window.h`
- [x] `SkullbonezTests/TestInputRouter.cpp`

Checked: 13. Deferred: 0. Unchecked: 0. Dense code names the local concept,
ownership/lifetime invariant, Win32 shared-state hazard, or ghost-input rule.
The one-line project-filter inventory change was inspected as a trivial tool
edit.

## Unrelated Live Blocker

Physics body-count P1 remains blocked only on exact owner approval for replay
`causal.topologyCount: 199 -> 200` and the mechanically derived
`physics_query_varied.json`. E7 did not touch either artifact.
