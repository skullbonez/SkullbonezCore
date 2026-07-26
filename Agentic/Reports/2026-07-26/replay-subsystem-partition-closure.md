# Replay Subsystem Partition Closure

Date: 2026-07-26
Branch: `nightrunner-25th-JUL-26`
Plan: completed at 6/6; live TODO deleted under master-plan inventory rule 4
Closure commit: the commit containing this report
Status: **complete**

## Outcome

The Replay partition is complete. `Runtime/Replay` owns recorded history,
scrubbing, artifacts, and Replay-authored values. `Runtime/Prediction` owns
future-state computation, cause-tree composition over Replay values, retained
prediction presentation, and its working-set reserve. `Runtime/Planning` owns
operator-facing planning overlays and tools built above Replay and Prediction.
`Runtime/App` remains the composition root and retains no Prediction draw-list
or presentation state.

The work preserved behavior and the flagship Replay feature surface. It did
not use line budgets, frozen counts, spelling ratchets, compatibility aliases,
callback packs, forwarding facades, or deduplication as a substitute for
ownership.

## Final File Census

The RS0 baseline counted the three concerns before the package moves. The
final census uses the same tracked source-bearing extensions and records the
App composition surface separately because App was never part of the original
three-package denominator.

| Package or concern | Files before RS0 | Physical lines before RS0 | Nonblank lines before RS0 | Final files | Final physical lines | Final nonblank lines |
|---|---:|---:|---:|---:|---:|---:|
| Replay | 46 | 29,913 | 26,492 | 37 | 16,565 | 14,694 |
| Prediction | 18 | 10,338 | 9,154 | 24 | 12,783 | 11,279 |
| Planning | 8 | 1,598 | 1,376 | 15 | 4,378 | 3,849 |
| Three-package total | 72 | 41,849 | 37,022 | 76 | 33,726 | 29,822 |
| App Replay composition | not in baseline | not in baseline | not in baseline | 8 | 8,885 | 7,836 |

The final three-package total plus the separately reported App composition
surface is 84 files, 42,611 physical lines, and 37,658 nonblank lines.

## Ruled Seams And Composition

| Source owner | May consume | Must not own or consume upward |
|---|---|---|
| Replay | Replay and lower engine value seams | Prediction or Planning |
| Prediction | Replay values and Prediction | Planning |
| Planning | Replay, Prediction, and Planning | lower-package authority |
| App | every Runtime package needed for composition | retained Prediction presentation state |
| Core, Physics, Rendering, Scene, World | their standing lower dependencies | Replay, Prediction, or Planning |

The final ownership remediation made the boundaries concrete:

- `ReplayPrediction` now composes and activates prediction cause-tree rows.
- `ReplayPredictionPresentation` owns the retained prediction draw list,
  presentation state, packet, and counters.
- `ReplayProbeRunner`, an App debug harness, lives with `ReplayRuntime`.
- `ReplayFrameSelection` and `ReplayRenderFrameView` are App composition
  packets rather than hidden lower Replay contracts.
- Planning receives an explicit overlay packet carrying the selected
  Prediction value and availability.
- Replay-only cause-window input and layout implementations live in
  `ReplayAuthoringCauseTreeInput.cpp`; the Prediction translation unit contains
  only Prediction cause-tree composition.

Project and filter ownership includes the new Replay translation unit, and the
project-filter validator recognizes its owner prefix.

## Dependency Matrices And Proofs

The RS0 internal include matrix and the final source matrix are:

| Source package | Replay before | Prediction before | Planning before | Replay final | Prediction final | Planning final |
|---|---:|---:|---:|---:|---:|---:|
| Replay | 111 | 15 | 11 | 59 | 0 | 0 |
| Prediction | 20 | 24 | 0 | 28 | 39 | 0 |
| Planning | 0 | 2 | 5 | 10 | 4 | 20 |

The final external consumer matrix is:

| Runtime consumer | Replay | Prediction | Planning |
|---|---:|---:|---:|
| App | 39 | 5 | 4 |
| Automation | 7 | 4 | 2 |
| DevelopmentTools | 0 | 0 | 1 |
| Diagnostics | 1 | 0 | 0 |
| Editor | 1 | 0 | 0 |
| Input | 1 | 0 | 0 |
| Render | 1 | 0 | 2 |
| Startup | 1 | 0 | 0 |
| Tools | 3 | 0 | 0 |
| UI | 0 | 0 | 1 |

All plan closure proofs returned zero rows:

- Replay includes of Prediction or Planning;
- Prediction includes of Planning;
- lower Core, Physics, Rendering, Scene, or World includes of any replay-family
  package;
- Planning product vocabulary left in Replay or Prediction; and
- unruled lower headers exporting hidden Prediction or Planning contracts.

The authoritative checker reports 27 include rules, one project rule, and zero
repository findings. Its self-test passes 43 negative include fixtures plus
the project fixture. The matrix includes positive Prediction-to-Replay and
Planning-to-Prediction cases and explicit negative Replay-to-Prediction,
Replay-to-Planning, Prediction-to-Planning, and every lower-engine-to-family
case. All 21 human-readable Runtime and Replay-family mirror proofs return no
rows.

## Reserve Inventory

The partition retains exactly the three RS0-approved reserve registrations:

| Owner id | Package owner | Phase gate | Hard cap | Observed high-water | Exhaustion policy | Growth evidence |
|---|---|---|---:|---:|---|---|
| `replay_recorder_samples` | Replay | Replay | 32 MiB | 17,737,640 bytes | fatal retained-state diagnostic | logged counter |
| `replay_solver_snapshot` | Physics | Replay | 8 MiB | 2,877,186 bytes | fatal retained-state diagnostic | logged counter |
| `replay_prediction_working_set` | Prediction | Replay | 256 MiB | 110,979,828 bytes | cancel prediction build | logged counter |

`ReplayReserveInventory` aggregates these exact three owners. No registration,
cap, phase gate, counter coverage, or growth privilege was added or expanded.
The strict two-generation allocation gate and the repository allocation-policy
scan both pass.

## Comment Audit

Checklist: this report section.

The campaign base is `fd284b46`, the parent of RS0. The final touched-source
inventory contains 85 C++ source/header/shader files and four substantial
Python tools:

- `tools/check_allocation_policy.py`;
- `tools/check_dependency_graph.py`;
- `tools/check_related_paths.py`; and
- `tools/validate_project_filters.py`.

Checked: 89/89. Deferred: 0. Unchecked: 0.

Every C++ file has the required learning header, the four tools retain complete
learning headers, dense ownership and phase-sensitive code has the nearby
guide-required comments, and all repository-relative `Related:` entries
resolve. The final review also verified that moved cause-tree code names its
post-change owner and no stale App-retained-presentation claim remains.

## Independent Ownership Review

One independent whole-plan rubber-duck review inspected the final package
placement, exported contracts, App state, reserve owners, direction proofs,
project ownership, and touched comments.

| Finding | Disposition |
|---|---|
| Lower Replay headers exported hidden Prediction contracts | moved cause-tree composition to `ReplayPrediction`, App frame contracts to `ReplayRuntimePackets`, and the debug runner to App |
| App retained Prediction draw-list and presentation state | moved all retained presentation state, packet construction, and counters to `ReplayPredictionPresentation` |
| Replay-only cause-window implementations remained in a Prediction translation unit | moved all eleven definitions to `ReplayAuthoringCauseTreeInput.cpp` and registered the file in the project/filter |

The reviewer rechecked the final source and reported no blocker, missing
evidence, callback/backpointer, duplicated authority, hidden upward header
contract, or placement ambiguity.

## Validation

| Gate | Final result |
|---|---|
| `tools\validate_fast.bat` | pass; formatting, 564 `Related:` paths, 779 project/filter items, dependency graph, Profile and Debug builds |
| `tools\validate_project_filters.bat` | pass; project and filter ownership agree |
| direct dependency checker | pass; 27 include rules, one project rule, zero findings |
| direct allocation-policy checker | pass; 455 files scanned, allowlist errors zero |
| `tools\validate_replay_allocation_policy.bat` | pass; strict two-generation policy |
| all static partition proofs | pass; every governed search returns zero rows |
| `tools\validate_full.bat` | pass in 235.3 seconds; CPU/coverage and all five required runtime processes |
| `tools\run_graphics_stress.bat 1` | pass; bounded one-minute DX12 run stopped normally |
| sole RS5 replay visual-fidelity invocation | pass; 2,401 ticks and all positive/negative controls |
| `git diff --check` | pass |

RS5 moved scrub-facing code, so its one permitted visual invocation used
`tools\validate_replay_scrub.bat`, the repository alias for
`validate_replay_visual_fidelity.bat`. The command wrapper reached its time
limit while the sole engine process continued; that process exited normally.
No second engine or gate invocation was made. The non-engine postchecks were
then run against that completed generation and all passed: main fidelity,
negative control, incomplete horizon, cause activation/topology/segment,
semantic packet, artifact byte, prediction artifact, and every determinism
control (seed mismatch, missing tick, event mutation, non-fixed step,
truncated horizon, reorder, byte change, dropped geometry, reserve growth,
and duplicate generation).

The final fidelity report records 2,401 ticks, 200 moved wall bricks, 175
toppled wall bricks, 200 causal nodes, one presented cascade, 62 saved/loaded
ticks, first reveal at tick 0, and last reveal at tick 2,400.

No tracked golden, baseline, screenshot, replay artifact, scene, config,
shader, or physics CSV was refreshed. The refresh-candidate scan returned
zero paths.

## Plan History

- RS0 ratified the 72-file census, 188 internal include edges, 35 external
  include sites, 26 upward-edge resolutions, and three reserve owners.
- RS1 created `Runtime/Prediction` and moved all 18 prediction files.
- RS2 created `Runtime/Planning` and moved all eight planning files.
- RS3 reconciled composition seams and the reserve inventory.
- RS4 installed permanent dependency rules, fixtures, and mirror proofs.
- RS5 closed final hidden contracts and retained-state ownership, repeated the
  census, completed the comment audit, obtained independent review, and passed
  the mapped closure gates.

The active/future ledger was 7/25 immediately after RS5 completed. Inventory
rule 4 removes this completed six-task plan, leaving 1/19, or 5% rounded
overall. `downward-domain-bleed-remediation` DB0 is the next binding task.
