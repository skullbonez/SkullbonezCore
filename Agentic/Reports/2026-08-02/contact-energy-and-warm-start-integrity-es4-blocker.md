# Contact Energy And Warm-Start Integrity ES4 Blocker

Date: 2026-08-02

Branch: `nightrunner-1st-AUG-26`

Closure: `Agentic/Reports/2026-08-02/contact-energy-and-warm-start-integrity-closure.md`

Owner: Physics contact solver

Outcome: BLOCKED at 4/7; ES4 remains unchecked

## Decision

Stop the ES4 identity/validity path. The exact loaded box-face identity defect
is reproducible and locally repairable, but correcting it does not satisfy the
binding 32/64 tower acceptance. The remaining scale failure persists while
feature identity and warm-start use are stable, so further cache, SAT, row, or
geometry tuning would no longer be attributed ES4 work.

No experimental source, test, checker, scene, configuration, or baseline change
is retained. The ignored diagnostic artifacts remain local evidence only.

## Bounded Evidence

The best canonical-identity/cache-anchor experiment established both the local
improvement and the scale boundary:

| Workload | Result |
|---|---|
| Tower 8 | 8/8 asleep; zero post-frame-300 launch reversals; zero tail identity churn; zero peak mechanical gain; maximum penetration 0.237 |
| Tower 32 | Completed only after the adjacent box-AABB broadphase experiment; collapsed onto terrain; 374 reversals; one tail launch; maximum penetration 7.501; maximum speed 324.857; peak mechanical gain 15,195.14 |
| Tower 64 | Completed only after the adjacent box-AABB broadphase experiment; 528 reversals; maximum penetration 6.941; maximum speed 827.727; 58 asleep and 60 asleep-or-supported; one body left terrain and fell far below the scene |

For the documented loaded body 6/7 role flip, the ES3 trace moves from `3648x`
to `4320x` feature families at frames 36 and 40 with zero warm starts. The
canonical-corner experiment instead retains the four-feature set
`{36496, 36505, 36532, 36541}` through frames 14-41 with two to four warm-started
rows. Its carried normal impulse rises from approximately 146 at frame 14 to
569 at frame 30 and continues fluctuating while the tower destabilizes. This
falsifies feature-identity discontinuity as the dominant remaining scale cause.

The bounded experimental matrix also covered full four-row retention,
three-row retention, reverse and alternating solve order, canonical global row
order, pair-key diagonal alternation, the historical 25% SAT challenger margin,
quantized spatial feature keys, synthetic centers, manifold position division,
loaded-contact Baumgarte suppression, coupled friction, angular split position
correction, and object support seeds. None reached the required floor; the
alternatives generally failed earlier or worsened launch, penetration, energy,
or support.

The swept box AABB refinement is an adjacent broadphase robustness idea. It
removes bounding-sphere false positives so the 32/64 experiments can run beyond
the previous candidate-capacity fatal, but it neither repairs contact energy nor
counts as ES4 progress. It was removed with the other partial implementation;
any future adoption needs its own conservative-coverage tests and mapped Physics
and performance validation.

## Independent Review

The successful read-only review returned:

> STOP the ES4 identity/validity path; stability closure is BLOCKED.

It found that stable IDs and warm starts during the collapse contradict the
remaining identity-discontinuity theory. It rejected another identity/validity
experiment unless a new trace shows a concrete cache discontinuity immediately
before an energy spike. It classified the swept AABB as adjacent broadphase
work and recommended a separately authorized solver-energy/convergence
investigation focused on impulse work or residual growth under the fixed
12-iteration PGS policy.

| Plan | Duck run | Reviewer/thread | Reason | Prompt chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|---|---|---|---|---:|---:|---|---|---|---|
| Contact Energy ES4 | contact-es4-duck-01 | `/root/contact_es4_duck_01` | Repeated-failure critique | 3,033 | 0 | n/a | n/a | Infrastructure-stalled; interrupted | Reissued with bounded context |
| Contact Energy ES4 | contact-es4-duck-02 | `/root/contact_es4_duck_02` | Bounded fresh critique | 1,531 | 0 | n/a | n/a | Infrastructure-stalled; interrupted | Reissued to an already initialized reviewer |
| Contact Energy ES4 | contact-es4-duck-03 | `/root/look_lab_duck_01` | Immediate bounded verdict | 914 | 1,394 | n/a | n/a | Blocking: stop ES4 identity path | Remove partial source and record blocker |

## SkullScope Accounting

No new trace was generated in this blocker-record slice. The bounded reads used
the existing ignored artifacts:

| Artifact | NDJSON bytes | SQLite bytes |
|---|---:|---:|
| `tower64_anchor_aabb.physicsdiag` | 212,317,276 | 102,772,736 |
| `tower32_identity_anchor_aabb.physicsdiag` | 112,037,173 | 54,439,936 |

The model did not ingest either raw trace. Three direct read-only SQLite query
packets were used: the `contacts`/`solver_stats` schema; body 6/7 frame, normal,
feature, impulse, and warm-start rows from `tower64_cached_anchor`; and the same
bounded body-pair comparison across `tower64_es3_locked`,
`tower64_canonical_face`, and `tower64_corner_identity`. Their outputs totaled
5,796 characters. No `tools\physics_query.bat` command was invoked in this
continuation.

## Blocker Contract

- Owner: Physics contact solver.
- Cause: the remaining required scale failure is not attributable to an ES4
  identity/cache discontinuity, and the plan forbids expanding ES4 into a
  broader solver rewrite or ungrounded tuning package.
- Evidence: stable four-feature identity and warm starts during collapse;
  failed 32/64 metrics; bounded rejected-experiment matrix; independent review.
- Exact unblock condition: owner authorization for a separately bounded stack
  load-propagation/convergence phase beyond ES4, or an explicit revision of the
  binding 32/64 acceptance target.
- Recommended unblock: authorize the bounded convergence phase while preserving
  the 32/64 target and all existing no-retune/no-baseline constraints.
- Affected dependents: ES5 scale/visible proof and ES6 engineering/decision
  packet.
- Verified progress: unchanged at Contact Energy 4/7 and portfolio 11/14 (78%).
- Repository state: experimental source removed; tracked baselines untouched;
  no independent dependency-safe plan remains.

## Validation

Documentation-only blocker record. No repository validation is required.
`git diff --check` is the required bounded hygiene inspection before commit.
