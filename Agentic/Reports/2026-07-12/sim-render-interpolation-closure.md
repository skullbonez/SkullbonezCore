# Simulation/Render Presentation Interpolation Closure

Date: 2026-07-12
Plan: `sim-render-interpolation`
Result: complete, 5/5 phases

## Result

- Live variable-time rendering now presents between the previous and current
  completed 120 Hz solver poses using the scheduler's bounded leftover alpha.
  Position uses linear interpolation; orientation uses normalized shortest-arc
  nlerp. Simulation stores, tick counts, editor picks, gizmos, and replay solver
  recording remain authoritative and unchanged.
- Previous/current endpoints live in the existing fixed-capacity
  `RenderInstanceRecord`; no dynamic store or steady-gameplay growth was added.
  Spawn/reset initializes both endpoints, swap-last deletion carries the paired
  row, and scene load, teleport, pre-step input mutation, identity change, and
  replay restore/scrub mismatches collapse history to the exact current pose.
- Generated tracking cameras, Attach/follow modes, ragdoll eyes, and the contact
  audio listener resolve the same interpolated presentation pose. The listener
  solve is mode-gated, so retained Attach state cannot affect another camera
  workspace. Editor selection and ray queries continue to use physics state.
- `presentationInterpolation` is enabled by default and exposed as
  `presentation_interpolation = 1`. The Options diagnostics show enabled state,
  live alpha, and capture-pin state.

## Capture Determinism

One pre-render decision pins alpha to 1 for every automated capture source:
scheduled frame/time/interval scenes, screenshot-and-exit, pending one-shot
millisecond captures, interaction-script screenshot actions, live-style pending
captures, and auto-cycle. Pending millisecond captures pin before their
threshold can cross during rendering. The exact current endpoint has a fast
path that avoids per-row quaternion normalization.

## Smoothness And Discontinuity Evidence

- A focused scheduler test models a 144 Hz display over 120 Hz physics and
  proves every warmed presentation frame advances by exactly 120/144 of a
  solver unit while the original integer committed-tick count remains 119 at
  the existing float-accumulator boundary.
- The live Profile demo was observed at the configured 75 Hz, which does not
  divide 120, through three sampled moving frames. Motion and camera tracking
  remained stable with no visible jump or discontinuity. This used the live
  window because engine screenshots intentionally pin alpha to 1.
- Store tests cover 0-to-8 interpolation at alpha 0.25, teleport collapse,
  teleport immediately before a solver tick, and antipodal quaternion shortest
  nlerp. Existing atomic lifecycle smoke in the full gate covers coordinated
  creation, swap-last deletion, and render mirror integrity.

## Performance And Allocation

`tools\validate_perf.bat` passed after adding the alpha-1 endpoint fast path.
The allocation guard reported zero steady-gameplay violations.

| Perf lane | Begin capture average | Complete capture average |
|---|---:|---:|
| DX12 1,000-body scene | 0.0051 ms | 0.0024 ms |
| Physics benchmark | 0.0010 ms | 0.0004 ms |

Both absolute budgets and baseline comparisons passed. The final run log is
`TestOutput/validation/agent_logs/sim_render_interpolation_validate_perf_rerun.log`.

## Tests And Comment Audit

- Profile doctest: 164/164 cases and 3,705/3,705 assertions passed.
- New coverage includes scheduler alpha and deterministic/paused pinning,
  144/120 cadence, position/orientation interpolation, discontinuity collapse,
  and pre-render capture prediction including the millisecond crossover guard.
- Touched-source comment audit: 34/34 source-bearing files inspected against
  `Agentic/Reference/comment-style-guide.md`; zero deferred. Presentation-alpha,
  endpoint lifetime, capture-trigger, camera/listener, and discontinuity
  invariants are recorded at their owning boundaries.

## Independent Review

Reviewer: `/root/interpolation_plan_end_review`. The review found millisecond
and alternate capture paths bypassing pinning, pre-step capture preceding the
established topology repair, prior-frame contact-audio listener state, and a
retained Attach-state mode leak. All four were fixed. The reviewer verified the
final mode guard and reported no remaining implementation blocker. Residual
risk is limited to the absence of isolated tests for the mode-exit listener,
live-style and interaction adapters, and deliberate topology drift; the broad
runtime and atomic-lifecycle gates exercise their integrated paths.

## Final Validation

- `tools\validate_perf.bat`: passed in 31 seconds on the final performance
  shape; zero gameplay allocation violations and both comparison lanes green.
- `tools\validate_full.bat`: passed in 145.500 seconds from final source.
  Formatting and 659 project/filter items were clean; all CPU lanes passed;
  Profile and Debug built at `/W4` with zero warnings; DX12 InfoQueue reported
  zero errors; visual maximum differences remained 33/61/0; standalone physics
  and runtime-handle smoke passed; `physics_regression_varied.csv` matched all
  44,401 lines byte-exactly.

## Handoff

Portfolio progress is 265/276 tasks (96%). The next binding plan is
`editor-undo-redo`.
