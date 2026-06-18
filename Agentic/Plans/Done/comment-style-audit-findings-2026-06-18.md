# Comment Style Audit Findings

Generated: 2026-06-18

Scope: repo source-bearing files (`.cpp`, `.h`, `.hlsl`) plus substantial tool
and hook scripts. Excluded vendored code, generated/binary artifacts, reports,
completed plans, audits, reference docs, and skill docs.

Worktree note: this document was created while
`SkullbonezSource/SkullbonezRunInput.cpp` already had user-owned local changes.
Do not clean up or rewrite that file without rechecking the current diff.

Validation: not run; this is documentation-only audit output.

## Summary

- Auditable files scanned: 286
- Header issues: 27
- Acronym/glossary issue files: 39
- Restatement-style comment files: 58
- Vague bare TODO/FIXME/HACK hits: 0

## Header Issues

- `.githooks\check-braces.py:2` - no learning header at first nonblank line
- `.githooks\check-headers.py:2` - no learning header at first nonblank line
- `.githooks\run-clang-format.py:2` - no learning header at first nonblank line
- `Agentic\Tests\SceneParserUnitTests\SceneParserUnitTests.cpp:1` - Glossary:
- `SkullbonezData\shaders\ui_render_target_preview.hlsl:1` - Glossary:
- `SkullbonezSource\SkullbonezAmortizedTask.cpp:1` - Glossary:
- `SkullbonezSource\SkullbonezAmortizedTask.h:1` - Glossary:
- `SkullbonezSource\SkullbonezConvexHullShape.cpp:1` - Glossary:
- `SkullbonezSource\SkullbonezDx12RenderGraphExecutor.cpp:1` - Glossary:
- `SkullbonezSource\SkullbonezFence.h:1` - Glossary:
- `SkullbonezSource\SkullbonezGameModelStreams.cpp:1` - Glossary:
- `SkullbonezSource\SkullbonezGameModelStreams.h:1` - Glossary:
- `SkullbonezSource\SkullbonezInputController.cpp:1` - Glossary:
- `SkullbonezSource\SkullbonezInputController.h:1` - Glossary:
- `SkullbonezSource\SkullbonezLockOrderValidator.cpp:1` - Glossary:
- `SkullbonezSource\SkullbonezLockOrderValidator.h:1` - Glossary:
- `SkullbonezSource\SkullbonezRuntimeDiagnostics.cpp:1` - Glossary:
- `SkullbonezSource\SkullbonezRuntimeDiagnostics.h:1` - Glossary:
- `SkullbonezSource\SkullbonezSceneRuntime.cpp:1` - Glossary:
- `SkullbonezSource\SkullbonezSimulationSystem.cpp:1` - Glossary:
- `SkullbonezSource\SkullbonezSimulationSystem.h:1` - Glossary:
- `SkullbonezSource\SkullbonezWorkerPool.cpp:1` - Glossary:
- `SkullbonezSource\SkullbonezWorkerPool.h:1` - Glossary:
- `SkullbonezSource\UI\UITabEditor.cpp:1` - Glossary:
- `SkullbonezSource\UI\UITabEditor.h:1` - Glossary:
- `tools\style_harness.ps1:1` - no learning header at first nonblank line
- `tools\validate_scene_parser_tests.bat:1` - Glossary:

## Acronym Or Glossary Issues

- `SkullbonezData\shaders\lit_textured.hlsl` - DX12 at 34,102
- `SkullbonezData\shaders\lit_textured_instanced.hlsl` - DX12 at 41
- `SkullbonezData\shaders\post_tonemap.hlsl` - DX12 at 29
- `SkullbonezData\shaders\post_volumetric_light.hlsl` - DX12 at 29
- `SkullbonezData\shaders\sky_atmosphere.hlsl` - DX12 at 29
- `SkullbonezData\shaders\solid_color.hlsl` - DX12 at 31
- `SkullbonezData\shaders\text.hlsl` - DX12 at 34
- `SkullbonezData\shaders\unlit_textured.hlsl` - DX12 at 30
- `SkullbonezData\shaders\water_calm.hlsl` - DX12 at 30
- `SkullbonezData\shaders\water_ocean.hlsl` - DX12 at 30
- `SkullbonezSource\SkullbonezAssetSystem.h` - GPU at 152
- `SkullbonezSource\SkullbonezBLASDX12.h` - TLAS at 58
- `SkullbonezSource\SkullbonezCollisionVisualizer.cpp` - AABB at 345
- `SkullbonezSource\SkullbonezFramebufferDX12.h` - DSV at 43; RTV at 43
- `SkullbonezSource\SkullbonezGameModel.cpp` - CCD at 904
- `SkullbonezSource\SkullbonezGameModelCollection.cpp` - SoA at 70
- `SkullbonezSource\SkullbonezHelper.cpp` - CPU at 144; DX12 at 183,1005
- `SkullbonezSource\SkullbonezInit.cpp` - COM at 1901,1902
- `SkullbonezSource\SkullbonezPhysicsDebugVisualizer.cpp` - CPU at 582
- `SkullbonezSource\SkullbonezPhysicsWorld.cpp` - CCD at 814,1050,1278; SoA at 318,322
- `SkullbonezSource\SkullbonezRenderBackendDX12.DXR.cpp` - COM at 106
- `SkullbonezSource\SkullbonezRenderBackendDX12.Pipeline.cpp` - RTV at 575
- `SkullbonezSource\SkullbonezRenderBackendDX12.Textures.cpp` - CPU at 214
- `SkullbonezSource\SkullbonezRenderBackendDX12.cpp` - COM at 1099,1425; DSV at 443,953,969,986,1293; DXR at 481,487,944,1360,1949; RTV at 443,952,969,974,1049,1057
- `SkullbonezSource\SkullbonezRenderBackendDX12.h` - CBV at 219; DXR at 385
- `SkullbonezSource\SkullbonezRenderDeviceDX12.cpp` - COM at 778,1036
- `SkullbonezSource\SkullbonezRenderDeviceDX12.h` - COM at 341,640
- `SkullbonezSource\SkullbonezRenderMaterial.h` - CPU at 120,175,258
- `SkullbonezSource\SkullbonezRun.h` - CLI at 98,856,857,858,859,860,868,869,870,871,872,873,978,1052,1053,1054,1055,1056,1057,1062,1064,1065,1079; SDF at 826
- `SkullbonezSource\SkullbonezRunInput.cpp` - DX12 at 1589; DXR at 1469,1470
- `SkullbonezSource\SkullbonezRunInternal.h` - CLI at 343
- `SkullbonezSource\SkullbonezRunPasses.cpp` - BLAS at 787; CPU at 605; DX12 at 777,787; DXR at 762; TLAS at 763,779
- `SkullbonezSource\SkullbonezRunScene.cpp` - CLI at 1134,1379,1478; DXR at 1569; GPU at 1015
- `SkullbonezSource\SkullbonezRuntimeDiagnostics.h` - CLI at 40,41,48
- `SkullbonezSource\SkullbonezSceneRuntime.h` - UI at 54,57
- `SkullbonezSource\SkullbonezShaderContracts.h` - DX12 at 65
- `SkullbonezSource\SkullbonezShaderDX12.h` - CBV at 120
- `SkullbonezSource\SkullbonezSimulationSystem.cpp` - UI at 78
- `SkullbonezSource\SkullbonezWindow.cpp` - DX12 at 96

## Restatement-Style Comment Candidates

These are mechanical candidates where comments look like they merely restate
nearby code. Review before deleting; some may deserve `Why:`, `Invariant:`,
`Lifetime:`, or `Hazard:` rewrites instead.

- `.githooks\check-braces.py` - lines 31
- `SkullbonezData\shaders\generate_mips.hlsl` - lines 125
- `SkullbonezData\shaders\lit_textured.hlsl` - lines 324
- `SkullbonezData\shaders\lit_textured_instanced.hlsl` - lines 157
- `SkullbonezData\shaders\reflect.rt.hlsl` - lines 309
- `SkullbonezData\shaders\water_calm.hlsl` - lines 167
- `SkullbonezSource\SkullbonezBLASDX12.h` - lines 57
- `SkullbonezSource\SkullbonezBoundingBox.cpp` - lines 103
- `SkullbonezSource\SkullbonezBroadphaseVisualizer.cpp` - lines 180,281
- `SkullbonezSource\SkullbonezCamera.cpp` - lines 44,45,46,52,55,65,68,75,157,160,175,236,246,254,276,292,301,314,342,348,416,423,426,435,438,466,500,557,563
- `SkullbonezSource\SkullbonezCamera.h` - lines 75
- `SkullbonezSource\SkullbonezCameraCollection.cpp` - lines 138,147,166,193,202,205,310,333,341,362,378,383,392,396,399,408,413,424
- `SkullbonezSource\SkullbonezCameraCollection.h` - lines 82,96,102
- `SkullbonezSource\SkullbonezFramebufferDX12.cpp` - lines 202,206
- `SkullbonezSource\SkullbonezGameModel.cpp` - lines 60,66,619,622,629,666,1042,1045,1061,1080,1083,1088,1287,1303,1374
- `SkullbonezSource\SkullbonezGameModel.h` - lines 132,173,174,188,189,190
- `SkullbonezSource\SkullbonezGameModelSoACache.cpp` - lines 70
- `SkullbonezSource\SkullbonezGeometricMath.cpp` - lines 40,45,54,71,75,231,259,275,422
- `SkullbonezSource\SkullbonezGeometricMath.h` - lines 87
- `SkullbonezSource\SkullbonezHelper.h` - lines 78,79,89,92,93,96,97,98,101,113,124
- `SkullbonezSource\SkullbonezInit.cpp` - lines 1043,1685
- `SkullbonezSource\SkullbonezMatrix4.cpp` - lines 522,546
- `SkullbonezSource\SkullbonezMatrix4.h` - lines 77
- `SkullbonezSource\SkullbonezMeshDX12.cpp` - lines 64,89,92,150
- `SkullbonezSource\SkullbonezObjectContactManifold.cpp` - lines 764
- `SkullbonezSource\SkullbonezPhysicsWorld.cpp` - lines 690
- `SkullbonezSource\SkullbonezProfiler.cpp` - lines 183,201,563,873,920,1221,1232,1244
- `SkullbonezSource\SkullbonezQuaternion.cpp` - lines 132
- `SkullbonezSource\SkullbonezRenderBackendDX12.DXR.cpp` - lines 210,256,334,346,356,387,392,540
- `SkullbonezSource\SkullbonezRenderBackendDX12.DynamicGeometry.cpp` - lines 132,208,213,226,268
- `SkullbonezSource\SkullbonezRenderBackendDX12.Pipeline.cpp` - lines 490,533,573
- `SkullbonezSource\SkullbonezRenderBackendDX12.Readback.cpp` - lines 90,112
- `SkullbonezSource\SkullbonezRenderBackendDX12.Textures.cpp` - lines 84,217,320,389,434,516
- `SkullbonezSource\SkullbonezRenderBackendDX12.cpp` - lines 944,1117,1121,1298,1328,1560,1869
- `SkullbonezSource\SkullbonezRenderDeviceDX12.cpp` - lines 414
- `SkullbonezSource\SkullbonezRenderDeviceDX12.h` - lines 379
- `SkullbonezSource\SkullbonezRenderGraph.h` - lines 173
- `SkullbonezSource\SkullbonezRigidBody.cpp` - lines 89,137,215,253,256,273,276,312
- `SkullbonezSource\SkullbonezRigidBody.h` - lines 96,99,100,103,111,121,122,123,124
- `SkullbonezSource\SkullbonezRotationMatrix.h` - lines 57
- `SkullbonezSource\SkullbonezRun.cpp` - lines 504,507,513,519,522,532,546
- `SkullbonezSource\SkullbonezRun.h` - lines 570,943,944,1011
- `SkullbonezSource\SkullbonezRunPasses.cpp` - lines 1038
- `SkullbonezSource\SkullbonezRunRender.cpp` - lines 388,392,592,595,668,705
- `SkullbonezSource\SkullbonezRunScene.cpp` - lines 514,517,550,1041,1079,1083,1118,1559,2360
- `SkullbonezSource\SkullbonezShaderDX12.h` - lines 120
- `SkullbonezSource\SkullbonezSkyBox.cpp` - lines 293
- `SkullbonezSource\SkullbonezSkyBox.h` - lines 54,55,59,60
- `SkullbonezSource\SkullbonezSpatialGrid.cpp` - lines 377
- `SkullbonezSource\SkullbonezTLASDX12.cpp` - lines 163
- `SkullbonezSource\SkullbonezTerrain.cpp` - lines 225,699,725,754,787,820,826,834,840,850,874,892,915,951,974,992,1028,1056,1084
- `SkullbonezSource\SkullbonezTerrain.h` - lines 116
- `SkullbonezSource\SkullbonezTestScene.h` - lines 219,220,227,228,230
- `SkullbonezSource\SkullbonezText.cpp` - lines 177,209,241,544,944
- `SkullbonezSource\SkullbonezTimer.cpp` - lines 47,79,86,93,100,113,117,124,130,133,140,143
- `SkullbonezSource\SkullbonezVector3.h` - lines 41,43,48,61,62,80,86,92,98
- `SkullbonezSource\SkullbonezWindow.cpp` - lines 111,119,120,126,128,142,164,237,242,252,275,290,291,302
- `SkullbonezSource\SkullbonezWorldEnvironment.cpp` - lines 479,482,485,488,491,494,497,581

## Vague TODO/FIXME/HACK Comments

- None found in the audited set.

## Suggested Cleanup Order

1. Fix header `Glossary:` misses first; those are low-risk and unblock acronym cleanup.
2. Add local glossary entries for repeated domain terms such as `DX12`, `CLI`, `DXR`, `CCD`, `SDF`, `COM`, `TLAS`, `BLAS`, `CBV`, `RTV`, and `DSV`.
3. Triage restatement clusters by subsystem, starting with older math/window/timer files where comments mostly mirror statements.
4. For render and physics files, prefer concept, invariant, lifetime, or hazard comments over deletion when the code protects GPU state, determinism, or validation behavior.
