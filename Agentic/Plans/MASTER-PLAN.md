# MASTER PLAN - Active

Date: 2026-08-15
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

The active portfolio contains one plan with three completed and two open phases.
RP0 now has focused fixtures for coherent bank switching, marker resume, and
trajectory reuse. RP3 is complete: count-authoritative frame invalidation and
trajectory active-prefix reuse reduce Predict-off from 26.0907-26.4603 ms to
0.0043-0.0110 ms while retaining both warmed prediction banks. RP2's incremental
child-marker scan reduces its measured direct range from 0.0021-35.5981 ms to
0.0006-0.9382 ms, but its immutable visual-oracle checkbox remains open because
the existing 4,200-frame harness enters a second live-playback pass after all
2,401 reveal ticks. RP1 is complete: opposite-bank committed publication now
preserves the captured prediction through fast completion, converges to
canonical record identities across narrow/varied/uninterrupted budget schedules,
and matches two Automation cadences at trajectory fingerprint
`0x0702E1DFBB57F16D` plus submitted geometry `0xF06608D189EFEEAD`. The latest
four-generation run does not observe completion trajectory publication; child
markers measure 0.0007-0.4411 ms, Predict-off 0.0047-0.0123 ms, and p99/p99.9
frames 15.2930/16.1074 ms. Its 121.4658 ms maximum is excluded Automation report
serialization. Final plan deletion still requires RP2's full-scan oracle and an
owner timing-threshold decision; the excluded visual harness remains untouched.
