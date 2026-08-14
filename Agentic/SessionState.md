# Session State

Date: 2026-08-14
Branch: `nightrunner-14th-AUG-26`
Status: Active - 0/5 phases complete

Replay Prediction Runtime Spike Reduction is the only live plan. It owns the
measured completion-publication, child-marker, and Predict-off runtime stalls.
Automation report serialization and prediction target-restart work remain
explicitly excluded. RP0 now reports named marker ranges and final oracle facts;
RP3 retains keyed trajectory-record capacity, reducing Predict-off from
26.0907-26.4603 ms to 15.6154-16.2350 ms. The remaining measured cost is nested
prediction-frame destruction; RP0 still needs the bank/marker resume fixtures.

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
