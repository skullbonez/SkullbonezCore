# Source Plan-Reference Comment Audit

Date: 2026-08-17
Status: Complete

## Scope

Review comments in every tracked first-party source, test, and substantial tool
file for references to deleted plans, completed campaign phases, task codes, or
historical campaign policy. The discovery inventory is the 784 paths returned
by:

```powershell
git ls-files SkullbonezSource SkullbonezTests Agentic/Tests tools
```

filtered to `.cpp`, `.h`, `.hpp`, `.inl`, `.hlsl`, `.py`, `.bat`, and `.ps1`.
Runtime value vocabulary such as trip plans, render-graph plans, scene-load
plans, and the governance tools' live `repair-plan` schema is not campaign
history and remains unchanged.

## Acceptance

- No reviewed comment cites a deleted `Agentic/Plans/TODO/` file.
- No reviewed source comment depends on a completed task code such as `T1`,
  `E12`, `P3`, or `G0-G5` to explain current behavior.
- Current reasons, invariants, ownership, and validation consequences replace
  historical campaign shorthand.
- Every candidate file below has been inspected against the repository comment
  style guide; unchecked rows name any deferral.
- The 784-file discovery inventory and candidate scan are rerun at closure.

## Candidate Checklist

- [x] `SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.cpp`
- [x] `SkullbonezSource/Core/FloatingPointContract.h`
- [x] `SkullbonezSource/Maths/DeterministicMath.cpp`
- [x] `SkullbonezSource/Physics/PhysicsWorld.cpp`
- [x] `SkullbonezSource/Physics/SolverBroadphaseStage.h`
- [x] `SkullbonezSource/Physics/SpatialGrid.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsForceStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- [x] `SkullbonezSource/Rendering/RenderGraph.h`
- [x] `SkullbonezSource/Runtime/App/RunLaunchOptions.Renderer.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.cpp`
- [x] `SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp`
- [x] `SkullbonezSource/UI/UI.h`
- [x] `SkullbonezTests/TestFixedSeed.h`
- [x] `SkullbonezTests/TestOrbitalMechanics.cpp`
- [x] `SkullbonezTests/TestShaderReflectionContracts.cpp`
- [x] `tools/check_coverage.py`
- [x] `tools/check_staged_file_sizes.py`
- [x] `tools/measure_dense_pile_sleep.py`
- [x] `tools/agent_validate.bat`
- [x] `tools/style_harness.ps1`
- [x] `tools/validate_coverage.bat`
- [x] `tools/validate_full.bat`
- [x] `tools/validate_look_lab_reuse.py`
- [x] `tools/validate_select.bat`

Checked: 31/31. Deferred: 0.

## Closure Evidence

- Reconciled tracked scope: 784 files.
- Candidate checklist: 31 checked, 0 deferred, 0 unchecked.
- Residual `Agentic/Plans/TODO/` strings are confined to governance validators
  and their synthetic negative fixtures; no source comment cites a live or
  deleted implementation plan.
- Completed task-code scan is clean after excluding runtime identifiers such as
  shader registers, percentile labels, and ordinary render/trip plan vocabulary.
