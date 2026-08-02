# Render Graph Transition Coverage

Date: 2026-08-02
Status: NOT STARTED — 0/5 phases complete
Impact area: Rendering render graph, DX12 render graph executor, tests
Owner: Rendering render graph
Priority: Third

## Problem And Evidence

`SkullbonezSource/Rendering/RenderGraph.h` states the strongest correctness claim
in the renderer:

> Declared pass accesses are the single source of truth for ordinary frame-resource
> transitions. The graph derives them; the backend executor emits them. A
> hand-written ordinary frame-pass barrier is a defect.

That invariant is currently enforced by code review alone. There is no
`TestRenderGraph.cpp` in `SkullbonezTests/`. The existing renderer-adjacent test
files are `TestDx12CachedPsoStore.cpp`, `TestDx12OnlyRuntime.cpp`,
`TestShaderReflectionContracts.cpp`, and `TestShadow.cpp`; none of them compiles
a graph or inspects a derived transition.

This is not a case where testing is expensive. The compile surface is pure CPU
and needs no device, no window, and no backend:

- `RenderGraphCompileResult Compile() const` and its out-parameter overload return
  the complete derived `transitions` list, transient allocation records with
  `firstPass`/`lastPass` lifetimes and `poolSlot` reuse, `createdThisCompile` and
  `reusedThisCompile` counters, and `dryRunValidatedPassCount`.
- `RenderGraphExecutionContractResult ValidateFrameExecutionContract( const char* )`
  is pure.
- `std::string DumpText() const` exists and is a ready-made human-readable oracle
  form.
- `AddExternalResource`, `AddTransientResource`, `AddPass`, `AddRead`, `AddWrite`
  build a graph from plain values.

The fixed ceilings are likewise exercised only by production traffic and never at
their boundary: `RENDER_GRAPH_MAX_RESOURCES` 24, `RENDER_GRAPH_MAX_PASSES` 24,
`RENDER_GRAPH_MAX_PASS_RESOURCE_USES` 8,
`RENDER_GRAPH_MAX_SUBRESOURCE_STATES_PER_RESOURCE` 8,
`RENDER_GRAPH_MAX_TRANSITIONS` 96, `RENDER_GRAPH_MAX_TRANSIENT_ALLOCATIONS` 16.
What happens at each boundary is undefined by test.

Barrier correctness today is proven by DX12 screenshot diffs plus a zero-error
debug layer. Those catch a wrong barrier at the point it corrupts an image or
trips validation. They do not pin the derivation that produced it, they cannot
run in `validate_tests`, they need a GPU, and they give no signal at all about
transient aliasing decisions that happen to be visually harmless today and
incorrect after the next pass is added.

The asymmetry with physics is the point. Physics has byte-exact CSVs, so physics
grew property tests, an energy oracle, and negative controls. Rendering has
screenshots, so rendering grew screenshots.

## Goal

Transition derivation, subresource state tracking, transient lifetime and
aliasing, and execution-contract validation are pinned by CPU-only unit tests that
run inside `validate_tests` with no device, and the fixed ceilings have defined,
tested boundary behavior.

## Non-Goals

- No GPU tests, no device creation, no backend test double. If a case needs a
  device it belongs in the DX12 renderer gate, not here.
- No change to render graph behavior. This plan adds tests to existing code.
- No replacement for `validate_dx12_renderer` or the bounded graphics-stress gate.
  Image-level regression remains their job.
- No render graph API change to make it testable. The surface is already pure and
  public; adding a test seam would be an admission this plan does not need to make.
- No test named for the gate or for coverage. The file is named for the subsystem.

## Ownership

- A new `SkullbonezTests/TestRenderGraph.cpp` owns graph compile, transition
  derivation, transient lifetime, and execution-contract coverage.
- `Dx12RenderGraphExecutor` behavior that requires a device stays out of this file
  and out of this plan. The boundary is exactly: the graph derives, and that is
  testable here; the executor emits, and that is not.
- The mapping from `RenderGraphResourceAccess` to `D3D12_RESOURCE_STATES` in
  `Dx12RenderGraphExecutor.cpp:95` is a pure function and may be covered here if
  it can be reached without a device; if it cannot, RG0 records that and it stays
  with the DX12 gate.

## Phases

- [ ] **RG0 — Census the compile contract and choose the oracle form.** Enumerate
  every input the graph accepts, every field of `RenderGraphCompileResult`, every
  documented derivation rule, and every failure mode reachable through
  `RenderGraphCompileResult::failed` and `ValidateFrameExecutionContract`. Decide
  and record whether assertions read structured `transitions` entries or compare
  `DumpText()` output, with a bias toward structured reads: a text golden captured
  from current output would pin whatever the graph does today rather than what it
  should do. Record which rules are stated in the header but not derivable from
  the public result, because those are review-only and must be named as such
  rather than silently uncovered.

- [ ] **RG1 — Pin ordinary transition derivation.** Build graphs by hand and
  assert the exact derived transition set: write-then-read across two passes,
  read-then-write, write-then-write, read-then-read with no transition emitted,
  a resource untouched by any pass, an external resource entering at a declared
  initial access, and the back-buffer path ending at Present. Assert transition
  count, order, source access, and destination access. Include at least one case
  where the naive answer is wrong — a resource read by two consecutive passes in
  the same access needs one transition, not two.

- [ ] **RG2 — Pin subresource state tracking.** Cover per-subresource access
  against `RENDER_GRAPH_ALL_SUBRESOURCES`, a resource whose subresources diverge
  and then converge, and the boundary at
  `RENDER_GRAPH_MAX_SUBRESOURCE_STATES_PER_RESOURCE`. Prove that an all-subresource
  transition after per-subresource divergence resolves correctly rather than
  leaving a stale per-subresource entry, which is the classic silent aliasing bug
  in this style of graph.

- [ ] **RG3 — Pin transient lifetime, aliasing, and pool reuse.** Assert
  `firstPass`/`lastPass` for transient resources under non-overlapping,
  overlapping, and nested lifetimes; assert that two transients with disjoint
  lifetimes share a `poolSlot` and that two with overlapping lifetimes do not;
  assert `createdThisCompile` and `reusedThisCompile` against hand-derived
  expectations; assert `releasedAtFrameEnd` behavior. Include a case where a
  transient is declared but never used by any pass.

- [ ] **RG4 — Pin capacity exhaustion and the execution contract, then close.**
  Drive each of the six fixed ceilings to its boundary and one past it, and assert
  the defined behavior at each — whether that is a `failed` compile result or a
  lane-F fatal, per what RG0 found. Cover `ValidateFrameExecutionContract` with a
  null/empty declaration name for the capture-only graph, with `"Present"` for an
  ordinary frame, with a missing declaration-only pass, and with a disabled
  callback. Run `tools\validate_tests.bat` and `tools\validate_fast.bat`. If no
  `Rendering/` source changed, record that DX12 gates are not triggered by a
  test-only addition; if RG1-RG4 found a defect and any `Rendering/` source
  changed to fix it, `tools\validate_dx12_renderer.bat` and
  `tools\run_graphics_stress.bat 1` both become mandatory. Audit touched files and
  obtain an independent read-only review.

## Dependencies And Decisions

- Independent of the two physics plans; ordered third only to keep one plan in
  flight at a time.
- Structured assertions are preferred over `DumpText()` goldens. A text golden is
  permitted only for a case where the structured result genuinely cannot express
  the property, and RG0 must name that case explicitly.
- Expectations are hand-derived from the graph under test. Capturing current
  compile output as the expectation would make this plan a change-detector rather
  than a correctness proof, and would leave the header's "hand-written barrier is
  a defect" claim exactly as unproven as it is now.
- If a ceiling has no defined boundary behavior today, RG4 reports that as a
  finding. Choosing the behavior is a design decision for the render graph owner,
  not something this plan decides by writing whichever assertion passes.
- This plan does not touch `Dx12RenderGraphExecutor` behavior. If RG1-RG3 show
  the graph derives correctly but the executor emits incorrectly, that is a
  separate finding and a separate plan.

## Acceptance

The plan closes when `TestRenderGraph.cpp` exists and pins transition derivation,
subresource tracking, transient lifetime and pool reuse, capacity boundaries, and
the frame execution contract; every expectation is hand-derived rather than
captured; every ceiling has tested boundary behavior or a recorded finding that it
has none; the tests run with no device inside `validate_tests`; mapped gates pass;
and independent review confirms no assertion merely restates current output.

## Validation

- `tools\validate_tests.bat`
- `tools\validate_fast.bat`
- `tools\validate_coverage.bat` run directly, since these tests raise Rendering coverage
- `tools\validate_dx12_renderer.bat` and `tools\run_graphics_stress.bat 1` — only
  if any `Rendering/` source changed
- Touched-source comment audit
- Independent read-only review

## Related

- `../../../SkullbonezSource/Rendering/RenderGraph.h`
- `../../../SkullbonezSource/Rendering/RenderGraph.cpp`
- `../../../SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp`
- `../../../SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.h`
