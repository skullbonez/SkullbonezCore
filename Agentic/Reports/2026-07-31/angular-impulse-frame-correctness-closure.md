# Angular Impulse Frame Correctness — Closure

Date: 2026-08-01
Plan: `Agentic/Plans/TODO/angular-impulse-frame-correctness.md`
Phase: AI4 complete
Branch: `nightrunner-1st-AUG-26`
Status: ACCEPTED AND COMPLETE

## Owner Decision

The owner accepted AI4's zero-delta proof on 2026-08-01 and authorized the
unchanged-baseline closure rerun. That rerun is complete. Angular Impulse Frame
Correctness is 5/5, the active/future ledger is 16/21 (76%), and Vector Frame
Contract Closure VF0 is now the binding next phase.

The plan grants no baseline-regeneration authority. No committed baseline,
scene, schema, project, dependency rule, Rendering file, or configuration file
changed in AI1-AI3 or in this owner packet.

## Correctness Outcome

One Physics-owned conversion now supplies pending gameplay, world-force, and
contact angular responses:

1. rotate the world torque into the body frame with `R^T`;
2. apply the caller's body-diagonal inertia division or multiplication; and
3. rotate the result back to the world frame with `R`.

The pending path constructs torque from a world-space center-relative offset
and world impulse. `PhysicsEngine`, retained body state, startup probes, the
launcher, and tests now use `worldApplicationOffset` or
`pendingImpulseWorldOffset`; no compatibility alias retains the false local-
frame contract. The launcher supplies `hitPoint - bodyPosition`, matching the
contract.

The rotated anisotropic-box characterization is now green. The rotated
isotropic-sphere case compares every component exactly with the former direct
division path, protecting the artifact-relevant sphere behavior from rotation
rounding drift.

## AI0 Prediction Versus Final Artifacts

| Artifact family | AI0 prediction | Final evidence | Delta |
|---|---|---|---|
| Focused pending/contact behavior | Rotated anisotropic mismatch becomes equality | 2 focused cases / 15 assertions pass | Intended test-only expectation change |
| Unit suite | No unrelated movement | 453/453 cases and 2,422,921/2,422,921 assertions pass | Zero failures |
| Core Physics | No committed byte movement | Two 44,401-line varied runs match the committed baseline byte-for-byte | Zero bytes |
| Deep Physics | No CSV, signature, or query movement | Varied, bullet wall/object/terrain, shooting volley, three-body chaos, known-issue signatures, and SkullScope query output pass exact | Zero bytes |
| `at_rest` | No extra frame fixture; complete CSV already owns the proof | 7,649,427 bytes / 54,001 lines / SHA-256 `0a46651405e181428aabb5cc5081bd0d90ac6ca73e3a0c2786353f00cf55a984` | Zero bytes |
| Performance and allocation | No regression or policy movement | Allocation guard, selected-ball structure, DX12 budgets/comparison, Physics benchmark budgets/comparison, and scale matrix pass | No regression |
| DX12 screenshots | No committed image movement | Full gate reports all DX12 screenshots match committed baselines | Zero bytes |
| Replay visual and causal proof | No visual, topology, semantic, or artifact movement | One 4,200-frame generation; four screenshots; 2,401 visual ticks; 2,401 causal ticks; every positive and mutation-negative offline comparison passes under the provenance normalization below | Zero compared values |
| Repository surface | No unrelated scene/config/schema/render change | `git diff --name-only b314480e..HEAD` contains only owning plans/reports/session state, Physics implementation, Runtime producers, and focused tests | Zero unrelated files |

AI1's mutual-gravity reduction and AI2's frame correction therefore contribute
zero committed artifact bytes, exactly matching AI0's prediction. The only
behavioral delta is the focused rotated-anisotropic expectation that AI0 first
recorded as a failing characterization.

## Invariant And Failure Proof

- The complete CPU and full composition gates report no body loss, non-finite
  state, energy explosion, test failure, invariant failure, DX12 validation
  error, or physics mismatch.
- The allocation guard reports no steady-gameplay allocation or reserve-policy
  violation. No Replay reserve registration, cap, phase gate, growth counter,
  or post-gameplay growth privilege changed.
- `PhysicsBodyRecord` gains no field; the retained application-offset slot is
  renamed in place. No hot-store stream changes.
- No downward Replay, Prediction, or Planning dependency appears.
- AI3 found no remaining incorrect direct division of a world torque by the
  body-principal inertia diagonal. Its separate findings are registered, with
  no baseline authority, in
  `Agentic/Plans/TODO/vector-frame-contract-closure.md`.

## Replay Visual Provenance Caveat

The committed replay-visual manifest records the scene's historical CRLF hash
`5e066982ea3830c06631e03d48c0a57600f93779f4e741da9ad37c283e899086`,
while the tracked scene has used LF and hashes to
`3b970ccebc040e6350ec92185b9d5b6ea830de7fbcd06aced793a6b59c66ea10`.
This known defect is already documented in
`Agentic/Reports/2026-07-31/solver-diagnostic-hot-path-cost-closure.md`.

AI4 launched exactly one authoritative Automation engine and produced exactly
one generation. The wrapper timed out while that process was still running;
the existing process was awaited to completion and was not relaunched. Its
report is successful: 4,200 frames, four requested screenshots, 2,401 visual
ticks, 2,401 causal ticks, one reveal generation, one presented cascade, and a
36,456,001-byte saved replay artifact.

The committed checker then stopped only at the known scene-hash provenance
field. Ignored diagnostic copies changed exactly:

- the visual manifest's CRLF scene hash to the tracked LF scene hash; and
- the causal manifest's dependent visual-manifest hash.

Against those copies, the positive visual/causal/replay checks and every
visual-fidelity, incomplete-horizon, causal activation/topology/segment,
semantic-packet, artifact-byte, prediction-artifact, and ten determinism
mutation controls passed. No committed manifest, baseline, scene, or screenshot
was edited or refreshed. Owner acceptance of this packet acknowledges this
pre-existing provenance limitation; it does not authorize changing a golden.

## Validation Record

| Command or proof | Result |
|---|---|
| `tools\validate_build.bat Profile` | PASS; zero warnings/errors |
| Focused Profile pending-impulse selection | PASS; 2 cases / 15 assertions |
| `tools\validate_tests.bat` | PASS; 453 cases / 2,422,921 assertions |
| `tools\validate_physics.bat` | PASS; two 44,401-line varied runs byte-exact |
| `tools\validate_physics_deep.bat` | PASS; all CSV, signature, and query artifacts exact |
| `tools\validate_perf.bat` | PASS on clean rerun in 65.8 s; allocation, interaction, DX12, Physics, and scale proofs pass |
| Replay visual single-generation proof plus offline controls | PASS with the documented LF provenance normalization |
| `tools\validate_full.bat` | PASS in 566.1 s after current Debug/Profile/Automation object refresh |
| Touched-source comment audit | 9/9 checked; zero deferred |

The first performance invocation ended during the selected-ball Automation
step even though its report recorded `ok: true`; the clean rerun passed every
lane and is the final evidence. During final composition, the first preflight
found a branch-owned compact-call formatting defect in `RuntimeTools.cpp`; the
repository's two-stage formatter repaired whitespace only. The next preflight
correctly rejected stale Profile/Automation objects created before that repair.
Serial refreshes passed with zero warnings/errors, and the final full gate then
passed end to end. An attempted parallel refresh produced only shared-
intermediate file locks and no accepted build result.

## Strict Ownership Inventories

| Inventory | Current result |
|---|---|
| Build configuration | 1,649 compile rows; 314 source files; 62 shared; 124 divergent pairs all ruled; zero dropped inheritance or blocking diagnostics |
| Symbol reachability | 79 rows: 48 no-reference, 10 own-TU-only, 21 test-only; all ruled; zero blocking diagnostics |
| Authority-free aggregates | 1,183 candidates; 18 invariant owners; 85 gated review rows all ruled; zero unruled, ambiguous, or pre-existing unreviewed rows |
| Extraction scars | One unrelated ruled `WorkerPool::indexFn` parameter alias; zero unruled |
| Wide signatures | Every operation at or above 12 parameters has a current ruling |
| Function complexity | 6,238 functions; 40 triggers all ruled; zero blocking diagnostics |
| Glossary | 575 files; 964 definitions; 964 unique; zero multi-file terms, drift, rulings, or issues |

The independent reviewer found no aggregate-owner, capability-slice,
extraction-scar, rename-evasion, false-comment, dependency, growth-privilege,
or test-coverage blocker. No changed function crosses the wide-signature or
complexity trigger, and no new unrooted out-of-line function appears.

## Independent Review

Verdict: **READY to present AI4 for explicit owner sign-off; no blocking
findings and no material evidence missing.** The reviewer independently checked
the source math, world-offset contract, launcher construction, ownership rules,
all seven strict inventories, Replay/growth boundaries, changed-file surface,
and validation record.

One non-blocking test-isolation caveat remains: after AI2, the rotated
anisotropic pending and contact paths both call the shared conversion helper.
The comparison catches path divergence but cannot catch every arbitrary error
common to the helper. AI0 established the formerly independent contact result
as the oracle before sharing; the helper's source explicitly implements
`R^T -> body diagonal -> R`; and the exact isotropic test protects unchanged
artifact behavior. A future analytic anisotropic assertion would improve
isolation but is not required to present this owner gate.

Rubber-duck accounting:

| Plan | Duck run | Reviewer/thread | Reason | Prompt/context chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|---|---|---|---|---:|---:|---|---|---|---|
| `Agentic/Plans/TODO/angular-impulse-frame-correctness.md` | `angular-impulse-frame-correctness-duck-01` | `/root/angular_impulse_duck` | End-of-plan AI4 owner packet | 3,963 | 5,540 | n/a | ~2m 30s | No blockers; READY to present | Disclose two non-blocking caveats; no code follow-up |

## Owner Gate

Owner acceptance and every matching unchanged-baseline closure proof are
complete. The plan remains temporarily under `Plans/TODO/` only because MASTER
retains the five Gate Blind Spot plans until the aggregate campaign closes;
Vector Frame Contract Closure may now begin.

## Post-Acceptance Closure Rerun

| Command or proof | Final result |
|---|---|
| `tools\validate_tests.bat` | PASS in 48.6 s; 453/453 cases and 2,422,921/2,422,921 assertions |
| `tools\validate_physics.bat` | PASS in 25.1 s; two 44,401-line varied runs byte-exact |
| `tools\validate_physics_deep.bat` | PASS in 112.0 s; every CSV, known signature, and SkullScope query artifact exact |
| Existing complete `at_rest` artifact | 7,649,427 bytes / 54,001 lines / SHA-256 `0a46651405e181428aabb5cc5081bd0d90ac6ca73e3a0c2786353f00cf55a984` |
| `tools\validate_perf.bat` | Absolute budgets, allocation, and interaction passed on both runs; the first comparison encountered host timing noise, and the 65.4-second idle-host rerun passed both DX12 and Physics regression comparisons |
| Accepted Replay offline proof | PASS in 32.7 s against the existing single generation; positive fidelity plus every visual, causal, semantic, artifact, prediction, and ten determinism mutation controls passed with the accepted LF provenance normalization |
| `tools\validate_full.bat` | PASS in 494.8 s; 453 cases / 2,422,921 assertions, Automation smoke, matching DX12 screenshots, and byte-exact 44,401-line Physics |

The closure rerun changed no tracked file. It did not start a second Replay
engine process, regenerate prediction, or modify a committed baseline.
