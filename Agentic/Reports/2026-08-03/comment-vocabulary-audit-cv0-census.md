# Comment Vocabulary And Banner Convention Audit — CV0 Census

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Status: CV0 complete; rulings remain for CV1
Impact area: tracked source comments and comment-style governance

## Outcome

The current tracked inventory contains 587 source-bearing files under
`SkullbonezSource/`: 260 `.cpp` and 327 `.h`. Every file has the exact modern
`File` / `Purpose` / `Summary` / `Invariants` / `Related` learning-header
spelling. The useful structured local tags are widespread and consistent.

The competing strata are broader than the plan's seed counts:

- six plain-language/orientation spellings produce 34 labelled comments across
  29 files;
- all 35 legacy-banner files also carry the modern header, so every banner is a
  duplicate presentation stratum even when its content may still be unique;
- the 58 physics citation markers remain concentrated in three files and are
  absent from the style guide;
- `Lane R`, `Lane F`, and `Lane P`, plus recurring `Docs:`, allocation-policy,
  and contract headings, are repeatable conventions not described by the guide.

CV0 changes no comment or source. CV1 owns the rulings.

## Method

The authoritative file set came from `git ls-files SkullbonezSource`, filtered
to `.cpp`, `.h`, `.hpp`, `.inl`, and `.hlsl`. The current tree contains only
the first two suffixes in that scope. Explicit tracked paths were then read so
ignored directory names could not hide tracked files. Counts distinguish
label occurrences from files containing the label; subsystem is the first path
component below `SkullbonezSource/`.

The inventory is current structure, not a budget or ratchet.

## Tracked File Distribution

| Subsystem | Files |
|---|---:|
| Assets | 5 |
| Core | 41 |
| Gameplay | 6 |
| Maths | 15 |
| Physics | 76 |
| Rendering | 75 |
| Runtime | 279 |
| Scene | 13 |
| UI | 70 |
| World | 7 |
| **Total** | **587** |

## Modern Header And Standard Local Tags

| Exact label | Occurrences | Files | Guide status |
|---|---:|---:|---|
| `File:` | 587 | 587 | Documented, required |
| `Purpose:` | 587 | 587 | Documented, required |
| `Summary:` | 587 | 587 | Documented, required |
| `Glossary:` | 416 | 416 | Documented, conditional |
| `Invariants:` | 587 | 587 | Documented, conditional; present everywhere |
| `Related:` | 587 | 587 | Documented, conditional; present everywhere |
| `Invariant:` | 586 | 234 | Documented |
| `Why:` | 400 | 160 | Documented |
| `Concept:` | 325 | 160 | Documented |
| `Lifetime:` | 265 | 147 | Documented |
| `Hazard:` | 112 | 75 | Documented |

The local tags span every relevant subsystem rather than forming an isolated
style pocket. Runtime carries the largest raw share because it contains 279 of
587 files. No alternative spelling competes with the five standard local tags.

## Plain-Language And Orientation Family

| Exact label | Occurrences | Files | Subsystems | Job overlap |
|---|---:|---:|---|---|
| `Mental model:` | 21 | 21 | Runtime 19, UI 2 | Pre-detail orientation; overlaps `Concept:` |
| `LAYMAN VERSION:` | 2 | 2 | Physics 2 | Plain-language restatement; overlaps `Concept:` |
| `Layman version:` | 4 | 4 | Physics 2, Runtime 2 | Same job, different case |
| `Layman physics map:` | 1 | 1 | World 1 | Physics-specific orientation |
| `Plain-language version:` | 4 | 1 | Rendering 1 | Local API explanation |
| `Plain-language rule:` | 2 | 1 | Rendering 1 | Local caller/lifetime rule |

The 29 files are:

- `Physics/CollisionShape.h`, `Physics/ObjectContactManifold.cpp`,
  `Physics/ObjectContactManifold.h`, and `Physics/SpatialGrid.h`;
- `Rendering/DX12/RenderDeviceDX12.h`;
- `Runtime/App/ReplayRuntime.h`, `Runtime/App/Run.h`, and
  `Runtime/App/RunFrame.cpp`;
- `Runtime/Automation/InteractionAutomationController.cpp/.h`,
  `InteractionAutomationInputDriver.cpp/.h`, and
  `InteractionAutomationReportWriter.cpp/.h`;
- `Runtime/Capture/RuntimeStressController.h`;
- `Runtime/Debug/BroadphaseVisualizer.h` and `CollisionVisualizer.h`;
- `Runtime/DevelopmentTools/ImGuiEditorOwner.cpp/.h`;
- `Runtime/Interaction/OperatorCommandTransaction.cpp/.h`;
- `Runtime/Render/UiDrawSubmission.cpp`;
- `Runtime/Scene/SceneLoadTransaction.h` and `SceneWorld.h`;
- `Runtime/Tools/RuntimeTools.h`;
- `Runtime/UI/OperatorEditorFrameComposer.cpp`;
- `UI/UIFontMetrics.cpp/.h`; and
- `World/Terrain.h`.

None of the six spellings is documented in the current guide. CV1 must decide
which are aliases of `Concept:` and whether `Mental model:` has a distinct
file-level orientation job worth retaining.

## Physics Citation Family

| File | `CATTO REF` | `ENGINE-SPECIFIC` |
|---|---:|---:|
| `Physics/ObjectContactManifold.cpp` | 5 | 26 |
| `Physics/ObjectContactManifold.h` | 1 | 1 |
| `Physics/PersistentContactSolver.cpp` | 17 | 8 |
| **Total** | **23** | **35** |

`CATTO REF` identifies the cited Catto algorithm/equation being implemented;
`ENGINE-SPECIFIC` identifies the local policy or geometry decision that is not
claimed by that citation. They are paired but non-duplicate jobs. Neither label
appears in the style guide.

## Execution And Proof Lanes

Literal counting, including compound labels such as `Lane F / Hazard`, gives:

| Marker | Occurrences | Files | Subsystems |
|---|---:|---:|---|
| `Lane R` | 127 | 75 | Assets, Core, Physics, Rendering, Runtime, Scene, UI, World |
| `Lane F` | 73 | 44 | Core, Gameplay, Physics, Rendering, Runtime, Scene |
| `Lane P` | 2 | 2 | Runtime |

These markers describe regular/result, failure/fatal, and probe lanes in local
multi-lane explanations. The exact expansion and when to use the markers is not
documented. They do not duplicate `Why:` or `Hazard:` mechanically, although a
compound `Lane F / Hazard` can carry both roles.

## Other Repeated Custom Headings

These exact labels recur as named headings outside the documented standard.
They are listed separately from hundreds of one-off domain/data labels, which
are local content names rather than repository conventions.

| Exact label | Occurrences | Files | Subsystems |
|---|---:|---:|---|
| `Docs:` | 59 | 11 | Rendering |
| `Runtime allocation policy:` | 34 | 24 | Core, Gameplay, Physics, Rendering, Runtime, World |
| `Pass contract:` | 14 | 2 | Rendering, Runtime |
| `Compatibility:` | 10 | 7 | Core, Runtime, Scene |
| `Capability:` | 7 | 3 | Rendering, Runtime |
| `Allocation policy:` | 6 | 6 | Rendering, Runtime |
| `Units:` | 6 | 4 | Gameplay, Runtime, Scene |
| `Cold boundary:` | 4 | 3 | Physics |
| `Fallback:` | 4 | 3 | Maths, Runtime |
| `Owner:` | 4 | 3 | Runtime |
| `Contract:` | 3 | 3 | Rendering, Runtime |
| `Phase:` | 3 | 2 | Runtime |
| `Caller contract:` | 2 | 2 | Core, Physics |
| `Precondition:` | 2 | 2 | Maths, Runtime |
| `Release/Profile:` | 2 | 2 | Maths, Physics |
| `Terminal drain:` | 2 | 2 | Rendering |

`Docs:` is a rendering-local link convention; the policy/contract labels name
real distinctions but compete in granularity. CV1 should avoid flattening
useful domain headings merely because they use colons, while documenting or
consolidating the labels intended as reusable comment syntax.

## Legacy Banner Stratum

Every file below matches the pre-standard `/* -- Name ---- */` opening style
and also contains all five modern core header fields. `Modern header` is
therefore `yes` for all 35; CV2 must still inspect whether each banner contains
unique information before removing or merging it.

| Subsystem | Banner files | Modern header |
|---|---|---|
| Core | `Log.h`, `Profiler.h`, `Timer.h` | yes (3/3) |
| Maths | `GeometricMath.h`, `GeometricStructures.h`, `Matrix4.h`, `Quaternion.h`, `RotationMatrix.h`, `Vector3.h` | yes (6/6) |
| Physics | `BoundingBox.h`, `BoundingSphere.h`, `CollisionShape.h`, `SpatialGrid.h` | yes (4/4) |
| Rendering | `DX12/BLASDX12.h`, `DX12/FramebufferDX12.h`, `DX12/MeshDX12.h`, `DX12/RenderDeviceDX12.h`, `DX12/SBTDX12.h`, `DX12/ShaderDX12.h`, `DX12/TLASDX12.h`, `PrimitiveBatchRenderer.h`, `PrimitiveMeshBuilder.h`, `RenderGraph.h`, `Text.h` | yes (11/11) |
| Runtime | `App/Run.h`, `App/Window.h`, `Camera/CameraCollection.h`, `Debug/BroadphaseVisualizer.h`, `Debug/CollisionVisualizer.h`, `Input/Input.h`, `Render/RuntimeRenderPasses.h` | yes (7/7) |
| Scene | `AuthoredScene.h` | yes (1/1) |
| World | `SkyBox.h`, `Terrain.h`, `WorldEnvironment.h` | yes (3/3) |

## Governance Dialect Boundary

The tracked source inventory contains zero literal occurrences of `extraction
scar`, `capability slice`, `courier`, or `closure failure`. The review dialect
has not leaked into source and CV1 has no evidence to import it.

## Glossary Inventory Context

The required current command passes:

```text
python tools/inventory_glossary_terms.py --repo . --strict
Glossary-term inventory: files=587 definitions=993 unique_terms=993
multi_file=0 drifted=0 ruled=0 unruled=0 ruling_issues=0
```

All 993 definitions are currently single-file terms. CV0 finds no multi-file
definition or wording drift for CV1/CV2 to repair.

## CV1 Decision Inputs

CV1 must rule on five distinct questions:

1. whether file-level `Mental model:` remains distinct from local `Concept:`;
2. how every layman/plain-language alias maps to the surviving spelling;
3. how to document the non-duplicate `CATTO REF` / `ENGINE-SPECIFIC` pair;
4. whether lanes and recurring policy/contract labels are repository syntax or
   intentionally subsystem-local vocabulary; and
5. that banners are resolved per file in CV2, never deleted by count alone.

