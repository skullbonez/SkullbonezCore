# Frame-View Calling Convention Closure

Date: 2026-07-12
Plan: `frame-view-calling-convention` — 4/4 complete
Branch: `nightrunner-12th-july`

## Outcome

Top-level frame orchestration now uses four non-copyable, stack-only capability
slices: host, interaction, scene, and presentation. Each slice contains five to
eight references, no ownership, callbacks, `void*`, copied owner state, or full
shell authority. Bounded per-call fact records carry snapshots separately from
owner access.

The views are constructed once inside `Run::Execute` and are never stored,
captured, queued, or recombined into a universal context. Helpers receive only
the capability slices they need; input and graphics-stress coordinators receive
all four because those operations genuinely cross the four explicit boundaries.

## Signature Inventory

Converted frame and direct-helper definitions:

- `RenderExecuteUiTextFrame`: 28 parameters to 8.
- `ExecuteGraphicsStressFrame`: 25 to 5.
- `ProcessInputFrame`: 25 to 4.
- `RunUIStressActions`: 19 to 5.
- `BeginRuntimeUIFrame`: 16 to 5.
- `ApplyRuntimeUIFrameCommands`: 24 to 7.
- `FinishRuntimeUIFramePointer`: 11 to 4.
- `ApplyGraphicsStressAction`: 17 to 4.
- `ApplyUIStressAction`: 12 to 5.
- interaction automation before/after ticks: 12/10 to 4/5.
- `CaptureReplayPostStep`: 10 to 2.

Intentionally left positional:

- `EvaluateInteractionAutomationAssertion` (11) is a cohesive automation-domain
  predicate, not a frame-orchestration boundary.
- `InputRouter` pointer/owner APIs, `SceneController` load/request APIs,
  `RuntimeRenderer::RenderUiText`, `InGameUI::UpdateInput`, and
  `RunCameraState::TickControls` remain owner-domain APIs. Hiding those contracts
  behind frame views would broaden capability reach rather than improve the
  frame calling convention.

The scoped definition inventory found no remaining frame-orchestration helper
above eight parameters.

## Independent Review

The first read-only rubber-duck review blocked the initial design because one
19-reference systems record plus a six-reference state record recreated the
prohibited transferable multi-domain bag. That design was removed. The second
review passed the four-slice design: each slice is coherent, no retention route
was found, and the two input-fact snapshots preserve the original live-read
timing before UI sampling and immediately before command application.

| Review pass | Duration | Result |
|---|---:|---|
| `frame-view-calling-convention-duck-01` | ~7m | Blocked universal context bag; redesigned |
| `frame-view-calling-convention-duck-02` | ~4m | Clear; no blocking findings |

## Final Validation

- Focused `tools\validate_build.bat Profile` iterations passed at `/W4`; final
  focused rebuild before the broad gate completed in 9.51s with zero warnings.
- `tools\validate_full.bat` passed in 98.92s after preflight exposed and drove
  two repository-integration fixes: touched-file formatting and the new
  `RuntimeFrameViews` project-filter prefix. The final run reported clean
  formatting/metadata, all CPU lanes passed, zero-warning Profile and Debug
  builds, zero DX12 InfoQueue errors with matching screenshots (artifact
  `20260712T064805Z`), and the 44,401-line physics baseline byte-exact.
- No DX12 source was modified, so the additional graphics-stress rule was not
  triggered.

## Comment Audit

Touched-file audit completed against
`Agentic/Skills/comment-style-audit/skill.md`: 11 source-bearing files checked,
0 deferred, 0 unchecked. All touched source files have learning headers; frame
view capability, lifetime, non-retention, input-snapshot timing, and validation
tool routing comments were inspected or refreshed next to the affected code.
