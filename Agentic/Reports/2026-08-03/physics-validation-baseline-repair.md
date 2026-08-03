# Physics Validation Baseline Repair

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`

## Scope

Render Graph RG4's closure inventory exposed two pre-existing validation
defects in `PersistentContactSolver.cpp`: the repository formatting pipeline
wanted one paragraph break before the terrain-seed guard, and the exact
function-complexity ruling for `PrecomputeRows` still hashed the body from
before the comment-only terrain support-seed decision (`49f45e24`).

## Repair

- Ran clang-format and the repository paragraph/compact-call writer on only
  `PersistentContactSolver.cpp`. The final source delta is one blank line;
  `git diff --word-diff=porcelain` reports no token change.
- Re-reviewed `PrecomputeRows` as the single guarded row-precompute phase. Its
  effective-mass derivation, exact-feature cache/lifetime lookup, friction
  bounds, warm-start application, and optional diagnostic publication still
  share one synchronous contact-row invariant. The existing `retain-owner`
  judgement and reason remain current.
- Refreshed only that ruling's full-body SHA-256 from
  `546fe81f1cab7d20868d04b5f8f35c0247db836b9af366b3bf1b26c438270346`
  to `babac6956b98be9027da23c15f34c3c206b1d888c3f0cbe92eff99240fc4b159`.

This prerequisite does not change Physics behavior, plan progress, baselines,
or any production token. It exists solely to restore honest current-source
validation before RG4 closure.

## Validation

- `tools\validate_format.bat`
- `python tools\inventory_function_complexity.py --repo . --strict`
- `git diff --check`

The touched-source comment audit is 1/1: the learning header names transaction
ownership and exact-contact invariants, the edited region carries the local
terrain-seed invariant, and every `Related:` path resolves through the format
gate.
