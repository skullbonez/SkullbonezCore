# Approved baseline reconciliation: Automation

Owner approval: 2026-09-13, "All baselines approved", following review of the
six exact hashes in the approval package. Only the listed golden bytes change.

- Previously accepted authored-sleep correction changes wake/collision order and retained path geometry. Fresh report matches the preserved pre-optimization report; all exact/negative/artifact checks pass with the candidate.
- Same authored-sleep correction changes causal topology; first canonical mismatch is id 91 versus 70. This candidate is hash-linked to the proposed visual baseline.

The predecessor executable comes from `Agentic/Plans/Artifacts/ragdoll-physics-unification/FP8/golden-transitions/predictive-contacts-2ac5f033/manifest.json` (its new behavior).
Its preserved source commit and inputs define the old reproduction envelope.
The new executable is the final Automation producer at 39bd94d2602c47ac73b4b75cb0db49b3aec1e097.
System/third-party dependencies listed by dumpbin are intentionally omitted;
restore them using the pinned repository setup. No first-party DLL is required.
Run each retained executable in an isolated repository-shaped directory with
its side's source commit's SkullbonezData and the declared output directories.
The manifest records executable launch commands; ordinary gate scripts own
the matching query/report/performance projections and negative controls.

For replay, the old golden metadata was reconciled after FP8 by 5e9975e27;
that hash-only correction did not change behavior or shader bytecode.
For performance, the old isolated source tree required the same existing
shader provenance correction (source/input hashes only). The old executable
fails its old timing references today; fresh old/new comparisons and the
independent holdout pass unchanged tolerances. For Debug, the accepted FP7
producer separately reproduces the new known-issue and query results exactly.

The complete approval analysis and commands are retained locally under
`TestOutput/gate-repair-2026-09-13/`; the commit body records mapped gate results.
No core Physics CSV, scene, solver, threshold or previous archive changes.
