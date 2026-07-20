# Run::Execute De-accretion

Status: Active — 2/3 tasks (X0-X1 complete; X2 pending)
Owner: repository owner; registered 2026-07-20 as campaign plan 4 of 8
Evidence: `../../Reports/2026-07-20/engine-architecture-review.md` (finding D)
Ledger: X0-X2

## Objective

Return `Run::Execute` to pure sequencing. Move the inlined ImGui automation
command interpreter (including the Win32 resize/DPI logic) to its rightful
owner, sweep the remainder of `Execute()` for other non-sequencing logic, and
record what legitimately stays. This is the God-Object Closure Rule applied
preventively: stop the composition root re-growing business logic while the
larger campaigns run.

## Problem / Evidence

`Runtime/RunFrame.cpp:949-1558` (~610 lines) contains a full switch
interpreting `InteractionAutomationDevelopmentUiCommand` values — panel
parse/visibility/focus, layout reset, DPI-scale, and raw Win32 window
resizing with `AdjustWindowRectExForDpi`/`SetWindowPos`
(`RunFrame.cpp:1059-1141`). Interpretation of automation commands is
automation/ImGui-owner business, not frame sequencing. The closure review of
2026-07-11 left Run as sequencer only; this logic accreted afterwards.

## Non-Goals

- No change to automation script semantics, command vocabulary, exit codes,
  or failure messages — move-only with identical strings and behavior.
- No new `*Services`/`*Context` bag and no callback pack: the receiving owner
  gets a typed apply call with explicitly borrowed parameters
  (`Window&`, `ImGuiEditorOwner&`), consistent with the Run member-shrink
  campaign's recorded rulings.
- No frame-view (`RuntimeFrame*View`) redesign; the borrow-map pattern stays.
- `FillOperatorRenderingParameters` reduction is explicitly out of scope
  (owned by the UI cleanup that follows the E17 verdict), unless X1 finds a
  trivially deletable field. (`FillOperatorAudioView` was deleted with the
  audio subsystem in PR #127; X1's sweep runs against post-removal source.)

## Binding Decisions

1. The command interpreter moves to `InteractionAutomationController` (which
   already owns the command values) as a typed
   `ApplyDevelopmentUiCommands(...)` operation borrowing `Window&` and
   `ImGuiEditorOwner&` for the call only; `Run` keeps a single call site and
   the existing `SelectDevelopmentUiSurface` hook (surface selection is
   process-mode policy and stays with Run).
2. The Win32 resize block moves with the command it serves; if `Window`
   already owns a suitable resize operation, the controller calls it instead
   of raw Win32 (record which).
3. `Execute()` after this plan contains: message pump, allocation scope,
   timer/profiler bookkeeping, frame-view construction, and ordered tick
   calls. Anything else found in X1 either moves to an owner or is recorded
   with a reason.

## Tasks

- [x] X0 — Move the automation development-UI command interpreter out of
  `Run::Execute` per binding decisions 1-2, preserving strings, exit paths,
  and `RequestOwnedFailure` behavior exactly. Both build flavors
  (`SKULLBONEZ_AUTOMATION_DIAGNOSTICS`, `SKULLBONEZ_DEVELOPMENT_TOOLS`)
  compile in all configurations. Validation: `tools\validate_full.bat`
  (Run* mapping row).
- [x] X1 — Non-sequencing sweep of `Execute()`, `TickPhysics`,
  `AfterPhysicsStep`, `TickScreenshots`, `TickAutoCycle`,
  `TickSceneAdvance`: classify each block as sequencing (stays) or owner
  logic (moves or is recorded with reason). Output: disposition table in the
  closure report; apply any cheap moves found. Validation:
  `tools\validate_full.bat` if source moved; documentation-only otherwise.
- [ ] X2 — Closure: independent rubber-duck review against the God-Object
  Closure Rule (no new bag, no reach-back, no forwarding wrapper); confirm an
  automation interaction script exercising the moved commands still passes
  inside the full gate's automation lane. Validation: final
  `tools\validate_full.bat` at closure tip.

## Acceptance

- The development-UI command switch no longer exists in `RunFrame.cpp`; the
  owning controller carries it with identical behavior.
- X1 disposition table shows every `Execute` block classified; no unrecorded
  owner logic remains.
- Independent review clear; automation lane of `validate_full` passes with
  unchanged interaction reports.

## Validation Summary

X0/X1: `validate_full` per Run* mapping. X2: final `validate_full` at
closure tip. No baseline or golden refresh is authorized.

X0 moved the complete development-UI interpreter into
`InteractionAutomationController::ApplyDevelopmentUiCommands`, which borrows
`Window&` and `ImGuiEditorOwner&` synchronously and returns only a typed
surface-selection request plus recoverable status. `Run` retains process-mode
selection and `RequestOwnedFailure`. `Window` has no client-area resize API, so
the command's exact Win32 DPI/style conversion moved with the interpreter.
Automation and Release direct builds pass; the focused UI stress matrix passes
all moved commands with clean logs and zero DX12 validation errors; and
`tools\validate_full.bat` passes at the X0 tip in 139.54s.

X1's complete disposition table is in
`../../Reports/2026-07-20/run-execute-deaccretion-closure.md`. The sweep moved
Ctrl+0 semantic interpretation into `ProcessInputFrame` as a typed result and
moved cross-scene pause precedence into
`RuntimeInteractionController::BuildFramePolicy`; Run now applies both without
rescanning actions or mutating published policy. The focused value-seam test,
UI stress matrix, and `tools\validate_full.bat` pass at the X1 tip.
