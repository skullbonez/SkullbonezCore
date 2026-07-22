# Replay Subsystem Consolidation RC3 — Presentation Consolidation

Date: 2026-07-22
Branch: `nightrunner`
Result: RC3 complete; campaign 4/7 (57%)

## Outcome

Presentation now has an explicit two-sided boundary:

- **Data selection:** `ReplayRuntime::BuildPresentationSelection()` produces
  one frame-local `ReplayPresentationSelection` containing the selected,
  latest, and current presentation/solver/prediction borrows. Overlay state and
  render-pose application consume that same answer instead of independently
  resolving scrub state.
- **Draw submission:** `ReplayPredictionDrawing`,
  `ReplayCauseFocusSubmission`, `ReplayOverlayRenderer`, and
  `ReplayOverlayLayout` consume immutable selection/publication values and emit
  bounded tracer or UI commands. They cannot mutate timeline, scrub, or
  prediction selection.

`ReplayCauseFocusSubmission.cpp` now owns cause-focus marker submission and the
typed Physics row-resolution seam shared with trajectory drawing.
`ReplayPredictionDrawing.cpp` fell from 2,084 to 1,752 lines; the new cause
focus unit is 387 lines. `ReplayPresentation.cpp` remains 1,291 lines,
`ReplayOverlayRenderer.cpp` 1,164, and `ReplayOverlayLayout.cpp` 506.

The duplicated future-tree readiness predicate was also removed. Prediction
state owns the publication-coherence predicate; the presentation view publishes
its result, while drawing retains only the stable-root equality guard needed to
reject a stale visualizer selection.

## Ownership And Policy Proof

- No public Replay boundary changed.
- No reserve owner, cap, phase gate, counter, or allocation allowlist row
  changed. Allocation policy self-test passes; repository scan covers 416 files
  with zero allowlist errors.
- Dependency-direction and downward Replay-include proofs return zero rows.
- Project metadata contains 732 project items and 732 filter items with zero
  errors.
- No new exception, heap, compatibility bridge, callback pack, service bag, or
  hot-path interface was introduced.

## Comment Audit Checklist

Checklist path: this report. Checked: 12. Deferred: 0. Unchecked: none.

- [x] `ReplayPrediction.h`
- [x] `ReplayPredictionDrawing.cpp`
- [x] `ReplayPredictionPublication.cpp`
- [x] `ReplayPredictionPublicationOperations.h`
- [x] `ReplayPredictionTopologyPublication.cpp`
- [x] `ReplayPredictionView.h`
- [x] `ReplayPresentation.h`
- [x] `ReplayRuntime.cpp`
- [x] `ReplayRuntime.h`
- [x] `ReplayCauseFocusSubmission.cpp`
- [x] `ReplayPresentationSubmission.h`
- [x] `tools/validate_project_filters.py`

The new selection, publication, stable-identity, and submission invariants are
documented beside the relevant code. The internal publication operations header
gained its missing local glossary. No terminology remains deferred for owner
wording.

## Validation Evidence

The desktop shell could not open a separate visible console, so commands ran in
the app shell and their output was captured there.

| Command | Time | Result |
|---|---:|---|
| Profile x64 focused build | 5.2 s | PASS |
| `Profile\SKULLBONEZ_TESTS.exe` | 3.7 s | PASS; 344 cases / 68,699 assertions |
| allocation self-test + repository scan | 9.4 s | PASS; 416 files, zero allowlist errors |
| `tools\validate_fast.bat` | 57.9 s | PASS; format, metadata, Profile/Debug builds, zero warnings/errors |
| `tools\validate_replay_visual_fidelity.bat` | 418.7 s | BLOCKED after launcher/typed controls by standing provenance mismatch |
| `tools\validate_full.bat` | 110.9 s | PASS; CPU/coverage, five runtime lanes, accepted DX12 images, byte-exact physics |

The one and only RC3 mega invocation proved
`engine_processes=1`, `prediction_starts=1`, `presented_cascades=1`, and
`nested_scrub_runs=0`; its typed controls passed 16 cases / 72 assertions. It
then reached the unchanged config provenance mismatch:

- expected: `83401df03cb6e212a6a74a38e815fc550d57aa983fc9b792c2c8f4e5c784a3f4`
- actual: `bd0bb719aad7231cf500ca9a61af7d2f017e557b1b18b7de82df7eb93a3b5d93`

Per the frozen-behavior contract, the gate was not retried and no config,
golden, baseline, or artifact metadata was edited. The blocker is recorded and
the campaign proceeds to RC4.
