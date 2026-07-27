# Extraction Scar Remediation Closure

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `extraction-scar-remediation` ES0-ES2

## Outcome

All 88 repair-ruled extraction scars are gone. Eighty-six member-prefixed
locals now use scope-honest names, and the two pure parameter aliases use their
parameters directly. The sole surviving finding is
`Core/WorkerPool.h:indexFn`; its forwarding-reference capture requirement and
retain reason are unchanged.

The originating plan's “12 files” text was stale. The authoritative scope was
89 findings in 15 files: 88 repairs across 14 edited files and the retained
WorkerPool binding in a fifteenth file. No signature, type, owner, schema,
configuration, baseline, golden, or behavior changed.

## ES0 Parameter-Use Audit

Zero-warning Profile and Debug builds confirm that every supplied parameter in
the affected functions remains used. The parameterless input helper is recorded
explicitly so it is not mistaken for an omitted audit row.

| Function | Parameter use | Unused parameters |
|---|---|---|
| `GeometricMath::ComputeTriangleNormal` | Every supplied parameter used | None |
| `GeometricMath::ComputeBarycentricCoordinates` | Every supplied parameter used | None |
| `SkullScope::EmitFrame` | Every supplied parameter used | None |
| `PhysicsContactSolverStage::Solve` | Every supplied parameter used | None |
| `PhysicsDiagnosticsSink::EmitRegressionLog` | Every supplied parameter used | None |
| `SleepIslandSystem::PropagateSupport` | Every supplied parameter used | None |
| `ProcessInputFrame` | Every supplied parameter used | None |
| `InputRouter::ApplyCameraMode` | Every supplied parameter used | None |
| `WndProc` | Every supplied parameter used | None |
| `RunUIStressActions` | Every supplied parameter used | None |
| `Input::GetClientMouseCoordinates` | No parameters | None |
| `LoadReplayPredictionArchive` | Every supplied parameter used | None |
| `ProjectCinematicSunToScreen` | Every supplied parameter used | None |
| `SceneController::Load` | Every supplied parameter used | None |
| `SceneController::ExecutePending` | Every supplied parameter used | None |

Borrowed view/span lifetimes are documented at the call-scoped sites in
`PersistentContactSolver.cpp`, `SkullScope.cpp`,
`PhysicsDiagnosticsSink.cpp`, `SleepIslandSystem.cpp`,
`InputFrameExecution.cpp`, and `SceneRequestExecution.cpp`. The comments state
the synchronous boundary and the owner that retains the backing state.

## ES1 Inventory And Rulings

- `tools/inventory_extraction_scars.py --self-test`: PASS.
- Repository scan: 1 finding / 1 ruled / 0 unruled.
- The remaining row is exactly
  `SkullbonezSource/Core/WorkerPool.h:228:indexFn`, verdict `retain`.
- Every completed `repair` row was removed from
  `tools/aggregate_ownership_rulings.json`; no verdict was widened.
- No new scar shape was found, so the existing fixtures required no change.

The companion ownership evidence also remains clean: 1,205 aggregate
candidates / 10 signalled / 10 ruled / 0 unruled. The wide-signature inventory
was rerun successfully; `PhysicsContactSolverStage::Solve` remains at the
accepted arity 12 and no signature changed.

## Independent Review

The one end-of-plan read-only rubber-duck review returned `ZERO BLOCKERS`.
Its explicit ownership answers were:

1. Aggregate ownership: pass; no aggregate or type changed, and the aggregate
   inventory remains fully ruled.
2. Capability slices: pass for this scope; no slice or signature changed, and
   the standing frame-view concern remains owned by its separate plan.
3. Extraction scars: pass; the scanner reports only the ruled WorkerPool
   retain.
4. Rename evasion: pass; no context/service/operand wrapper or second-name
   parameter alias appeared.
5. False claims: pass; the added lifetime statements match the synchronous
   post-change call paths.

The reviewer separately confirmed that every remaining local is scope-honest,
no alias survived under a different spelling, the WorkerPool reason is exact,
and the byte-exactness proof comes from final binaries rather than assertion.
Residual risk is ordinary platform/compiler variation only.

## Comment Audit

All 14 edited source files were inspected against the final implementation.
Every file has `File`, `Purpose`, `Summary`, `Glossary`, `Invariants`, and
`Related` header sections. Ownership, sequencing, and lifetime claims were
checked against the post-change source. Checked: 14. Deferred: 0. Unchecked:
none. This was a touched-file audit, so no subsystem checklist was required.

## Validation

- `tools\validate_fast.bat`: PASS in 115.5 s; 410/410 doctests and
  2,406,382/2,406,382 assertions passed; Profile and Debug builds completed
  with zero warnings and errors.
- `tools\validate_physics.bat`: PASS in 23.8 s; the 44,401-line regression CSV
  is byte-exact.
- `tools\validate_physics_deep.bat`: PASS in 97.2 s; every CSV and known-issue
  signature is exact, and `physics_query_varied.json` exactly matches.
- `tools\validate_perf.bat`: COMPLETE in 92.1 s; absolute DX12 and
  `PHYSICS_BENCH` budgets passed with no regressions.
- `tools\validate_full.bat`: DEFAULT GATE PASSED in 306.0 s; CPU, coverage,
  Automation, DX12, and Physics lanes passed, including accepted screenshots
  and the byte-exact 44,401-line Physics regression.

The first fast-gate attempt exposed a name collision between the borrowed sleep
counter collection and its per-body scalar. The collection became
`sleepCounters`; the focused ready-build check and every final gate above then
passed. No baseline or golden was refreshed.
