# Render Graph Transition Coverage RG3 Transients

Date: 2026-08-03
Phase: RG3 complete
Impact area: Rendering render graph tests
Production behavior change: None

## Outcome

The main doctest lane now pins the residual transient lifetime, aliasing,
compatibility, allocation, and diagnostics contract with hand-derived structured
expectations. No device, backend double, captured dump, or production edit is
involved.

The first graph declares one unused external resource and three compatible
transients. A spanning lifetime covers passes 0-2, a nested lifetime occupies
pass 1, and a disjoint lifetime starts at pass 3. Exact lifetime rows prove the
external resource remains `used=false`; the nested transient cannot alias and
uses pool slot 1; the disjoint transient reuses the spanning resource's pool
slot 0. All three allocation rows pin resource identity, first/last pass,
four descriptor needs, reuse state, and `releasedAtFrameEnd`. Diagnostics are
exactly three allocations, one reuse, three releases, two live resources, and
eight live descriptor rows at high water.

The second graph schedules eleven one-pass, non-overlapping transients. An exact
match reuses base pool slot 0. Nine variants independently change kind, format,
width, height, mip count, render-target need, depth-stencil need,
shader-resource need, or unordered-access need; each refuses aliasing and owns a
distinct slot. Every lifetime and allocation row is checked, descriptor counts
are exact, and diagnostics are exactly eleven allocations, one reuse, eleven
releases, one live resource, and four live descriptor rows at high water.

The deliberately synthetic all-descriptor base exists only so each descriptor
variant flips one logical bit while retaining a nonzero descriptor set. The test
never asks the backend to materialize that combination and makes no claim about
backend-only `createdThisCompile`, `reusedThisCompile`, native release, HRESULT,
or materialization-pool state.

The existing unused-transient invariant is now also merge-gated in the main
lane. A RenderGraph child graph declares but never uses one transient and
reaches the exact diagnostic:

```text
Transient resource must be read or written by at least one pass. resourceIndex=0
```

## Scope And Ownership

- Only `SkullbonezTests/TestRenderGraph.cpp` changed.
- No production file under `SkullbonezSource/Rendering` changed.
- No project/filter integration or build-configuration ruling changed.
- The 16 standalone DX12 architecture tests remain intact and merge-gated.
- No baseline, golden, screenshot, schema, coverage floor, or backend statistic
  was added or refreshed.

## Validation

- Serial Profile build plus
  `Profile\SKULLBONEZ_TESTS.exe --test-case="Render graph*" --duration=true`:
  PASS in 19.4 seconds, 8/8 cases and 357/357 assertions. Both RenderGraph fatal
  children terminate with their exact diagnostics.
- `tools\validate_tests.bat`: PASS in 54.5 seconds; project/filter validation,
  Profile build, and the full doctest harness completed successfully.
- Strict `clang-format --dry-run --Werror` on the touched source: PASS. The
  repository-wide gate remains blocked only by the main-identical, untouched
  `SkullbonezSource/Physics/PersistentContactSolver.cpp` formatting finding
  recorded by RG2; RG3 does not absorb that unrelated Physics work.
- Touched-source comment audit: 1/1 checked, zero deferred. The learning header,
  local lifetime/aliasing claims, synthetic descriptor rationale, and
  repository-relative `Related:` paths are current.

No DX12 renderer or graphics-stress gate was triggered because this phase is a
test-only addition and production Rendering source is unchanged.

## Review

No rubber-duck pass ran for this ordinary incremental phase. RG4 owns the final
plan checkpoint, all seven governance inventories, broad validation, and
independent closure review.

## Related

- `../../../SkullbonezTests/TestRenderGraph.cpp`
- `../../../SkullbonezSource/Rendering/RenderGraph.h`
- `../../../SkullbonezSource/Rendering/RenderGraph.cpp`
- `../../Plans/TODO/render-graph-transition-coverage.md`
- `render-graph-transition-coverage-rg2-subresources.md`
