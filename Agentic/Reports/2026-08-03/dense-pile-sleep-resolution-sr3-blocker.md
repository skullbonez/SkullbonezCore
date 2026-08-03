# Dense Pile Sleep Resolution — SR3 Blocker And Owner Decision Packet

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Status: SR3 rejected; plan blocked at 3/5 pending owner direction
Impact area: Physics restitution lifetime and dense-pile sleep

## Outcome

The half-quiet adjacent-pair witness selected by SR2 is rejected. Its complete
20,000-frame dense-pile run does not reach permanent all-sleep and does not meet
or beat the pair-prefix evidence required by the plan. The implementation,
tests, and ownership-ruling edits from the failed attempt were removed; no
production source, baseline, or ruling changed.

The failure satisfies SR2's explicit falsifier. Broadening the predicate after
the run would replace the reviewed design with a new restitution policy, so SR3
stops rather than hiding that decision inside implementation.

## Candidate Result

The candidate used the existing exact `lower_bound` for warm-start identity and
checked only its insertion entry or predecessor for a loaded same-pair row.
Restitution across an exact miss was suppressed only when both live Sleep
counters had reached `ceil(physics_sleep_frames / 2)`, 15 at the tracked
setting. No impulse transferred between features and no retained or replay
state was added.

`tools\validate_tests.bat` passed after the focused boundary, no-contact, and
elastic controls were added. A fresh Debug build then produced:

| Metric | Required pair-prefix point | Current exact point | Rejected candidate |
|---|---:|---:|---:|
| Permanent all-sleep frame | 8,513 | >20,000 | >20,000 |
| Last transition through 20,000 | 8,513 | 19,991 | 19,999 |
| Final sleeping at 20,000 | 330/330 | 327/330 | 327/330 |
| Never sleeping through 20,000 | 0 | 1 | 1 |
| Wake oscillations through 20,000 | 4,227 | 5,168 | 5,441 |
| Maximum wakes for one body | 182 | 475 | 492 |
| Final sleeping at 6,800 | 309/330 | 320/330 | 316/330 |
| Wake oscillations through 6,800 | 3,687 | 4,287 | 4,462 |
| Tail kinetic mean, frames 6,500–6,799 | 21.912017 | 19.713287 | 20.376720 |
| Tail kinetic peak, frames 6,500–6,799 | 77.229217 | 65.474819 | 67.508151 |

Candidate artifacts are ignored diagnostic evidence under
`TestOutput/dense_pile_sr3/`. The complete diagnostic trace is 2,665,434,875
bytes, its SQLite cache is 1,309,683,712 bytes, and the 20,000-frame CSV is
986,160,332 bytes. The structured measurement is
`TestOutput/dense_pile_sr3/pile_candidate_measurement.json`.

## Why The Predicate Failed

Sleep counters are not cleared indiscriminately at step start. They are,
however, cleared by force/explicit wake paths and by narrowphase when it wakes a
sleeper before `PersistentContactSolver::PrecomputeRows`; ordinary counter
increment/reset happens only later in `RunIslandStage`.

A bounded reconstruction of candidate-trace rows with closing speed above the
restitution threshold found 570 exact misses with a loaded prior pair. Among
awake endpoints, 538 had a mutual prior-counter minimum of zero, 17 were 1–4,
4 were 5–10, 3 were 11–14, and only 7 were 15–29. Those seven selected rows
occur at frames 448, 625, 628, 636, 712, 1,048, and 3,414 and carry 59.407
solved normal impulse. No qualifying row appears later.

The gate is therefore self-starving: renewed restitution prevents the quiet
progress required to suppress renewed restitution. It also cannot touch the
first causal divergence at frame 24 because both involved counters are zero.

## Rejected Post-Run Broadening

One exploratory discriminator selected the frame-24 pile row and zero accepted
four-brick/wall rows: an adjacent witness carrying tangent but no normal load,
a four-point contact, `0.25 < abs(normal.y) < 0.50`, and closing speed between
2.0 and 3.0. Independent review rejected this as scene fingerprinting. The 3.0
ceiling is the sole separator from seven accepted wall rows, whose minimum
closing speed is 3.506; orientation and point count encode this particular box
pose rather than a general lifetime invariant.

No source change was made from that exploration.

A cleaner feature-topology discriminator also fails accepted exactness. The
causal feature IDs decode to the same physical box-face pair with reference
ownership swapped. Canonical patch continuity selects 143/148 pile box-face
admissions, but also all 15 four-brick and all 2,420 wall admissions in that
class. Requiring the ownership flip still selects 3 four-brick and 1,445 wall
rows; adding tangent-only history leaves 41 wall rows. Existing cache/current-
row values therefore provide no policy-clean stateless separator that repairs
frame 24 while preserving both accepted traces byte-for-byte.

## Costed Owner Options

1. **Authorize a policy-shaped hysteresis probe (recommended if repair remains
   desired).** Reopen SR2 around a friction-only adjacent-pair witness plus an
   explicitly owner-ratified low-speed band above the restitution threshold.
   Retained memory and replay payload remain zero; lookup cost is the existing
   binary search plus at most two neighbor checks. This introduces a new tuned
   restitution policy and is not yet proven on the dense oracle or broader
   scene families. Authorization must name the tuning ownership and whether an
   otherwise bounded golden transition may be considered.
2. **Authorize retained pair-lifetime state.** A fixed pair-token hash gives
   expected O(1) lookup but costs at least one 64-bit key per occupied pair plus
   occupancy/generation storage, reaching hundreds of KiB at the scene ceiling.
   It requires replay, wake, growth, memory, and determinism accounting. Broad
   pair lifetime is known to alter 15 four-brick and 7,609 wall restitution
   decisions, so this option also requires explicit baseline-transition scope.
3. **Authorize manifold/patch identity work.** Stabilizing harsh rocking
   features risks changing identity and compatible warm starts across shape
   families. Transferring a retiring patch needs roughly 36 raw scalar bytes per
   cached row before alignment, more than 7 MiB at the 196,608-row ceiling, and
   must be mirrored into Replay. Either route requires a new geometry/replay
   plan and explicit transition authority.
4. **Accept current exact-feature behavior and park this plan.** This makes no
   code or baseline change but leaves `box_pile_throw_300` oscillatory past
   20,000 frames and intentionally abandons SR3/SR4 acceptance.

## Blocker Record

- **Owner:** Physics contact and sleep owner.
- **Cause:** the reviewed zero-retained-byte candidate failed its explicit
  dense-pile oracle; the remaining clean alternatives introduce a new tuned
  restitution rule, retained/replay state, or geometry identity transition.
- **Evidence:** this report plus SR0–SR2 reports and the ignored SR3 artifacts.
- **Unchanged verified count:** 3/5. SR3 and SR4 remain incomplete.
- **Affected dependents:** only SR4. Comment Vocabulary Audit and Source
  Modernization Sweep are independent and remain runnable.
- **Exact unblock condition:** the owner selects option 1, 2, or 3 and states
  the permitted tuning/baseline-transition scope, or selects option 4 and parks
  the plan under inventory rule 9.

## Independent Review

The read-only reviewer confirmed wake/counter order, reconstructed live branch
reach, agreed that the half-quiet candidate must be rejected under SR2's
falsifier, and rejected the frame-24 filter as overfit. Verdict: **BLOCKED —
record the owner decision packet and continue independent queue work.**
