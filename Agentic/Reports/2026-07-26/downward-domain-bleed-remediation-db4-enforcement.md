# Downward Domain Bleed Remediation DB4 Enforcement

Date: 2026-07-26

Branch: `nightrunner-25th-JUL-26`

Plan task: DB4 of 6

## Outcome

DB4 installs permanent mechanical enforcement for the two boundaries that can
be expressed without a frozen count or general spelling budget:

- Physics source cannot include Assets, Gameplay, Scene, World, Runtime, or UI.
- Rendering source cannot reintroduce the exact retired
  `RetainedTrajectory` or `RETAINED_TRAJECTORY` concept names.

`AGENTS.md` now carries the broader qualitative rules that do not belong in a
spelling checker: Rendering contracts remain feature-neutral, and new hot
physics record fields require an owner ruling that names the consuming stage
and explains why a stage-owned fixed-capacity parallel store is insufficient.

## Validator Contract

`tools/check_dependency_graph.py` remains a generic data-driven checker. DB4
adds a bounded content-rule evaluator rather than a feature-specific branch:

- `source_prefixes` bounds the inspected package.
- `forbidden_literals` names exact deleted concepts.
- one positive snippet proves generic vocabulary remains legal.
- one negative snippet per literal proves every tombstone is live.
- the same evaluator scans the temporary fixtures and repository source.

The rule data in `tools/dependency_graph_rules.json` now contains:

| Rule | Positive fixture | Negative fixtures |
|---|---|---|
| `physics_direction` | `Physics/Fixture.cpp` includes `Core/Common.h` | Assets, Scene, Runtime, and exact relative `#include "../World/Terrain.h"` edges |
| `rendering_retired_trajectory_vocabulary` | `struct RetainedGeometryStream {};` | planted `struct RetainedTrajectory {};` and `RETAINED_TRAJECTORY` constant |

The exact literals are deletion tombstones for the removed B1 API. They are not
a count budget and do not reject unrelated words by frequency. The standing
zero-row review proof remains responsible for renamed feature vocabulary.

## Standing Rules And Proofs

`AGENTS.md` now:

- names Assets, Scene, and World in the Physics dependency sentence and proof;
- separates the Physics and Rendering include proofs so their allowed lower
  seams remain explicit;
- records Rendering feature neutrality and the exact retired-name deletion
  proof;
- records the hot-record owner-ruling requirement without freezing field count.

All DB4 static closure commands returned no rows:

```text
PASS: Physics upward includes returned no rows
PASS: Physics World terrain type returned no rows
PASS: Rendering feature vocabulary returned no rows
PASS: Rendering retired trajectory symbols returned no rows
```

## Project Ownership And Formatter Reconciliation

The first fast-gate attempt exposed older DB1-DB3 integration debt rather than
a DB4 rule failure:

- 19 previously changed source files were not in repository formatter form.
- `PhysicsTerrainView` and `TerrainSupportClassifier` were duplicated in the
  app and Physics projects.
- the project-filter classifier still treated
  `TerrainSupportClassifier` as World-owned and had no rows for
  `PhysicsTerrainView` or `ReplayPredictionRetainedGeometry`.

The repository formatter produced 22 tracked C++ diffs, all limited to
canonical layout, using-order normalization, and namespace-end comments.
Project metadata now assigns the two Physics headers only to
`SKULLBONEZ_PHYSICS.vcxproj`; the classifier places the terrain boundary under
Physics collision and retained trajectory storage under Runtime/Prediction.
The final project-filter scan reports 782 project items, 782 filter items, and
zero errors across four production projects.

## Validation

Direct validator:

```text
SELF_TEST_PASS: 27 include rules with 46 negative edge fixtures and 1 content
rules with 2 negative content fixtures and 1 project-rule fixtures passed
Dependency graph summary: include_rules=27 content_rules=1 project_rules=1
findings=0
PASS: dependency graph and project ownership are valid.
```

Final `tools\validate_fast.bat`:

```text
PASS: All source files correctly formatted.
PASS: Project filter validation passed.
PASS: dependency graph and project ownership are valid.
Build succeeded. 0 Warning(s), 0 Error(s).
VALIDATE_FAST: ALL PASSED
```

Final `tools\validate_all_cpu_tests.bat`:

```text
validate_tests.bat                       PASS
validate_coverage.bat                    PASS
validate_runtime_interaction_policy.bat  PASS
validate_scene_parser_tests.bat          PASS
validate_ui_boundary_tests.bat           PASS
validate_dx12_arch_tests.bat             PASS
VALIDATE_ALL_CPU_TESTS: ALL PASSED
```

The doctest lane passed 393/393 cases and 2,403,315/2,403,315 assertions. Every
ratified subsystem coverage floor passed; whole instrumented product coverage
was 20,060/27,639 lines (72.58%).

The cumulative `tools\validate_full.bat` gate also passed on the final tree:
mandatory CPU/coverage preflight, Automation boundary, replay/prediction smoke,
DX12 renderer validation with zero InfoQueue errors and accepted committed
baselines, physics lifecycle/runtime-handle smoke, and the byte-exact
44,401-line physics regression comparison.

No baseline, golden, replay artifact, screenshot, shader, scene, config, or
physics CSV reference was refreshed.

## Comment Audit

The touched-file audit inventory contains 24 source-bearing files: 22 formatted
C++ files plus `tools/check_dependency_graph.py` and
`tools/validate_project_filters.py`.

- Checked: 24
- Deferred: 0
- Unchecked: none

Every file has `File`, `Purpose`, `Summary` or `Mental model`, `Glossary`, and
`Related` sections. Existing ownership, stage-order, terrain lifetime,
prediction-retention, and determinism claims remain true after DB4. The checker
header and nearby comments now explain retired-vocabulary tombstones, their
bounded purpose, and why they are not a general word census.
