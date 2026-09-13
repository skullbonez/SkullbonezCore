# Approved baseline reconciliation: Profile

Owner approval: 2026-09-13, "All baselines approved", following review of the
six exact hashes in the approval package. Only the listed golden bytes change.

- Machine-local reference drift: the exact old baseline-producing executable fails its own old reference in all three fresh runs. Current median run passes against the fresh old median with unchanged thresholds.
- Same machine-local reference drift; current median passes against fresh old median, including the existing 5 MiB memory-growth bound. Proposed bytes are one actual measured run, not synthesized statistics.

The predecessor executable comes from `Agentic/Plans/Artifacts/ragdoll-physics-unification/FP9/golden-transitions/performance-references-8f40ec6e/manifest.json` (its new behavior).
Its preserved source commit and inputs define the old reproduction envelope.
The new executable is the final Profile producer at 39bd94d2602c47ac73b4b75cb0db49b3aec1e097.
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
