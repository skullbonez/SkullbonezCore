# Render Graph Transition Coverage Closure

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Plan result: RG0-RG4 complete; plan deleted under MASTER inventory rule 4

## Outcome

The main doctest and instrumented coverage lane now compiles the production
render graph and pins the complete device-free contract selected by RG0:
ordinary access derivation, numeric-subresource divergence and convergence,
transient lifetimes and exact alias compatibility, all six public fixed
ceilings, every one-past fatal diagnostic, and every frame execution-contract
field. The sixteen existing standalone DX12 architecture cases remain intact.

RG4 adds the final boundary matrix:

- 24 resources and 24 passes succeed; the 25th resource/pass terminates with
  the exact owner diagnostic.
- One pass independently accepts eight reads and eight writes; the ninth read
  and ninth write terminate in separate child processes.
- Twelve alternating passes over eight resources produce exactly 96 ordered
  transitions; a following transition is the exact 97th-row fatal.
- Sixteen used transients produce sixteen allocation rows. Two non-overlapping
  eight-resource groups prove pool slots 0-7 are reused, with allocation 16,
  reuse 8, release 16, and resource/descriptor high-water 8. The 17th logical
  allocation terminates first.
- The compiler's separate 24-slot local transient-pool fatal is therefore
  publicly unreachable: public compilation cannot pass the 16-allocation
  ceiling to request a 25th distinct pool slot. The test records this ownership
  fact rather than manufacturing a private seam.
- Null and empty capture names, valid Present, missing Present, wrong and extra
  declaration-only rows, and a disabled callback assert all five result fields
  plus `IsValid()`.

Every fatal probe uses the shared child harness. A false pass cannot masquerade
as success: the parent requires launch success, bounded completion, nonzero
exit, and the exact diagnostic fragments; a child returning normally exits
zero and fails the parent.

## Scope And Ownership

Only `SkullbonezTests/TestRenderGraph.cpp` changes in RG4. No production file
under `SkullbonezSource/Rendering/` changed, so the DX12 renderer and graphics-
stress gates were not triggered by this test-only phase. No baseline, project
integration, Rendering API, backend statistic, or allocation policy changed.

The touched-source comment audit is 1/1 with zero deferrals. The learning
header names the structured-oracle, capacity, lifetime, and execution-contract
invariants; local `Why:` comments document synthetic descriptor ownership.

## Evidence

- Focused Profile: `Profile\SKULLBONEZ_TESTS.exe --test-case=Render graph*`
  passes 13/13 cases and 483/483 assertions.
- `tools\validate_all_cpu_tests.bat` passes in 177.8 seconds, including main
  doctests, instrumented coverage, and the standalone DX12 architecture lane.
- Instrumented product coverage is 23,994/30,835 lines (77.81%); production
  `RenderGraph.cpp` is 252/459 lines in the final report.
- `tools\validate_fast.bat` passes in 424.8 seconds: format, metadata,
  dependencies, ownership governance, Profile/Debug builds, and tests.
- Direct current-object runs pass for build-configuration consistency,
  authority-free aggregates, extraction scars, wide signatures, function
  complexity, unreachable symbols, and glossary terms. Reachability contains
  only current ruled rows after the final Debug/Profile builds.
- Strict clang-format and `git diff --check` pass.

## Independent Review

One read-only rubber-duck closure review returned no blocking or material
non-blocking findings. It independently traced all boundary constructions,
confirmed reads and writes exhaust separate stores, verified the 96/97 and
16/17 derivations avoid competing limits, checked every execution-result field,
and confirmed the child harness cannot silently accept a nonfatal path. The
only residual note was that the 96-row case samples the first and last row;
RG1/RG2 already pin row contents and ordering, while RG4 hand-derives and pins
the complete count.

## Closure

RG0-RG4 satisfy the plan acceptance contract. The completed TODO is deleted;
the active/future portfolio changes from 4/20 (20%) to 0/15 (0%) under ledger
rule 4. Dense Pile Sleep Resolution SR0 is now the binding next task.
