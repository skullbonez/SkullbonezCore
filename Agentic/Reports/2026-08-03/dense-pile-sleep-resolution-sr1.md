# Dense Pile Sleep Resolution SR1 — Lifetime Continuity Diagnosis

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Plan phase: SR1 of 5
Impact area: Physics contact lifetime, warm start, solver convergence, sleep support

## Ruling

The leading hypothesis is confirmed in its important half and contradicted in
its broad cache-rate form.

Exact feature identity is too narrow to own restitution lifetime. A body pair
that carried load in the previous frame can select a different feature in the
current frame. The exact lookup then reports no loaded history, admits
restitution, and adds impulse to a contact that pair continuity would classify
as continuing support. This is the initiating difference between the historical
pair-prefix build and the authoritative exact-feature build.

Feature churn also prevents reuse of the retiring feature's warm-start impulse,
but aggregate cache-hit degradation is not the cause of the pair/exact outcome.
Both builds deliberately retain exact-feature warm-start compatibility. Across
frames 1,200–6,799, exact-feature records a slightly *higher* cache hit rate
(94.7904% versus 94.5217%) but a lower warm-started-row rate (95.8138% versus
96.2671%). Pair-prefix still sleeps permanently because it decouples
restitution lifetime from exact warm-start compatibility. SR2 must preserve
that separation at O(1); it must not restore the rejected per-row prefix scan.

Residual solver non-convergence is a downstream amplifier, not the initiating
cause. Support-footprint classification is not unstable in the late window and
is rejected as the mechanism.

## Reused Deterministic Artifacts

The traces were generated in clean detached Debug worktrees by the SR0 command
below, with `<point>` equal to `pair` or `exact`:

```powershell
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off `
  --hide-top-text --automation-hidden-window --frames 6800 `
  --scene SkullbonezData/scenes/box_pile_throw_300.scene.json `
  --physics-diag TestOutput/dense_pile_sr0/pile_<point>_6800.physicsdiag.ndjson `
  --physics-regression-log TestOutput/dense_pile_sr0/pile_<point>_6800.csv
```

| Point | Commit | NDJSON bytes | SQLite bytes |
|---|---:|---:|---:|
| pair-prefix | `12dbb3eb` | 2,578,989,107 | 1,281,052,672 |
| exact-feature | `194cbf82` | 2,635,666,951 | 1,293,840,384 |

The raw NDJSON, SQLite, and CSV artifacts were never read into the model. Only
bounded `physics_query.bat` packets listed below were exposed.

## Measured Lifetime And Restitution

Frame 1,200 is the settling threshold `N`: the initial throw has completed and
the SR0 energy series has entered its long decay. The query computes each
metric per frame and then folds frames into four bounded windows.

| Window | Pair churn | Exact churn | Pair false exact lifetime rows | Exact false exact lifetime rows | Exact-only restitution rows | Exact-only solved normal impulse |
|---|---:|---:|---:|---:|---:|---:|
| 1,200–2,399 | 7.8927% | 8.6034% | 5,063 | 5,913 | 47 | 312.205613 |
| 2,400–3,599 | 6.2924% | 6.6272% | 2,024 | 3,570 | 12 | 106.746788 |
| 3,600–4,799 | 7.5291% | 6.9368% | 1,107 | 2,130 | 7 | 90.326450 |
| 4,800–6,799 | 7.8396% | 12.6138% | 1,941 | 1,469 | 0 | 0 |

“False exact lifetime” means that a cache-eligible body-pair row carried
non-zero normal or tangent load last frame but the current exact feature did
not. Cache eligibility follows `StoreCache`: horizontal contacts qualify, while
vertical contacts require the emitted `object_contact` support edge. There are
13,082 exact-feature rows and 10,135 pair-prefix rows over the settling
interval. Churn therefore exists in both histories. Only the exact-feature
restitution policy treats it as a new impact.

The exact build admits 66 cache-eligible continuing-pair rows after frame 1,200.
Their solved normal impulse totals 509.278851 and ranges from 3.192805 to
26.971516:

| Solved normal impulse | Rows | Mean | Maximum |
|---|---:|---:|---:|
| (1, 5] | 23 | 4.229364 | 4.836221 |
| (5, 20] | 41 | 8.793977 | 19.052013 |
| >20 | 2 | 25.725206 | 26.971516 |

The first such row is at frame 1,212 and the last at frame 4,255. Across their
132 body endpoints, ten quiet-counter zero resets, sixteen support-bit flips,
and twenty-seven inhibition-bit flips correlate in the same frame. This is
state association, not a counterfactual claim that one row alone caused each
transition. Later feature churn does not cross the 2.0 restitution threshold,
but it continues to discard exact warm starts and preserves the state
divergence created by earlier admissions.

## First Material Divergence

`physics_query compare` reports frame 24 as the first differing aggregate:
total energy is 1,846,773.852468 under exact-feature lifetime and
1,846,773.739304 under pair lifetime.

The responsible body pair is 306/308. Feature 34306 was not the loaded exact
feature from frame 23, but the pair had carried load. Both builds see the same
2.631919 closing speed and both miss the exact warm start. Pair lifetime
suppresses renewed restitution and solves 1.979763 normal impulse; exact-feature
lifetime admits it and solves 2.221948. The 0.242185 impulse difference is the
first material result of the only policy difference between the two commits.

Frame 24 is also the first cache-eligible exact-only candidate, so it is the
causal boundary used by SR2.

## Competing Hypotheses

### Solver non-convergence: amplifier, not initiator

| Window | Pair cap frames | Exact cap frames | Pair mean final delta² | Exact mean final delta² |
|---|---:|---:|---:|---:|
| 1,200–2,399 | 1,200/1,200 | 1,200/1,200 | 1.017550900 | 2.821121680 |
| 2,400–3,599 | 1,200/1,200 | 1,200/1,200 | 1.099546119 | 2.991893669 |
| 3,600–4,799 | 1,165/1,200 | 1,193/1,200 | 0.140636513 | 2.044614232 |
| 4,800–6,799 | 1,905/2,000 | 1,960/2,000 | 0.163010654 | 0.952502793 |

Both histories are commonly above the `1.0e-6` early-out threshold at the
12-iteration cap, including pair-prefix. Exact-feature carries more rows and a
larger residual after the lifetime divergence, so non-convergence amplifies the
motion. It cannot initiate the frame-24 difference and is insufficient to
explain why pair-prefix later sleeps all 330 bodies.

### `hasRestingFootprint` / `supportsRestingPolicy`: rejected

The trace does not emit `supportsRestingPolicy` directly. Its `point_count` is
the post-reduction solver-row count, so a four-point quiet footprint reduced to
two rows cannot always be distinguished from an original two-point edge. SR1
therefore uses two bounded projections instead of claiming an exact field
reconstruction:

- a conservative source-form proxy from diagnostic point count, normal, and
  supported-body quaternion; and
- the actual `object_contact` support-edge stream, which is emitted only when
  the source predicate admits vertical support.

The proxy reports 34 pair-prefix and 37 exact-feature classification flips after
frame 1,200. The actual support-edge stream reports 36 and 56 respectively;
rates remain below 0.078% in every window. Both projections record **zero**
flips after frame 4,800 in both histories. Exact-feature retains stable
edge-only state after divergence, but neither trace shows late classification
instability, and the support stream does not precede the frame-24 lifetime
impulse difference.

## SR2 Design Boundary

SR2 must compare at least three mechanisms against this evidence:

1. a compact O(1) pair-lifetime token carried beside the exact-feature cache;
2. manifold feature stabilization across sub-slop rocking;
3. manifold-owned transfer from a retiring support feature to a reselected
   feature known to describe the same support patch.

The selected design must keep exact-feature warm-start compatibility, suppress
restitution for a pair that carried load last frame, admit restitution after a
full no-contact frame, preserve elastic mutual-gravity behavior, and avoid the
rejected prefix walk. It must state its per-row memory and lookup cost and its
expected byte-exact impact on the accepted wall/four-brick goldens before SR3
changes production source.

## SkullScope Query Log And Accounting

The pair and exact paths below abbreviate only the absolute worktree prefix:

```text
PAIR  = C:\Temp\SkullbonezCore-sr0-pair\TestOutput\dense_pile_sr0\pile_pair_6800.physicsdiag.ndjson
EXACT = C:\Temp\SkullbonezCore-sr0-exact\TestOutput\dense_pile_sr0\pile_exact_6800.physicsdiag.ndjson
```

Every successful decision query is printed here. SQL A–J are the exact
single-line statements passed as the `statement` argument.

```powershell
tools\physics_query.bat PAIR summary --pretty
tools\physics_query.bat EXACT summary --pretty
tools\physics_query.bat EXACT sql "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name" --pretty --limit 100
tools\physics_query.bat EXACT sql "<SQL A>" --pretty --limit 20
tools\physics_query.bat EXACT compare PAIR --frames 0:1200 --pretty --limit 20
tools\physics_query.bat PAIR sql "<SQL B>" --pretty --limit 20
tools\physics_query.bat EXACT sql "<SQL B>" --pretty --limit 20
tools\physics_query.bat PAIR sql "<SQL C>" --pretty --limit 20
tools\physics_query.bat EXACT sql "<SQL C>" --pretty --limit 20
tools\physics_query.bat PAIR sql "<SQL D>" --pretty --limit 20
tools\physics_query.bat EXACT sql "<SQL D>" --pretty --limit 20
tools\physics_query.bat PAIR sql "<SQL E>" --pretty --limit 20
tools\physics_query.bat EXACT sql "<SQL E>" --pretty --limit 20
tools\physics_query.bat EXACT sql "SELECT source,COUNT(*) AS rows,MIN(frame) AS first_frame,MAX(frame) AS last_frame FROM support_edges GROUP BY source ORDER BY source" --pretty --limit 20
tools\physics_query.bat PAIR sql "<SQL H>" --pretty --limit 20
tools\physics_query.bat EXACT sql "<SQL H>" --pretty --limit 20
tools\physics_query.bat EXACT sql "<SQL F>" --limit 100
tools\physics_query.bat PAIR sql "<SQL G>" --pretty --limit 20
tools\physics_query.bat EXACT sql "<SQL G>" --pretty --limit 20
tools\physics_query.bat EXACT sql "<SQL I>" --limit 30
tools\physics_query.bat EXACT sql "<SQL J>" --pretty --limit 20
```

- **SQL A — schema:** `SELECT name, sql FROM sqlite_master WHERE type='table' AND name IN ('contacts','solver_stats','solver_iteration_summaries','bodies','frames','support_edges') ORDER BY name`
- **SQL B — lifetime windows:** builds `prior_cache` with the same load and `supportsRestingPolicy` eligibility as `StoreCache`, derives loaded body pairs, joins current rows to both any prior exact row and the eligible prior exact row, and groups the churn/lifetime/restitution metrics over `1200:6799`.
- **SQL C — cache windows:** `SELECT CASE WHEN frame<2400 THEN '1200-2399' WHEN frame<3600 THEN '2400-3599' WHEN frame<4800 THEN '3600-4799' ELSE '4800-6799' END AS frame_window,COUNT(*) AS frames,SUM(cache_hits) AS cache_hits,SUM(cache_misses) AS cache_misses,ROUND(1.0*SUM(cache_hits)/NULLIF(SUM(cache_hits)+SUM(cache_misses),0),6) AS cache_hit_rate,SUM(warm_started_rows) AS warm_started_rows,SUM(row_count) AS solver_rows,ROUND(1.0*SUM(warm_started_rows)/NULLIF(SUM(row_count),0),6) AS warm_start_rate FROM solver_stats WHERE frame BETWEEN 1200 AND 6799 GROUP BY frame_window ORDER BY frame_window`
- **SQL D — convergence windows:** joins every `solver_stats` row to the maximum recorded iteration in `solver_iteration_summaries`, then reports cap/early-out counts and final `stopping_impulse_delta_sq` over the four windows.
- **SQL E — support classification:** reconstructs the source quaternion matrix and the maximum supported-body face-axis dot, classifies `point_count <= 2`, vertical, `<0.95` rows as edge-only, joins the previous pair classification, and reports losses/gains/flips over the four windows.
- **SQL F — admitted restitution rows:** selects exact-feature rows in `1200:6799` with closing speed above 2.0, zero separation bias, a cache-eligible loaded prior-frame pair, and no cache-eligible loaded prior exact contact; it returns frame, solved normal impulse, and closing speed ordered by frame.
- **SQL G — frame 24:** `SELECT c.frame,c.body_a,c.body_b,c.feature_id,c.point_count,ROUND(c.pre_solve_closing_speed,6) AS closing_speed,ROUND(c.normal_impulse,6) AS solved_normal_impulse,ROUND(c.tangent_impulse,6) AS solved_tangent_impulse,c.warm_started,ROUND(f.total_energy,6) AS frame_energy FROM contacts c JOIN frames f ON f.run_id=c.run_id AND f.frame=c.frame WHERE c.frame=24 AND c.body_a=306 AND c.body_b=308 ORDER BY c.feature_id`
- **SQL H — actual support-edge continuity:** canonicalizes distinct `object_contact` support edges and distinct object-contact pairs, limits the population to pairs present in consecutive frames, and reports support-edge presence flips, losses, and gains over the four settling windows.
- **SQL I — early restitution rows:** applies SQL F's cache-eligible lifetime predicates to frames `1:100` and returns the first 30 body/feature/impulse rows used to locate frame 24.
- **SQL J — endpoint transition correlation:** materializes SQL F's admitted body pairs, expands them to both endpoints, joins current/previous body state, and counts same-frame quiet-counter decreases or zero resets plus support/inhibition flips.

For auditability, the non-trivial statements above are printed verbatim below;
line breaks are whitespace only.

```sql
-- SQL B: cache-eligible lifetime windows
WITH prior_cache AS (
  SELECT p.* FROM contacts p
  WHERE p.body_b >= 0 AND p.frame BETWEEN 1199 AND 6798
    AND (p.normal_impulse > 0.0 OR p.tangent_impulse > 0.00001)
    AND (ABS(p.normal_y) <= 0.25 OR EXISTS (
      SELECT 1 FROM support_edges se
      WHERE se.run_id = p.run_id AND se.frame = p.frame
        AND se.source = 'object_contact'
        AND MIN(se.supporter,se.supported) = p.body_a
        AND MAX(se.supporter,se.supported) = p.body_b))
), prior_pairs AS (
  SELECT run_id, frame, body_a, body_b, 1 AS loaded_pair
  FROM prior_cache GROUP BY run_id, frame, body_a, body_b
), per_frame AS (
  SELECT c.frame, COUNT(*) AS object_rows,
         SUM(CASE WHEN pp.body_a IS NOT NULL THEN 1 ELSE 0 END) AS continued_loaded_pair_rows,
         SUM(CASE WHEN pp.body_a IS NOT NULL AND any_exact.contact_id IS NULL THEN 1 ELSE 0 END) AS feature_churn_rows,
         SUM(CASE WHEN pp.body_a IS NOT NULL AND e.contact_id IS NULL THEN 1 ELSE 0 END) AS false_exact_lifetime_rows,
         SUM(CASE WHEN pp.body_a IS NOT NULL AND e.contact_id IS NULL AND c.pre_solve_closing_speed > 2.0 AND c.separation_bias = 0.0 THEN 1 ELSE 0 END) AS exact_only_restitution_rows,
         SUM(CASE WHEN pp.body_a IS NOT NULL AND e.contact_id IS NULL AND c.pre_solve_closing_speed > 2.0 AND c.separation_bias = 0.0 THEN c.normal_impulse ELSE 0.0 END) AS exact_only_normal_impulse
  FROM contacts c
  LEFT JOIN contacts any_exact ON any_exact.run_id = c.run_id AND any_exact.frame = c.frame - 1 AND any_exact.contact_id = c.contact_id
  LEFT JOIN prior_cache e ON e.run_id = c.run_id AND e.frame = c.frame - 1 AND e.contact_id = c.contact_id
  LEFT JOIN prior_pairs pp ON pp.run_id = c.run_id AND pp.frame = c.frame - 1 AND pp.body_a = c.body_a AND pp.body_b = c.body_b
  WHERE c.body_b >= 0 AND c.frame BETWEEN 1200 AND 6799 GROUP BY c.frame
), windowed AS (
  SELECT CASE WHEN frame < 2400 THEN '1200-2399' WHEN frame < 3600 THEN '2400-3599' WHEN frame < 4800 THEN '3600-4799' ELSE '4800-6799' END AS frame_window, * FROM per_frame
)
SELECT frame_window, COUNT(*) AS frames_with_object_rows, SUM(object_rows) AS object_rows,
       SUM(continued_loaded_pair_rows) AS continued_loaded_pair_rows, SUM(feature_churn_rows) AS feature_churn_rows,
       ROUND(1.0 * SUM(feature_churn_rows) / NULLIF(SUM(continued_loaded_pair_rows), 0), 6) AS churn_rate,
       SUM(false_exact_lifetime_rows) AS false_exact_lifetime_rows,
       ROUND(AVG(false_exact_lifetime_rows), 3) AS false_rows_per_active_frame,
       MAX(false_exact_lifetime_rows) AS max_false_rows_frame,
       SUM(exact_only_restitution_rows) AS exact_only_restitution_rows,
       ROUND(SUM(exact_only_normal_impulse), 6) AS exact_only_normal_impulse
FROM windowed GROUP BY frame_window ORDER BY frame_window;
```

```sql
-- SQL D: solver convergence windows
WITH last_iter AS (
  SELECT run_id, frame, MAX(iteration) AS last_iteration
  FROM solver_iteration_summaries WHERE frame BETWEEN 1200 AND 6799 GROUP BY run_id, frame
), per_frame AS (
  SELECT s.frame, s.solver_iterations, i.stopping_impulse_delta_sq
  FROM solver_stats s
  LEFT JOIN last_iter l ON l.run_id = s.run_id AND l.frame = s.frame
  LEFT JOIN solver_iteration_summaries i ON i.run_id = l.run_id AND i.frame = l.frame AND i.iteration = l.last_iteration
  WHERE s.frame BETWEEN 1200 AND 6799
), windowed AS (
  SELECT CASE WHEN frame < 2400 THEN '1200-2399' WHEN frame < 3600 THEN '2400-3599' WHEN frame < 4800 THEN '3600-4799' ELSE '4800-6799' END AS frame_window, * FROM per_frame
)
SELECT frame_window, COUNT(*) AS frames,
       SUM(CASE WHEN solver_iterations = 12 THEN 1 ELSE 0 END) AS cap_frames,
       SUM(CASE WHEN solver_iterations < 12 THEN 1 ELSE 0 END) AS early_out_frames,
       ROUND(AVG(solver_iterations), 6) AS avg_iterations,
       ROUND(AVG(stopping_impulse_delta_sq), 9) AS avg_final_delta_sq,
       ROUND(MAX(stopping_impulse_delta_sq), 9) AS max_final_delta_sq,
       SUM(CASE WHEN stopping_impulse_delta_sq >= 0.000001 THEN 1 ELSE 0 END) AS final_delta_above_stop
FROM windowed GROUP BY frame_window ORDER BY frame_window;
```

```sql
-- SQL E: box/box supportsRestingPolicy reconstruction
WITH base AS (
  SELECT DISTINCT c.run_id, c.frame, c.body_a, c.body_b, c.point_count,
         c.normal_x, c.normal_y, c.normal_z, b.q_x, b.q_y, b.q_z, b.q_w,
         CASE WHEN c.normal_y > 0.0 THEN c.normal_x ELSE -c.normal_x END AS sx,
         ABS(c.normal_y) AS sy,
         CASE WHEN c.normal_y > 0.0 THEN c.normal_z ELSE -c.normal_z END AS sz
  FROM contacts c JOIN bodies b ON b.run_id = c.run_id AND b.frame = c.frame
   AND b.body_id = CASE WHEN c.normal_y > 0.0 THEN c.body_b ELSE c.body_a END
  WHERE c.body_b >= 0 AND c.frame BETWEEN 1199 AND 6799
), dots AS (
  SELECT *,
    ABS((1-2*q_y*q_y-2*q_z*q_z)*sx + (2*q_x*q_y+2*q_w*q_z)*sy + (2*q_x*q_z-2*q_w*q_y)*sz) AS dx,
    ABS((2*q_x*q_y-2*q_w*q_z)*sx + (1-2*q_x*q_x-2*q_z*q_z)*sy + (2*q_y*q_z+2*q_w*q_x)*sz) AS dy,
    ABS((2*q_x*q_z+2*q_w*q_y)*sx + (2*q_y*q_z-2*q_w*q_x)*sy + (1-2*q_x*q_x-2*q_y*q_y)*sz) AS dz
  FROM base
), classified AS (
  SELECT run_id, frame, body_a, body_b,
    CASE WHEN point_count <= 2 AND ABS(normal_y) > 0.25 AND MAX(dx,dy,dz) < 0.95 THEN 0 ELSE 1 END AS has_resting_footprint
  FROM dots
), per_frame AS (
  SELECT c.frame, COUNT(*) AS object_pairs,
    SUM(CASE WHEN c.has_resting_footprint = 0 THEN 1 ELSE 0 END) AS edge_only_pairs,
    SUM(CASE WHEN p.body_a IS NOT NULL AND c.has_resting_footprint <> p.has_resting_footprint THEN 1 ELSE 0 END) AS classification_flips,
    SUM(CASE WHEN p.has_resting_footprint = 1 AND c.has_resting_footprint = 0 THEN 1 ELSE 0 END) AS footprint_losses,
    SUM(CASE WHEN p.has_resting_footprint = 0 AND c.has_resting_footprint = 1 THEN 1 ELSE 0 END) AS footprint_gains
  FROM classified c LEFT JOIN classified p ON p.run_id = c.run_id AND p.frame = c.frame - 1 AND p.body_a = c.body_a AND p.body_b = c.body_b
  WHERE c.frame BETWEEN 1200 AND 6799 GROUP BY c.frame
), windowed AS (
  SELECT CASE WHEN frame < 2400 THEN '1200-2399' WHEN frame < 3600 THEN '2400-3599' WHEN frame < 4800 THEN '3600-4799' ELSE '4800-6799' END AS frame_window, * FROM per_frame
)
SELECT frame_window, COUNT(*) AS frames_with_object_pairs, SUM(object_pairs) AS object_pair_frames,
       SUM(edge_only_pairs) AS edge_only_pair_frames,
       ROUND(1.0 * SUM(edge_only_pairs) / NULLIF(SUM(object_pairs),0), 6) AS edge_only_rate,
       SUM(classification_flips) AS classification_flips,
       ROUND(AVG(classification_flips),3) AS flips_per_active_frame,
       SUM(footprint_losses) AS footprint_losses, SUM(footprint_gains) AS footprint_gains
FROM windowed GROUP BY frame_window ORDER BY frame_window;
```

```sql
-- SQL F: exact-feature restitution admissions after settling threshold N
SELECT c.frame, ROUND(c.normal_impulse,6) AS normal_impulse,
       ROUND(c.pre_solve_closing_speed,6) AS closing_speed
FROM contacts c
WHERE c.body_b >= 0 AND c.frame BETWEEN 1200 AND 6799
  AND c.pre_solve_closing_speed > 2.0 AND c.separation_bias = 0.0
  AND EXISTS (SELECT 1 FROM contacts p WHERE p.run_id = c.run_id AND p.frame = c.frame - 1
              AND p.body_a = c.body_a AND p.body_b = c.body_b
              AND (p.normal_impulse > 0.0 OR p.tangent_impulse > 0.00001)
              AND (ABS(p.normal_y) <= 0.25 OR EXISTS (
                SELECT 1 FROM support_edges se WHERE se.run_id = p.run_id AND se.frame = p.frame
                  AND se.source = 'object_contact' AND MIN(se.supporter,se.supported) = p.body_a
                  AND MAX(se.supporter,se.supported) = p.body_b)))
  AND NOT EXISTS (SELECT 1 FROM contacts e WHERE e.run_id = c.run_id AND e.frame = c.frame - 1
                  AND e.contact_id = c.contact_id
                  AND (e.normal_impulse > 0.0 OR e.tangent_impulse > 0.00001)
                  AND (ABS(e.normal_y) <= 0.25 OR EXISTS (
                    SELECT 1 FROM support_edges se WHERE se.run_id = e.run_id AND se.frame = e.frame
                      AND se.source = 'object_contact' AND MIN(se.supporter,se.supported) = e.body_a
                      AND MAX(se.supporter,se.supported) = e.body_b)))
ORDER BY c.frame, c.body_a, c.body_b, c.feature_id;
```

```sql
-- SQL H: actual support-edge continuity for continuing contact pairs
WITH pairs AS (
  SELECT DISTINCT run_id, frame, body_a, body_b FROM contacts
  WHERE body_b >= 0 AND frame BETWEEN 1199 AND 6799
), edges AS (
  SELECT DISTINCT run_id, frame, MIN(supporter,supported) AS body_a,
         MAX(supporter,supported) AS body_b
  FROM support_edges WHERE source = 'object_contact' AND frame BETWEEN 1199 AND 6799
), per_frame AS (
  SELECT c.frame, COUNT(*) AS continued_contact_pairs,
    SUM(CASE WHEN ce.body_a IS NOT NULL THEN 1 ELSE 0 END) AS current_support_edges,
    SUM(CASE WHEN pe.body_a IS NOT NULL THEN 1 ELSE 0 END) AS previous_support_edges,
    SUM(CASE WHEN (ce.body_a IS NOT NULL) <> (pe.body_a IS NOT NULL) THEN 1 ELSE 0 END) AS support_edge_flips,
    SUM(CASE WHEN ce.body_a IS NULL AND pe.body_a IS NOT NULL THEN 1 ELSE 0 END) AS support_edge_losses,
    SUM(CASE WHEN ce.body_a IS NOT NULL AND pe.body_a IS NULL THEN 1 ELSE 0 END) AS support_edge_gains
  FROM pairs c JOIN pairs p ON p.run_id = c.run_id AND p.frame = c.frame - 1
    AND p.body_a = c.body_a AND p.body_b = c.body_b
  LEFT JOIN edges ce ON ce.run_id = c.run_id AND ce.frame = c.frame
    AND ce.body_a = c.body_a AND ce.body_b = c.body_b
  LEFT JOIN edges pe ON pe.run_id = c.run_id AND pe.frame = c.frame - 1
    AND pe.body_a = c.body_a AND pe.body_b = c.body_b
  WHERE c.frame BETWEEN 1200 AND 6799 GROUP BY c.frame
), windowed AS (
  SELECT CASE WHEN frame < 2400 THEN '1200-2399' WHEN frame < 3600 THEN '2400-3599'
              WHEN frame < 4800 THEN '3600-4799' ELSE '4800-6799' END AS frame_window, *
  FROM per_frame
)
SELECT frame_window, COUNT(*) AS frames, SUM(continued_contact_pairs) AS continued_contact_pairs,
       SUM(support_edge_flips) AS support_edge_flips,
       ROUND(1.0 * SUM(support_edge_flips) / NULLIF(SUM(continued_contact_pairs),0),6) AS flip_rate,
       SUM(support_edge_losses) AS losses, SUM(support_edge_gains) AS gains
FROM windowed GROUP BY frame_window ORDER BY frame_window;
```

```sql
-- SQL I: first cache-eligible exact-only restitution candidates
SELECT c.frame, c.body_a, c.body_b, c.feature_id,
       ROUND(c.pre_solve_closing_speed,6) AS closing_speed,
       ROUND(c.normal_impulse,6) AS solved_normal_impulse,
       ROUND(c.tangent_impulse,6) AS solved_tangent_impulse, c.point_count
FROM contacts c
WHERE c.body_b >= 0 AND c.frame BETWEEN 1 AND 100
  AND c.pre_solve_closing_speed > 2.0 AND c.separation_bias = 0.0
  AND EXISTS (SELECT 1 FROM contacts p WHERE p.run_id = c.run_id AND p.frame = c.frame - 1
              AND p.body_a = c.body_a AND p.body_b = c.body_b
              AND (p.normal_impulse > 0.0 OR p.tangent_impulse > 0.00001)
              AND (ABS(p.normal_y) <= 0.25 OR EXISTS (
                SELECT 1 FROM support_edges se WHERE se.run_id = p.run_id AND se.frame = p.frame
                  AND se.source = 'object_contact' AND MIN(se.supporter,se.supported) = p.body_a
                  AND MAX(se.supporter,se.supported) = p.body_b)))
  AND NOT EXISTS (SELECT 1 FROM contacts e WHERE e.run_id = c.run_id AND e.frame = c.frame - 1
                  AND e.contact_id = c.contact_id
                  AND (e.normal_impulse > 0.0 OR e.tangent_impulse > 0.00001)
                  AND (ABS(e.normal_y) <= 0.25 OR EXISTS (
                    SELECT 1 FROM support_edges se WHERE se.run_id = e.run_id AND se.frame = e.frame
                      AND se.source = 'object_contact' AND MIN(se.supporter,se.supported) = e.body_a
                      AND MAX(se.supporter,se.supported) = e.body_b)))
ORDER BY c.frame, c.body_a, c.body_b, c.feature_id LIMIT 30;
```

```sql
-- SQL J: body-state transitions correlated with cache-eligible admissions
WITH admitted AS MATERIALIZED (
  SELECT c.run_id, c.frame, c.body_a, c.body_b FROM contacts c
  WHERE c.body_b >= 0 AND c.frame BETWEEN 1200 AND 6799
    AND c.pre_solve_closing_speed > 2.0 AND c.separation_bias = 0.0
    AND EXISTS (SELECT 1 FROM contacts p WHERE p.run_id = c.run_id AND p.frame = c.frame - 1
                AND p.body_a = c.body_a AND p.body_b = c.body_b
                AND (p.normal_impulse > 0.0 OR p.tangent_impulse > 0.00001)
                AND (ABS(p.normal_y) <= 0.25 OR EXISTS (
                  SELECT 1 FROM support_edges se WHERE se.run_id = p.run_id AND se.frame = p.frame
                    AND se.source = 'object_contact' AND MIN(se.supporter,se.supported) = p.body_a
                    AND MAX(se.supporter,se.supported) = p.body_b)))
    AND NOT EXISTS (SELECT 1 FROM contacts e WHERE e.run_id = c.run_id AND e.frame = c.frame - 1
                    AND e.contact_id = c.contact_id
                    AND (e.normal_impulse > 0.0 OR e.tangent_impulse > 0.00001)
                    AND (ABS(e.normal_y) <= 0.25 OR EXISTS (
                      SELECT 1 FROM support_edges se WHERE se.run_id = e.run_id AND se.frame = e.frame
                        AND se.source = 'object_contact' AND MIN(se.supporter,se.supported) = e.body_a
                        AND MAX(se.supporter,se.supported) = e.body_b)))
), endpoints AS (
  SELECT run_id, frame, body_a AS body_id FROM admitted
  UNION ALL SELECT run_id, frame, body_b AS body_id FROM admitted
)
SELECT COUNT(*) AS endpoints,
       SUM(CASE WHEN prev.sleep_counter > 0 AND cur.sleep_counter = 0 THEN 1 ELSE 0 END) AS correlated_quiet_counter_zero_resets,
       SUM(CASE WHEN cur.sleep_counter < prev.sleep_counter THEN 1 ELSE 0 END) AS correlated_quiet_counter_decreases,
       SUM(CASE WHEN cur.sleep_supported <> prev.sleep_supported THEN 1 ELSE 0 END) AS support_state_flips,
       SUM(CASE WHEN cur.sleep_inhibited <> prev.sleep_inhibited THEN 1 ELSE 0 END) AS inhibited_state_flips,
       ROUND(AVG(cur.speed),6) AS avg_endpoint_speed,
       ROUND(MAX(cur.speed),6) AS max_endpoint_speed,
       ROUND(AVG(cur.omega_mag),6) AS avg_endpoint_omega,
       ROUND(MAX(cur.omega_mag),6) AS max_endpoint_omega
FROM endpoints x
JOIN bodies cur ON cur.run_id=x.run_id AND cur.frame=x.frame AND cur.body_id=x.body_id
JOIN bodies prev ON prev.run_id=x.run_id AND prev.frame=x.frame-1 AND prev.body_id=x.body_id;
```

| Packet | GPT-read chars / UTF-8 bytes | Retained file bytes | Truncated |
|---|---:|---:|---|
| pair/exact summaries | 9,098 / 9,098; 9,124 / 9,124 | 18,198; 18,250 | no |
| table list / schema | 1,058 / 1,058; 4,919 / 4,919 | 2,118; 9,840 | no |
| final pair/exact lifetime windows | 2,391 / 2,391; 2,415 / 2,415 | 4,784; 4,832 | no |
| pair/exact cache windows | 1,576 / 1,576; 1,579 / 1,579 | 3,154; 3,160 | no |
| pair/exact convergence windows | 1,666 / 1,666; 1,668 / 1,668 | 3,334; 3,338 | no |
| pair/exact support windows | 1,896 / 1,896; 1,914 / 1,914 | 3,794; 3,830 | no |
| exact support-edge source census | 650 / 650 | 1,302 | no |
| pair/exact support-edge continuity | 1,333 / 1,333; 1,344 / 1,344 | 2,668; 2,690 | no |
| exact/pair compare | 1,462 / 1,462 | 2,926 | no |
| early exact rows | 5,953 / 5,953 | 11,908 | no |
| exact settling restitution rows | 4,676 / 4,676 | 9,354 | no |
| pair/exact frame 24 | 1,160 / 1,160; 1,164 / 1,164 | 2,322; 2,330 | no |
| exact endpoint reset summary | 986 / 986 | 1,974 | no |

Successful, fully accounted decision output totals **58,032 characters / UTF-8
bytes**. `Tee-Object` retained UTF-16LE files, hence the approximately doubled
file-byte counts. Accounting replays used `Out-Null` and exposed zero additional
query bytes.

Exploratory accounting is explicit: one pair/exact solver packet retained
20,100/20,124 characters but their combined tool display was truncated, so
neither packet is used as evidence; SQL C and D are the bounded replacements.
One earlier lifetime query returned 2,214/2,218 characters but used SQL `NOT`
over a nullable exact join and was rejected; the corrected final packets above
supersede it. The first cache-eligibility pass also counted one loaded row that
`StoreCache` excludes: frame 1,700, pair 63/207, feature 51840, solved normal
impulse 12.951690. The final SQL B, F, I, and J packets exclude it through the
actual horizontal-or-support-edge eligibility rule. Two multiline
batch-boundary invocations failed with incomplete
SQL, two early-row attempts timed out or were stopped, and two histogram attempts
timed out or were stopped. Those failed attempts emitted no query JSON. No
successful decision query was truncated.

## Change Scope And Validation

SR1 changes tracked documentation only. It changes no source, scene, config,
baseline, golden, trace, or cache. Repository validation is therefore not
required for this phase; `git diff --check` is the pre-commit hygiene gate.
