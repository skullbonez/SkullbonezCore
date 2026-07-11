# Render Backend Decomposition Closure

Date: 2026-07-12
Plan: `render-backend-decomposition`
Result: complete, 8/8 tasks

## Ownership result

- `Dx12RenderDevice` owns the native factory, device, queue, swap chain,
  allocators, command list, and fence timeline. The backend retains no borrowed
  factory, queue, or allocator aliases.
- `Dx12FrameOwner` is the sole owner of command-recording, submitted-work,
  device-health, fault-injection, fence/index, profiler-stack, upload-arena,
  SRV-allocation, backbuffer-state, and deferred-retirement state. Backend
  lifecycle helpers delegate to that owner; there is no parallel submit/wait/
  flush implementation.
- Resource wrappers receive only concrete narrow capabilities. Shader upload,
  mesh draw entry, framebuffer draw entry, and deferred release cannot reach
  submission, fault, profiler, or unrelated resource authority.
- `Dx12GeometryOwner` owns dynamic and instanced registries, warmed grid and
  transient shaders, bounded PSOs, and their complete lifecycle.
- `Dx12TextureOwner`, `Dx12PipelineOwner`, and `Dx12RaytracingOwner` remain
  cohesive domain owners. The DXR reflection handle is cold-published during
  initialization and revoked during shutdown; its runtime getter is pure.

## Review closure

The independent plan-end review reopened broad wrapper references, retained
geometry authority, lazy DXR handle publication, profiler epoch reset, lost
draw preflight, upload-reset ordering, a successor coordinator bag, duplicated
submission authority, and reciprocal capability friendship. Each finding was
fixed and re-reviewed. The final follow-up found no remaining ownership or
behavioral blocker.

The final boundary has no backend pointer/reference in shader, mesh, or
framebuffer wrappers; no callback pack, adapter, context bag, hot-path
polymorphic seam, or reciprocal capability friendship; and one implementation
for ensure-open, close/submit, wait, upload flush, fault evidence, PIX
suspension/restoration, and deferred retirement.

## Validation evidence

- Comment-style audit: 18/18 touched source-bearing files inspected, 0
  deferred.
- `tools\validate_all_cpu_tests.bat`: passed all four CPU lanes in 29.444s;
  136 doctest cases and 2,853 assertions passed, including capture failure
  ownership and profiler stale-epoch coverage.
- `tools\validate_dx12_renderer.bat`: the first attempt stopped on the touched
  header format gate; after targeted formatting, the rerun passed in 39.264s
  with zero InfoQueue errors and all committed screenshot baselines matched.
- `tools\validate_perf.bat`: passed in 35.862s; allocation policy and the
  steady-gameplay guard were clean, with absolute and comparison budgets
  accepted.
- `tools\validate_full.bat`: passed in 81.508s; formatting and filters were
  clean, all CPU lanes passed, DX12 validation remained zero, screenshots
  matched, and the 44,401-line physics regression was byte-exact.
- `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --frames 2`: exited
  0 in 2.044s with marker emission enabled.
- `tools\run_graphics_stress.bat 1`: completed the intended PID-bounded
  60.918s run and exited 0. `latest_stderr.txt` was empty; final artifacts were
  stdout 34,931 bytes, memory CSV 671 bytes, and memory JSON 4,578 bytes.
- Final project-filter inventory: 605 project items and 605 filter items, zero
  errors.

## Handoff

Portfolio progress is 229/276 tasks (83%). The next binding serial plan is
`dx12-post-final-cleanup`; it must finish before shader-pipeline modernization
P0 so obsolete shaders and duplicated cinematic config do not enter the new
inventory.
