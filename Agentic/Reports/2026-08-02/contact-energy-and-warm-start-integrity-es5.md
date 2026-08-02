# Contact Energy And Warm-Start Integrity — ES5

Date: 2026-08-02
Status: Complete
Impact area: Physics contact solving, diagnostics, deterministic scenes, and tests

## Outcome

The non-stacking workloads now satisfy the locked 12-iteration policy. The
four-brick reproduction has no sustained relaunch and permanently sleeps. The
200-box topple retains all 211 dynamic bodies, has no repeated popcorn cycle,
contains no invalid state sample, stays within its attributed energy envelope,
and permanently sleeps. Deep stack convergence remains explicitly deferred to
`Agentic/Plans/WNF/contact-stack-stability-techniques.md`.

The owner-authorized catcher wall is the only scene change. It is fixed at
`[1008, 25, 500]` with half extents `[2, 25, 64]`, zero restitution, and no
dynamic body. Its near face is at x=1006, roughly 414 units beyond the authored
impact wall, so it cannot participate in the primary topple. It catches only the
post-demo striker that formerly left the terrain.

## Solver Boundary And Performance Control

Restitution suppression follows one exact loaded contact feature. One sorted
cache lookup supplies both the feature-lifetime proof and warm-start row; a
different feature is fresh geometry, and one no-contact frame ends the lifetime.
Terrain and mutual-gravity elastic policy remain separate.

An initially broader body-pair lifetime probe was rejected after repeatable
same-machine performance evidence showed it retained too many rows:

| Measurement | Exact pre-change control | Broad pair-prefix probe | Final exact-feature result |
|---|---:|---:|---:|
| Physics/Step mean | 0.0754 ms | 0.0944 ms | 0.0728 ms |
| Solve rows/frame | 14.2128 | 21.4761 | 14.5068 |
| Frame mean | 0.4797 ms | — | 0.4763 ms |

The final `tools/validate_perf.bat` run passed. No global or workload-local
iteration increase was implemented; every accepted diagnostic row reports at
most 12 solver iterations.

## Semantic Gate Sensitivity

`tools/check_contact_energy_scenes.py` now rejects the three false-pass classes
found by independent review:

1. The wall computes a post-contact running energy path and subtracts only
   `separation_bias * normal_impulse`. Restitution records zero separation bias.
   A running minimum exposes a later recovery even after the first impact loses
   about 703,000 energy units. The planted SQL path `[-700000, 0, +40, +40]`
   reports an 80-unit unexplained recovery instead of a negative peak.
2. Every dynamic-body sample is checked for NULL, non-finite import, and
   unbounded position, velocity, angular velocity, orientation, inertia, and
   energy values. Aggregate SQL cannot silently skip a bad transient row.
3. Ordinary first-impact bounce remains allowed, while popcorn means the same
   body is relaunched upward through more than its nominal height twice. A
   planted two-cycle body fails. The real wall has one 1.285755-height early
   launch at frame 324 and zero repeated-popcorn bodies.

The checker also validates the catcher geometry, the authored gravity/timestep,
all final body identities, terrain clearance, permanent sleep, and the unchanged
12-iteration ceiling. The self-test and Python compilation pass.

## Final Deterministic Evidence

| Metric | Four-brick | 200-box wall |
|---|---:|---:|
| Last frame | 1199 | 6799 |
| Dynamic bodies | 4 | 211 |
| Maximum iterations | 12 | 12 |
| Energy tolerance | 0.681924 | 57.603553 |
| Peak over initial | 0 | -702,854.799132 from pre-contact reference |
| Maximum unexplained local recovery | not an acceptance metric | 51.063762 |
| Invalid body samples | 0 | 0 |
| Repeated popcorn bodies | 0 | 0 |
| Final-tail relaunches | 0 | 0 |
| Final sleeping | 4/4 | 211/211 |
| Permanent all-sleep frame | 132 | 3286 |

The final CSV hashes are:

```text
four-brick 1EDDE31D1CF8E987C71445DC98E5AB89EC16F49AFC0398F44B2BB38B4B82118E
wall-200   7E8691D96ADD82602206DDB76E96158875AC1FD50EBDB41DEFA10050A8E7A63B
```

Each hash matches the earlier automatic/worker-zero witnesses. The diagnostic
field addition changes trace text only; it does not change either simulation
CSV. The final striker is asleep at approximately
`(998.982605, 7.000844, 464.544312)` with zero speed against the fixed catcher.

## Waited Visible Evidence

Fresh final-source DX12 captures were inspected after the waited settled frames:

| Capture | SHA-256 |
|---|---|
| `TestOutput/contact_energy_es5/visual/four_brick_settled.png` | `CA7BC477346B64A1D67C09FC7D4D8CC14F9B8A6AE47A4491E4529EB2C8FB9CC0` |
| `TestOutput/contact_energy_es5/visual/wall200_settled.png` | `6E8DFD57078A3964E722A3427E37C9112C695CB38A87CFE2345C4A54D0C650A9` |

The four bricks are upright and still. The wall has toppled, every body remains
terrain-clear, and the striker is visibly parked at the far catcher. No exact
chaotic final pose is part of acceptance.

## Validation Boundary

`tools/validate_tests.bat` passes after the final source and diagnostic changes.
The compact four-brick and wall semantic packets pass, and the planted SQL and
packet controls fail for the intended reasons. ES6 owns the final fast,
Physics/deep Physics, performance repeat, replay visual-fidelity generation,
full validation, seven inventories, comment audit, candidate-baseline packet,
and final independent closure decision.

The touched-source comment audit is 7/7 with zero deferred:
`PersistentContactSolver.cpp`, `PhysicsDebugData.h`,
`PhysicsDiagnosticsView.h`, `Diagnostics/SkullScope.cpp`,
`TestPersistentContactSolver.cpp`, `check_contact_energy_scenes.py`, and
`physics_query.py`. The audit corrected the obsolete pair-lifetime wording,
documented separation-only energy attribution, and verified that diagnostic
values remain borrowed observations rather than simulation or replay authority.

Tracked physics baselines remain untouched.
