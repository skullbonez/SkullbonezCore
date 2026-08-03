# Comment Vocabulary Audit CV3 — Summary Honesty And Related Durability

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Plan phase: CV3 of `Agentic/Plans/TODO/comment-vocabulary-audit.md`
Impact: comments and documentation only

## Result

The complete tracked scope is 587 source files (260 `.cpp`, 327 `.h`). Every
file has a non-empty `Purpose:`, `Summary:`, `Invariants:`, and `Related:`
learning-header field. Every Summary was read against the ownership, decision,
or flow rule. Forty-three filename/Purpose restatements were repaired, and the
post-repair census contains zero exact Purpose/Summary copies.

The tracked inspection ledger is
`Agentic/Plans/TODO/comment-vocabulary-summary-related-checklist.md`: 587/587
checked, zero deferred, zero missing, zero extra, and zero duplicate rows.

## Summary Findings

The 43 definite failures were distributed as follows:

| Subsystem | Repaired files |
|---|---:|
| Assets | 4 |
| Core | 6 |
| Maths | 3 |
| Physics | 7 |
| Rendering | 8 |
| Runtime | 9 |
| Scene | 1 |
| UI | 3 |
| World | 2 |
| **Total** | **43** |

The repairs name the concrete owner and boundary rather than restating the
filename. Examples include AssetSystem's identity/source-record ownership,
PhysicsWorld's stores and stages, RenderGraph's declaration/derivation/backend
split, Run's composition-root phase order, and UI tabs' detached-value command
contract.

Two additional claims failed current-source verification:

- `Runtime/Input/InputController.cpp` described retained compatibility state
  and future migration. Current declarations and methods prove that
  InputController is stateless policy over InputRouter and CameraControlState.
- `Runtime/Scene/SceneRequestQueue.h` described an unfinished ownership move.
  Current execution proves that SceneController owns and drains the ring while
  SceneLoadTransaction owns the load-phase order.

SleepIslandSystem's Purpose and Summary also overstated its responsibility.
The implementation performs bounded support propagation; it neither groups
islands nor chooses sleep transitions. Both source/header claims now state that
actual boundary.

All 20 remaining `Mental model:` blocks were folded into their file Summary,
as CV1 required. The retained paragraphs continue to explain phase order or
ownership, but no second file-orientation heading remains. The current tree has
zero `Mental model:`, `LAYMAN VERSION:`, generic `Allocation policy:`, or
generic `Contract:` headings.

## Related Census

The authoritative resolver scanned 2,036 repository-relative Related entries
with zero findings. Resolving local shorthand to its actual destination gives:

| Destination | Entries |
|---|---:|
| `SkullbonezSource/` | 1,102 |
| `Agentic/Reference/` | 778 |
| `Agentic/Reports/` | 114 |
| tests | 20 |
| `tools/` | 14 |
| other repository files | 7 |
| other `Agentic/` documentation | 1 |
| **Total** | **2,036** |

There are 893 Agentic citations in total. The 114 report citations occur in 103
source files and resolve to 38 permanent dated reports; no Related entry points
to `Agentic/Plans/TODO/`. A file contains at most four report citations.
`runtime-shell-final-ownership-review.md` is the most central permanent report
with 35 inbound source links.

Two duplicate navigation rows were removed:

- `Runtime/Direction/DemoDirectorPlayback.h` repeated
  `CameraControlState.h`.
- `Runtime/Replay/ReplayArtifactSource.h` repeated
  `ReplayV2Artifact.cpp`.

The largest Related blocks contain eight entries:

- `Runtime/Prediction/ReplayPrediction.cpp` links its five split implementation
  owners and the three shared references.
- `Rendering/DX12/RenderBackendDX12.h` links five concrete DX12 companions and
  the three shared references.
- `Physics/PersistentContactSolver.cpp` links its header, four permanent
  decision reports, and three shared references.

Each remains focused navigation for one logical owner; none has become an
index.

## Long Comment Review

A qualitative scan of long body-comment blocks surfaced the deliberate local
mini-articles in `RenderDeviceDX12.h`, `RenderBackendDX12.cpp`, and
`RenderGraph.h`. The longest explain descriptor allocation, upload lifetimes,
the DX12 architecture flow, and render-graph resource transitions beside the
APIs whose hazards they govern. They are not navigation indexes and were
retained under CV1's ruling for precise domain explanation. This inventory is a
review aid, not a count threshold or comment budget.

## Report-Link Risk Ruling

Report-link volume is a managed maintenance surface, not an active systemic
failure. The dated report paths are permanent, all 114 links resolve, no live
plan is cited, no file has an index-sized report list, and `validate_fast`
already runs the fail-closed Related resolver. The 35 links to the central
runtime ownership report make that file intentionally immovable, but do not
justify a new count budget or mass rewrite.

The continuing strategy is:

1. cite source or a maintained Reference page for current mechanics;
2. cite one dated report when a durable decision or closure proof matters;
3. never cite deletion-bound TODO plans from source; and
4. keep the existing path resolver authoritative for rename/deletion failures.

No new enforcement or migration is proposed.

## Touched-Source Audit And Validation

Sixty-seven source files changed: 43 Summary repairs, 20 Mental-model folds,
two verified stale-claim repairs, and two duplicate Related-row removals. The
repository comment-audit checklist was applied to every touched file. No term
needs human-approved wording and no Glossary definition moved.

- touched-source comment audit: 67/67, zero deferred;
- comment-stripped comparison against `HEAD`: 67 files, zero code-token
  mismatches;
- tracked ledger reconciliation: 587 files, 587 checked rows, zero missing,
  extra, duplicate, or unchecked rows;
- `python tools/check_related_paths.py --repo .`: 587 files, 2,036 paths, zero
  findings;
- `git diff --check`: pass.

No repository validation was run because the phase is comments and
documentation only. `tools\validate_fast.bat` remains the CV4 closure gate.

## Complete Restatement Repair Scope

- Assets: `AssetSystem.cpp`, `AssetSystem.h`, `TextureCollection.cpp`,
  `TextureCollection.h`.
- Core: `Config.cpp`, `Config.h`, `Log.cpp`, `PlatformProfiler.cpp`,
  `PlatformProfiler.h`, `Timer.cpp`.
- Maths: `Matrix4.cpp`, `Quaternion.cpp`, `RotationMatrix.cpp`.
- Physics: `BoundingBox.cpp`, `BoundingSphere.cpp`,
  `ContactSolverCommon.h`, `PhysicsDiagnosticsSink.cpp`, `PhysicsWorld.h`,
  `SleepIslandSystem.cpp`, `SleepIslandSystem.h`.
- Rendering: `BLASDX12.cpp`, `FramebufferDX12.cpp`, `SBTDX12.cpp`,
  `TLASDX12.cpp`, `RenderGraph.cpp`, `RenderInstanceRenderer.h`, `Shadow.h`,
  `Text.cpp`.
- Runtime: `Run.cpp`, `Camera.cpp`, `CameraCollection.cpp`,
  `BroadphaseVisualizer.cpp`, `CollisionVisualizer.cpp`,
  `PhysicsDebugVisualizer.h`, `EditorInteractionTools.cpp`,
  `LauncherTools.cpp`, `RuntimePickService.h`.
- Scene: `AuthoredScene.cpp`.
- UI: `UITabCinematic.cpp`, `UITabCinematic.h`, `UITabSky.cpp`.
- World: `SkyBox.cpp`, `WorldEnvironment.cpp`.
