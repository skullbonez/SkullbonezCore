# Session State

Date: 2026-08-19
Branch: `nightrunner-18th-AUG-26`
Status: One active plan; 41/47 tasks complete

At-Rest Ball Stability (8 tasks) is active. Causal C0-C8, Determinism T0-T8,
Catto CD0-CD5, Predicted Solver Cause Hierarchy PSD0-PSD7, and Continuous
Orbital Forecast OF0-OF6 are complete. `MASTER-PLAN.md` carries the binding
order: at-rest ball stability. Completed plan files were deleted under the
repository convention; Git history retains their phase evidence.

ORBIT_FORECAST OF0 is complete. It ratifies the fixed sun as the primary,
Earth and Mars as core bodies, and the ship as an auxiliary whose orbital-
configuration failure does not by itself end the system-wide horizon;
numerical health remains globally blocking. The owning plan records exact radial
envelopes, a 600-tick sustained escape rule, core collision policy,
reset/retirement semantics, informational conservation drift, and the existing
5.0 ms worker slice plus separate frame-admission deadline. Two 120-second
fixed-step live captures
are byte-identical at SHA-256
`F3D71F660228561D155E11511FDF58DBAD8F5EF966765A21B92B711420C2AE62`.
Two isolated bounded forecasts both pass and share their rendered submission
hash, but their private simulation hashes and trajectory fingerprints differ;
OF2 and OF6 own that pre-existing determinism closure.

ORBIT_FORECAST OF1 is complete. `ContinuousPredictionSampleRing` preallocates
all-body rows through the existing Replay prediction reserve owner, publishes
each absolute-tick row only after every body is complete, and exposes a logical
oldest-to-newest snapshot with one or two wrap-safe physical segments. Slot
versions make concurrent copying retry instead of accepting torn data;
cancellation and checked counters fail closed. The focused group passes 6/6
cases and 116/116 assertions, its concurrency case passed ten stress runs, and
`validate_tests` passes 610/610 cases with 2,483,870 assertions.

ORBIT_FORECAST OF2 is complete. `ContinuousPredictionProducer` snapshots the
live body and solver values into a private Physics engine, advances unlimited-
target whole ticks under separate five-millisecond frame-admission and worker
slice clocks, and publishes only complete all-body positions through the OF1
ring. The focused case passes 53/53 assertions through three 14,401-row wraps;
retained bytes and Replay growths stay flat after warm-up, live and bounded
prediction state remain unchanged, tick 1,024 is exact across zero-thread and
one-worker execution, and stop joins in-flight work. `validate_tests` passes
611/611 cases with 2,483,563 assertions.

ORBIT_FORECAST OF3 is complete. The solar scene authors a fixed-capacity
Primary/CoreOrbiter/Auxiliary stability contract that setup resolves to stable
scene-object IDs and snapshot saving round trips through current entity names.
The Planning-owned analyzer consumes detached complete tick publications,
globally blocks numerical failure, distinguishes system-wide primary/core
orbital failure from auxiliary-only failure, applies the exact softened
fixed-primary 600-tick escape law, and publishes normalized all-member
conservation drift plus first-failure evidence. Eight focused cases cover the
positive and negative semantics; `validate_tests` passes 620/620 cases with
2,487,883 assertions. All seven ownership inventories are current, the 17/17
touched-source comment audit has no deferrals, and the change adds no Replay
include, reserve privilege, Physics row field, or post-start growth path.

ORBIT_FORECAST OF4 is complete. Planning now composes the continuous producer
and stability analyzer; App owns lifetime, frame admission, mutual exclusion
with bounded `PREDICT`, scene-transition joins, and shutdown. Legacy and ImGui
route typed continuous/reset/exit commands and publish aligned detached status,
timing, first-cause, and conservation readouts while retaining the accepted
bounded-horizon control discrepancy. `validate_fast`, all 620 unit cases,
dependency, allocation-policy, and performance gates pass; the 21/21 touched-
source comment audit has no deferrals. Physics and replay visual gates reproduce
only their inherited owner-controlled CSV and topology-version mismatches, and
no oracle was refreshed.

ORBIT_FORECAST OF5 is complete. Planning converts the detached logical sample
ring into fixed-capacity, double-buffered generic ribbon and head-marker
packets. Authored colors and one coherent newest absolute tick reach every
configured body; logical oldest-to-newest sampling never draws across the
physical ring seam. The generic DX12 retained-range path now accepts compact
record prefixes while proving every populated range remains inside both its
reserved lane and supplied span. Focused wrap/downsampling tests pass, and the
solar automation probe captures pre-wrap and post-wrap views with an advancing
absolute forecast tick and overwritten old geometry. The 15/15 touched-source
comment audit has no deferrals, and all seven ownership inventories are current.

ORBIT_FORECAST OF6 is complete. Automation separates process-local source
clock metadata from deterministic private-simulation hashes and covers every
private frame, body pose/velocity/sleep value, tornado elapsed value,
contact-completeness flag, and `PhysicsDebugContact` field. A paused solar
120-second witness produces 14,401 identical private frames with workers 0 and
1, identical submitted geometry hash `0x0E0FF9DB0F2EF6E0`, and flat Replay
reserve growth from 475 to 475 after warm-up. Focused continuous-prediction
tests pass 7/7 cases and 173/173 assertions; the complete Profile suite passes
622/622 cases and 2,520,795/2,520,795 assertions. Dependency, strict Replay
allocation, DX12, one-minute graphics stress, and performance pass. All seven
ownership inventories are current, the 4/4 touched-source comment audit has no
deferrals, and the independent post-fix review reports zero findings. Physics
and replay visual fidelity reproduce only the inherited owner-controlled varied
CSV and reveal-0 `header.topologyVersion` stops; no oracle was refreshed.

The owner parked Deterministic Trigonometry under `Agentic/Plans/WNF/` on
2026-08-18 and replaced its active slot with At-Rest Ball Stability. The new
plan removes the authored witness's arbitrary 1,800-frame timeout: `at_rest`
now runs unlimited and must not auto-exit until a generic authored requirement
proves `ball_a`, `ball_b`, and `ball_c` are all Physics-asleep. SkullScope
diagnosis and semantic false-pass controls precede tuning, and vertical
vibration, excessive sliding, rolling reversals, and delayed supported sleep
remain separate quality metrics. Friction and sleep policy remain valid
diagnostic hypotheses, but proper contact/solver/support fixes come first and
any production policy change is blocked on isolated evidence plus explicit
owner approval.

Catto CD5 retained the authored partial-TOI sequence and derives one local
contact interval from the remaining time of each awake dynamic participant.
Terrain uses its dynamic body's interval, two awake dynamics use the shorter
interval, and authored-fixed or sleeping solver-static bodies contribute no
clock. Baumgarte, terrain support/friction, and object constant friction use
that interval; near-zero intervals disable bias and friction while retaining
impact restitution. Six focused interval cases are included. The full tests
pass 590 cases / 2,480,638 assertions, performance and the independent review
are green, and the terminal gate passes preflight, CPU/coverage, Automation,
and DX12 before the approved immutable physics golden reports 20,394 changed
rows. Physics and replay candidates plus their generating executables remain
under `TestOutput/validation/candidates/CATTO_REPAIRS_CD5*`; no golden was
refreshed.

Catto CD4 added scalar accumulated-impulse warm starting to point joints and
proved a loaded ten-link chain reduces final sag by about 30.5 percent and peak
sag by about 11.8 percent. Because the cache is next-step state, solver
checkpoints now persist it through durable scene identity in version 3, with
preflight-before-mutation restore, stable topology trimming, legacy cold-cache
compatibility, sparse deltas, hashing, and cold Clear/recreate coverage. The
full test gate passes 584 cases / 2,480,611 assertions; replay artifact and
strict allocation gates pass with snapshot high water 3,401,552 bytes and
recorder high water 16,223,044 bytes. The varied physics candidate remains
byte-identical to CD3; replay timing moves for 155 nodes. Fresh executables,
CSV, report, logs, and replay are preserved under
`TestOutput/validation/candidates/CATTO_REPAIRS_CD4*`; no golden was refreshed.
All governance inventories, the 19/19 touched-source comment audit, and the
independent re-review are green.

Predicted Solver Cause Hierarchy was registered last on 2026-08-18 after an
adversarial plan review. Its High mode retains exact predicted Body -> Manifold
-> SolverRow evidence; Low keeps the selected-root trajectory topology while
hiding causal inspection and releasing all detail capacity through an
observable F6 checkpoint. The bottom-timeline `HIGH DETAIL` checkbox replaces
the mouse Pause button, remains on by default, and preserves keyboard `P`.

PSD0 is complete. Pure policy values and tests pin High-default transition
effects, preference persistence across generation and archive boundaries, and
explicit selected-tree versus authored-space presentation. The at-rest witness
retains flat synthetic rows and `SolverDetailNotAvailable`; the seeded ordinary
generated demo has one selected root (body 162) with private mutual gravity and
draw-list all-body presentation both false, while `solar_system.scene.json`
retains four intentional roots with both facts true. Automation reports now
serialize complete bounded trajectory, future-node, and cause-row topology.
Focused tests pass and the touched-source comment audit is 5/5 with none
deferred. `validate_tests`, `validate_fast`, and `validate_automation` are
green. The canonical replay visual gate retains the inherited one-frame causal
golden mismatch (`topology[0].firstFrame` 137 -> 136), so no golden was
refreshed; the exact executable/report/artifact evidence is preserved under
`TestOutput/validation/candidates/PREDICT_SOLVER_DETAIL_PSD0/`. Its candidate
comparison and all nine mutation-control families pass.

PSD1 is complete. `ReplayPrediction` owns the sole retained High/Low mode and
the typed command reaches it through both established replay input seams. The
former mouse Pause slot is a shared checked-by-default High Detail checkbox;
drawing, hit testing, and automation use one rectangle, while keyboard `P`
retains ReplayRuntime play/pause and cross-scene pause remains a separate typed
UI command. Prediction-mode transitions clear prediction inspection and exit
its camera without disturbing recorded inspection. The full 595-case suite,
all nine fast-gate stages, Automation smoke, one-minute graphics stress, and all
eight isolated DX12 stages pass. The touched-source audit is 19/19 with none
deferred. The canonical replay visual oracle retains only the inherited 137 ->
136 causal first-frame mismatch; no golden changed, and the exact PSD1 evidence
plus candidate baselines live under
`TestOutput/validation/candidates/PREDICT_SOLVER_DETAIL_PSD1/`, where the
candidate comparison and all nine false-pass control families pass.

PSD2 is complete. Paired immutable segmented evidence banks now provide stable
generation/mode/epoch/frame/topology/publication identity, release/acquire
prefix publication, independent build/committed promotion, overflow-safe
reserve, cancellation, and explicit capacity release at the post-join High ->
Low boundary. The measured 20/120-second matrix selected 128-frame,
256-contact, and 1,024-pipeline segments with a 320 MiB per-bank cap. The dense
120-second witness measured 317,157,376 bytes; two banks plus the prior base
working-set high water total 653,016,512 bytes, so the shared
`replay_prediction_working_set` cap is now 960 MiB with 1.542x headroom. F6 and
diagnostics expose current/peak, bank split, and release checkpoints. Five
focused cases, the full 600-case / 2,485,514-assertion suite, strict allocation
policy, project filters, UI fingerprint, and all nine fast-gate stages pass.
The touched-source audit is 14/14 with none deferred.

PSD3 is complete. `ReplayPrediction` now owns the typed selected-causal versus
all-bodies-space presentation policy, carried through scene/demo seeding,
promotion, archives, views, and draw lists without Physics-force inference.
Ordinary scenes publish the selected root plus only its debug-contact cascade;
six authored mutual-gravity showcase scenes explicitly preserve all-body space
paths. Parser/snapshot/archive, promotion, cycle, disconnected-body, partial-
budget, and draw-helper tests pin the contract. Runtime witnesses record one
generated-demo root with 240 future nodes and four explicit solar roots. The
full suite passes 601 cases / 2,484,572 assertions, Automation smoke passes,
all nine fast-gate stages pass, and the touched-source audit is 24/24 with none
deferred. The immutable replay visual oracle was not refreshed: it correctly
rejects PSD3's 201 contact-derived depth-1-to-11 nodes and 806 trajectory
records against its retired 200-node flat topology and 802 records. Exact
reports, logs, and executables remain under
`TestOutput/validation/candidates/PREDICT_SOLVER_DETAIL_PSD3/`.

PSD4 is complete. High prediction builds now acquire the full-pipeline consumer
only on their private Physics engine, publish exact persistent-contact and
ordered pipeline evidence behind the matching sealed frame prefix, and balance
the consumer across promotion, cancellation, restart, and destruction. Low
builds allocate and copy no solver evidence. High and Low generated-demo
witnesses publish the same 90-frame private simulation hash
`0x18C9CE2B02FF5399`; High retains 14 contacts / 24,364 pipeline rows with two
balanced acquire/releases, while Low retains zero evidence and performs no
acquire/release. Terrain and dense witnesses retain non-empty exact evidence,
with the 200-body case remaining inside the 320 MiB bank cap. The full
602-case / 2,484,212-assertion suite, Automation, strict allocation,
frame-spike, and all nine fast-gate stages pass; compiled reachability has 90
ruled rows and zero blockers. The touched-source audit is 9/9 with none
deferred. The stale Physics CSV and PSD3 visual-topology oracles remain
unchanged and are recorded in the phase evidence; neither mismatch is caused
by PSD4.

PSD5 is complete. Planning now consumes source-neutral recorded or predicted
solver-detail views with exact generation, bank-epoch, topology, publication,
frame, range, sequence, feature, and completeness checks. High detail emits the
Body -> Manifold -> SolverRow hierarchy from immutable Prediction evidence and
matching predicted poses; Low retains synthetic PredictionContact/Motion rows.
Scrubber selection, manifold presentation, and camera focus preserve the exact
evidence identity, including same-frame replacement rejection. Final High and
Low witnesses share live solver hash `0xFC1E96D513B66A0B`: High has 4 Body / 3
Manifold / 3 SolverRow / 0 synthetic rows with balanced 2/2 consumer activity,
while Low has 4 Body / 0 Manifold / 0 SolverRow / 3 synthetic rows and zero
evidence capacity. The 603-case / 2,483,802-assertion suite, Automation, strict
allocation, and four-generation frame-spike gates pass. The touched-source
comment audit is 14/14 with none deferred. The unchanged Physics oracle still
differs in 20,394 lines beginning at frame 102, and the replay visual oracle
still rejects the corrected topology at reveal 0 (`header.topologyVersion`);
neither baseline was refreshed.

PSD6 is complete. RVPD schema 4 carries captured capability, explicit path
policy, ordered section descriptors, cumulative byte closure, lightweight
state, and optional bounded High event-frame/contact/pipeline evidence. Load
preflights the complete layout and builds reserve-accounted candidates before
atomically replacing prediction and evidence-bank state. High restores exact
inspection without Physics; active Low validates but does not retain High
evidence; Low and v2/v3 never upgrade. Repeated load rebases bank epochs, and
all corruption/future-schema failures preserve the prior state. Allocation,
Automation, replay-v2 artifact, and all nine fast stages pass, including 603
tests / 2,485,922 assertions and zero-warning Automation/Debug/Profile builds.
The touched-source comment audit is 11/11 with none deferred. The visual gate
reaches only the inherited reveal-0 `header.topologyVersion` mismatch after the
archive controls complete. Performance passes DX12 and absolute Physics
budgets; the unchanged relative Physics sample oscillates around its threshold
across three runs while memory improves, so no baseline was refreshed.

PSD7 is complete. App now samples complete replay totals and independently
summed categories immediately before and after the synchronous High -> Low
evidence release. The release oracle rejects stale totals, mismatched category
snapshots, capacity disagreement, underflow, and fabricated extreme values; the
final witness proves matching positive evidence/replay/category deltas of
1,925,120 bytes. One shared availability predicate governs predicted cause-row
publication, rendering, hit testing, automation, and transition clearing, so
Low visibly removes the cause window while leaving the compact High Detail
control available. The at-rest workflow passes 19 assertions through frame
2,094 and rebuilds fresh High detail; the multi-body witness passes six
assertions through frame 1,002 with three manifolds and three solver rows.

The full 604-case / 2,484,279-assertion terminal suite, all seven ownership
inventories, strict replay allocation, four-generation frame-spike, replay
artifact, dependency, fast, Automation, and DX12 gates pass. Touched-source
comment audit: 13/13 checked, zero deferred. The independent review first found
the retired tautological memory proof and solver-panel-only Low assertion; both
were corrected, and the fresh post-fix review found no remaining blocker. The
immutable visual oracle still stops at inherited reveal-0
`header.topologyVersion`, Physics retains its inherited 20,394-line mismatch
from frame 102, and relative Physics performance remains noisy while absolute
budgets pass. The terminal `tools\agent_validate.bat --plan-completion` rerun
passes preflight, every mandatory CPU lane, Automation, and DX12 before
stopping at that exact inherited Physics mismatch; its verbatim log is
`TestOutput/validation/PREDICT_SOLVER_DETAIL_PSD7_agent_validate.log`. No
baseline was refreshed.

Catto CD3 replaced the global summed squared impulse-delta early-out with the
maximum per-contact-row squared delta while retaining the historical sum for
diagnostics only. The focused eight-contact oracle fails the retired gate and
the full test gate passes 582 cases / 2,479,968 assertions. The deterministic
physics mismatch changes 13,369 rows across 23 bodies from frame 102, while the
replay causal candidate moves `topology[1].firstFrame` from 154 to 174. Exact
Debug and Automation/Profile executables, CSV, report, log, and replay remain
under `TestOutput/validation/candidates/CATTO_REPAIRS_CD3*`; no golden was
refreshed. Performance, dependency proof, all seven inventories, a 3/3 touched-
source comment audit, and independent review are green.

Catto CD2 landed R5 and R6 as separate commits. R5 removed terrain manifold-
count scaling from restitution; R6 removed the hardcoded solver-local quiet
velocity snap so `PhysicsSleepController` remains the configured transition
owner. Focused tests pin the 4.5 metres-per-second target, one-row/four-row
agreement, and preservation of `0.04` linear / `0.01` angular residual motion.
The full test gate passes 581 cases / 2,479,964 assertions. R5 and R6 divergence
executables, reports, logs, CSVs, and replay artifacts remain under
`TestOutput/validation/candidates/CATTO_REPAIRS_CD2_*` for later owner
assessment. By explicit owner direction, the canonical physics, replay visual,
and replay causal goldens were refreshed; `validate_physics` and the complete
replay visual-fidelity gate now pass. All seven governance inventories are
green, the touched-source comment audit is 2/2 checked with none deferred, and
independent review found no implementation or ownership blocker.

Catto CD1 replaced per-contact-row position projection with one deepest-row
linear correction per contiguous manifold, inverse-mass shared and accumulated
to one body-store publication. The focused family passed 13 cases / 172
assertions and the full test gate passed 579 cases / 2,479,944 assertions. The
physics gate reached its expected immutable-golden mismatch (40,905 lines); a
direct T4 comparison is identical through frame 289 and first differs at frame
290. The replay gate's build and 17 cases / 75 assertions passed before its
older causal golden reported firstFrame 185 -> 184.

The independent review required and cleared a same-scene Automation A/B. Its
non-physics envelope was identical, while the Physics-owned trajectory and RVPD
hashes diverged; a second CD1 run reproduced canonical projection hash
`E16AD39C7CA4CAF8E3DE4F809F3E7FACB313B5AF13CA72216FE0CA59D0D465D3`
and byte-exact replay hash
`932DC9FADFBEED901D27743C4D90C2F1849BBCAC7406C0B62B138C1657F53765`.
Direct, replay, and pre-CD1 attribution candidates are retained under
`TestOutput/validation/candidates/CATTO_REPAIRS_CD1*`. The owner accepted the
improved CD1 simulation on 2026-08-17 and authorized the varied, shooting,
space, known-issue, query, replay-visual, and causal golden refresh. All seven
ownership inventories are green and the touched-source comment audit is 4/4
checked, 0 deferred.

Determinism T4 replaced ragdoll `acosf` with the shared deterministic vector-
angle owner and closed the Physics-reachable CRT transcendental set. Focused
ragdoll tests passed 4 cases / 31 assertions, the 0/1/4-worker oracle passed
30,709 assertions, and independent ownership review returned clear. The full
gate passed 578 cases / 2,479,932 assertions, coverage, standalone CPU suites,
Automation, and DX12 before the expected inherited 40,909-row Physics mismatch.
The direct candidate is retained under
`TestOutput/validation/candidates/TIER2_DETERMINISM_T4/` with CORE hash
`8DAAB85DAF180C7292FB4203AA64FC671FA52EB26892C439816795929F46819D`,
TESTS hash `44F60FD4EEDDBCB1B75A00BC7A32D83FE39FD95F7BC11B25DF29B1568F0B96C8`,
and CSV hash `0F25F3B6813401B7D9EA4B52CBB088D30E03EEBAFFEFCAF15FEE63B3DFF72FFD`.
The final full-gate candidate in the same directory has CORE hash
`C09EEF7B8B616349BD979BDFC1AD10D58FB36214322F8338EA0A417C8AB8B774`,
TESTS hash `182D75A1CB79294AB9A51A5622B0331B9F4A95C629F0CD5C744F174AFF24D5C3`,
and the identical CSV hash. That CSV is also byte-identical to T3, proving T4
added no further drift in the varied regression scene. No golden was refreshed.

Replay Prediction Adversarial Repair completed on
`codex/replay-prediction-adversarial-fixes` in two `REPLAY_ADVERSARIAL` commits.
It corrected all five committed-frame readers, repaired the Automation evidence
harnesses, added deterministic best-fit reuse and whole-node append resume,
bound markers to complete coherent publication, and recorded real-run retained
high water. Its completed plan was deleted from the live queue; Git history is
the audit archive. No baseline was refreshed.

Source Modernization Sweep, Dense Pile Sleep Resolution, Broadphase Dense
Dedup Restoration, and Look Lab Random Style Authoring remain closed by owner
direction.

The mandatory CPU CI lane was repaired this session; see the CI note below
before assuming a red lane is your change.

## Merge Note

`nightrunner-14th-AUG-26` was merged into `main` on 2026-08-16 by owner
direction, and repository validation was skipped on an explicit owner decision.
The merged tree has therefore never been built. Both sides passed independently
first — the branch passed `tools/validate_full.bat` end to end at its tip, and
`main`'s fifteen intervening commits changed only documentation, CI workflow,
and `tools/validate_native_diagnostics.py`, touching no engine source — so the
merge had no source-level conflict and only the two ledger files needed
reconciliation. Treat the first validation run on `main` after this merge as
the gate that was deferred, and do not read the branch's green full-gate result
as covering the merged tree.

That deferred merged-tree gate is now discharged by the adversarial-repair
branch. Its exact final source state passed `tools\validate_fast.bat` and
`tools\validate_full.bat` without a baseline refresh, along with the focused
Replay CPU family, visual-fidelity, allocation-policy, dependency, and spike
diagnostic gates. The repair's two commit bodies retain the measured output.

`claude/codebase-overview-jatmcg` needed no merge. It landed earlier via PR #152
(`5dd31893d`) and was already an ancestor of `main`; the remote branch is stale.

## Next Work

Execute `REST_STABILITY` RS2-RS7. RS0 added the authored all-three-ball sleep
completion gate; no frame/time cap or aborted run can complete the unlimited
scene. Its preserved baseline and 42-query SkullScope packet ratify the owning
causes and exact semantic thresholds. RS1 turned those thresholds into a
failure-first semantic analyzer with 19 focused clean/negative cases, exact
all-six timeline coverage, authored-gate evidence, and box non-regression
ceilings before Physics behavior changes.
Semantic oracles must pin all three balls' vertical tail motion, slip, rolling
reversals, support/sleep latency, and final sleeping state while retaining the
boxes as controls. Repair normal vibration, tangent/rolling response, and sleep
transition separately, preserve candidate artifacts, and refresh no baseline
without owner approval.

Deterministic Trigonometry remains owner-parked in `Agentic/Plans/WNF/` and is
not part of the active queue.

## Blockers

- None. Continue with `REST_STABILITY` RS2. The stale Physics CSV,
  noisy relative Physics performance sample, and
  corrected-topology visual oracle remain owner-controlled validation findings;
  do not refresh any of them without approval.

## CI Note

`.github/workflows/mandatory-cpu-validation.yml` now installs the pinned
clang-format 21.1.8 from the PyPI wheel instead of the Chocolatey community feed.
The feed dropped that version on 2026-08-15, `choco` still exited 0 after
upgrading 0/1 packages, and only the workflow's explicit version assertion caught
that the runner's pre-installed 20.1.8 had been left in place. Do not repin
downward: `.clang-format:43` sets `BinPackLongBracedList`, which clang-format 20
rejects outright, and all 651 tracked C++ sources format differently under 20.

The same workflow now builds one Debug physics runtime artifact after restoring
the pinned PIX package with patched NuGet 6.14.3, then runs that exact
manifest-hashed payload on two fresh hosted runners without rebuilding. Run
`31990868600` passed end to end on `eddb25e88`, with both replicas reporting
`byte_comparison=PASS` for executable SHA-256
`2C5CFEF85DAD5595A104277A2A2185D3ACBE18ECF2C2B8FCCA288CD5CA27EDE8`.

## Finding That Motivated The Plan

A `SkullbonezSource/Physics` scan reports one `acosf` and no other
implementation-defined transcendental, which understates the exposure. The trig
is one call level down: `Quaternion::RotateAboutAxis`
(`SkullbonezSource/Maths/Quaternion.cpp:94-95`) calls `sinf` and `cosf`, and
`IntegrateBodyRecordPose` at `SkullbonezSource/Physics/PhysicsBodyStore.cpp:1007`
calls it for every rotating body every tick. Any future scan of this class must
cover `Maths` as well as `Physics`, which is why the T2 gate scopes both.

Separately, `SkullbonezSource/Physics/Ragdoll.cpp:253` passes an unclamped dot
product to `acosf`; a normalized dot product routinely exceeds 1.0 by an ULP and
`acos` returns NaN. That is a live correctness defect independent of the
determinism work, and T4 lands the clamp as its own reviewable change.

## Repository Presentation Conventions

Still current; read before your first commit.

- `Agentic/Reports/` is deleted and must not be recreated. Closure and
  investigation evidence belongs in the commit body and the owning plan. Git
  history is the archive. Source `Related:` blocks cite only durable targets —
  source, `tools/`, `Agentic/Reference/`, or a root document.
- The commit progress header carries no percentage. Use
  `<PLAN_NAME>, TASK <DONE>/<TASK_COUNT> — <ACTION SUMMARY>` and keep the whole
  subject under 72 characters. Commits made outside a plan runner, including
  plan registration, use normal subject rules and claim no plan progress.
- `tools/validate_build_all.bat` builds Automation, Debug, and Profile.
  `validate_fast` calls it, because the compiled-symbol reachability scan reads
  three object roots.

Ownership rulings pin exact line numbers. Removing or adding a comment line
above a ruled aggregate shifts its recorded `site` and fails `validate_fast`;
re-derive sites from `inventory_authority_free_aggregates.py --format json`
rather than editing them by hand.
