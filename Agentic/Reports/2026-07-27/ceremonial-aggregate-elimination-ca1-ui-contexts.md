# Ceremonial Aggregate Elimination CA1 — UI Contexts

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `ceremonial-aggregate-elimination` CA1

## Outcome

CA1 deleted the eight authority-free operator command contexts:

- `TornadoUICommandContext`
- `PhysicsSleepPolicyUICommandContext`
- `PhysicsFrictionUICommandContext`
- `RuntimePresentationUICommandContext`
- `CinematicUICommandContext`
- `RunSimulationUICommandContext`
- `RenderDeviceUICommandContext`
- `SceneFixedStepUICommandContext`

Each operation now borrows the concrete renderer, device, scene, simulation,
config, worker, defaults, or gameplay owner it actually uses. `InputFrame`
passes those owners directly. No replacement context or alternate courier
suffix was introduced, and the widest resulting operation remains below the
accepted ceiling at 10 parameters.

## Evidence

- `rg -n 'UICommandContext' SkullbonezSource SkullbonezTests` returns no rows.
- `inventory_authority_free_aggregates.py` reports 1,199 candidates, 111 review
  rows, 111 ruled, and zero unruled rows.
- `TestOperatorCommandApplier.cpp` pins the concrete signatures for all eight
  removed context families without linking the full application runtime.
- `tools\validate_tests.bat` passes 416/416 cases and 2,409,556/2,409,556
  assertions.
- `tools\validate_runtime_interaction_policy.bat` passes.
- `tools\validate_ui_boundary_tests.bat` passes.
- `tools\validate_project_filters.bat` passes.

CA2 is now binding: remove the Scene setup and runtime context family.
