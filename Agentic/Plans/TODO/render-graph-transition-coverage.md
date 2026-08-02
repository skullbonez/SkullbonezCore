# Render Graph Transition Coverage

Date: 2026-08-02
Status: IN PROGRESS — 2/5 phases complete
Impact area: Rendering render graph, DX12 render graph executor, tests
Owner: Rendering render graph
Priority: First — binding remaining campaign plan

## Problem And Evidence

`SkullbonezSource/Rendering/RenderGraph.h` states the strongest correctness claim
in the renderer:

> Declared pass accesses are the single source of truth for ordinary frame-resource
> transitions. The graph derives them; the backend executor emits them. A
> hand-written ordinary frame-pass barrier is a defect.

That invariant is not wholly review-only. Sixteen device-free RenderGraph test
functions already exist in
`Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp:742-1132`. They compile
the production `RenderGraph.cpp` directly and cover representative transition,
subresource, transient, callback, fatal, and execution-contract behavior. They
are merge-gated through
`validate_all_cpu_tests.bat -> validate_dx12_arch_tests.bat`, but not through
`validate_tests.bat`. `validate_coverage.bat` instruments only the Debug
`SKULLBONEZ_TESTS.exe`, so none of the existing graph execution contributes to
the product coverage report.

There is still no `TestRenderGraph.cpp` in `SkullbonezTests/`, and
`SKULLBONEZ_TESTS.vcxproj` does not compile `RenderGraph.cpp`. The remaining gap
is a complete hand-derived rule matrix in the main test/coverage lane, not the
absence of all CPU coverage.

This is not a case where testing is expensive. The compile surface is pure CPU
and needs no device, no window, and no backend:

- `RenderGraphCompileResult Compile() const` and its out-parameter overload
  return `transitions`, one `resourceLifetimes` row per resource,
  `transientAllocations`, and `transientDiagnostics`.
- `RenderGraphExecutionContractResult ValidateFrameExecutionContract( const char* )`
  is pure and reports counts/booleans; it does not terminate on an invalid
  contract.
- `AddExternalResource`, `AddTransientResource`, `AddPass`, `AddRead`, `AddWrite`
  build a graph from plain values.

The compile result has no `failed` field. Builder and compiler contract failures
are Lane-F `SB_FATAL` paths. `createdThisCompile`, `reusedThisCompile`, backend
failure state/HRESULT, and backend release counts belong to the separate
`RenderGraphTransientMaterializationStats`; `dryRunValidatedPassCount` belongs
to `RenderGraphCallbackExecutionResult`.

The fixed ceilings have no boundary tests: `RENDER_GRAPH_MAX_RESOURCES` 24,
`RENDER_GRAPH_MAX_PASSES` 24, `RENDER_GRAPH_MAX_PASS_RESOURCE_USES` 8 for each
of the independent read and write lists,
`RENDER_GRAPH_MAX_SUBRESOURCE_STATES_PER_RESOURCE` 8,
`RENDER_GRAPH_MAX_TRANSITIONS` 96, `RENDER_GRAPH_MAX_TRANSIENT_ALLOCATIONS` 16.
The compiler's separate 24-slot local transient-pool fatal is unreachable
through the public graph because the 16-allocation ceiling terminates the 17th
used transient first.

DX12 screenshot diffs and the zero-error debug layer remain the on-device
proof. The existing architecture executable adds off-device structured proof,
including access-to-state mapping and dry-run barrier records. What remains
unproved is the exhaustive derivation/capacity matrix in `validate_tests`, the
instrumented Rendering coverage contribution, and backend-only properties that
no compile result can express.

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
  derivation, transient lifetime, capacity, and execution-contract coverage in
  the main doctest/coverage lane. Minimal project/filter and shared fatal-child
  dispatcher wiring belongs to that test integration, not to production graph
  behavior.
- The existing 16 cases in `Agentic/Tests/Dx12ArchUnitTests` remain the
  standalone architecture/DX12 mapping proof. RG1-RG4 add the missing matrix to
  `SKULLBONEZ_TESTS`; they do not erase valid independent coverage merely to
  move it.
- `Dx12RenderGraphExecutor` behavior that requires a device stays out of this file
  and out of this plan. The boundary is exactly: the graph derives, and that is
  testable here; the executor emits, and that is not.
- The pure mapping from `RenderGraphResourceAccess` to
  `D3D12_RESOURCE_STATES` is already covered by the standalone architecture
  executable. It stays there; this plan does not duplicate DX12-owner tests in
  the subsystem doctest file.

## Phases

- [x] **RG0 — Census the compile contract and choose the oracle form.** The
  complete input/result/derivation/failure census corrects the stale plan model,
  maps all 16 existing standalone tests and their validation routing, and names
  exact RG1-RG4 residual work. Assertions will be hand-derived structured reads;
  Lane-F boundaries use child processes; `DumpText()` goldens are rejected.
  Backend emission, reviewed manual-barrier exceptions, transient
  materialization, and cross-queue behavior remain explicitly review/device-gate
  owned. Evidence:
  `../../Reports/2026-08-02/render-graph-transition-coverage-rg0-census.md`.

- [x] **RG1 — Pin the residual ordinary transition matrix in the main lane.**
  `TestRenderGraph.cpp` now runs production `RenderGraph.cpp` in the main
  doctest and coverage executable. Four hand-derived cases pin write-then-read,
  read-then-write, write-then-write, repeated-read/no-op, untouched-resource,
  Unknown-entry, concrete-entry, copied-native-token, and Present-return facts
  through exact structured rows. The existing 16 standalone architecture cases
  remain unchanged. Evidence:
  `../../Reports/2026-08-02/render-graph-transition-coverage-rg1-ordinary-transitions.md`.

- [ ] **RG2 — Pin residual subresource state tracking.** Preserve the existing
  independent-subresource, uniform-specific, return-to-all, and mixed-state fatal
  facts. Add a genuinely divergent multi-subresource graph that converges through
  an all-subresource use equal to the base state; assert each numeric transition
  in deterministic stored order and prove the next all-resource use sees no stale
  override. Prove eight active overrides succeed and the ninth reaches the exact
  Lane-F capacity path.

- [ ] **RG3 — Pin residual transient lifetime, aliasing, and diagnostics.** Add
  overlapping and nested lifetimes that cannot alias alongside the existing
  compatible disjoint reuse. Vary kind, format, dimensions, mip count, and each
  descriptor flag to prove exact compatibility. Assert all lifetime rows,
  including unused external `used=false`, pool-slot/reuse decisions,
  `releasedAtFrameEnd`, descriptor counts, and exact allocation/reuse/release and
  high-water diagnostics. Carry the existing unused-transient Lane-F fact into
  the main lane. Do not assert backend-only `createdThisCompile` or
  `reusedThisCompile` as compile output.

- [ ] **RG4 — Pin capacity exhaustion and the execution contract, then close.**
  Drive each of the six named ceilings to its boundary and use child processes
  for every one-past Lane-F diagnostic. Treat reads and writes as independent
  eight-row lists; record the 24-slot transient-pool fatal as publicly
  unreachable because the 16-allocation fatal wins first. Cover null and empty
  capture names, valid Present, missing Present, wrong/extra declaration-only
  rows, and a disabled callback; assert every count/boolean and `IsValid()`.
  Close through `tools\validate_all_cpu_tests.bat` and
  `tools\validate_fast.bat`. If no `Rendering/` source changed, record that DX12
  gates are not triggered by a test-only addition; if RG1-RG4 finds a production
  defect, `tools\validate_dx12_renderer.bat` and
  `tools\run_graphics_stress.bat 1` become mandatory. Audit touched files and
  obtain one independent read-only closure review.

## Dependencies And Decisions

- The two physics plans and RG1 are complete; RG2 is binding next.
- Use hand-derived structured assertions. RG0 found no required property that the
  structured compile, callback, execution-contract, resource, or pass surfaces
  cannot express, so `DumpText()` goldens are rejected for this plan.
- Expectations are hand-derived from the graph under test. Capturing current
  compile output as the expectation would make this plan a change-detector rather
  than a correctness proof, and would leave the header's "hand-written barrier is
  a defect" claim exactly as unproven as it is now.
- All six named ceilings have defined Lane-F one-past behavior. RG4 pins those
  diagnostics without changing policy. The separate local transient-pool
  capacity is unreachable through the public graph and is recorded, not faked.
- `ValidateFrameExecutionContract` is a recoverable structured predicate. It is
  not a compile failure channel and no test may expect it to fatal.
- This plan does not touch `Dx12RenderGraphExecutor` behavior. If RG1-RG3 show
  the graph derives correctly but the executor emits incorrectly, that is a
  separate finding and a separate plan.

## Acceptance

The plan closes when `TestRenderGraph.cpp` exists and pins the residual ordinary
transition matrix, subresource tracking, transient lifetime/pool reuse, all six
capacity boundaries, and the frame execution contract; every expectation is
hand-derived rather than captured; the unreachable local pool fatal is recorded;
the tests run with no device inside `validate_tests` and instrumented coverage;
the existing standalone architecture cases remain merge-gated; mapped gates
pass; and independent review confirms no assertion merely restates current
output.

## Validation

- `tools\validate_all_cpu_tests.bat` at closure; it runs `validate_tests`,
  `validate_coverage`, and `validate_dx12_arch_tests` once through the mandatory
  CPU umbrella
- `tools\validate_fast.bat`
- `tools\validate_dx12_renderer.bat` and `tools\run_graphics_stress.bat 1` — only
  if any `Rendering/` source changed
- Touched-source comment audit
- Independent read-only review

## Related

- `../../../SkullbonezSource/Rendering/RenderGraph.h`
- `../../../SkullbonezSource/Rendering/RenderGraph.cpp`
- `../../../SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp`
- `../../../SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.h`
- `../../Reports/2026-08-02/render-graph-transition-coverage-rg0-census.md`
