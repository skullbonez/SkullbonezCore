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

The active portfolio contains one plan with two completed and three open phases.
RP0 now has focused fixtures for coherent bank switching, marker resume, and
trajectory reuse. RP3 is complete: count-authoritative frame invalidation and
trajectory active-prefix reuse reduce Predict-off from 26.0907-26.4603 ms to
0.0043-0.0110 ms while retaining both warmed prediction banks. RP2's incremental
child-marker scan reduces its measured direct range from 0.0021-35.5981 ms to
0.0006-0.9382 ms, but its immutable visual-oracle checkbox remains open because
the existing 4,200-frame harness enters a second live-playback pass after all
2,401 reveal ticks. The next binding slice is RP1's coherent, budgeted completion
publication; its current direct range is 97.0515-116.7760 ms.
