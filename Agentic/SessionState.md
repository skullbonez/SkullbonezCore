# Session State

Date: 2026-08-14
Branch: `nightrunner-14th-AUG-26`
Status: Active - 2/5 phases complete

Replay Prediction Runtime Spike Reduction is the only live plan. It owns the
measured completion-publication, child-marker, and Predict-off runtime stalls.
Automation report serialization and prediction target-restart work remain
explicitly excluded. RP0 is complete with named attribution and focused bank,
marker-resume, and trajectory-reuse fixtures. RP3 is complete: keyed trajectory
reuse plus a count-authoritative committed-frame prefix retains both warmed
prediction banks and reduces Predict-off from 26.0907-26.4603 ms to
0.0043-0.0110 ms; its containing frames are 8.9767-9.3338 ms. The two-generation
allocation report is engine-successful with 121 final frames and flat reserve
growth at 1459 events, although its wrapper retains the known obsolete 180/181
end-frame check against the current 208-frame script. RP2's implementation is
in place: per-node suffix cursors and inner-loop budget checks reduce
`BuildChildMarkerContext` from 0.0021-35.5981 ms to 0.0006-0.9382 ms while
retaining all 776 final records and flat reserve growth. RP2 remains open
because the immutable visual gate captures
all 2,401 reveal ticks, then its existing 4,200-frame interaction enters a
second live-playback pass; this plan excludes harness changes. RP1's pending
publication state now keeps the completed build bank visible while committed
records resume between nodes under the overlay budget. The previous
97.0515-116.7760 ms completion marker is no longer observed; trajectory slices
are 0.9804-5.2610 ms across two runs and the new worst runtime frame is 30.6322 ms, led by a
14.4368 ms retained-render refresh. RP1 remains open only for its exact immutable
fingerprint checkbox behind the same excluded harness failure.

Source Modernization Sweep, Dense Pile Sleep Resolution, Broadphase Dense
Dedup Restoration, and Look Lab Random Style Authoring remain closed by owner
direction.

## Repository Presentation Cleanup (owner-directed, this session)

Three conventions changed; read these before your first commit.

- `Agentic/Reports/` is deleted and must not be recreated. Closure and
  investigation evidence belongs in the commit body and the owning plan. Git
  history is the archive. Source `Related:` blocks now cite only durable
  targets — source, `tools/`, `Agentic/Reference/`, or a root document.
- The commit progress header dropped its percentage. Use
  `<PLAN_NAME>, TASK <DONE>/<TASK_COUNT> — <ACTION SUMMARY>` and keep the whole
  subject under 72 characters. The retired `<OVERALL_PERCENT>% OVERALL COMPLETE`
  field divided by the *current* portfolio total, so every plan-closing commit
  reported `0%`.
- `tools/validate_build_all.bat` builds Automation, Debug, and Profile.
  `validate_fast` calls it, because the compiled-symbol reachability scan reads
  three object roots and previously only two were built.

Ownership rulings pin exact line numbers. Removing or adding a comment line
above a ruled aggregate shifts its recorded `site` and fails `validate_fast`;
re-derive sites from `inventory_authority_free_aggregates.py --format json`
rather than editing them by hand.
