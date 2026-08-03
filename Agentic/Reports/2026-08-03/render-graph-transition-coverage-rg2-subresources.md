# Render Graph Transition Coverage RG2 Subresources

Date: 2026-08-03
Phase: RG2 complete
Impact area: Rendering render graph tests and shared Lane F test harness
Production behavior change: None

## Outcome

The main doctest lane now pins the two residual subresource-state contracts that
RG0 left for RG2. Both oracles are hand-derived from the declared base state,
pass order, and the compiler's insertion-ordered numeric override store; no
compiler output was captured as an expectation.

One external resource starts in `PixelShaderResource`. Numeric subresources 5
and 2 diverge to `RenderTarget` and `CopyDest`, then an all-resource read
converges them to the base state in stored order 5 then 2. A following
all-resource `CopySource` read emits exactly one whole-resource transition,
proving no numeric override survived convergence. The test checks five exact
rows including pass, graph resource, copied native token, before/after access,
and numeric or all-resource scope.

The capacity oracle authors eight active overrides, one per pass, in literal
order `7,0,6,1,5,2,4,3`. Compilation succeeds and emits the eight outward rows
followed by eight convergence rows in the same order. A separate child graph
adds subresource 8 in a ninth pass and reaches the exact Lane F diagnostic:

```text
Subresource state capacity exceeded. count=8 capacity=8
```

`TestRenderGraph.cpp` supplies a small subsystem dispatcher. The existing
`TestRuntimeContracts.cpp` child launcher still owns process creation, timeout,
termination, output capture, and diagnostic matching; its exported assertion
seam lets the RenderGraph test remain in its owning translation unit.

## Scope And Ownership

- No production file under `SkullbonezSource/Rendering` changed.
- No project/filter integration or RG1 build-configuration ruling changed.
- The 16 standalone DX12 architecture tests remain intact and merge-gated.
- No graphics device, backend double, baseline, golden, screenshot, schema, or
  coverage floor was added or refreshed.

## Validation

- Serial Profile build plus
  `Profile\SKULLBONEZ_TESTS.exe --test-case="Render graph*" --duration=true`:
  PASS in 9.2 seconds, 6/6 cases and 180/180 assertions. This includes the
  exact ninth-state child diagnostic.
- `tools\validate_tests.bat`: PASS in 47.5 seconds; project/filter validation,
  Profile build, and the full doctest harness completed successfully.
- Strict `clang-format --dry-run --Werror` on all three touched source files:
  PASS. The repository-wide `tools\validate_format.bat` also passed all 2,037
  `Related:` paths, then reported only 14 paragraph/compact-call findings in
  untouched `SkullbonezSource/Physics/PersistentContactSolver.cpp`. That file is
  byte-identical to current `main` and remains outside this test-only phase.
- The first `tools\validate_tests.bat` attempt stopped before source diagnostics
  with MSVC infrastructure error `D8040` while creating a compiler child. A
  serial `/MP1` build passed, and the unchanged formal command then passed.
- Touched-source comment audit: 3/3 checked, zero deferred. Learning headers,
  local invariants, shared fatal-harness caller contracts, behavior claims, and
  repository-relative `Related:` paths are current.

No DX12 renderer or graphics-stress gate was triggered because this phase is a
test-only addition and production Rendering source is unchanged.

## Review

No rubber-duck pass ran for this ordinary incremental phase. The orchestrator
reserves independent review for the RG4 plan checkpoint unless a repeated
failure mode makes earlier review necessary.

## Related

- `../../../SkullbonezTests/TestRenderGraph.cpp`
- `../../../SkullbonezTests/TestFatalCases.h`
- `../../../SkullbonezTests/TestRuntimeContracts.cpp`
- `../../../SkullbonezSource/Rendering/RenderGraph.cpp`
- `../../Plans/TODO/render-graph-transition-coverage.md`
- `render-graph-transition-coverage-rg1-ordinary-transitions.md`
