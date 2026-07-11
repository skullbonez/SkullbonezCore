# Shadow Edge Quality Closure

Date: 2026-07-12
Plan: `shadow-edge-quality`
Result: complete, 5/5 phases

## Rendering Result

- Terrain receivers retain the broad terrain map at `t3` and use the tight
  object map at appended slot `t5`; instanced material data remains at `t4`.
- Lit terrain and instanced-object receivers use fixed light-texture-space
  Poisson disks: one center sample at radius 0, 12 taps at radius 1, and 16 taps
  at radius 2 or higher. The fixed sequence is unrolled and never rotates or
  reseeds between frames.
- Terrain evaluates the detail projection first and skips the broad PCF kernel
  inside the authoritative detail footprint. This preserves the selected result
  while avoiding two full kernels per covered terrain pixel.
- Both broad and detail orthographic projections snap the world origin to the
  integer shadow texel grid before publishing their view-projection matrices.
- Object receiver bias floors scale from High (2048: depth 0.00035, slope
  0.00075) to Ultra (4096+: depth 0.000175, slope 0.000375). Terrain continues
  to use the authored bias exactly.

## Visual And Motion Evidence

- Matched High and Ultra scenes explicitly enable cinematic rendering so their
  authored shadow overrides are active. High uses 2048/radius 1; Ultra uses
  4096/radius 2.
- Final accepted captures differ across 45,191 color channels with pixel RMS
  0.468751. High SHA-256 is
  `33dfaa3379950e9cceed18c7d08ece894490c21dff477117200249797ce77641`;
  Ultra is
  `1d56c52b1318dc94d7579631d5863f6fba1cd2cfe22fbc17a66c5e929e77a7fc`.
- The dedicated fixed-step `shadow_motion_stability` probe produces 35
  consecutive interval captures and therefore 34 measured adjacent
  transitions. All captures are unique; adjacent-frame changes range from
  460 to 3,108 pixels (median 1,367). Visual inspection at the moving contact
  shadow found no alternating kernel pattern, detached contact, or stationary
  texel crawl.

## GPU Budget

The quality budget is less than 0.15 ms average for the complete shadow-map GPU
marker in the matched small-scene probes, while the repository perf gate must
remain green.

| Preset | Map/filter | ShadowMap GPU pass 1 | ShadowMap GPU pass 2 |
|---|---|---:|---:|
| High | 2048 / 12 taps | 0.0416 ms | 0.0415 ms |
| Ultra | 4096 / 16 taps | 0.1167 ms | 0.1168 ms |

Both presets meet the explicit budget. `tools\validate_perf.bat` initially
exposed the receiver-side cost of evaluating broad and detail kernels together.
After the result-preserving detail-first early return, the final 53.833-second
run passed DX12 and physics-benchmark absolute budgets and comparisons.

## Cascades / Clipmaps Decision

No follow-up cascade or clipmap plan is opened. The broad projection supplies
world coverage and the tight snapped projection supplies nearby density; the
accepted silhouette, terrain, contact, and consecutive-motion evidence meets
the current target. Reopen as a new plan if a supported traversal scene exposes
broad-map stair steps outside the tight footprint, or if world-scale moving
camera evidence cannot remain stable with projection snapping.

## Tests And Comment Audit

- New CPU tests prove integer-texel snapping, invalid-map no-op behavior, and
  the exact High/Ultra bias floors.
- Touched-source comment audit: 6/6 hand-authored source-bearing files checked,
  zero deferred. PCF vocabulary, deterministic kernel behavior, matrix storage,
  receiver bias intent, and the detail-first performance invariant are recorded
  at their owning code.

## Independent Review

Reviewer: `/root/shadow_plan_end_review`. The initial plan-end review found no
correctness defect in the Poisson loop, snapping arithmetic, bias helper, or
binding ABI. It blocked closure on sparse motion evidence, byte-identical preset
captures, a red perf run, and the missing S4 decision. Those findings are
resolved above. The same reviewer independently reproduced the motion counts,
preset hashes/difference, perf PASS, and S4 decision, then cleared all closure
blockers in the narrow follow-up.

## Final Validation

- `tools\validate_dx12_renderer.bat`: passed in 49.691 seconds with zero DX12
  validation errors and screenshot maximum differences 33/61/0.
- `tools\validate_full.bat`: final post-scene-split run passed in 88.018
  seconds; 158/158 doctest cases, all standalone CPU targets, DX12 captures,
  zero validation errors, standalone physics, and the 44,401-line byte-exact
  varied-physics baseline all passed.
- `tools\run_graphics_stress.bat 1`: passed in 61.959 seconds wall time with
  59.852 seconds engine sample time, 12,099 frames, 337 scene loads, clean
  PID-scoped `WM_QUIT`/`Execute returned`, and zero stderr bytes.

## Handoff

Portfolio progress is 260/276 tasks (94%). The next binding plan is
`sim-render-interpolation`.
