# Concrete Parameter-Bag Elimination PB2 Pointer Routing

Date: 2026-07-26  
Implementation base: `1ccd39da`  
Branch: `nightrunner-25th-JUL-26`

## Result

PB2 passes. The Runtime, editor, and mouse-pick projection bags are deleted.
`RuntimePointerEvent` remains the single semantic pointer value, while
InputRouter samples normal and viewport-clamped world rays once and passes
only focused leaf operands to concrete owners.

The retired symbols have zero definitions/usages:

- `RuntimePointerRouteInput`;
- `EditorPointerRouteInput`;
- `MousePickupPointerInput`.

## Concrete Routing Boundary

`ProcessInputFrame` publishes the existing post-UI `RuntimePointerEvent` and
passes it to `InputRouter::RouteRuntimePointer` with camera mode, Replay
inspection state, active-model capacity, Window, and the concrete domain
owners. InputRouter derives camera, Scene, and tool borrows from those owners,
samples both ray variants before any consumer can mutate state, and retains
none of them.

The three repaired operations have 10, 11, and 12 parameters. The
repository-wide threshold-13 inventory contains only the three PB3 render/UI
operations already assigned by PB0.

## Arbitration

The exact world-pointer precedence remains:

1. editor preview;
2. active editor placement/gizmo teardown;
3. editor gizmo/scale begin;
4. editor selection;
5. mouse pickup;
6. attached-camera selection;
7. Replay world/path picking;
8. launcher.

Each later owner runs only when the preceding owner declines the gesture.
UI suppression and native-cursor policy continue to come from the published
`RuntimePointerEvent`. Camera-look capture still returns before world routing.

PB0-approved leaf values remain unchanged:

- `EditorPointerPreviewInput`;
- `EditorPointerSelectionInput`;
- `EditorGizmoDragPointerInput`;
- `LauncherPointerInput`;
- `ReplayWorldPointerInput`;
- `ReplayPathPickInput`;
- `RuntimePickRequest`.

## Test Ruling

CodeGraph found no isolated test caller for the three orchestration methods.
Adding a test-only routing seam would have introduced the same callback or
union-of-consumer-needs indirection that PB2 removes. Existing input,
interaction-policy, Replay, Automation, and runtime suites therefore provide
the behavioral evidence through the mandatory CPU and full runtime gates.

## Comment Audit

Touched-source inventory: 6 files checked, 6 compliant, 0 deferred.

The audit verified or corrected:

- the single-sample lifetime of normal and clamped world rays;
- InputRouter ownership of world-pointer arbitration;
- the exact editor and cross-domain precedence order;
- the synchronous lifetime of Scene, Window, camera, and tool borrows;
- mouse-pick retention of only its typed body handle and camera-plane values.

Every touched source-bearing file has the required learning header and local
invariant/lifetime comments. No term needs human-approved wording.

## Static Proofs

The retired-symbol scan returns no rows:

```powershell
rg -n 'RuntimePointerRouteInput|EditorPointerRouteInput|MousePickupPointerInput' `
  SkullbonezSource SkullbonezTests
```

The introduced-line scan has no inheritance, virtual dispatch,
`std::function`, callback/service/bindings indirection, runtime allocation, or
retained host pointer. The UI-to-Runtime dependency proof also returns no
rows.

## Validation

- focused Debug core build: PASS;
- `tools\validate_fast.bat`: PASS after one mechanical paragraph-spacing
  formatter repair;
- `tools\validate_full.bat`: PASS in 256.6 seconds:
  - formatting and `Related:` paths;
  - 783/783 project filters;
  - dependency graph;
  - Profile/Automation/Debug builds;
  - mandatory CPU and coverage chain;
  - Automation, Replay, and prediction runtime lanes;
  - DX12 validation with zero errors and no baseline refresh;
  - 44,401-line physics regression byte-exact.
