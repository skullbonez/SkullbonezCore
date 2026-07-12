# DX12 Frame Path Comment-Rot Sweep

Date: 2026-07-12
Status: WNF — owner-parked 2026-07-12 ("no comment changes yet"); restore to
TODO/ only by explicit owner decision. 0/3 phases complete.
Impact area: DX12 backend frame path (Present/Finish/FlushGPU), GPU timer
readback
Owner: rendering/DX12
Priority: Parked (was: must do, small; 2026-07-12 adversarial review)

## Problem And Evidence (measured 2026-07-12)

The GPU timer readback block in `Present` contains a dead store wrapped in two
comments that contradict each other
(`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp:2762-2777`): the outer
comment promises "a blocking consume now to avoid permanently losing that
frame's GPU timing data", the inner comment says dropping the stale sample is
better than blocking, and the code sets `readPending = false;` then
unconditionally `readPending = true;` — a no-op. The code's actual behavior
(drop the stale sample) is defensible; the prose asserting the opposite is the
defect, and it is exactly the failure mode a comment-heavy codebase must police.

Scope is deliberately narrow: one verified defect plus a bounded audit of the
same file's frame-path functions for further code/comment disagreement, kept
out of behavior-changing diffs so review stays byte-level easy.

## Goal

The timer readback block says what it does and does it without dead stores,
and the Present/Finish/FlushGPU/WaitForGpu/FlushUploadBuffer comment claims
match the code they annotate.

## Non-Goals

- No behavior changes beyond removing the dead store; if the audit finds a
  comment describing *better* behavior than the code implements, file it as a
  finding for the owning plan rather than changing behavior here.
- No repository-wide comment pass; this is the DX12 frame path only.

## Phases

- [ ] **C1 — Fix the timer readback block.** Remove the dead store, keep the
  drop-stale-sample behavior, and replace both comments with one accurate
  Hazard note (CPU can lap the GPU off-vsync; dropping one sample beats
  blocking Present). Acceptance: the block has one truthful comment and no
  redundant writes.
- [ ] **C2 — Frame-path claim audit.** Read `Present`, `Finish`, `FlushGPU`,
  `WaitForGpu`, `FlushUploadBuffer`, and `SubmitClosed` against their comments
  and the header's epoch/sticky-failure invariants. Fix prose that misstates
  behavior; log any code defect discovered as a new finding (do not fix
  behavior in this plan). Acceptance: a short audit note in the commit body
  listing each function and its verdict.
- [ ] **C3 — Gates.** Although intended as comment-only, C1 touches executable
  code in DX12 runtime source, so run the full DX12 gate set per the map
  below. Acceptance: zero DX12 validation errors, matching baselines,
  crash-free stress run.

## Dependencies And Decisions

- Should land before or after — not interleaved with — the upload-arena plan,
  since both edit `RenderBackendDX12.cpp` frame functions and clean diffs are
  the point of this plan.

## Acceptance

Dead store removed, comments truthful, audit note recorded, gates green.

## Validation

`tools\validate_dx12_renderer.bat`, then `tools\run_graphics_stress.bat 1`
(record command, measured runtime >= 10s, crash-free exit). If the final diff
turns out strictly comment-only (C1 code fix dropped for any reason), the
comment-only rule applies instead and no validation is required — prove the
diff is comments-only in the commit body.
