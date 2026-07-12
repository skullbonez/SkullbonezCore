# Frame-View Calling Convention

Date: 2026-07-12
Status: Not started — 0/4 phases complete
Impact area: runtime frame loop (`RunFrame.cpp` and peers), UI text pass,
graphics stress controller
Owner: runtime
Priority: Nice to have (highest-value of the two; 2026-07-12 adversarial review)

## Problem And Evidence (measured 2026-07-12)

The `Run` decomposition moved authority out of the god object but left call
sites threading enormous positional borrow lists:

- `RenderExecuteUiTextFrame` is called with ~28 arguments
  (`SkullbonezSource/Runtime/RunFrame.cpp:712-740`).
- `ExecuteGraphicsStressFrame` takes ~24 arguments (`RunFrame.cpp:653-677`).
- Several `Tick*`/`Render*` helpers in the same file are in the 10-20 range.

When a signature is longer than most functions, the borrowed-view struct that
was avoided in the name of the god-object closure rule has reappeared as a
positional argument list — harder to review, trivial to transpose two
same-typed references, and a merge magnet. A small number of typed, read-only
frame-view structs is a calling convention, not a context bag: the closure
rule bans *mutable multi-domain state* collected in a bag, not immutable
per-call borrow groupings.

## Goal

No frame-loop function takes more than ~8 parameters. Borrows are grouped into
a few typed frame-view structs, constructed on the stack per call, holding
references only (no ownership, no mutable cross-domain state, no storage
beyond the call).

## Non-Goals

- No ownership moves; every referenced owner stays where it is.
- No stored context objects — frame views are constructed per call and never
  retained as members (that would recreate the banned `*Context` bag).
- No behavior change; this is signature/call-site mechanics only.

## Phases

- [ ] **F1 — Define the frame views.** From the actual argument lists, derive
  two or three groupings (proposal: `RuntimeFrameSystems` for long-lived owner
  borrows — window, config, assets, workers, scene controller, renderer, UI,
  audio, tools; `FrameTickState` for per-frame values — timers, camera state,
  alpha, diagnostics, launch options). Document in each struct header that the
  views are per-call, reference-only, and non-storable, referencing the
  god-object closure rule. Acceptance: struct definitions with lifetime
  comments; no member is a value copy of owner state.
- [ ] **F2 — Convert the worst offenders.** `RenderExecuteUiTextFrame` and
  `ExecuteGraphicsStressFrame` take frame views; call sites shrink
  accordingly. Acceptance: both signatures at or under the parameter target;
  behavior identical.
- [ ] **F3 — Convert the remainder.** Every remaining frame-loop function
  above ~12 parameters in `RunFrame.cpp` and its direct helpers adopts the
  views. Record any function deliberately left positional with a reason.
  Acceptance: a listed inventory in the commit body of converted and
  intentionally-skipped functions.
- [ ] **F4 — Review and gates.** Independent ownership review confirming the
  views did not become mutable state bags or grow storage, comment-style audit
  of touched files, then `tools\validate_full.bat` (touches `Run*`) plus the
  DX12 stress rule if any DX12-adjacent file was modified.

## Dependencies And Decisions

- Run after the must-do plans; pure churn that would otherwise collide with
  their diffs in `RunFrame.cpp`.
- Binding constraint honored: views hold no mutable multi-domain state, no
  callbacks, no `void*`, and are never stored — this is the line between a
  calling convention and the banned context bag.

## Acceptance

Frame-loop signatures at or under target, views reference-only and unstored,
review clean, `validate_full` green.

## Validation

`tools\validate_full.bat` (file-to-gate mapping for `Run*`). Add
`tools\run_graphics_stress.bat 1` with recorded evidence if any DX12 source is
touched.
