# FP9 validation evidence

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

The following sections preserve earlier checkpoints and failures; the terminal
result above supersedes their pending status.

Full attempt 03 (`TestOutput/fp9-terminal-full-03.log`) exits 1 after 919.531 s.
All preflight and CPU lanes pass, including 1,026 Profile cases / 3,484,266
assertions and Debug coverage. Runtime validation finds stale Skarness/UI
expectations and a shader-tool path permission failure. The supported Visual
Studio clang-format override resolves shader freshness. An isolated default
layout removes personal Editor preferences from DX12 captures. The resulting
comparison `20260910T124849Z` has zero DX12 errors but fails UI chrome pixels;
outside the baseline commit's UI regions, Water and Three Body match exactly
and Solver has seven differing pixels of at most one channel value. The
diagnostic does not waive or pass the screenshot gate, and no DX12 golden changed.

The updated command catalog passes its native route test. The new position-gate
observation copies the exact object/frame/center values used by drawing, then
consumes and clears them after rendering. Causal-playback run 12 passes the
identity-bound marker, held controls, camera, retained geometry and Evidence
checks. Its selected marker has 88 cyan pixels for ID 1 at frame 100. Run 13 passes in 17.594 s, including the added prediction-off clearing assertion.
Selected and inspector screenshots were inspected. Automation attempt 09 passes
the complete suite in 448.954 s; that producer precedes the later frame-boundary
reset described in `position-gate-review.md`. Automation attempt 08 builds
Profile and Automation, then exits 1 after 63.562 s at the loading screenshot:
clock activation preserved an unfinished fade. The test now advances the clock
after activation, preserving the existing pixel thresholds. Runtime source
design is clean after extracting a RunFrame helper; dependency checks pass.
The old producers and source are retained in
`TestOutput/fp9-before-position-gate-observation/`. The bounded independent review found a skipped-publication lifetime gap, now
repaired by clearing at App frame start. Its follow-up review is clean and the
focused Profile tests pass 4 cases / 45 assertions. Source design passes five
contexts; Profile build 05 passes with zero warnings/errors.

Performance attempt 01 stops before timing because its generator emits scene
version 3 while the committed sleepy fixture uses version 5 from FP6. All other
values match. Updating that version makes `--check` pass without rewriting the
fixture. Exclusive attempt 02 exits 9 after 37.469 s: allocation guard and
selected-ball structural checks pass, but the recording never opens Evidence.
The repository fixture now routes through Editor, Causes, Evidence and Raw at
a pinned window size, preserving its contact/pipeline/frame assertions. Focused
`TestOutput/fp9-causal-perf-route-05` passes with maximum phase 0.0384 ms,
panel/overlay ratio 0.819, 231552 bytes fixed storage and zero steady allocations.
The old recording and raw output remain in
`TestOutput/fp9-causal-perf-failure-02/`. Exclusive attempt 03 exited before timing on the new diagnostic JSON growth
site; its exact entry now belongs to the existing Skarness Diagnostics owner.
Static policy passes with 925 growth findings and zero errors. Full exclusive
attempt 04 completed in 118.375 s but failed the two frame-time comparisons.
All native workloads, allocation guard, dense causal inspection and absolute
budgets pass. Frame averages are 1.4301 ms DX12 and 0.9866 ms physics_bench versus
0.9090 / 0.4860 in the references. UI work absent from those references costs
0.5641 / 0.5479 ms; average Physics time is lower in both workloads. The preserved
pre-FP8 Profile producer also exceeds the references. Matched old/current
Automation runs give 1.2133/1.1555 ms frame averages for DX12 and 0.7562/0.7752 ms
for physics_bench; all four exit 0. These are diagnostic comparisons, not a new
baseline approval or a replacement for the ragdoll A/B evidence.
Current reports and producers are archived in
`TestOutput/fp9-pre-fp8-perf-comparison-01/current/`. These findings do not pass
the performance gate and no timing baseline has been changed.

Perf04 also exposed preferences contamination: the causal recording left Editor
layout in the file later benchmarks shared. Its measurement tool now uses a
fresh private preferences path per run, recorded with the artifacts. The native
isolation check passes the complete measurement while preserving an inherited
Editor preferences file byte-for-byte. Evidence:
`TestOutput/fp9-causal-perf-preferences-isolation-01/`. Full attempt 04 is terminal: exit 1 in 1438.922 s. Current Physics, preflight,
all six CPU lanes and Automation pass. Profile records 1,027 cases / 3,485,267
assertions. DX12 fails on Editor-layout contamination from concurrent UI smoke.
The runner now isolates preferences per lane, with a failing-before/passing-after
concurrent regression and clean independent review. Native isolation passes in 12.482 s under
`TestOutput/fp9-native-parallel-ui-isolation-01/verified-result.json`; smoke uses
Editor, DX12 retains Canvas and the parent file is unchanged. DX12 still exits 7
on the older UI differences. Reviewed candidates and the exact current producer
are retained in `ui-baseline-review/`; the original producer remains missing.
Timing and valid Canvas
screenshot comparisons still require resolution.

FP8 and FP9 remain unaccepted. Native determinism, prediction publication and
allocation measurements pass for the archived b9df producer. Static allocation
policy now passes. Final full validation and the owner's performance/stability
decision remain outstanding. The FP8 replay visual/causal golden transition is
installed with exact old/new producers retained; the Physics CSV is unchanged.

## Replay visual transition and retained investigation

The investigation below is now explained by an identity-bound speculative head
contact. The accepted transition and first-divergence proof are in
`../FP8/golden-transitions/predictive-contacts-2ac5f033/`. Guarded writers, the
mapped gate and independent review of this transition pass. The
historical failed comparisons below remain evidence of how it was diagnosed.

The terminal 2ac5 native reveal run exited normally and passed report shape,
causal shape and durable-artifact round-trip checks. Its comparison against the
previous golden failed at causal topology count: 200 versus 201. The missing downstream ID is
202, the fixed striker catcher wall; all 200 wall brick IDs remain. Toppled/sustained-toppled bricks change
185 to 192 and settled bricks 194 to 200. The first wall activation changes
frame 100 to 98. The retained first-divergence proof explains the earlier changed
striker/head interaction that causes this cascade.

`TestOutput/fp9-replay-visual-current-01/` preserves the exact 2ac5 producer,
report, log, artifact and screenshots before any follow-up run. Locally generated
candidate files pass internal comparison and all ten injected-failure control
modes. At that stage, the candidates were unapproved and their self-comparison
did not establish correct behavior against the accepted golden. The archived
pre-FP8 5820 producer subsequently reproduced all approved visual ticks and
causal nodes with the unchanged recording under
`TestOutput/fp9-replay-visual-pre-fp8-02/`. The explained transition now updates
both tracked replay goldens and preserves their exact old versions.

The final allocation repair review is clean. The first full-plan attempt passes
Debug and Physics, then fails Automation's DLL-copy postbuild because the old
native run still holds those DLLs (`TestOutput/fp9-terminal-full-01.log`). It
requires a serial rerun after native shutdown; exclusive performance remains due.

## Diagnostic response allocation repair

Producer `2ac5f0334084e28bcc94d63e5a048f90b64f20fc447ceb9fd1f283a31e33541f`
adds local Diagnostics scopes for object-response copies, screenshot completion
identity and deferred comparison-load reply identity. Gameplay command
application remains outside those scopes; Physics source is unchanged.
The native object list/resolve, intercept/inspection-selection and screenshot
probe verifies response identity and an actual rendered capture. Archived 0350
reports 100 gameplay allocation violations and exit 9; 2ac5 reports zero and
exit 0. The current screenshot was inspected. Raw evidence is under
`TestOutput/skarness/fp9-response-scope-{old,current}-01/`; old and new executable
sets are preserved in `fp9-before-response-scope-producer/` and
`fp9-response-scope-producer/` under `TestOutput/`.

Static policy passes with 681 files, 63 direct heap sites, 157 dynamic members,
924 growth sites and zero policy errors; synthetic checker cases also pass.
Each newly recorded context was checked against startup allocation, pre-reserved
prediction capacity, diagnostic transport or explicit file loading. Comparison
playback model lists remain within their prepared geometry count. No runtime
reserve registration or byte cap changed. Logs:
`fp9-allocation-policy-final.log`, `fp9-allocation-policy-self-test.log`.
Automation compilation and source-design checks pass for the three modified
response paths in nine compiler contexts. Full source-design passes 134 files
in 1,188 compiler contexts with zero findings (`fp9-terminal-source-design.log`);
build configuration reports zero blocking diagnostics. The mapped Physics
gate passes all four worker variants against the unchanged 44,401-line golden
SHA `50bca7c0f2c420832c4fd99b1812f4db48d88cfadb4d475622a3d3bd3465a1c1`
(`fp9-terminal-physics.log`). The capture driver now returns failure for
allocation violations or a nonzero producer exit even when replay bytes match;
its self-tests cover those failures and unequal replay output.

## Producer for the native worker and A/B measurements

Automation SHA-256:
`b9df9507c6fb1c80a7a84af5d07320f1ec1242ab5e8bc861a81a968b7721cae9`.
The executable, companion binaries/PDBs, tracked diff and new Physics sources
are preserved in `TestOutput/fp9-allocation-ready-producer/` with hashes.
The original pre-FP8 producers remain in `TestOutput/fp8-prechange/`.
Additional intermediate producers remain archived separately.

`native-workers-allocation-clean.json` records 360 fixed ticks after fresh
0/repeat/1/4-worker launches. BODY, PRES, HASH and SCHK match byte-for-byte;
all processes exit 0 with zero gameplay allocation violations and zero Physics
allocation rows. Raw evidence: `TestOutput/skarness/fp9-native-oracle-final-05/`.

`prediction-generations-current.json` proves actual in-flight cancellation,
target replacement and identical published target-6 futures across generations
1/4/6 at one paused source. Target 16 owns generation 5. Selected, published
and rendered identity agree; the final screenshot was inspected. All 28,630,460
published body/solver-evidence bytes repeat exactly. This is published-state
coverage; the separate worker oracle covers complete solver state. Raw evidence:
`TestOutput/skarness/fp9-generations-final-02/`; orderly process exit 0.

`selector-phase-current.json` proves an unpaused speculative-mode request is
rejected and leaves the Physics diagnostic mode enabled, while a subsequent
paused request disables it. The process exits 0.

## Current isolated speculative cost

Each `*-ab-current.json` contains same-executable A/B/A/B measurements, four
workers, 120 Hz, seed 12345 and 1200 ticks. The first 240 ticks warm the scene;
960 ticks are measured. Every repeated A and B has exact BODY/PRES/HASH/SCHK
bytes, every allocation gate passes, and all twelve processes exit 0.

| Workload | Dynamic bodies | Off ms/tick | On ms/tick | Added cost | Off linear RMS m/s | On linear RMS m/s |
|---|---:|---:|---:|---:|---:|---:|
| Sleep island | 40 | 0.8751 | 1.1261 | 28.69% | 0.066048 | 0.249414 |
| Dropped ragdolls and boxes | 46 | 0.9262 | 1.2542 | 35.41% | 0.029460 | 0.047884 |
| Floating ragdolls | 30 | 0.3548 | 0.4035 | 13.73% | 0.381339 | 0.387391 |

Physics timing subtracts its nested DiagnosticsDump marker; full average,
p95/p99/max and unadjusted timing remain in the reports. These are instrumented
Automation measurements, not shipping Release claims. Raw output directories
are `fp9-sleep-ab-final-01`, `fp9-pile-ab-final-01` and
`fp9-water-ab-final-02` under `TestOutput/skarness/`. The first water refresh
is retained but excluded from the selected timings because a policy-parser
process overlapped it; the second was captured without concurrent validation.

No dynamic body sleeps by ten seconds. The land workloads retain more motion
with speculative contacts enabled; the sleep-island on result is materially
worse than the earlier provisional measurement. The older 100-second old/new
pile comparison also failed to establish reliable sleeping. FP8's demonstrated benefit is
preventing the tested fast translating/rotating limbs from crossing thin
fixed/dynamic walls. General pile settling is not established.

## Current 100-second pile rest observation

`long-rest-current.json` records separate off/on runs of producer 2ac5 with
seed 12345, four workers and 12,000 fixed ticks. Each preserves all Physics
diagnostic ticks and the final 2,400-frame replay window; both final screenshots
were inspected. These runs make no performance claim, and other source checks
ran concurrently. Raw evidence:
`TestOutput/skarness/fp9-long-rest-current-01/`.

| Observation | Speculative off | Speculative on |
|---|---:|---:|
| Dynamic bodies | 46 | 46 |
| Maximum bodies asleep at one time | 32 | 43 |
| Bodies asleep at the end | 10 | 10 |
| Bodies supported at the end | 46 | 46 |
| Ticks with a decrease in sleeping count | 1,707 | 1,670 |
| Final-second linear RMS, m/s | 0.040508 | 0.038485 |
| Final-second angular RMS, rad/s | 0.068236 | 0.052858 |

Neither run ever puts all 46 bodies to sleep. Predictive contacts produce
slightly less final motion in this observation and reach a higher peak sleeping
count, but both retain repeated wakes. This does not prove reliable whole-pile
settling. Both allocation guards pass with zero Physics allocations and orderly
process exit 0. This rest observation supplements the earlier exact worker and
short A/B measurements; it is not an exact repeated long-run comparison.

## Implementation and verification

Independent review verified fixes for finite rotational terrain reach,
separated-contact support nudges, and deterministic angular bounds. Negative
terrain/tip controls are retained in `fp9-review-negative-tests.log`; their
repairs pass 18 cases / 90,163 assertions in `fp9-review-fixes-tests.log`.

The expanded 520-body/130-joint matrix preserves real parallel work and covers
exact-arrival, grazing, topology changes and snapshot/restore. It exposed a
deferred topology reset erasing restored sleep counters. Clearing that flag
after validated restore repairs exact continuation; focused restore/wake tests
pass 5 cases / 645,744 assertions (`fp9-restore-repair-tests.log`).

The allocation closure repairs move Skarness observation/response storage into
diagnostics scopes, avoid temporary strings in cached event-log lookup, keep
cold traffic out of the steady callsite table, and reserve fixed 64-ray/32-shot
launcher payloads through the existing capped recorder owner. Gameplay command
application stays outside diagnostics scopes. The current plan inventories the
expanded recorder coverage. Focused allocation/recorder tests pass 13 cases /
134 assertions. They prove fresh nested payloads within a primed outer window;
they do not prove arbitrary growth of the outer recorder window.

Compiler source-design checks pass the five allocation-related production
files in 31 contexts and the recorder-test/Skarness pair in 5 contexts. The
explicit attempt to feed the standalone DX12 test to that root-project checker
failed because it selected Core's exception-disabled context; the actual DX12
project builds/runs through its owning gate. The first full gate's DX12 crash
UI stall is repaired, with its previous test executable preserved.

The renderer-free UI boundary gate now passes after correcting expectations
for already committed selection colours, themes and combo text clipping. Old
and updated test executables/source and causal hashes are preserved in
`TestOutput/fp9-ui-boundary-prechange/`. Six chevron rectangles, disabled colour
changes and wrapper parity remain asserted; clip commands are now explicit.

An earlier complete Profile suite passed 1,025 cases / 3,483,859 assertions.
The latest full rerun instead crashed in the body-local broadphase fixture
with heap corruption after 351 passing cases. The isolated fixture reproduces
exit 0xC0000374. `fp9-allocation-closure-profile-suite.log` and the executable/PDB
under `fp9-profile-crash-producer/` preserve the failure. A clean rebuild restores the isolated case and passes the full Profile suite:
1,026 cases / 3,483,256 assertions (`fp9-allocation-clean-profile-suite.log`).
The Automation solution had linked Profile test objects to Automation Physics
libraries with incompatible diagnostic class layouts. Removing the test
project's Automation Build.0 row prevents this relink. A following Automation
build leaves the Profile test executable byte-identical and the isolated test
still passes (`fp9-automation-solution-isolation.json`). Automation itself was
relinked to SHA `0350311bf2fc8aa61360768ca85584222767858a9a0464d08f4915262717db97`;
all native measurements above remain bound to the archived b9df producer.

The earlier static allocation check had 118 inherited findings after
owner-preserving updates to changed Physics sites and 17 unchanged statements'
nearby context. Exact transition records are in
`TestOutput/fp9-policy-context-transitions.json`; no blanket allowance was added.
Independent review found no blocking issue in these context-only transitions
or the solution mapping repair. The subsequent owner review and diagnostic
response repair above make the static gate green; full closure remains due.

## Earlier evidence and reproduction

The original `sleep-ab.json`, `pile-ab.json`, `water-ab.json`,
`native-workers.json` and `prediction-generations.json` remain historical
measurements from before the final Physics repairs. Current reports have
explicit `current` or `allocation-clean` filenames; do not mix their producers.

Run `tools/validate_ragdoll_predictive.py --scene <scene> --output <fresh-dir>`
for A/B, or add `--mode determinism --ticks 360 --warmup 120` for the worker
matrix. Run `tools/validate_ragdoll_prediction_generations.py --output
<fresh-dir>` for cancellation/reselection. Capture directories must be new and
under `TestOutput/skarness/`. The tool pins scene/config/assets and executable
hashes, checks tick coverage and records the actual process exit separately
from byte equality. `--self-test` covers malformed/missing measurement rows;
the retained PRES bit-flip control rejects byte 100.


Latest exclusive performance run 05 exits 7 in 95.974 seconds. Parent preferences
remain Canvas, reducing frame averages to 1.1852 / 0.8025 ms and UI averages to
0.3517 / 0.3561 ms. All native, allocation, structural and absolute-budget checks
pass, but both older frame-time references still fail. See
`performance-validation-current.md`. Current mapped replay validation attempt 02
is running under `TestOutput/fp9-terminal-replay-visual-approved-02.log`.


Current mapped replay attempt 02 passes in 370.488 seconds: 18 tests / 82
assertions, exact 2401-tick visual and causal oracle, all durable-artifact and
negative controls. The final screenshot was inspected; `mapped-replay-current.json`
binds current artifacts and hashes. No replay golden or prior bundle changed.


The timing-reference transition at
`golden-transitions/performance-references-8f40ec6e/` is independently reviewed
and written through the generic guard. Old/new exact executables and golden
files are retained; thresholds remain unchanged. Post-write complete performance
attempt 06 is running. The screenshot preservation exception is still pending.


Post-write full performance attempt 06 passes in 98.211 seconds; both relative
comparisons, absolute budgets and native/allocation/structural checks pass.
The transition's `mapped-validation.json` binds the result and raw artifacts.
No process remains active. Screenshot exception and final aggregate gate remain
outstanding; FP8/FP9 are still unaccepted at 8/10.


Git storage verification found that the two native-generated timing goldens use
CRLF, while the repository JSON rule would convert them to LF and invalidate
the exact recorded transition hashes. `.gitattributes` now preserves bytes for
performance goldens and governed transition bundles. All 35 checked files, comprising the two timing goldens and 33 FP8/FP9
transition files, pass the Git filtered-versus-raw object identity check. The candidate
hashes and measured values are unchanged. See git-artifact-byte-preservation.json.
This metadata fix needs no repeat of the passing native gates; include it in the
atomic source/golden/archive commit.


The user accepted the three reviewed screenshots and their disclosed missing-
producer exception, then requested FP8/FP9 be committed before sleep experiments.
The exact PNG candidates are installed and the approval is recorded in
ui-baseline-review/owner-approval.json. A changed Profile binary b6645ceb was
preserved, then a full Profile rebuild passed in 60.954 s, producing 2386e9e3.
Full05 is running on current source against the accepted references.
