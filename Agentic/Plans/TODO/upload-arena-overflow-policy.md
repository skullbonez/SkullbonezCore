# Upload Arena Overflow Policy

Date: 2026-07-12
Status: Not started — 0/4 phases complete
Impact area: DX12 frame upload system, replay prediction overlay geometry
Owner: rendering/DX12
Priority: Must do (2026-07-12 adversarial review)

## Problem And Evidence (measured 2026-07-12)

Exhausting the per-frame upload arena triggers a full mid-frame GPU drain
inside an allocation call:

- `Dx12FrameOwner::ReserveUpload` on a full arena calls `FlushUploadBuffer()`
  — Close, ExecuteCommandLists, `WaitForGpu()` (full fence drain), allocator
  and list reset, pipeline/texture state invalidation
  (`SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp:496-553`). That is a
  multi-millisecond hitch hidden inside "reserve memory", plus a resubmission
  of descriptor-heap and root-signature binds.
- The trigger condition is data-driven: `UPLOAD_BUFFER_SIZE = 32MB` per frame
  allocator, and the header's own Hazard note names replay prediction ribbons
  as the unbounded consumer
  (`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h:932-935`). A hazard
  comment is documentation, not mitigation.
- `ReserveGeometryUpload` compounds the risk: it probes a combined budget and
  flushes at most once (`:556-581`), which is correct locally but means one
  oversized overlay can force the drain on an otherwise healthy frame.

## Goal

A full upload arena degrades the offending overlay, not the frame. Steady
gameplay never executes the drain path; the drain remains only as a
cold-path safety net (texture creation bursts during load, editor actions).

## Non-Goals

- No triple buffering or frame-count change; `FRAME_COUNT = 2` stays.
- No dynamic arena growth (allocation policy forbids it).
- No redesign of the upload system's address/offset model.

## Phases

- [ ] **A1 — Measure and attribute.** Add per-frame upload high-water and
  per-caller-category (constants, dynamic VB, instance data, texture rows,
  debug/prediction overlay) byte accounting to the existing render memory
  stats; surface it in the memory UI tab. Capture measurements from a normal
  scene, a heavy prediction-overlay scene, and the graphics-stress scenario.
  Acceptance: a dated report under `Agentic/Reports/` naming worst-case
  consumers and headroom against 32MB.
- [ ] **A2 — Bound the known unbounded consumer.** Cap replay prediction
  ribbon / debug overlay geometry at a configured vertex budget with explicit
  truncation (draw what fits, count what was dropped, expose the drop count in
  diagnostics). Acceptance: the heavy-overlay scene from A1 stays under budget
  with visible-but-truncated ribbons and zero flushes.
- [ ] **A3 — Overflow policy for steady runtime.** During the gameplay/render
  phases, a reservation that would not fit fails the *caller* (skip that
  overlay draw, log rate-limited diagnostics with owner and requested bytes)
  instead of draining the GPU; the drain path remains permitted only for
  cold phases (load, editor mutation, screenshot) per the allocation-policy
  phase model. Wire the phase through the existing
  `RuntimeAllocationScope`/phase machinery rather than a new flag.
  Acceptance: a synthetic oversized reservation in the render phase drops the
  draw and completes the frame; the same reservation during load performs the
  legacy flush.
- [ ] **A4 — Review and gates.** Independent review of the failure-path
  semantics (a skipped draw must not leave pipeline state half-bound), then
  final validation per the map below including a perf gate to prove no steady
  regression and 3 consecutive DX12 runs per the upload-buffer danger-zone
  rule.

## Dependencies And Decisions

- Independent of the descriptor-lifetime plan; either order.
- Decision to record: the configured ribbon vertex budget default (proposal:
  size for the largest current legitimate overlay from A1 evidence plus 25%).
- Lane note: A3 skip-with-diagnostics is Lane R behavior at the draw boundary;
  arena corruption or double-flush remains Lane F.

## Acceptance

A1 report committed; heavy-overlay scene renders flush-free; steady-phase
overflow drops the offending draw with diagnostics and an intact frame; perf
gate shows no regression.

## Validation

`tools\validate_dx12_renderer.bat` run 3 consecutive times (upload
buffer danger zone), `tools\run_graphics_stress.bat 1` (record command,
runtime >= 10s, crash-free exit), and `tools\validate_perf.bat`.
