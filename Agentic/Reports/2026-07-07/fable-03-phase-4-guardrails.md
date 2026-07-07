# Fable-03 Phase 4 Guardrails And Closure

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

Completed fable-03 phase 4 and marked PHYS-035 resolved in the 2026-07-07
blocker ledger. Phase 3 worker-job stepping remains deferred until after soak;
the blocker itself is closed because prediction no longer mutates live physics
and the static guardrail prevents that shape from returning.

## Change

- Added `check_replay_prediction_private_engine_restore_guardrails_text(...)`
  to `tools/check_runtime_boundaries.py`.
- The checker now rejects prediction-file calls that restore solver snapshots
  or body backups against anything except the private `predictionEngine`.
- Added synthetic self-tests for live solver restore, live body-state apply,
  allowed private-engine restore, and comment-only historical mentions.
- Updated `Agentic/Reference/runtime-reference.md` to describe private-engine
  prediction and current prediction profiler markers.
- Updated the fable-03 source plan/progress checklist and moved PHYS-035 from
  remaining blockers to resolved remediation work.

## Validation

```text
python tools\check_runtime_boundaries.py --self-test
SELF_TEST_PASS: runtime boundary checker synthetic cases passed

python tools\check_runtime_boundaries.py
Runtime boundary summary: TestOutput\validation\runtime_boundaries\summary.json (0 errors)
PASS: Runtime boundary validation passed.
FABLE03_P4_RUNTIME_BOUNDARIES_EXIT=0
FABLE03_P4_RUNTIME_BOUNDARIES_ELAPSED_SECONDS=15.179

tools\validate_fast.bat
PASS: All source files correctly formatted.
Project filter summary: TestOutput\validation\project_filters\summary.json (0 errors, 551 project items, 551 filter items)
Runtime boundary summary: TestOutput\validation\runtime_boundaries\summary.json (0 errors)
VALIDATE_TESTS: ALL PASSED
Build succeeded. 0 Warning(s), 0 Error(s). [ready Profile]
Build succeeded. 0 Warning(s), 0 Error(s). [ready Debug]
VALIDATE_FAST: ALL PASSED
FABLE03_P4_VALIDATE_FAST_EXIT=0
FABLE03_P4_VALIDATE_FAST_ELAPSED_SECONDS=33.065
```

Logs:
- `Agentic/Reports/2026-07-07/logs/fable-03-p4-runtime-boundaries.log`
- `Agentic/Reports/2026-07-07/logs/fable-03-p4-validate-fast.log`

## Comment Audit

Touched source-bearing file: `tools/check_runtime_boundaries.py`.

The file already has a learning header. The new PHYS-035 rule is grouped with
existing replay-prediction guardrails and has a nearby `# Why:` comment naming
the determinism risk: restoring into the live engine would reopen the old
prediction mutation window.
