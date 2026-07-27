# Ceremonial Aggregate Elimination Closure

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `ceremonial-aggregate-elimination` CA0-CA4

## Outcome

The five-task campaign is complete. CA0 ruled the full small-aggregate census;
CA1 removed eight UI command couriers; CA2 removed nine Scene setup/runtime
couriers; and CA3 removed the remaining 18 Editor, Diagnostics, Input, Capture,
Assets, Render, Replay, and view-model couriers.

All 35 authority-free types are deleted. Their consumers now take the concrete
owners or values each operation uses. No deleted aggregate was renamed, wrapped
under another suffix, or replaced by a new reference-carrying slice. The
full-tree operation maximum remains 12 parameters.

## Final Governance State

- Authority-free aggregate inventory: 1,172 candidates, 11 stated owner
  invariants, zero signalled rows, 84 review rows, 84 ruled, zero unruled.
- Rulings: 62 `retain`, 22 `retain-prior`, zero `remove`, zero duplicate keys,
  and zero stale sites.
- Extraction-scar inventory: one ruled `WorkerPool::indexFn` retain and zero
  unruled findings.
- Wide-signature inventory: 406 rows, maximum arity 12, and zero rows above 12.
- The CA1-CA3 source diff adds no aggregate definition. Its only new domain
  type, `GeneratedPopulationMode`, is a value enum rather than an operand
  courier.

## Independent No-Bag Review

The CA4 reviewer checked aggregate ownership, capability slices, extraction
scars, rename evasion, false claims, and the parameter ceiling. The review
found two stale comment phrases left by deletion: “Asset context” in
`AssetSystem.h` and “interaction context” in `EditorGizmoTools.cpp`. Both were
repaired to describe direct borrows/owners. With those corrections included,
the reviewer’s verdict is clear.

## Validation

- `tools\validate_fast.bat`: passed; Profile and Debug build with zero errors.
- `tools\validate_tests.bat`: 416/416 cases and
  2,409,556/2,409,556 assertions passed.
- `tools\validate_dx12_renderer.bat`: passed without a DX12 baseline change.
- `tools\run_graphics_stress.bat 1`: completed its one-minute DX12 stress run.
- `tools\validate_full.bat`: default gate passed from final source; Profile,
  Automation, and Debug builds are ready, and
  `physics_regression_varied.csv` matches all 44,401 lines byte for byte.

The first clean Profile closure build also exposed two configuration-specific
unused timing operands after `RuntimePerfTickContext` removal. The Profile path
now marks those explicit operands intentionally unused while non-profiler
builds continue writing them to the fallback CSV row. A clean Profile rebuild
and the restarted full gate both pass.

No physics, DX12, replay, schema, configuration, or visual baseline was
refreshed.

## Commit Sequence

- `11bd6d11` — CA1, UI command contexts.
- `9d32f332` — CA2, Scene setup/runtime contexts.
- `0d57c468` — CA3, remaining couriers.
- CA4 — final comments, independent review, inventories, and closure evidence.

Plan 5 remains blocked on plan 6. The binding next task is
`render-backend-service-bag-removal` RB0.
