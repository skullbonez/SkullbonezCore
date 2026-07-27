# New Aggregate Ruling Gate — NA1

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan phase: NA1

## Gate Armed

`tools\validate_fast.bat` now runs the aggregate repository scan with
`--strict`. `tools/README.md` documents the mode, and the Invariant Ownership
Rule plus its validation mapping state the bounded suffix/no-invariant contract,
the requirement for a disputable ownership reason, and the deliberate
`FooFrameData` naming residual.

## Observed Failure

The exact seven-member `FooFrameContext` probe was temporarily planted in a
production-tracked header so the preceding format and project filters remained
unchanged. Running:

```bat
tools\validate_fast.bat
```

reached step `[4/8]` and exited 1 with:

```text
Authority-free aggregate inventory: candidates=1168 state_own_invariant=11 signalled=0 review=85 gated=85 ruled=84 unruled=1 pre_existing_unreviewed=0
FAIL: 1 gated aggregate(s) require an owner ruling in tools/aggregate_ownership_rulings.json: FooFrameContext
```

The probe was then removed. Its temporary insertion also moved two existing
declaration lines, and the same run independently reported both site drifts,
demonstrating that the anti-gaming source-location check is live.

## Final-Source Validation

- `python tools/inventory_authority_free_aggregates.py --self-test`: passes.
- `tools\validate_fast.bat`: passes all eight stages.
- Final strict inventory: 1,167 discovered, 84 gated, 84 ruled, zero unruled,
  and zero `pre-existing-unreviewed`.
