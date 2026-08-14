# Session State

Date: 2026-08-15
Branch: `nightrunner-14th-AUG-26`
Status: Active - 4/5 phases complete

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
retaining all 776 final records and flat reserve growth. RP2 is complete: a
46,477-assertion production-path oracle covers 361 frames and all 200 children,
then proves the incremental scan and its exact retained entry/rest marker values
match an independent legacy full scan through prefix, reveal, topology,
generation, completion, and materially different frame-bank transitions. The
existing 4,200-frame visual interaction remains excluded and unchanged. RP1's
pending publication state now keeps the exact visible frame prefix/storage bank,
topology, retained markers, trajectory facts, and publication token coherent
through same-target builds, failed begins/workers, fast completion, and
Promote-and-Begin. A queued cross-target click remains only the next dirty
request; hidden topology, trajectories, and markers stay bound to the promoted
target through its coherent flip. Committed topology, child/all-body
trajectories, and markers resume under the overlay budget and flip together. The
latest four-generation run no longer observes the previous 97.0515-116.7760 ms
completion marker. RP1 is complete: hidden publication selects the bank opposite
the captured prediction, fast completion cannot overwrite visible root/child
records, and the flip canonicalizes trajectory plus marker-cache bank identity.
Narrow, varied, and uninterrupted budget schedules produce identical ordered
records and packet fingerprints. The rebuilt Automation probe matches at
trajectory fingerprint `0x0702E1DFBB57F16D` and submitted geometry
`0xF06608D189EFEEAD`; 87 Replay cases pass with 48,973 assertions. The latest
diagnostic measures child markers at 0.0007-0.4411 ms and Predict-off at
0.0047-0.0123 ms, with p99/p99.9 frames at 15.2930/16.1074 ms; the 121.4658 ms
maximum is excluded Automation report serialization. The two-minute visual
fidelity attempt remained in the existing excluded long interaction and was
stopped without changing its harness. Final closure now awaits only an owner
threshold ruling; no timing threshold has been added.

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
