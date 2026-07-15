# Win32 Message Pump Drain — One Frame Per Loop Turn

Date: 2026-07-15
Status: Active — 0/3 tasks complete
Impact area: Runtime frame loop (`Run::Execute`), input timing, automation smoke
Owner: runtime shell

## Problem And Evidence

`Run::Execute` uses the naive single-message pump shape
(`SkullbonezSource/Runtime/RunFrame.cpp:529-552`):

```cpp
if ( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) ) { /* dispatch ONE message */ }
else { /* run a whole frame */ }
```

Consequences:

1. A frame runs only when the message queue is momentarily empty. Under a
   high-rate input flood (raw/precision mouse input, window drag storms),
   rendering starves while messages drain one per loop iteration.
2. Message dispatch and frame work alternate at loop granularity, so a frame's
   input can arrive split across many pump turns instead of landing as one
   coherent pre-frame batch, which is the shape the edge-detecting
   `InputRouter` actually wants.

Adversarial review 2026-07-15 flagged this as finding #8 (hostile review of
`nightrunner-14th-july`, PR #120 merge).

## Goal

Each outer loop turn drains the entire pending message queue, then runs exactly
one frame turn. `WM_QUIT` handling, the exit-latch semantics, and graphics
stress accounting are preserved unchanged.

## Non-Goals

- No raw-input (WM_INPUT) migration and no input-binding changes.
- No change to frame timing policy (the 0.05 s clamp is a separate concern).
- No change to `Window`/message handler code.

## Tasks

- [ ] T1 — Restructure `Run::Execute` so the pump is
      `while ( PeekMessage( ... ) ) { translate/dispatch }` followed by one
      frame turn per outer iteration. `WM_QUIT` inside the drain sets the
      existing exit path (`m_applicationExit.RequestNormalExit()`, exit-code
      capture, graphics-stress print) and breaks out of both loops. Add a
      bounded drain cap (compile-time constant, e.g. 256 messages per frame)
      with a `Hazard:` comment explaining it exists only to bound a
      pathological flood; hitting the cap defers remaining messages to the
      next frame, it is not an error.
- [ ] T2 — Verify input semantics: manual interactive smoke (camera drag,
      editor pointer gestures, key edges) plus the automation lane, since all
      of a frame's messages now land before `ProcessInputFrame` instead of one
      per empty-queue gap. Record any observed behavioral difference in the
      plan before closing.
- [ ] T3 — Final gate and closure. `Run*` files changed, so the mapped gate is
      `tools\validate_full.bat`. Physics baselines must pass byte-exact with no
      refresh — this change must not alter simulation input ordering within a
      frame in a way the deterministic scenes can observe (they are scripted,
      not live-input driven).

## Dependencies And Decisions

- Owner decision 2026-07-15: canonical drain-then-frame loop, optional paranoia
  cap acceptable. No raw-input rework in this plan.
- Independent of all other 2026-07-15 remediation plans; safe to land first.

## Acceptance

- The pump drains all pending messages before every frame turn.
- `WM_QUIT` exits with the same exit-code and failure-latch semantics as
  before (diff review of the exit path plus an interactive quit smoke).
- No physics baseline change; automation smoke lane passes.

## Validation

- `tools\validate_full.bat` (Run* mapping row), output pasted at closure.
- Interactive input smoke recorded in the closing commit body.
