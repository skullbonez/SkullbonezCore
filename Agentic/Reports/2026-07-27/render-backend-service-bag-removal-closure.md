# Render Backend Service Bag Removal Closure

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `render-backend-service-bag-removal` RB0-RB3

## Outcome

The four-task campaign is complete. RB0 counted the backend service bag and its
rediscovery sites; RB1 supplied concrete capabilities to each consumer; RB2
deleted `RuntimeRenderBackendView` and made optional capability presence a
startup composition decision; and RB3 reconciled comments, ownership, and
final-tree behavior.

Capture is required by reference. Raytracing availability is decided once from
backend diagnostics and flows as a value beside the required raytracing owner.
Shader development remains one explicit optional startup capability, while the
development UI renderer remains compile-time gated. No consumer gained backend
authority that it did not already use.

## Final Ownership State

- `RuntimeRenderBackendView`, `RequireBackbufferCapture`, and
  `BackendEpochOwners` have zero source occurrences.
- No replacement eleven-pointer aggregate exists.
- Runtime raytracing propagation contains no nullable
  `Dx12RaytracingOwner` pointer, `optional`, or `reference_wrapper`.
- `ReflectionPassInputs` receives a required raytracing owner and a precomputed
  `useDxrReflection` decision.
- `RuntimeRenderer` is created once during backend binding and remains one
  explicit process-lifetime owner.
- Aggregate governance reports 1,170 candidates, 11 stated invariants, 84
  review rows, 84 ruled rows, and zero unruled rows. The operation-parameter
  maximum remains 12.

## Independent Ownership Review

The RB3 reviewer asked whether the eleven pointers reappeared under another
name, whether any consumer gained backend authority, and whether optional
capability presence was still inspected through nullable state at more than one
site.

The first pass found that raytracing availability was still converted back into
nullable pointers downstream. RB3 replaced that propagation with the required
owner plus a value-only availability decision. A follow-up review also verified
that replay-focus behavior was preserved by passing only the original
transparent-body condition to the DXR selection policy. The final verdict was
clear on all three questions.

## Validation

- Focused raytracing policy test: 1/1 case and 5/5 assertions passed.
- `tools\validate_fast.bat`: passed; Profile and Debug build with zero warnings
  and zero errors.
- `tools\validate_dx12_renderer.bat`: three consecutive final-tree runs passed
  against committed baselines at `20260727T014426Z`, `20260727T014521Z`, and
  `20260727T014616Z`, each with zero DX12 validation errors.
- `tools\run_graphics_stress.bat 1`: timeout-owned exit 124 after 60.474
  seconds, 15,162 frames, and 417 scene loads; renderer and ImGui shutdown
  evidence is present and stderr is empty.
- `tools\validate_full.bat`: 417/417 cases and
  2,409,561/2,409,561 assertions passed; DX12 output matched committed
  baselines and the 44,401-line physics regression remained byte-exact.
- `tools\validate_perf.bat`: allocation policy and allocation guard passed;
  absolute budgets passed and both DX12 and physics comparisons reported no
  regressions.

No physics, DX12, replay, schema, configuration, or visual baseline was
refreshed.

## Commit Sequence

- `c37cf387` — RB0 census.
- `0035ac40` — RB1 concrete bindings.
- `cfad57fd` — RB2 explicit optional capabilities.
- RB3 — ownership reconciliation, review remediation, final validation, and
  closure evidence.

Plan 6 no longer blocks plan 5. The binding next task is
`runtime-frame-view-retirement` FV0.
