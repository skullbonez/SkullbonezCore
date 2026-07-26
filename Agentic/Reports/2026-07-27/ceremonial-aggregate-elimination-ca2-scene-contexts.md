# Ceremonial Aggregate Elimination CA2 — Scene Contexts

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `ceremonial-aggregate-elimination` CA2

## Outcome

CA2 deleted the nine authority-free Scene setup/runtime couriers:

- `SceneRuntimeCreateContext`
- `SceneRuntimeUiOptionsContext`
- `SceneRuntimeStyleContext`
- `SceneAuthoredCameraContext`
- `SceneAuthoredModelContext`
- `SceneSimpleRagdollAppendContext`
- `SceneGeneratedCameraContext`
- `SceneGeneratedModelContext`
- `SceneGeneratedPopulationRequest`

Create, UI-option, authored, generated, live-style, cinematic-mode, validation,
and Director paths now pass only the concrete owners each operation uses.
Generated population precedence resolves to the domain enum
`GeneratedPopulationMode`; the application endpoint takes eight parameters.
No aggregate was renamed or replaced, and no operation exceeds the
12-parameter ceiling.

## Evidence

- The literal search for all nine deleted names returns no rows under
  `SkullbonezSource` or `SkullbonezTests`.
- The aggregate inventory reports 1,190 candidates, 102 review rows, 102 ruled,
  and zero unruled or stale rows.
- `tools\validate_tests.bat` passes 416/416 cases and 2,409,556/2,409,556
  assertions, including scene snapshot and lifecycle coverage.
- `tools\validate_build.bat Profile` builds the application with zero errors.
- `tools\validate_physics.bat` passes without changing a physics CSV or
  baseline.

CA3 is now binding: remove the Editor, Diagnostics, Input, Capture, Assets,
Render, and Replay remainder.
