# Session State

Date: 2026-08-17
Branch: `codex/master-plan-2026-08-16`
Status: Two active plans; 15/24 tasks complete

Determinism Envelope Tier-2 Hardening (9 tasks) and Catto Divergence Repairs
(6 tasks) are registered under `Agentic/Plans/TODO/`; Causal C0-C8,
Determinism T0-T2 and T5-T6, and Catto CD0 are complete. `MASTER-PLAN.md` carries
the binding order, with Determinism T7 next. The completed causal plan was deleted under the
repository convention; Git history retains its phase evidence.

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

`TIER2_DETERMINISM` T7. Add the independent scheduled/manual `ubuntu-latest`
lane for the T6 portable target, with Clang and GCC warning-clean builds and
actionable UBSan/TSan evidence. Decide and record whether Linux ASan's second
opinion is worth its duplicate coverage cost; never compare this lane against a
Windows-generated golden. T1's deterministic math owner remains unadopted in
Physics until T3/T4. Continue in the plan's recorded order, then run
`CATTO_REPAIRS` CD1-CD5.
`future_physics.md` remains unregistered.

## Blockers

- `TIER2_DETERMINISM` T3 and T4 change physics bits by construction. The plan
  grants no baseline-refresh authority. A baseline mismatch no longer stops this
  run: preserve every gate executable under a plan-and-phase name, record the
  diff and artifact path, and continue for later owner behavior review.
- `CATTO_REPAIRS` CD0 is complete. CD1-CD5 are selectable only after Causal and
  Determinism finish, in the approved R1, R5, R6, R3, R2(a), R4 order.
  Baseline-failing candidates are preserved and execution continues for later
  owner review; do not re-ask the approved scope questions or refresh a golden.
- `TIER2_DETERMINISM` T3/T4 baseline sequencing now has a downstream consumer.
  The landed replay-prediction oracle pins trajectory fingerprint
  `0x0702E1DFBB57F16D` and submitted geometry `0xF06608D189EFEEAD`, which a
  physics-bit transition would be expected to move. Decide the baseline ruling
  knowing those replay fingerprints follow from it.

## CI Note

`.github/workflows/mandatory-cpu-validation.yml` now installs the pinned
clang-format 21.1.8 from the PyPI wheel instead of the Chocolatey community feed.
The feed dropped that version on 2026-08-15, `choco` still exited 0 after
upgrading 0/1 packages, and only the workflow's explicit version assertion caught
that the runner's pre-installed 20.1.8 had been left in place. Do not repin
downward: `.clang-format:43` sets `BinPackLongBracedList`, which clang-format 20
rejects outright, and all 651 tracked C++ sources format differently under 20.

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
