# Session State

Date: 2026-08-18
Branch: `nightrunner-18th-AUG-26`
Status: Four active plans; 22/47 tasks complete

Catto Divergence Repairs (6 tasks), Deterministic Trigonometry Adoption
(8 tasks), Continuous Orbital Forecast (7 tasks), and Predicted Solver Cause
Hierarchy (8 tasks) are registered under `Agentic/Plans/TODO/`; Causal C0-C8,
Determinism T0-T8, and Catto CD0-CD3 are complete. `MASTER-PLAN.md` carries the
binding order: Catto CD4 next, deterministic trig following Catto, then
continuous orbital forecast, then predicted solver detail. The completed causal
and determinism plans were deleted under the repository convention; Git history
retains their phase evidence.

Predicted Solver Cause Hierarchy was registered last on 2026-08-18 after an
adversarial plan review. Its High mode retains exact predicted Body -> Manifold
-> SolverRow evidence; Low keeps the selected-root trajectory topology while
hiding causal inspection and releasing all detail capacity through an
observable F6 checkpoint. The bottom-timeline `HIGH DETAIL` checkbox replaces
the mouse Pause button, remains on by default, and preserves keyboard `P`.

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

Execute `CATTO_REPAIRS` CD4: add R2(a) accumulated joint-impulse warm starting
on the constraint handle and measure ragdoll sag under load. Then execute R4.
Preserve any new divergence
executables by phase; the CD2 golden authorization does not pre-approve later
phase movement. `future_physics.md` remains unregistered.

After Catto closes, execute `TRIG_DETERMINISM` DT0-DT7. Its dated inventory is
120 direct CPU production sine/cosine calls across 15 files plus 20 test-only
calls across 5 files. Establish the A/B oracles and exact-tick headless solar
fast-forward before migrating callers; the current fixed-step time-scale path
caps work at five ticks per presented frame and drops excess ticks, so it is not
the long-horizon test instrument.

After deterministic trig closes, execute `ORBIT_FORECAST` OF0-OF6. The owner
reactivated it from `WNF/` on 2026-08-17. OF0 first ratifies the authored solar
stability cohort and thresholds; later phases add coherent circular
publication, continuous isolated prediction, Planning-owned stability
diagnostics, operator controls, and rolling orbital presentation without
changing bounded `PREDICT`.

After orbital forecast closes, execute `PREDICT_SOLVER_DETAIL` PSD0-PSD7. Start
by pinning mode persistence and the ordinary-demo all-root regression, then add
immutable segmented evidence banks, exact private-pipeline capture, the body ->
manifold -> solver-row hierarchy, transactional archives, and the combined
High/Low release workflow.

## Blockers

- None. Continue with R3, R2(a), and R4 in the approved order.

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
