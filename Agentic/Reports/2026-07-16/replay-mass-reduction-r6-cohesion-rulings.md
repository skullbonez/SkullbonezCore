# Replay Mass Reduction R6 — Oversized Translation-Unit Cohesion Rulings

Date: 2026-07-16
Owner: replay
Task: `replay-mass-reduction` R6

## Outcome

R6 makes no source, project, filter, build, or ownership change. The post-R4b
dependency review found no honest moves-only partition among the four replay
translation units near or above the plan's approximate 2,000-line review
threshold. Each remaining file is one cohesive concern; splitting any of them
would require a new cross-TU internal API, duplicated byte-contract constants,
or a cosmetic include fragment.

No `.inl` file, forwarding facade, broad context structure, friend boundary,
callback pack, or preprocessor partition was introduced.

## Physical-Line Census And Rulings

The inventory uses tracked files and physical lines, including blanks and
comments.

| Translation unit | Lines | Ruling | Cohesion evidence |
|---|---:|---|---|
| `ReplayPrediction.cpp` | 4,424 | KEEP cohesive | One prediction state machine owns worker start/cancel/complete, prefix publication, final future-node rebuild, trajectory publication, retained baseline, reveal clock, and offline state restoration. Completion and presentation paths directly sequence cache clear/rebuild and trajectory validity against the same `RunReplayPredictionState`. Moving a stage would require exposing anonymous-namespace operations and mutable state through a new internal header. |
| `ReplayRecorder.cpp` | 3,488 | KEEP cohesive | Presentation, solver, event, checkpoint, delta, and hash operations share the recorder's chronological/ring traversal rules and field-order/hash helpers. A physical type split would either duplicate the encoding/hash contract or publish a new cross-TU helper ABI. The existing public recorder types are already the owner boundary. |
| `ReplayV2Artifact.cpp` | 2,913 | KEEP cohesive | One v2-v4 chunked byte ABI owns constants, POD cursor rules, chunk builders/parsers, manifest facts, table validation, and supported-version migration. Read/write splitting would move shared cursor/layout helpers into a new internal surface or duplicate the byte contract. R4b's canonical RVIS writer remains part of that same codec. |
| `ReplayPredictionDrawing.cpp` | 2,061 | KEEP cohesive | The file is only 61 lines above the review guide and implements one ordered 3D prediction ribbon/marker/ghost emission pass. Its loops share quota arithmetic, styles, trajectory visibility, and submission order. A split would create a tiny physical partition without a distinct owner or independent invariant. |

## Why No Move-Only Split Is Honest

R6 permits moves only. It does not authorize logic edits, new internal
interfaces, or authority changes. All plausible seams require at least one of
those forbidden changes:

- prediction stages need shared mutable build/cache/trajectory state and direct
  calls across worker completion and frame-thread presentation;
- recorder categories need the same ordered field/hash and retained-history
  helpers;
- artifact read/write paths need the same byte sizes, cursor rules, chunk IDs,
  and migration interval;
- drawing sub-loops are ordered phases of the same render submission.

A mechanical `.inl` extraction would reduce a file counter without changing
compilation, ownership, coupling, or product mass and is therefore rejected.

## Validation

- `tools\validate_tests.bat`: 2.22 s; 202/202 cases and 12,595/12,595
  assertions passed.
- The single R6 `tools\validate_replay_visual_fidelity.bat` invocation:
  431.54 s; one engine process, one prediction generation, 2,401 ticks,
  200 moved bricks, 187 toppled bricks, 199 causal nodes, and all false-pass
  controls passed.
- No golden, baseline, artifact schema, provenance, source, project, or build
  file changed.

Comment audit is not applicable: R6 is documentation-only and touches no
source-bearing file.
