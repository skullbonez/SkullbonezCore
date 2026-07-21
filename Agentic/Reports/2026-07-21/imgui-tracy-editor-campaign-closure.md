# ImGui + Tracy Development Editor Campaign Closure

Date: 2026-07-21
Branch: `nightrunner-21st-july`
Accepted checkpoint: `32aed676`
Plan result: E0-E17 complete, 18/18

## Outcome

The owner replied **Ready** on 2026-07-21 after the current-tip assisted
ImGui/Tracy/Legacy playtest. That verdict satisfies E17's sole retained closure
condition: the new editor is accepted as ready for extended hands-on use.

The acceptance does not retire Legacy or change the launch default. Legacy
remains compiled, selectable, and the development default; ImGui remains the
explicit `--dev-ui imgui` mode. Atomic hot swap remains allowed, while
simultaneous Legacy/ImGui activation remains forbidden.

## Closure evidence

- `../2026-07-19/imgui-tracy-e17-evaluation.md` records the complete Legacy
  control disposition, six original-resolution captures, same-frame production
  `Ctrl+0` exclusivity proof, three overhead lanes, allocation ownership, final
  independent review, and all mapped campaign gates.
- `../2026-07-19/imgui-tracy-e17-playtest.md` records the concise owner matrix
  and the accepted current build.
- The 2026-07-21 current-tip assisted pass on synchronized-main checkpoint
  `d4c8088d` found no blocking layout, clipping, overlap, focus, replay, or hot-
  swap issue. `OPEN TRACY` launched the pinned Tracy Profiler 0.13.1 and
  connected automatically to `127.0.0.1`; live Frame, Render, DX12, worker,
  CPU, and physics data appeared. The menu hot swap removed ImGui before Legacy
  appeared, and both processes closed cleanly.
- The campaign-wide comment checklist
  `../../Plans/imgui-tracy-e17-comment-audit.md` is complete at 95/95 checked,
  0 deferred, and 0 unchecked.
- The final independent ownership review reopened and closed Tracy rpmalloc
  backing, embedded LZ4 allocation, and late Legacy replay-presentation
  authority defects, then reported no remaining blocker.

## Validation and artifact policy

This closure changes documentation and governance only, so no repository
validation is required. The underlying synchronized-main checkpoint passed
`tools\validate_full.bat` in 243.7 seconds with 337 tests / 68,634 assertions,
all five runtime lanes, zero DX12 validation errors, accepted images, and the
44,401-line physics CSV byte-exact. The E17 evaluation report retains the full
UI, UI-stress, DX12, bounded graphics-stress, performance, allocation,
platform-marker, Release-exclusion, and replay-fidelity evidence.

No source, authored data, baseline, golden, screenshot oracle, or coverage
floor changed for owner acceptance or closure.

## Ledger reconciliation

The completed TODO plan is deleted under MASTER inventory rule 4. The active/
future denominator returns from 18 to 0; MASTER and SessionState report the
repository convention `0 / 0` and `100% (no live plans)`. No implementation
plan remains active.
