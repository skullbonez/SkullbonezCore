# Replay Subsystem Partition RS1 — Prediction Extraction

Date: 2026-07-25

Branch: `nightrunner-25th-JUL-26`

Plan: `Agentic/Plans/TODO/replay-subsystem-partition.md`

## Outcome

RS1 creates `SkullbonezSource/Runtime/Prediction/` and moves the complete
RS0-ratified 18-file prediction inventory into it. No forwarding header,
compatibility alias, behavior change, artifact-format change, or baseline
refresh was introduced.

The moved inventory is:

- `ReplayPrediction.{h,cpp}`
- `ReplayPredictionArchive.{h,cpp}`
- `ReplayPredictionArchive.Automation.cpp`
- `ReplayPredictionDrawing.cpp`
- `ReplayPredictionPackets.h`
- `ReplayPredictionPublication.{h,cpp}`
- `ReplayPredictionPublicationOperations.h`
- `ReplayPredictionReserve.{h,cpp}`
- `ReplayPredictionScheduling.{h,cpp}`
- `ReplayPredictionTopologyPublication.cpp`
- `ReplayPredictionView.h`
- `TrajectoryStore.{h,cpp}`

All production, test, allocation-policy, coverage-floor, and Visual Studio
project/filter paths now name `Runtime/Prediction`. The project filter checker
classifies the package independently from Replay and proves exact
single-project ownership.

## Dependency Reconciliation

`ReplayPredictionArchive.cpp` and its Automation companion no longer rely on
the `ReplayRuntime.h` umbrella. They include only the prediction state and
Replay-owned path packet definitions they instantiate.

The physical move creates 17 temporary `Replay -> Prediction` includes across
13 Replay files. These are exactly the RS0-identified composition/value seams;
RS3 owns their split and removal. RS1 therefore records one bounded temporary
validator allowance. It is not a permanent direction decision: RS3 must remove
the allowance and prove zero Replay-to-Prediction edges.

The validator now also recognizes the new Prediction package and permits only
its ratified Runtime targets: Prediction, Replay, Editor, Scene, and Tools.
Automation's concrete Prediction consumer edge is recorded. No lower engine
layer includes Replay, Prediction, or Planning.

Both source-inventory checkers were made safe for in-progress moves. They scan
tracked plus untracked live source and ignore deleted worktree paths, so an
unstaged rename cannot crash validation or omit its destination file.

## Static Proofs

- Prediction directory inventory: 18 files.
- `rg -n 'Replay/ReplayPrediction|Replay/TrajectoryStore' SkullbonezSource`
  returns no rows.
- The same stale-path scan across tests, project files, and tools returns no
  rows.
- Lower-engine Replay/Prediction/Planning include proof returns no rows.
- Dependency validator: 26 include-rule fixtures, one project-rule fixture,
  zero findings.
- Project filters: 767 project items, 767 filter items, zero errors.
- Related paths: 552 source files, 1,458 repository paths, zero findings.

## Comment Audit

The touched-file comment audit inspected all 42 live source-bearing files in
the RS1 diff. Checked: 42. Deferred: 0. Unchecked: none.

All 18 moved files retain complete learning headers and now name their
`Runtime/Prediction` paths. Four stale replay-owner claims in
`ReplayPrediction.cpp`, `ReplayPredictionReserve.h`, and
`ReplayPredictionScheduling.cpp` were corrected to distinguish the Prediction
owner from the still-required Replay allocation phase. Existing Replay-file
ownership claims remained truthful because RS1 changes only their include
locations; the owner splits remain explicitly assigned to RS3. Every
repository-relative `Related:` path resolves.

## Validation

All RS1 and cumulative Runtime gates pass:

- focused Profile build: PASS, zero warnings/errors;
- `tools\validate_project_filters.bat`: PASS;
- `tools\validate_fast.bat`: PASS, Profile and Debug zero warnings/errors;
- `tools\validate_replay_allocation_policy.bat`: PASS, strict two-generation
  policy clean;
- `tools\validate_replay_v2_artifact.bat`: PASS;
- `tools\validate_replay_visual_fidelity.bat`: one authoritative engine
  invocation, PASS from its generated report and offline controls: 2,401
  ticks, 200 moved wall bricks, 175 toppled wall bricks, 200 causal nodes, one
  presented cascade, all negative/determinism controls detected their planted
  divergence;
- `tools\validate_full.bat`: PASS, mandatory CPU/coverage lanes, Automation,
  replay prediction smoke, DX12, and physics; the 44,401-line physics
  regression remained byte-exact.

The visual-fidelity wrapper outlived the invoking shell's observation timeout.
Its single engine process was monitored through completion; only the offline
checks were resumed against that process's report and artifact. No second
engine launch or prediction generation occurred.

No golden, baseline, manifest, replay artifact, scene, config, shader, or
physics CSV file is changed in the commit.

## Handoff

RS2 extracts the four planning feature pairs into `Runtime/Planning` and
updates their consumers/projects/filters without behavior or baseline changes.
RS3 then resolves the 17 temporary Replay-to-Prediction edges, splits sibling
composition, and removes the temporary validator allowance.
