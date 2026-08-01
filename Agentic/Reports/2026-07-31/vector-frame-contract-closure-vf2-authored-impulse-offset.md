# Vector Frame Contract Closure - VF2 Authored Impulse Offset

Date: 2026-08-01
Branch: `nightrunner-1st-AUG-26`
Impact area: Scene authoring and initial Physics impulse setup
Phase: VF2 complete

## Outcome

The authored ball schema now names its application lever arm
`impulseWorldOffsetFromCenter`. The same explicit vocabulary crosses the parsed
`SceneBall` value and authored/generated setup before reaching Physics'
`SetPendingBodyImpulse` world-offset contract. Scene schema v4 owns the new
spelling and the snapshot writer emits v4. Runtime parsing is version-gated:
v1-v3 accept only historical `forcePosition`, while v4 accepts only the current
key. The cold migration tool performs the deterministic rename and rejects a
future key under a legacy stamp; there is no silent runtime alias.

All 56 values across 23 committed scenes migrated mechanically. The sole
semantic correction is `ragdoll_playground`'s `wake_ball`: its former value was
identical to the body's absolute world position `(515, 28, 492)`. Converting
that application point into a center-relative offset yields `(0, 0, 0)`, so the
wake impulse acts through the ball's center instead of manufacturing an
enormous lever arm.

The default migration census is deliberately bounded to scenes that need the
legacy quaternion step or carry an impulse-offset key. Therefore the 23
unrelated v3 orientation scenes—including Replay provenance-sensitive
`prediction_ragdoll_wall_200`—retain their original stamps.

## Focused Oracles

Three focused Profile cases prove the authoring contract:

- `cardinal_roll_test` retains a nonzero world-axis lever arm `(10, 0, 0)` and
  the expected `-500` Z impulse, proving the new JSON name reaches the parsed
  impulse record; and
- `ragdoll_playground` loads `wake_ball` with its `120` Z impulse and a zero
  center-relative offset, rejecting the previous absolute-position outlier;
- versions 1, 2, and 3 accept the historical key, current v4 accepts the new
  key, and both incompatible version/key pairings fail; and
- the production authored-setup seam queues `wake_ball` into a real
  `PhysicsEngine`, where `PhysicsBodyStore` exposes the expected `(0, 0, 120)`
  pending world impulse and `(0, 0, 0)` world-center offset.

The focused selections pass 3 cases / 90 assertions. The snapshot-writer
selection passes 1 case / 500 assertions and pins schema v4 output. Committed
scene data has zero historical-key occurrences; the old spelling remains only
in version-gated parser diagnostics/tests and the named cold migration. The new
key occurs exactly 56 times across the same 23 changed scene files.

## Artifact Result

VF0 predicted zero committed movement because `ragdoll_playground` is not a
mapped Physics artifact scene. VF2 confirms that prediction:

- core Physics passes both varied runs against the committed 44,401-line CSV;
- deep varied, bullet wall/object/terrain, shooting reaction, and three-body
  CSVs remain exact;
- known-issue signatures and Physics query output remain exact; and
- no golden or baseline file was regenerated.

Physics does **not** need a new baseline.

## Complexity Ownership Review

VF2 invalidated the exact complexity digests for
`SceneAuthoredSetup::SetUpSceneEntities` and `SceneSnapshotWriter::Save`.
Full-body review confirms both existing retain-owner rulings. Setup remains one
atomic scene-load transaction that constructs the complete entity topology and
automation gates before publication; its extracted inline seam owns only the
ball impulse handoff and is independently testable. Snapshot save remains the
single deterministic serialization transaction for one scene document, now
with its current schema-v4 stamp. Both current digests are recorded in
`tools/function_complexity_rulings.json`; the strict inventory passes all 40
triggered bodies.

## Comment Audit

Checklist path: this report.

- [x] `SkullbonezSource/Scene/AuthoredScene.h`
- [x] `SkullbonezSource/Scene/AuthoredSceneParser.cpp`
- [x] `SkullbonezSource/Scene/AuthoredSceneParserBodies.cpp`
- [x] `SkullbonezSource/Scene/SceneSnapshotWriter.cpp`
- [x] `SkullbonezSource/Scene/SceneSnapshotWriter.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.InitialImpulse.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`
- [x] `SkullbonezTests/TestSceneAuthoredImpulseSetup.cpp`
- [x] `SkullbonezTests/TestSceneParserUnit.cpp`
- [x] `SkullbonezTests/TestSceneSnapshotWriter.cpp`
- [x] `tools/migrate_data_formats.py`

Checked: 12. Deferred: 0. Unchecked: none.

The stored member documents its world-axis, center-relative meaning; the
parser/setup names carry that contract without an explanatory alias; the
schema/migration comments state the version boundary; and the tests record the
absolute-position prohibition and handoff ownership. All learning headers are
ownership-bearing, every `Related:` path resolves, and the strict glossary
inventory is green.

## Validation

- Focused Profile parser/handoff oracles: 3/3 cases, 90/90 assertions, pass.
- Focused snapshot-writer selection: 1/1 case, 500/500 assertions, pass.
- Migration self-test and default 62-file `--check`: pass.
- Dedicated scene-parser gate: pass.
- Formatting, project filters, dependency graph, and all ownership inventories:
  pass.
- Function complexity: 40/40 triggered bodies have current rulings.
- Profile, Debug, and Automation builds: pass.
- Unit suite: 460/460 cases and 2,423,067/2,423,067 assertions, pass.
- Strict compiled-symbol reachability: pass after refreshing the Automation
  objects for the touched source files.
- `validate_physics`: pass with exact committed comparisons.
- `validate_physics_deep`: pass with every mapped artifact exact.

The initial pre-review fast run failed closed at the expected edited-body
digest. After review and digest refresh, its next run passed all code, build,
and unit stages but rejected stale Automation object provenance. Rebuilding
Automation made the direct strict reachability stage pass. The final
post-remediation complete fast rerun passed all nine stages in 362.5 seconds
and is the phase acceptance command.

No baseline refresh or owner decision is required for VF2.
