# Replay Subsystem Partition RS2 — Planning Extraction

Date: 2026-07-25

Branch: `nightrunner-25th-JUL-26`

Plan: `Agentic/Plans/TODO/replay-subsystem-partition.md`

## Outcome

RS2 creates `SkullbonezSource/Runtime/Planning/` and moves the complete
RS0-ratified eight-file planning inventory into it:

- `ReplayGuideArcs.{h,cpp}`
- `ReplayInterceptReadout.{h,cpp}`
- `ReplayPorkchopPanel.{h,cpp}`
- `ReplayTripPlanner.{h,cpp}`

Every live source, test, app-project, test-project, and filter path now names
`Runtime/Planning`. No forwarding header, compatibility alias, behavior
change, or baseline refresh was introduced.

The project filter checker recognizes Planning as its own semantic package.
Both the production set (767 items) and the test project (110 items) have
exact filter ownership with zero errors.

## Dependency Reconciliation

Planning is registered as an upper Runtime package that may include Planning,
Prediction, and Replay. The moved implementation currently needs only Planning
and Prediction. Prediction has zero Planning includes.

The physical move creates 11 temporary `Replay -> Planning` includes across
four Replay files:

- `ReplayCoordination.h`
- `ReplayOverlayLayout.h`
- `ReplayOverlayPackets.h`
- `ReplayRuntime.h`

These are the RS0-identified composition and packet/layout seams. RS3 owns
their split and removal together with the 17 temporary Replay-to-Prediction
edges. The dependency validator temporarily permits Replay to reach Planning
only so the move-only slices remain buildable; RS3 must delete that allowance
and prove the permanent direction.

## Static Proofs

- Planning directory inventory: eight files.
- No stale `Runtime/Replay/ReplayGuideArcs`,
  `ReplayInterceptReadout`, `ReplayPorkchopPanel`, or `ReplayTripPlanner` path
  remains in live source, tests, projects, filters, or tools.
- `Runtime/Prediction` has zero Planning includes.
- Dependency validator: 27 include-rule fixtures, one project-rule fixture,
  zero findings.
- Production filters: 767 items, zero errors.
- Test filters: 110 items, zero errors.
- Related paths: 552 source files, 1,458 repository paths, zero findings.

## Comment Audit

The touched-file comment audit inspected all 17 live source-bearing files in
the RS2 diff. Checked: 17. Deferred: 0. Unchecked: none.

All eight moved files retain complete learning headers and now name their
`Runtime/Planning` paths. Their ownership and sequencing claims remain
truthful: Planning stores bounded product state, consumes detached Prediction
views, and is still composed by `ReplayRuntime` until the RS3 sibling-owner
split. Existing Replay headers changed include locations only. Every
repository-relative `Related:` path resolves.

## Validation

All RS2 and cumulative Runtime gates pass:

- focused Profile build: PASS, zero warnings/errors;
- production and test project/filter validation: PASS;
- `tools\validate_fast.bat`: PASS, Profile and Debug zero warnings/errors;
- `tools\validate_replay_visual_fidelity.bat`: exactly one engine process and
  one prediction generation, PASS: 2,401 ticks, 200 moved wall bricks, 175
  toppled wall bricks, 200 causal nodes, one presented cascade, and every
  negative/determinism control detected its planted divergence;
- `tools\validate_full.bat`: PASS, mandatory CPU/coverage lanes, Automation,
  replay prediction smoke, DX12, and physics; the 44,401-line physics
  regression remained byte-exact.

The first focused build exposed four stale implementation entries in the test
project. They were updated to Planning with a matching test filter before the
successful focused and formal gates.

No golden, baseline, manifest, replay artifact, scene, config, shader, or
physics CSV file is changed in the commit.

## Handoff

RS3 reconciles the sibling Replay, Prediction, and Planning composition;
splits the shared packet/layout seams; removes all 28 temporary upward
includes; and re-homes the prediction reserve policy inventory without
changing its owner, phase gate, cap, counters, or exhaustion behavior.
