# Runtime Include-Closure Reduction

Date: 2026-07-29
Owner: skullbonez
State: In progress (IC0 complete)
Ledger tasks: 4 (IC0-IC3)
Branch: `nightrunner-29th-JUL-26`
PR: TBD

## Goal

Cut the transitive header closure of the seventeen heaviest translation units,
which are concentrated almost entirely in `Runtime/App`, without inventing a new
indirection convention or weakening any ownership boundary.

## Problem And Evidence

Measured on 2026-07-29 against `main` tip `90e4d52f` by resolving every
first-party `#include "..."` edge transitively across 254 translation units and
317 first-party headers.

| Closure size | TUs |
|---|---:|
| > 200 headers | 17 |
| > 150 headers | 26 |
| > 100 headers | 62 |
| median | **38** |

The median translation unit is healthy. The cost is concentrated: ten of the
seventeen worst TUs are in `Runtime/App`, and the top of the list sees roughly
80% of every first-party header in the repository.

| TU | Headers |
|---|---:|
| `Runtime/App/RunFrame.cpp` | 255 |
| `Runtime/App/InputFrameExecution.cpp` | 253 |
| `Runtime/App/Run.cpp` | 251 |
| `Runtime/Capture/RuntimeStressController.cpp` | 249 |
| `Runtime/App/InputFrame.cpp` | 249 |

Fan-in of the heavy physics headers:

| Header | TUs reached |
|---|---:|
| `PhysicsBodyStore.h` | 113 |
| `SpatialGrid.h` | 73 |
| `PhysicsWorld.h` | 66 |
| `PhysicsEngine.h` | 61 |
| `UI.h` | 43 |
| `SceneController.h` | 37 |

The dominant chain is a single line of by-value members:
`Run.h` → `SceneController.h` → `SceneWorld.h` → `PhysicsEngine.h` →
`PhysicsWorld.h` → all sixteen physics stage headers → `SpatialGrid.h`. Breaking
one edge in that chain removes the entire solver subtree from every Runtime TU
that only sequences physics rather than implementing it.

## The Seam Already Exists

`SkullbonezSource/Physics/PhysicsApi.h` is already the declared public command,
query, and immutable-view contract, and its own invariant block states it
"exposes no simulation owner or solver-private container." 26 files use it.

33 files still include `PhysicsEngine.h` directly. Most of them need
descriptors, handles, and query values — not the solver. This plan finishes a
boundary the repository already drew; it does not draw a new one.

Likewise `Run.h` already keeps `UI.h` out of the composition root through
`std::unique_ptr<UI::InGameUI> m_operatorUi`, with a stated reason
(`Run.h:187-189`). That is the repository's own precedent for this move, and it
is allocation-policy compliant because composition-root construction happens
before steady gameplay.

## Design Constraints

- **No ownership change.** This plan moves declarations, not authority. No owner
  gains or loses a responsibility, and no member changes owner.
- **No new indirection convention.** Use the two mechanisms already present:
  the `PhysicsApi.h` value seam, and composition-root `unique_ptr` members with
  a stated lifetime comment. Do not introduce pimpl as a general pattern, a
  forward-declaration header, or an umbrella "fwd" file.
- **Any new heap member is startup-phase.** It registers with
  `RuntimeReserveAllocator` if it is scene-sized, carries the same lifetime
  comment style as `m_operatorUi`, and never appears in a steady-frame path.
  `check_allocation_policy` must stay clean.
- **Hot paths keep direct access.** Physics stages, solver, narrowphase, and
  render submission continue to include and use concrete owners directly. A
  pointer chase at an owner boundary is acceptable; one inside a per-body or
  per-pair loop is not.
- **Measure, do not assume.** IC0 records baseline closure counts and a clean
  full-rebuild wall time. IC3 reports both again. If closure falls but build
  time does not move, say so plainly in the closure report.

## Non-Goals

- Reducing the median TU. 38 headers is fine; there is nothing to win there.
- Splitting `Config.h`. It reaches 103 TUs because it is genuinely process-wide
  configuration. Its domain structs are already separated and its fan-in is by
  design.
- Precompiled headers or unity builds. Both hide the coupling rather than
  reduce it.
- Any change to the dependency-direction rules or the generated proof block.

## Ledger

- [x] IC0 — Baseline and target selection. Record per-TU closure counts, header
  fan-in, and clean full-rebuild wall time per configuration. For each of the 33
  direct `PhysicsEngine.h` includers, classify as owner (keeps it) or consumer
  (moves to `PhysicsApi.h`). Identify the single highest-value edge in the
  `Run.h` → `SpatialGrid.h` chain and record why it is the right one to break.
- [ ] IC1 — Convert every classified consumer to `PhysicsApi.h`, adding any
  missing descriptor or query value to that header rather than widening a
  consumer's include set. Zero behavior change.
- [ ] IC2 — Break the selected chain edge and reduce `UI.h`'s eleven tab
  includes to what the composer actually needs at declaration scope. Zero
  behavior change.
- [ ] IC3 — Closure. Re-measure closure counts and rebuild time, confirm
  allocation policy and dependency direction are clean, audit touched comments,
  pass independent review confirming no ownership moved and no forwarding
  header, alias, or umbrella declaration file was introduced, and run the mapped
  gates.

## IC0 Evidence

The current 254-TU/317-header graph exactly reproduces the registered baseline:
17 TUs above 200 headers, 26 above 150, 62 above 100, lower median 38, and
maximum 255. The six named heavy-header fan-in rows also reproduce exactly.
The complete per-TU inventory is permanent evidence in
`Agentic/Reports/2026-07-29/runtime-include-closure-reduction-ic0-census.md`.

The 35 physical `PhysicsEngine.h` includes reconcile as the engine
implementation, the selected `SceneWorld.h` ownership-chain edge, and the 33
classification rows required here. Three owner implementations keep the
concrete header: prediction simulation, prediction-engine reserve/construction,
and the standalone startup lifecycle harness. The other 30 are consumers and
move to the public `PhysicsApi.h` seam in IC1.

IC2 will break `SceneWorld.h -> PhysicsEngine.h` with the existing
composition-root `std::unique_ptr` lifetime pattern. Ownership and accessors
remain with `SceneWorld`; the edge removes the solver subtree from every
non-physics `SceneWorld` consumer without adding a new declaration convention.

Clean full solution rebuilds pass in 43.066 seconds for Debug x64 and 44.614
seconds for Profile x64. These are single wall-clock baselines, not a build-time
improvement claim. IC0 is documentation/measurement only, so no repository
validation gate was required.

## Acceptance

- The count of TUs above 200 headers falls from 17. Report the exact new
  distribution and the new maximum.
- `SpatialGrid.h` and `PhysicsWorld.h` are no longer reachable from TUs that do
  not implement physics. Report the new fan-in for both.
- Clean full-rebuild wall time is reported before and after for Debug and
  Profile. An unchanged time with a reduced closure is an acceptable outcome and
  is reported honestly, not framed as a win.
- Zero behavior change: every physics baseline, replay golden, and screenshot
  baseline is byte-identical. No refresh.
- `python tools\check_dependency_graph.py --repo .` and
  `python tools\check_allocation_policy.py --repo .` are clean, and the
  generated `AGENTS.md` proof block is unchanged or regenerated by the checker's
  own write mode.
- No forwarding header, compatibility alias, umbrella forward-declaration file,
  or service/context bag is introduced.

## Dependencies

- `broadphase-capacity-right-sizing` should close first so `SpatialGrid.h` is
  not a moving target while IC2 is breaking the chain that reaches it.

## Validation

- Iteration: focused Profile builds; closure re-measurement between edits.
- IC1-IC2: `tools\validate_fast.bat`.
- IC3: `tools\validate_full.bat` (mapped: `Run*` and `Runtime/*` require it),
  `tools\validate_physics.bat`, `tools\validate_dx12_renderer.bat`,
  `tools\run_graphics_stress.bat 1`, and
  `tools\validate_replay_visual_fidelity.bat`.

## Comment-Audit Checklist

Populate from `git diff --name-only` at IC3; the touched set is not knowable
before IC0 classifies the 33 includers. Seed entries:

- [ ] `SkullbonezSource/Physics/PhysicsApi.h`
- [ ] `SkullbonezSource/Runtime/App/Run.h`
- [ ] `SkullbonezSource/Runtime/Scene/SceneWorld.h`
- [ ] `SkullbonezSource/UI/UI.h`
