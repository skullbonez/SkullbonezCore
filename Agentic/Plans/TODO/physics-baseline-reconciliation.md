# Approved Physics Baseline Reconciliation

Date: 2026-09-13
Owner: repository owner; coordinator on `codex/unified-ui`
Code: `BASELINE_REPAIR`
Status: 0/1

The owner approved all six exact candidates in
`TestOutput/gate-repair-2026-09-13/approval-manifest.json` and requested a clean
PR. This bounded approval/validation task owns the archived replacements;
PHYSICS_SCALE implementation is complete and PHYSICS_AB remains untouched.

- [ ] BR0: Preserve old/new first-party producers and exact golden bytes in new
  transition bundles; apply only the six approved hashes; run deep Physics,
  replay fidelity and performance gates; commit the repairs and evidence.

Accepted differences: FP7 shared-solver known-issue/query output, corrected
construction wake behavior in replay, and measured machine-local performance
reference drift. The approval report contains retained-producer comparisons
and negative controls. No runtime source, scene, threshold or core golden may
change under this task. All other protected files must remain unchanged.

Validation uses the mapped baseline gates after replacement and the current
preflight/CPU/Automation/DX12 evidence for unchanged executable hashes. This
is closure of approved references, not a new runtime implementation plan or
a rerun of the already-recorded PHYSICS_SCALE terminal wrapper. Remove this
completed checklist at commit; retain approval and reproducibility facts in
the immutable bundles, MASTER-PLAN and commit body. Update existing PR #169
and verify hosted checks after the commit is pushed.
