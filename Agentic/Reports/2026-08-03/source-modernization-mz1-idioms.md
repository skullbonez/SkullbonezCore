# Source Modernization MZ1 — Idiom Retirement

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Plan phase: MZ1 of `Agentic/Plans/TODO/source-modernization-sweep.md`
Impact: Maths, Physics, Rendering/DX12, Text, and Runtime debug source

## Result

MZ1 retired every occurrence classified by the MZ0 census. All 136 C-style or
functional-style casts in the 15 implementation files now use the equivalent
`static_cast`; the two `NULL` tokens in `Rendering/Text.cpp` now use `nullptr`;
and the two internally defined `SKULLBONEZ_INTRINSICS` constants are replaced by
the header-owned `inline constexpr bool INTRINSICS_ENABLED`. `RotationMatrix.h`
uses the same default Debug/non-Debug build predicate at its two preprocessor
sites. An externally supplied `SKULLBONEZ_INTRINSICS=0/1` remains authoritative,
preserving both the Debug scalar / Profile-Release SSE defaults and the existing
force-off / force-on override contract without defining a macro in the header.

The inspected semantic diff contains no arithmetic, target type, expression
ordering, branch, loop, storage, signature, or data-flow change. Formatting
changes are limited to the repository formatter's layout of the rewritten
expressions. No retain-classified occurrence exists in this tranche.

## Residual And Audit Evidence

- Exact scoped scans find zero `NULL` tokens, zero header-owned
  `SKULLBONEZ_INTRINSICS` definitions, and zero remaining MZ0 target-type cast
  spellings in the 17 touched files. The remaining intrinsics tokens only read
  the supported externally supplied override.
- `git diff --check` passes.
- `tools\validate_format.bat` passes for all 587 implementations and 327
  headers.
- `tools\validate_build.bat Profile` passes with zero warnings and zero errors.
- Explicit preprocessing proves all four intrinsics cases: default Profile SSE,
  default Debug scalar, forced-on Debug SSE, and forced-off Profile scalar.
- The required touched-source comment audit is 17/17. Every file retains the
  required learning-header sections; the expression-only edits introduce no
  new ownership, lifetime, invariant, unit, or hazard explanation need.

The mapped byte-exact Physics, deep Physics, performance, and full validation
gates remain deferred to MZ4, after the parameter-only MZ2 tranche.
