# Dense Pile Sleep Resolution SR2 — Candidate Ruling

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Plan phase: SR2 of 5
Impact area: Physics restitution lifetime, sleep progress, persistent cache

## Ruling

SR3 will implement a **half-quiet adjacent pair witness**.

The exact cache lookup remains authoritative for warm-start compatibility. On
an exact-feature miss, the solver may additionally treat the contact as a
continuing loaded pair only when all of these conditions hold:

1. the existing `lower_bound` insertion entry or its predecessor has the same
   object-pair prefix;
2. both bodies carried at least `ceil(sleep_frames / 2)` quiet frames at the
   start of the solve.

At the current settings this is 15 of 30 quiet frames. The value is derived
from existing policy, not a new tuning knob.

The cache already stores only rows that carry normal/tangent load and satisfy
`supportsRestingPolicy`. Its key already stores the pair prefix above the exact
feature ID. After the existing binary search, at most two adjacent entries are
therefore sufficient to prove that a loaded row exists for the pair. This adds
constant neighbor work and no prefix walk.

Warm starting still requires `cachedIt->key == exactKey`. The pair witness only
suppresses renewed restitution during established quiet progress; it never
copies impulse from a different feature.

## Why The Additional Quiet Qualification Is Required

A global pair witness reproduces the rejected broad policy. The retained
authoritative traces contain many exact-only restitution decisions:

| Trace | Exact-only pair-continuity admissions | Solved normal impulse |
|---|---:|---:|
| four-brick | 15 | 319.904702 |
| wall-200 | 7,609 | 64,847.127018 |

Changing all of those decisions cannot preserve accepted bytes. Restricting the
witness to actual vertical support edges also fails: all 15 four-brick rows are
support-to-vertical transitions. Pair age is not a separator either; one of the
two wall rows after frame 1,200 belongs to a pair loaded for 188 consecutive
frames.

The final predicate uses only state available at `PrecomputeRows` time. The
query joins frame `F` contacts to frame `F-1` quiet counters, matching the
replayed sleep progress the next fixed step receives.

| Trace | All admissions | Half-quiet adjacent-pair rows | Selected solved normal impulse |
|---|---:|---:|---:|
| four-brick | 15 | **0** | **0** |
| wall-200 | 7,609 | **0** | **0** |
| dense pile | 1,015 | **8** | **67.425641** |

The dense-pile count is deliberately smaller than SR1's broad 66-row settling
census. SR3 targets the rows that can erase mature quiet progress, not every
feature change. The selected eight rows are sufficient to make the mechanism
falsifiable: if they do not restore the SR0 sleep oracle, SR3 must reject this
candidate rather than broaden it silently.

The eight rows are an offline upper bound: a wake path may reset a counter or
forget cache state before `PrecomputeRows`. SR3 must observe the live branch and
reject the candidate if the reachable subset cannot restore the oracle.

## Candidate Comparison

| Candidate | Per-row / retained cost | Lookup cost | Accepted-byte risk | Ruling |
|---|---|---|---|---|
| Global adjacent pair witness | 0 bytes | existing binary search plus at most two neighbor checks | Proven high: changes 15 four-brick and 7,609 wall decisions | Reject |
| Fixed pair-token hash beside exact cache | At least one 64-bit key per occupied pair plus occupancy/generation storage; a 2x table reaches hundreds of KiB at the scene ceiling and needs replay/wake/memory accounting | expected O(1) hash | Same broad semantic risk as global pair lifetime | Reject |
| Stabilize manifold feature selection | 0 retained bytes if purely algorithmic | no extra solver lookup | High: changes narrowphase identity and compatible warm starts across box/hull families; NM2 does not prove harsh rocking equivalence | Reject |
| Transfer a retiring feature's support patch | At least normal plus two contact arms (36 raw scalar bytes per cached row, before alignment), mirrored into replay; 196,608 ceiling rows exceed 7 MiB raw | exact lookup plus patch comparison | High: risks reusing incompatible impulse geometry and the Replay 8 MiB reserve cap | Reject |
| **Half-quiet adjacent pair witness** | **0 retained bytes**; one borrowed counter span and one normalized policy scalar | **existing binary search plus at most two neighbor checks and two counter reads** | **Predicate selects zero rows in both accepted traces** | **Choose** |

The chosen mechanism is a stage-local read of Sleep-owned values, not a second
sleep-state owner. `PhysicsSleepController` retains `m_sleepCounter`; PhysicsWorld
will lend its immutable span synchronously to the contact stage. Replay already
captures/restores that counter, so no new replay payload or growth privilege is
introduced. The changed `Solve` signature must refresh its qualitative wide-
signature ruling rather than hide the span inside a context bag.

## SR3 Implementation Boundary

SR3 may change only the following behavior:

- add the normalized sleep-frame count to
  `PersistentContactSolverStepPolicy`;
- lend `PhysicsSleepController::GetSleepCounters()` to the contact solve;
- extend `InspectPreviousObjectContact` so the existing insertion point reports
  a same-pair loaded witness by constant neighbor checks;
- suppress restitution on an exact miss only when the half-quiet qualification
  passes; and
- add focused tests for exact warm-start separation, no-contact reset, the
  half-quiet boundary, above-threshold motion, and mutual-gravity elasticity.

SR3 may not add cache rows, transfer a retiring impulse, change feature IDs,
scan a pair prefix, add a per-body field, or alter replay snapshot shape.

The existing four-brick and wall CSVs are predicted byte-exact because the new
branch is unreachable in both retained traces. This is a design prediction, not
closure evidence. SR3 must prove it against the approved hashes and stop without
a baseline transition if either moves.

## SkullScope Commands And Accounting

The source artifacts are the retained SR0/ES5 traces:

```text
TestOutput/contact_energy_es5/four_brick_final_guard.physicsdiag.ndjson
TestOutput/contact_energy_es5/wall200_final_guard.physicsdiag.ndjson
C:/Temp/SkullbonezCore-sr0-exact/TestOutput/dense_pile_sr0/pile_exact_6800.physicsdiag.ndjson
```

Every successful query used the following command shape with the exact trace,
statement, packet path, and limit named below:

```powershell
tools\physics_query.bat <TRACE> sql "<STATEMENT>" --pretty --limit <LIMIT> |
  Tee-Object -FilePath <PACKET>
```

The final ruling query was run once for each trace with limit 5:

```sql
WITH prior_cache AS (
  SELECT p.* FROM contacts p
  WHERE p.body_b >= 0
    AND (p.normal_impulse > 0 OR p.tangent_impulse > 0.00001)
    AND (ABS(p.normal_y) <= 0.25 OR EXISTS (
      SELECT 1 FROM support_edges se
      WHERE se.run_id = p.run_id AND se.frame = p.frame
        AND se.source = 'object_contact'
        AND MIN(se.supporter,se.supported) = p.body_a
        AND MAX(se.supporter,se.supported) = p.body_b))
), prior_pairs AS (
  SELECT run_id,frame,body_a,body_b FROM prior_cache
  GROUP BY run_id,frame,body_a,body_b
)
SELECT
  SUM(CASE WHEN pp.body_a IS NOT NULL AND e.contact_id IS NULL
                AND c.pre_solve_closing_speed > 2.0 AND c.separation_bias = 0.0
           THEN 1 ELSE 0 END) AS admissions,
  SUM(CASE WHEN pp.body_a IS NOT NULL AND e.contact_id IS NULL
                AND c.pre_solve_closing_speed > 2.0 AND c.separation_bias = 0.0
                AND (SELECT sleep_counter FROM bodies pa WHERE pa.run_id=c.run_id AND pa.frame=c.frame-1 AND pa.body_id=c.body_a) >= 15
                AND (SELECT sleep_counter FROM bodies pb WHERE pb.run_id=c.run_id AND pb.frame=c.frame-1 AND pb.body_id=c.body_b) >= 15
           THEN 1 ELSE 0 END) AS selected_half_quiet_rows,
  ROUND(SUM(CASE WHEN pp.body_a IS NOT NULL AND e.contact_id IS NULL
                      AND c.pre_solve_closing_speed > 2.0 AND c.separation_bias = 0.0
                      AND (SELECT sleep_counter FROM bodies pa WHERE pa.run_id=c.run_id AND pa.frame=c.frame-1 AND pa.body_id=c.body_a) >= 15
                      AND (SELECT sleep_counter FROM bodies pb WHERE pb.run_id=c.run_id AND pb.frame=c.frame-1 AND pb.body_id=c.body_b) >= 15
                 THEN c.normal_impulse ELSE 0 END),6) AS selected_normal_impulse
FROM contacts c
LEFT JOIN prior_cache e ON e.run_id=c.run_id AND e.frame=c.frame-1 AND e.contact_id=c.contact_id
LEFT JOIN prior_pairs pp ON pp.run_id=c.run_id AND pp.frame=c.frame-1
                        AND pp.body_a=c.body_a AND pp.body_b=c.body_b
WHERE c.body_b >= 0;
```

The other successful bounded statements reused the same cache-eligible
`prior_cache`/`prior_pairs` CTE and projected: broad lifetime counts; prior
support/horizontal class; post-1,200 counts, frames, and body/feature IDs; the
two wall pairs' recursive loaded age; their four body-state rows; exploratory
current-speed screening; either-endpoint and both-endpoint quiet-progress screens; the two
four-brick mutual-quiet rows; and the dense-pile 11/15/20/25 counter histogram.
These were exploratory discriminator queries; the final pre-state statement
above supersedes their current-frame speed approximations.

| Packet group | GPT-read chars / UTF-8 bytes | Retained file bytes |
|---|---:|---:|
| broad four/wall lifetime | 644; 647 | 1,290; 1,296 |
| four support classification | 724 | 1,450 |
| wall post-1,200 aggregate/frames/ids | 638; 573; 485 | 1,278; 1,148; 972 |
| wall pair ages and late body rows | 725; 1,234 | 1,452; 2,470 |
| current-speed screen | 529 | 1,060 |
| either-endpoint quiet screens, four/wall/pile | 540; 459; 554 | 1,082; 920; 1,110 |
| mutual-quiet screens, four/wall/pile | 477; 473; 490 | 956; 948; 982 |
| four mutual-quiet rows / pile thresholds | 847; 564 | 1,696; 1,130 |
| superseded speed-qualified and final counter-only, four/wall/pile | 532; 528; 550; 532; 528; 550 | 1,066; 1,058; 1,102; 1,066; 1,058; 1,102 |

All 23 successful packets were untruncated and total **13,823 characters /
UTF-8 bytes**. `Tee-Object` retained UTF-16LE packet files, explaining the
approximately doubled file sizes. No raw NDJSON, SQLite database, or CSV was
read into the model.

Seven exploratory statements timed out while attempting broad support-class,
joined-row, full streak, or materialized body joins. They exposed no result
packet and are not evidence. Their bounded replacements are accounted above.
The three speed-qualified final packets are also superseded: end-of-frame body
speed is not the exact post-force velocity seen by the next `PrecomputeRows`.
The chosen counter-only query avoids that timing approximation and selects the
same eight pile rows while remaining empty for both accepted traces.

## Validation

SR2 changes documentation only. `git diff --check` is the required phase check;
repository validation begins with the SR3 source implementation.
