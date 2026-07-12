# Shader Pipeline Modernization Closure

Date: 2026-07-12
Plan: `shader-pipeline-modernization`
Result: complete, 8/8 tasks

## Architecture result

- The shipping inventory is 43 pinned SM6 DXIL stages: 21 raster programs and
  one generate-mips compute stage. Runtime shader creation accepts only
  manifest-current offline-DXC artifacts; the old runtime `D3DCompile` path is
  gone.
- Generated reflection is checked against an independent CPU declaration for
  every raster program. The contract is exhaustive for authored cbuffer fields
  and texture resources, validates optional-present rows, register space,
  type/dimension, slot, input layout, and the shared `UnifiedRaster` ABI.
- One named `UnifiedRaster` root signature owns b0 and t0-t4 for all raster
  families. Compute-mips and DXR retain their distinct domain signatures.
- Persistent explicit cached PSO blobs are keyed by the complete shader
  manifest and serialized root-signature identity. Corrupt or rejected blobs
  fall back to cold PSO creation without weakening correctness.
- Bindless remains deliberately closed: current stress uses 22 textures, 25
  static SRV rows, and a 115/2,048 transient peak. There is no measured t0-t4
  or descriptor-pressure failure worth raising the baseline to SM6.6 plus
  Resource Binding Tier 3.
- Opt-in F9 development reload rebakes through the pinned offline-DXC path,
  stages every live raster shader and generate-mips candidate, drains GPU work,
  then publishes bytecode/PSO/cache changes transactionally. Candidate DXIL is
  reflected directly so a changed ABI cannot hide behind the executable's
  compile-time generated metadata.

## Independent review closure

Review id: `shader-pipeline-modernization-duck-01`

- Reviewer: `/root/shader_plan_end_review`
- Scope: commits `54ed6bb2..a491192f`, plan P0-P6, current source, tests, and
  validation evidence.
- Accounting: prompt 901 characters; response approximately 3,900 characters;
  token counts unavailable; elapsed approximately 4 minutes.
- Initial verdict: two blocking contract findings, one stale comment, and two
  missing hot-reload mutation drills.
- Resolution: optional-present rows now validate instead of being skipped; all
  21 raster programs have exhaustive CPU-owned declarations; startup rejects a
  raster program without one; the stale FXC fallback comment was corrected.
  The requested drills then exposed and fixed a deeper live-candidate defect:
  reload now checks actual replacement DXIL resources, fields, slots, spaces,
  types, and dimensions before publication.
- Follow-up disposition: no second reviewer was invoked. The user requested one
  plan-end duck; every credible finding was resolved with focused mutations and
  the final formal gates below.

## Hot-reload mutation evidence

- Success drill: after baseline startup, `post_tonemap` received a reversible
  pixel-only 0.75 green-channel multiplier. F9 rebaked and printed
  `[shader-hot-reload] committed`; frame 5,030 was captured and the interaction
  report was `ok=true`. The source and baked artifacts were then restored.
- Rollback drill: after baseline startup, `lit_textured.uShadowMap` moved from
  t3 to t4. F9 reported `Shader hot reload rejected changed or invalid bytecode
  contract`; no commit marker appeared. The old shaders rendered 30 more frames,
  captured a screenshot at frame 5,030, and wrote an `ok=true` interaction
  report. The source and baked artifacts were then restored.
- Evidence logs:
  `TestOutput/logs/shader_p7_live_mutation_final.log` and
  `TestOutput/logs/shader_p7_rejection_final_drill.log`.

## Validation evidence

- P7 touched-source comment audit: 6/6 inspected, zero deferred. Full learning
  headers remain present; local comments explain optional-present semantics,
  exhaustive CPU ownership, live-DXIL validation, transaction hazards, and the
  container-reflection compatibility path.
- Focused Profile build passed in 8.53s with zero warnings/errors.
- Focused shader contracts passed 9/9 cases and 136 assertions before the final
  broad gate; the broad gate passed 150/150 cases and 3,505 assertions.
- Allocation policy scanned 317 files with 39 direct-heap findings, 139 dynamic
  member findings, 622 growth findings, and zero allowlist errors.
- Direct `tools\validate_full.bat` passed in approximately 85s: formatting and
  657-item project filters were clean; all four CPU lanes passed; Profile and
  Debug built with zero warnings/errors; DX12 InfoQueue reported zero errors;
  screenshot maximum differences were 33/61/0; and the 44,401-line physics CSV
  matched byte-exactly.
- `tools\run_graphics_stress.bat 1` completed the PID-bounded 59.820s run with
  12,978 frames and 361 scene loads. Stderr was empty; stdout was 53,815 bytes,
  memory CSV 671 bytes, and shutdown JSON 4,580 bytes.

## Handoff

Portfolio progress is 248/276 tasks (90%). The next binding implementation plan
is `render-visibility-architecture`, beginning with P0 baseline instrumentation.
