# 11 — Render Abstraction Leaks

Date: 2026-07-08
Status: Proposed
Priority: P2
Owner: Rendering
Source issue: audit iss-09 (severity 3)

## Problem

The render layer has three leaks: a large "render graph" that admits it does not
own barriers, backbuffer state tracked by a drift-prone bool, and a
replay-specific draw call baked into the generic command interface.

Verified evidence:

- `RenderGraph` (2,000+ lines) documents that it exists so "a future compiler can
  derive the needed barriers," but its policy enum documents
  `DiagnosticOnly = "hand-written backend barriers still own execution"`.
  Transitions are hand-coded via `ExecuteGraphTransition` with caller-supplied
  before/after; `DumpFrameGraphSkeleton` writes audit files whose own footer says
  "Unlabeled live resources remain telemetry, not proof."
- Backbuffer state is a single bool
  [`m_backBufferIsRT`](../SkullbonezSource/Rendering/DX12/RenderBackendDX12.h:426):
  `Clear()`/`PrepareDraw()` only transition when `!m_backBufferIsRT`, but
  `Present()` unconditionally emits `RenderTarget→Present`, and
  `ExecuteGraphTransition` early-outs on `before == after` rather than real
  state — a text-only frame that skips `Clear()` could submit a mismatched
  barrier, the exact DX12 validation-error class the "zero validation errors"
  gate forbids.
- [`IRenderCommandContext`](../SkullbonezSource/Rendering/IRenderCommandContext.h:122)
  — declared a deliberately narrow capability — carries `DrawReplayRibbons`, and
  the DX12 device hardcodes `m_replayRibbonShader` compiled at init. Gameplay
  leaks into the GPU abstraction.

## Goal

Make the render graph honest about what it owns, replace the backbuffer bool with
a real resource-state machine, and remove replay-specific calls from the generic
command interface.

## Approach

- [ ] **Phase 0 — Decide RenderGraph's fate.** Either finish barrier derivation
  so the graph actually owns transitions, or delete the 2,000 lines and keep
  explicit hand-coded barriers *honestly* (no "future compiler" pretense).
- [ ] **Phase 1 — Real backbuffer state.** Replace `m_backBufferIsRT` with a
  tracked resource-state value reconciled at each transition point; ensure a
  frame that skips `Clear()` cannot emit a mismatched `Present` barrier.
- [ ] **Phase 2 — De-leak the command interface.** Move `DrawReplayRibbons` /
  `m_replayRibbonShader` into a replay-owned draw path built on generic
  primitives; `IRenderCommandContext` exposes only generic drawing.

## Risks / GPU safety

Barrier changes are a danger zone (GPU hang / corruption / validation errors).
Run the renderer gate three consecutive times and confirm zero validation
errors after Phase 1.

## Step-by-step implementation

Barriers are a GPU danger zone: run the renderer gate **3×** and confirm
`dx12_validation.txt` == 0 after any resource-state change.

- [ ] **0.1 (DECIDE — stop for a human).** RenderGraph's fate — finish barrier
  derivation so the graph truly owns transitions, or delete the ~2,000 lines and
  keep explicit hand-coded barriers honestly. A smaller model must **not** decide
  this alone. Leave unchecked with a note until a human chooses; the steps below
  do not depend on it.
- [ ] **1.1** Replace the single bool `m_backBufferIsRT`
  (`RenderBackendDX12.h:426`) with a tracked backbuffer resource-state value.
  Reconcile it at `Clear()`, `PrepareDraw()`, and `Present()` so a text-only
  frame that skips `Clear()` cannot emit a mismatched `Present` barrier. Gate:
  `validate_dx12_renderer` **×3**, `dx12_validation.txt` == 0 each time. Commit.
- [ ] **2.1** Move `DrawReplayRibbons` and `m_replayRibbonShader` out of
  `IRenderCommandContext` (`:122`) and the DX12 device into a replay-owned draw
  path built on the generic drawing primitives. `IRenderCommandContext` then
  exposes only generic draws. Gate: `validate_dx12_renderer`
  (`dx12_validation.txt` == 0). Commit.

## Validation

`tools\validate_dx12_renderer.bat` run 3× consecutively; verify
`dx12_validation.txt` == 0.

## Acceptance (structural)

- [ ] `IRenderCommandContext` has no replay-specific method; replay ribbons draw
  through a replay-owned path.
- [ ] Backbuffer state is a reconciled state value, not a lone bool; the
  skip-`Clear()` mismatch case cannot occur.
- [ ] RenderGraph either owns barriers or is removed — no code claims a capability
  it does not have.
- [ ] `dx12_validation.txt` == 0 across three consecutive runs.
