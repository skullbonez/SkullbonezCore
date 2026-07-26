# Downward Domain Bleed DB1 — Feature-Neutral Retained Geometry

Date: 2026-07-26

Plan: `Agentic/Plans/TODO/downward-domain-bleed-remediation.md`

Result: Complete (DB1 of 6; portfolio ledger 3/19, 16%)

## Outcome

Rendering now owns only a generic retained-geometry transport contract:
consumer-supplied stride/capacity values, lane identity, fixed range tokens,
stream revision, and value-shaped upload plans. `Runtime/Prediction` owns the
nineteen-float record meaning, ordinary/priority/range capacities, adjacency
repair, continuation rules, and the retained storage owner.

The DX12 backend retains bounded generic buffers and interprets no trajectory,
replay, prediction, planning, or porkchop vocabulary. The Runtime composition
root supplies Prediction's cold physical shader path separately from
Rendering's generic retained-ribbon ABI name. No callback, virtual seam,
dynamic runtime growth path, compatibility alias, or forwarding facade was
introduced.

The generated reflection header moved from the Rendering source package to
`SkullbonezData/generated/`. This keeps physical asset provenance with authored
data while the generic reflection consumer remains in Rendering.

## Contract And Ownership Proof

- `RetainedGeometryCapacity` carries consumer-supplied stride and physical
  maxima.
- `RetainedGeometryStreamToken`, `RetainedGeometryRangeToken`, and
  `RetainedGeometryUploadPlan` are feature-neutral values.
- `BuildRetainedGeometryUploadPlan` preserves the prior incoming/cached token,
  range, dirty-span, and full-upload cases.
- `ReplayPredictionRetainedGeometry` owns the record packing, fixed ranges,
  continuation repair, and memory accounting.
- `EditorTracer` retains only its marker and frame-local line responsibilities;
  compact retained Prediction geometry moved to its owning package.
- The approved physical shader family remains `trajectory_ribbon` outside the
  Rendering package. Rendering's public ABI is `retained_ribbon`.

Final exact scans:

```text
rg -ni "trajectory|porkchop|replay|prediction" SkullbonezSource/Rendering
PASS: no rows

rg -n "trajectory_ribbon|TrajectoryRibbon|RetainedTrajectory|RETAINED_TRAJECTORY|DebugPredictionOverlay" SkullbonezSource/Rendering SkullbonezTests
PASS: no rows
```

## Shader Provenance And Visual Fidelity

The single required replay visual-fidelity launcher invocation ran its one
6,800-frame Automation process and produced an `ok=1` report plus the expected
screenshots. Its provenance comparison correctly rejected an intermediate
physical shader-path rename:

```text
expected shadersSha256:
9eb658302f3258db762f4383f572ecde5e95a7be05df81f23c1bc069ad434b02
```

DB1 therefore restored the authored HLSL, VS/PS DXIL, and shader manifest paths
byte-for-byte instead of refreshing the oracle. The final shader-tree hash is
exactly the approved value above. Against the same captured report, all ten
non-engine checker modes passed: primary comparison, negative control,
incomplete horizon, three causal controls, semantic packet, artifact byte,
prediction artifact, and the full determinism-control set. No second visual
engine process was launched.

No baseline, golden, replay artifact, scene, config, shader byte, screenshot
reference, or physics CSV was refreshed.

## Validation

| Gate | Result |
|---|---|
| `tools\validate_dx12_renderer.bat` | PASS; 43 stages current, Profile/Debug builds pass, zero DX12 validation errors, all three committed screenshot comparisons accepted |
| DX12 manifest | PASS; `TestOutput/validation/dx12_renderer/20260726T014453Z/manifest.json` |
| `tools\run_graphics_stress.bat 1` | PASS; final checked run reached 13,149 frames / 361 scene loads, zero upload drops, shutdown reconciliation delta 0 |
| `tools\validate_tests.bat` | PASS; 391 cases / 2,403,286 assertions |
| replay visual fidelity | PASS as the one engine capture plus all ten offline comparisons/controls described above |
| `git diff --check` | PASS |

## Comment Audit Checklist

The comment-style audit inspected every hand-authored source-bearing path in
the DB1 diff after the final implementation. Dense ownership, lifetime,
capacity, physical/logical shader identity, and continuation-sensitive code
has nearby `Concept:`, `Why:`, `Invariant:`, `Lifetime:`, or `Hazard:` context
where required. The generated header is mechanically emitted and exempt; the
old generated location is deleted.

- [x] `SkullbonezSource/Assets/AssetSystem.cpp`
- [x] `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp`
- [x] `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12ResourceBuilder.h`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.h`
- [x] `SkullbonezSource/Rendering/RenderCommandTypes.h`
- [x] `SkullbonezSource/Rendering/RenderDiagnosticsTypes.h`
- [x] `SkullbonezSource/Rendering/RenderGraph.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.h`
- [x] `SkullbonezSource/Rendering/ShaderContracts.h`
- [x] `SkullbonezSource/Rendering/ShaderReflectionContracts.h`
- [x] `SkullbonezSource/Rendering/WorldRenderExtension.h`
- [x] `SkullbonezSource/Runtime/App/Init.cpp`
- [x] `SkullbonezSource/Runtime/App/ReplayRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Capture/RuntimeStressController.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorTracer.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedGeometry.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.h`
- [x] `SkullbonezSource/UI/UIRenderDiagnostics.h`
- [x] `SkullbonezSource/UI/UITabMemory.cpp`
- [x] `SkullbonezTests/TestDx12OnlyRuntime.cpp`
- [x] `SkullbonezTests/TestReplayVisualPacket.cpp`
- [x] `SkullbonezTests/TestShaderReflectionContracts.cpp`
- [x] `tools/bake_shaders.py`
- [x] `SkullbonezData/generated/GeneratedShaderReflection.h` — generated exemption
- [x] `SkullbonezSource/Rendering/DX12/GeneratedShaderReflection.h` — deleted/moved

Checked: 40. Deferred: 0. Unchecked: 0.
