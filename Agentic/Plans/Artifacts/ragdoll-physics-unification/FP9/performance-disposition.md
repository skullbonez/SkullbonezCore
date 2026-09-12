# FP9 performance and stability decision

Decision: retain predictive contacts for articulated bodies. Required full-plan,
performance and replay gates pass. The tested fast-limb collision benefit
justifies the measured extra Physics work. Reliable pile sleep is not part of
the demonstrated benefit and remains the owner's next investigation.
This is the reversible engineering decision under the orchestrator autonomy rule.

## Evidence informing the decision

The same-executable A/B/A/B measurements isolate speculative contacts from the
completed joint solver. They use four workers, 120 Hz, fixed seed 12345 and 960
measured ticks after 240 warmup ticks. A/A and B/B recordings are exact, all
allocation guards pass and all twelve processes exit normally. Timings subtract
the nested DiagnosticsDump marker; the original reports retain all markers.

| Workload | Added average Physics cost | Relative increase | Larger on-run p99 | Largest on-run tick |
|---|---:|---:|---:|---:|
| Sleep island | 0.2511 ms | 28.69% | 1.3822 ms | 1.4592 ms |
| Ragdolls and boxes | 0.3280 ms | 35.41% | 1.5693 ms | 1.7804 ms |
| Floating ragdolls | 0.0487 ms | 13.73% | 0.5215 ms | 0.7949 ms |

The p99 column is the larger of the two on-run p99 values, not a pooled
percentile. These are local instrumented Automation measurements of the archived
b9df producer. They neither establish shipping performance nor absorb FP4's
separately accepted Discrete improvement into a net number.

## Memory cost

The same-executable selector retains the same reserved solver storage in both
modes. All four runs of each workload report zero Physics allocations and zero
gameplay allocation violations. Scene-load allocated bytes and high-water bytes
are identical across A/B/A/B for each workload:

| Workload | Scene-load allocated bytes, either mode | Scene-load high-water bytes, either mode | Replay high-water bytes, off / on |
|---|---:|---:|---:|
| Sleep island | 44,135,745 | 4,552,341 | 81,533,944 / 74,355,168 |
| Ragdolls and boxes | 44,652,455 | 5,024,075 | 80,132,722 / 77,084,362 |
| Floating ragdolls | 43,129,848 | 3,593,304 | 61,872,500 / 61,678,964 |

These are allocation-tracker phase measurements, not process working-set or
peak stack measurements. Replay storage varies with the resulting recorded
contacts; its lower on-run values are not a general memory-saving claim.
The raw reports also retain capture and diagnostic allocation totals.

Relative to the pre-FP8 source, the new articulation/path list retains one
`uint8_t` per reserved body row plus its list metadata. The motion-eligibility
stage reserves it at scene load and includes its capacity in memory reporting.
It is allocated in both selector modes, so an on/off allocation delta cannot
measure that added persistent storage. The bounded convex-distance and contact
work uses stack storage; no per-tick heap growth is authorized.

## Correctness benefit

Focused fixed/dynamic-wall controls prove that predictive contacts prevent the
tested fast translating/rotating limbs from crossing thin walls. The native
200-brick reveal provides another concrete benefit: current first striker/head
contact brakes across a 0.596154 m gap at prediction tick 14, without friction or
warm start, while the preserved old producer first reports contact at tick 15
with 0.816796 m penetration. The source prefixes match their full reveal runs
exactly. The governed replay transition retains this identity-bound evidence.

## Stability limit

Both short land workloads retain more residual motion with speculative contacts
enabled. In the separate 100-second pile observation, both off and on finish
with 10/46 bodies asleep; neither ever sleeps the whole pile. On reaches 43
asleep at its peak versus 32 off and has slightly lower final-second motion,
but both repeatedly wake bodies. This is insufficient evidence for stable pile
settling and is not a reason to claim that the FP8 collision change fixes sleep.

The accepted behavior must preserve the genuine fast-impact benefit while
reporting these limits. Any later sleep/jitter repair must prove its own effect
on contact/joint motion and wake causes, without disabling the required collision
protection or refreshing a baseline merely to make the result look quieter.

## Terminal acceptance evidence

Final required validation passes on 2026-09-11. Full05 exits 0 in 1317.204 s:
unchanged Physics CSV, 134-source / 1188-context source design, all six CPU
lanes, 1,027 Profile tests / 3,483,587 assertions, Automation in 438.419 s,
and DX12 in 13.667 s against the accepted screenshots. Exclusive perf07 exits
0 in 102.502 s on rebuilt Profile 2386e9e3, including relative and absolute
budgets, native allocation checks and structural checks. Mapped replay passes
in 370.488 s on Automation 6dbb35b9. The final 4ec3be56 relink changes only COFF
and debug timestamps plus CodeView PDB age; every other executable byte matches.
automation-relink-equivalence.json records that proof. terminal-validation.json
binds the final logs, producers and performance artifacts.

The optional frame-spike diagnostic exits 1 because its recorded
predictionFullHorizonComplete assertion is false. The full script explicitly
classifies this diagnostic as informational; it produced no usable spike
measurement. This failure is retained and is not reported as a diagnostic pass.
