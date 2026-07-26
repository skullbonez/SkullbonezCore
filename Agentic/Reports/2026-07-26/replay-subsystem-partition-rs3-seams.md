# Replay Subsystem Partition RS3 — Composition And Shared Seams

Date: 2026-07-26
Branch: `nightrunner-25th-JUL-26`
Plan task: RS3

## Result

RS3 is complete. `Runtime/App` now composes the Replay, Prediction, and
Planning siblings. Replay no longer includes either upper sibling, Prediction
does not include Planning, and lower engine packages do not include any member
of the replay family.

The retained prediction presentation state is owned by
`ReplayPredictionPresentation` inside Prediction. App supplies synchronous
Replay path/camera values, and no callback pack, friend edge, `void*`, owner
backpointer, or retained sibling borrow was introduced.

Shared trajectory packets remain in Replay as lower value records. Planning
owns its overlay packets, layout, renderer, and retained runtime owner.
Application-only replay runtime, scrubber, validation, and aggregate packets
live in `Runtime/App`.

## Direction Proofs

The final source produced zero rows for:

- Replay to Prediction or Planning includes.
- Prediction to Planning includes.
- Core, Physics, Rendering, Scene, or World includes of
  `Runtime/(Replay|Prediction|Planning)/`.
- Added source-level `void*`, friend, backpointer, or callback-pack
  indirection.

The dependency validator passed its 27 include fixtures and one project
fixture with zero findings. Project/filter validation passed with 778 project
items and 778 filter items.

## Reserve Inventory

The App aggregate contains exactly the pre-partition three-owner set:

| Owner package | Registered owner | Phase | Hard cap | Measured high water | Exhaustion |
|---|---|---|---:|---:|---|
| Replay | `replay_recorder_samples` | Replay | 32 MiB | 17,737,640 bytes | Fatal retained state |
| Physics | `replay_solver_snapshot` | Replay | 8 MiB | 2,877,186 bytes | Fatal retained state |
| Prediction | `replay_prediction_working_set` | Replay | 256 MiB | 110,979,828 bytes | Cancel prediction build |

Each owner continues through the registered reserve allocator and its logged
growth counter. The strict two-generation gate reported the same owner set and
zero policy violations. The repository allocation scan reported
`scanned=454`, `direct_heap_findings=30`,
`dynamic_stl_member_findings=131`, `stl_growth_findings=648`, and
`allowlist_errors=0`.

## Comment Audit

Checklist: this report section.

- Checked: 62 touched, existing source-bearing files.
- Deferred: 0.
- Unchecked: none.

All checked files have the required learning-header fields. The five new files
that initially lacked a local glossary were corrected. Ownership, synchronous
borrow, reserve policy, and package-direction claims were checked against the
post-change source. `validate_fast` resolved all 563 source `Related:` paths
with zero findings.

## Validation

- `tools\validate_fast.bat` — PASS; Profile and Debug builds have zero
  warnings/errors.
- `tools\validate_replay_allocation_policy.bat` — PASS.
- `python tools\check_allocation_policy.py --repo .` — PASS.
- Visual-fidelity generation — PASS from the single permitted invocation.
  The external five-minute command wrapper timed out after the complete reveal
  image, while its sole engine process continued and exited normally with the
  6.4 MB report. The remaining CPU-only checks were completed directly against
  that same report; the 2,401-tick, 200-body oracle and every false-pass,
  artifact, prediction, and determinism control passed. No second engine
  process or prediction generation was started.
- `git diff --check` — PASS.

No golden, baseline, manifest, durable replay artifact, scene, config, shader,
or physics CSV was refreshed. Generated validation output remains ignored and
is not part of the change.
