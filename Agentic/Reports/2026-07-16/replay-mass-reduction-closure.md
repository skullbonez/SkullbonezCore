# Replay Mass-Reduction Campaign Closure

Date: 2026-07-16
Branch: `nightrunner-15th-july`
Plan result: 9/9 ledger tasks complete plus the uncounted R8 aggregate closure
gate
Status: Closed. No active local campaign step remains.

## Result

Replay diagnostics now link only in their intended Debug/Automation
configurations, the product build has one fewer replay translation unit and
2,354 fewer compiled implementation lines, the two ruled mechanical
duplications have one owner, and only owner-ratified dead paths were deleted.
Four oversized/near-threshold TUs remain under explicit cohesion rulings rather
than cosmetic splits or new migration interfaces.

The final tracked directory is 45 files / 34,093 lines versus R0's 42 / 33,783.
That physical +310-line result includes the configuration-specific diagnostic
files and the explicit artifact canonicalization/verifier contract. The actual
product-compiled implementation falls from 17 TUs / 26,875 lines to 16 /
24,521. Binary map attribution is also reported honestly: Release changes
498,264 → 498,628 bytes (+364), and Profile changes 456,044 → 458,032
(+1,988). The campaign reduced product compilation surface, not measured
linked bytes.

Detailed census, object rows, map hashes, Automation totals, and independent
review evidence are in
`Agentic/Reports/2026-07-16/replay-mass-reduction-r7-final-census-review.md`.

## Artifact Determinism Closure

R3 formally found schedule-sensitive topology/store/reserve bookkeeping in
whole artifact bytes. The owner ruled that fix into the campaign as R4b. R4b
canonicalizes only matching-token and non-presenting bookkeeping at the writer,
keeps readers/layout/version support unchanged, and retains a content-sensitive
semantic digest independently verified by the Python oracle and mutation tests.

The two original mismatching artifacts (46,104,063 and 46,104,064 bytes), the
repaired encoder, the final R7 gate, and the final R8 gate all resolve to the
same 36,564,003-byte artifact at SHA-256:

`F916DED3AB5CE52EB0A2AA99FBAD846512F9B4EFEE6D49CC6DAD1F825ABC0B24`

Direct transformed-original versus encoder comparisons are byte exact. The
original mismatch would therefore be byte exact with the final change; this is
not a projection-only or silently weakened oracle.

## Independent Review

The whole-campaign reviewer initially blocked closure on a fixed semantic
sentinel and stale governance wording. R4b reopened, replaced the sentinel with
a content-sensitive canonical hash, added independent checker recomputation and
exact-vector/mutation coverage, and reconciled the ledger. The same reviewer
then confirmed both blockers resolved and no new material blocker. Final review
has zero unresolved credible findings across authority, facades/context bags,
configuration boundaries, deduplication honesty, deletion rulings, cohesion,
artifact semantics, and golden/baseline discipline.

## R8 Aggregate Validation

All commands ran from final source at `fd2dea663` through the headless Codex
execution channel with complete output mirrored under
`TestOutput/agent_logs/`.

| Gate | Time | Result |
|---|---:|---|
| `tools\validate_tests.bat` | 2.31 s | 203/203 cases, 12,600/12,600 assertions, zero failures |
| `tools\validate_full.bat` | 113.24 s | CPU umbrella; zero-warning Profile/Automation/Debug builds; replay smoke; DX12 zero InfoQueue errors and screenshot matches; standalone plus 44,401-line byte-exact physics lanes |
| allocation self-test | 0.14 s | Synthetic policy cases passed |
| allocation repo scan | 8.32 s | 370 files; 42 direct-heap, 139 dynamic-member, and 661 growth findings all governed; zero allowlist errors |
| `tools\validate_perf.bat` | 69.26 s | DX12 and physics-bench comparisons both report no regressions; Profile/Debug ready |
| `tools\validate_replay_visual_fidelity.bat` | 428.08 s | Exactly one engine/generation; 2,401 ticks, 200 moved, 187 toppled, 199 causal nodes, every negative/determinism control passed |

The final replay artifact is 36,564,003 bytes at the exact SHA above. Its
73,428-byte transcript SHA is
`3A04D635F61A608EC6D48CF0569B7B400572339417AAFD6EB9C2AE4B516584D2`.

No golden manifest, visual baseline, physics baseline, screenshot baseline,
scene, authored schema, engine config, or provenance input changed anywhere in
R8. The R8 game processes were launched by validation scripts through a
headless execution channel, so no interactive game window was expected.

## Closure

The replay mass-reduction plan is deleted in this closure commit under MASTER
inventory rule 4. Git history and the R0–R8 reports are the evidence archive.
The externally administered validation-gate V3 lane remains blocked and is
excluded from the local active/future portfolio.
