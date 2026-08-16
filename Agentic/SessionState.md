# Session State

Date: 2026-08-16
Branch: `main`
Status: Four active plans; 4/29 tasks complete

Determinism Envelope Tier-2 Hardening (9 tasks), Causal Event Inspection
(9 tasks), and Catto Divergence Repairs (6 tasks) are registered under
`Agentic/Plans/TODO/`, 0 complete. `MASTER-PLAN.md` carries the binding order.

Replay Prediction Runtime Spike Reduction (5 tasks, 4 complete) merged into
`main` this session from `nightrunner-14th-AUG-26`. It owns the measured
completion-publication, child-marker, and Predict-off runtime stalls; Automation
report serialization and prediction target-restart work remain explicitly
excluded. RP0 through RP3 are complete: committed opposite-bank publication,
incremental child-marker scanning, and count-authoritative frame invalidation
with trajectory active-prefix reuse. The measured effect is Predict-off from
26.0907-26.4603 ms down to 0.0043-0.0110 ms and child markers from
0.0021-35.5981 ms down to 0.0006-0.9382 ms, with completion publication no
longer observed at all. The Automation probe pins trajectory fingerprint
`0x0702E1DFBB57F16D` and submitted geometry `0xF06608D189EFEEAD`. Full per-phase
evidence lives in `Agentic/Plans/TODO/replay-prediction-runtime-spike-reduction.md`
under Current RP4 evidence; it is not duplicated here.

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

`claude/codebase-overview-jatmcg` needed no merge. It landed earlier via PR #152
(`5dd31893d`) and was already an ancestor of `main`; the remote branch is stale.

## Next Work

`TIER2_DETERMINISM` T0. Establish the pre-change envelope and determine whether
the statically linked UCRT dispatches on processor features for `sinf`, `cosf`,
and `acosf`. This needs a Windows build and either a disassembly of the linked
routines or the same binary executed on two different microarchitectures. No
source edit.

`CAUSAL_INSPECT` C0. Fix the seek contract: confirm every cause row kind
addresses a frame the scrubber can restore, and define the refusal behavior for a
row whose frame has aged out of the recorder ring. C2 then picks the transport
semantics, which decides what "fast forward to the causal moment" means for the
whole feature; the plan recommends restoring to a short lead-in and running
forward while the camera arrives.

`TIER2_DETERMINISM` T1, T2, T5 through T8 and all of `CAUSAL_INSPECT` are
unblocked and change no physics output.

## Blockers

- `TIER2_DETERMINISM` T3 and T4 change physics bits by construction. The plan
  grants no baseline-refresh authority; each needs its own owner approval of that
  exact transition, reviewed as behavior.
- T0's CRT dispatch question cannot be answered from a Linux session. It needs
  Windows tooling.
- All of `CATTO_REPAIRS` is gated behind CD0, which is an owner-only scope
  ratification: the owner picks which solver repairs R1 through R6 are in scope
  and in what order. A run may not answer CD0 by reasoning about it. CD1 through
  CD5 each additionally change byte-exact physics baselines and need their own
  owner ruling, so no task in that plan is currently selectable.
- Replay Prediction Spike Reduction RP4 is owner-only and is the plan's last
  task. The post-fix distributions are recorded in the plan; the owner either
  picks the marker/frame limits that make
  `tools/validate_replay_prediction_frame_spikes.bat` a hard failure, or rules
  that the gate stays informational. The gate is informational as merged. Plan
  deletion follows that ruling in the same commit. No agent task remains, so a
  run that selects this plan has nothing it may legitimately do.
- `TIER2_DETERMINISM` T3/T4 baseline sequencing now has a downstream consumer.
  The replay plan's RP1 oracle pins trajectory fingerprint
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
