# MASTER PLAN - Active

Date: 2026-08-14
Status: Active; one live implementation plan

## Owner Direction

- Replay Prediction Runtime Spike Reduction is active under
  `TODO/replay-prediction-runtime-spike-reduction.md`. It owns the measured
  completion-publication, child-marker, and Predict-off runtime stalls; harness
  serialization and target-restart work are excluded.
- Source Modernization Sweep is complete. No further work remains.
- Dense Pile Sleep Resolution is complete by owner direction. No further work
  will be performed and no additional baseline or solver change is requested.
- Broadphase Dense Dedup Restoration is complete. The dense pair-dedup bitset
  is retained because its roughly 4 MiB maximum memory cost avoids the measured
  CandidatePairs CPU regression.
- Look Lab Random Style Authoring is closed. No further work remains.

Other completed plan files were deleted; git history is the archive.

## Portfolio Progress

The active portfolio contains one plan with five open phases (RP0-RP4). RP0
attribution and RP3 trajectory-capacity reuse are implemented; the next handoff
finishes the remaining CPU fixtures and the measured nested-frame invalidation.
