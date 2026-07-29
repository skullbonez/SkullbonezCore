# Quaternion Convention Normalization Closure Evidence

Date: 2026-07-29
Plan: archived under ledger rule 4 after QN0-QN5 closure
Branch: `nightrunner-29th-JUL-26`
Implementation range: `3d0164f2` through the closure commit
Working base: `90e4d52f`

## Outcome

Quaternion multiplication now uses the textbook Hamilton product. Orientation
matrices are active, untransposed rotation matrices, world-axis updates use a
positive-sine delta with world-space left multiplication, and the four live
Euler authoring paths compose in the matching canonical order. The retired
anti-Hamilton glossary and both old-convention invariants are deleted rather
than preserved behind compatibility spelling.

Five representation-independent characterization cases stayed unchanged from
the pre-change binary through final closure. The full `Quaternion*` selection
passes 15 cases and 66 assertions. `Matrix4::FromQuaternion`, world inertia,
contact tangent construction, camera bases, and authored/editor placement keep
their established physical behavior.

## Persisted Data And Migration

Scene schema v3, replay artifact v5, and prediction archive v3 store canonical
Hamilton quaternion components. Scene v1/v2, replay v2-v4, and prediction
archive v2 readers conjugate xyz once. Current writers emit only the canonical
versions. Replay verifies historical presentation hashes before migrating an
old sample and publishes the canonical hash afterward.

All 23 committed acceptance scenes carry scene schema v3. Structural migration
proof shows that 22 raw-orientation scenes changed only their version and xyz
quaternion signs; the Euler-authored buoyancy fixture changed only its version.
Legacy/current/future/writer coverage passes for scenes, replay, and prediction,
and the signed-zero/subnormal round-trip case proves double conjugation is
bitwise lossless.

The Python replay readers and artifact validators now recognize durable replay
v5 while continuing to accept the intended legacy fixtures and reject v6 as a
future version. The generated 4,000-sleeper/1,000-awake performance scene is
also canonical scene v3 and byte-stable under its generator.

## Owner Visual Acceptance

QN4 completed before any baseline file changed. Both the pre-change
`90e4d52f` Profile binary and the final Profile binary rendered 60 fixed-step
frames for all 26 acceptance scenes with exit code zero. Seventeen paired
frame-zero captures were pixel exact. The other nine had average channel delta
at or below `0.004605`; eight differed materially only in tiny HUD glyph
regions, and the shoreline pair contained only sub-LSB presentation noise.

The owner reviewed the four contact sheets and comparison metrics, confirmed
they "look good", and explicitly directed regeneration. Zero scenes were
rejected.

## Baseline Regeneration And Delta Inspection

Regeneration came from the final executable and final committed scene content.
`tools/update_baselines.py` was corrected to include the third DX12 image
actually checked by the renderer gate,
`baseline_dx12_space_three_body.png`.

The complete tracked baseline delta is:

- five Physics/SkullScope data artifacts;
- two replay visual-fidelity artifacts;
- `baseline_dx12_solver_smoke.png`;
- `baseline_dx12_space_three_body.png`.

The water screenshot remains byte-identical.

CSV inspection proves:

| Artifact | Data rows | `(frame, idx, name)` identity | Finite state | Peak speed, before/after | Peak angular speed, before/after |
|---|---:|---|---|---:|---:|
| `physics_regression_varied.csv` | 44,400 | exact | yes | 43.3859 / 43.3859 | 8.4026 / 8.4026 |
| `shooting_reaction_volley.csv` | 640 | exact | yes | 5413.88 / 5413.88 | 504.264 / 504.264 |
| `space_three_body_chaos.csv` | 360 | exact | yes | 20.2199 / 20.2199 | 0 / 0 |

The Physics query and known-issue JSON artifacts preserve their complete
top-level structures. Replay preserves all 2,401 visual ticks, a 200-node
causal topology, valid target identities, the fixed horizon, and one prediction
generation/presentation. Every semantic, byte, hash, determinism, truncated
horizon, dropped geometry, reserve growth, and duplicate-generation false-pass
control rejects its mutation.

Both screenshots remain 1784 by 961 RGB images. The solver image changes 1,185
pixels (`0.06911942%`), with maximum channel delta 33 and mean channel delta
`0.00030292`. The three-body image changes 6,819 pixels (`0.39774292%`), with
maximum channel delta 255 and mean channel delta `0.42446559`; the accepted
renderer threshold and prior QN4 owner review both cover this expected
orientation-driven transition.

No row identity loss, non-finite state, peak-energy explosion, unrelated scene
or config edit, or unapproved schema transition rode along with the refresh.

## Gate-Driven Repairs

The final graphics-stress run exposed a real persistent-grid under-reservation
in a 101-body generated scene: the measured layout used 1,340 rows while the
earlier `8 * bodies + 32` capacity admitted only 840. The final scene-load
formula is `8 * bodies + 1,024`. The fixed spill is one quarter of the retired
4,096-row blanket and covers the measured layout plus bounded motion margin
without granting runtime or Replay growth.

A focused regression combines 100 ordinary separated bodies with one
radius-40 body, reaches more than 1,340 persistent rows, and stays within the
reserved capacity. Exact owner-census and fatal-contract tests now pin 17,024
rows for 2,000 bodies, 1,032 rows for one body, startup-phase reserve denial,
and Physics-phase failure on request 1,033. SpatialGrid passes 22 cases and
8,594 assertions. The one-minute graphics stress run passes descriptor churn
and terminates by its exact launched PID. Its wrapper now treats any `FATAL:`
stderr diagnostic as a failure even if Windows teardown reports process exit
zero.

Performance A/B isolated a hidden, disabled broadphase-overlay snapshot copy.
`RuntimeOverlayDiagnostics` now requests active-cell data only when the overlay
is visible or the authored automation gate still needs the observation. Both
consumers use the same captured snapshot. The pre-change worktree passed the
same performance baseline, and the final branch passes without changing the
performance baseline.

The scene writer's exact-current function-complexity ruling was refreshed only
for the scene schema `2` to `3` body change. Its owner, lifetime, control flow,
and retain-owner disposition remain unchanged.

## Validation

- `python tools/migrate_data_formats.py --check`: 62 files pass.
- Modified Python tools compile with `python -m py_compile`.
- `python tools/generate_physics_scale_sleepy_scene.py --check`: 4,000
  sleepers, 1,000 awake bodies, capacity 6,000, byte-stable output.
- `tools\validate_tests.bat`: 457/457 cases and 2,422,070/2,422,070 assertions.
- `tools\validate_physics.bat`: passes against regenerated artifacts.
- `tools\validate_physics_deep.bat`: passes against regenerated artifacts.
- `tools\validate_dx12_renderer.bat`: passes both accepted screenshot deltas.
- `tools\run_graphics_stress.bat 1`: passes descriptor churn and the bounded
  PID-timeout path with no fatal diagnostic.
- `tools\validate_replay_visual_fidelity.bat`: 17/17 focused cases,
  75/75 assertions, 2,401 ticks, 200 causal nodes, v5 durable round-trip, and
  every offline false-pass control.
- `tools\validate_replay_v2_artifact.bat`: writer v5, authentic
  presentation-only v3 quaternion/state-hash migration, future v6 rejection,
  and mutated visual-state rejection all pass.
- `tools\validate_fast.bat`: formatting, metadata, dependency direction,
  ownership inventories, Profile build, and tests pass.
- `tools\validate_perf.bat`: both lanes pass without a performance-baseline
  change.
- `tools\validate_full.bat`: passes the mandatory CPU, ownership, dependency,
  Automation, Debug, DX12, Physics, and runtime gates.

## Comment Audit

The plan checklist was reconciled against the complete source/tool diff from
`0ded422e`. All 41 scoped source-bearing files were read against
`Agentic/Reference/comment-style-guide.md`; none are deferred or unchecked.
Learning headers, local invariants, lifetime notes, hazards, ownership names,
and repository-relative `Related:` paths match the final source.

Checklist result: 41 checked, 0 deferred, 0 unchecked. The final reconciliation
added `ReplayPredictionArchive.Automation.cpp`, which the first review caused
QN5 to touch.

## Independent Review

The plan-required fresh read-only review examined the complete change from
`90e4d52f`, including canonical math, compensation sites, migrations, replay
tooling, regenerated artifacts, SpatialGrid capacity, overlay diagnostics,
ownership, dependencies, and comment closure.

The initial QN5 review found two blocking validation-quality gaps. The
ReplayV2 legacy fixture relabelled canonical PRES/SCHK bytes instead of
carrying historical quaternion bytes, and RVPD compatibility was claimed from
a current-schema round trip alone. QN5 repaired both:

- the ReplayV2 fixture is now presentation-only v3, conjugates every stored
  quaternion, recomputes every historical presentation state hash, and is
  scrubbed by the real Debug loader; the focused codec test independently
  proves the loaded values and hashes are canonical;
- the Automation RVPD verifier now writes authentic schema-v2 quaternion
  bytes, loads them, rebuilds byte-identical canonical schema-v3 output, and
  rejects schema 4 with the frozen header failure.

Focused follow-up verdict: **CLEAR — no remaining findings.** The same
independent reviewer verified the historical PRES bytes and hashes, canonical
post-load proof, genuine RVPD schema-v2 emission and reconstruction, exact
schema-4 rejection, and this closure evidence.
