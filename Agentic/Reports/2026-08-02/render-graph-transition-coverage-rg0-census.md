# Render Graph Transition Coverage RG0 Census

Date: 2026-08-02
Phase: RG0 complete
Impact area: Rendering render graph, CPU tests, validation routing
Behavior/source change: None; this phase is documentation-only

## Outcome

The render graph already has a useful device-free test seam, but the live plan
described the wrong seam and the wrong result type. Sixteen pure RenderGraph
tests already live in
`Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp:742-1132`. They are
merge-gated through
`validate_all_cpu_tests.bat -> validate_dx12_arch_tests.bat`, but they are not
part of `validate_tests.bat` and they do not contribute to instrumented product
coverage. `validate_coverage.bat` instruments only the Debug
`SKULLBONEZ_TESTS.exe` process.

The existing tests cover representative transition, subresource, transient,
callback, and execution-contract behavior. They do not cover the complete
ordinary transition matrix, the six fixed-capacity boundaries, multi-resource
transient overlap, or the complete execution-contract invalid matrix. RG1-RG4
therefore remain justified, but as residual coverage rather than first coverage.

The selected oracle policy is:

1. Hand-derive expected values and assert the structured graph result.
2. Use the existing child-process pattern for Lane-F `SB_FATAL` boundaries.
3. Do not use `DumpText()` goldens. A dump golden would pin formatting and
   current output together, while the structured types express every property
   this campaign needs.

## Sources Read

- `SkullbonezSource/Rendering/RenderGraph.h`
- `SkullbonezSource/Rendering/RenderGraph.cpp`
- `SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.cpp`
- `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp`
- `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.vcxproj`
- `SKULLBONEZ_TESTS.vcxproj`
- `tools/validate_tests.bat`
- `tools/validate_coverage.bat`
- `tools/validate_dx12_arch_tests.bat`
- `tools/validate_all_cpu_tests.bat`
- `.github/workflows/mandatory-cpu-validation.yml`

CodeGraph was current at the census tip and was used first to map the public
surface, compile path, callers, and existing tests. Every conclusion below was
then checked against the files above.

## Complete Public Input Surface

The graph accepts the following semantic inputs. `Clear()` and the two
`ReserveForRuntimePassGraph()` calls only reset or verify fixed storage; they do
not add graph meaning.

| Input | Exact accepted facts |
|---|---|
| External resource | Nullable/empty `name` (resolved to `UnnamedResource`), any initial `RenderGraphResourceAccess`, and an optional non-owning native-resource token. A repeated resolved name returns the existing external handle; it may fill a previously empty token, but two different nonzero tokens are fatal. The first declaration retains the initial access. |
| Transient resource | Nullable/empty `name` (resolved to `UnnamedTransientResource`), `Texture2D` or `Buffer`, `Unknown`/`RGBA8`/`RGBA16F`/`Depth24Stencil8` format, nonzero width/height/mip count, four descriptor-need booleans (RTV, DSV, SRV, UAV) with at least one selected, and any initial access. Each call declares a new resource. |
| Pass | Nullable/empty `name` (resolved to `UnnamedPass`), insertion order, and `Graphics`, `Compute`, or `Copy` queue. A new pass is declaration-only and callback-enabled until callback registration changes its owner. |
| Read/write use | Pass index, resource handle, one concrete access, and either `RENDER_GRAPH_ALL_SUBRESOURCES` or a numeric subresource. Reads and writes have independent fixed lists. |
| Callback | Pass index, typed callback with or without a borrowed payload, enabled flag, and nullable/empty debug label. The label falls back to the pass name. |
| Compile | By-value `Compile()` or the out-parameter overload. Both clear and rebuild the same structured result; compilation does not invoke callbacks or materialize backend resources. |
| Callback execution | `DryRun` or `Execute`, plus a first-pass index and pass count. The range is ordered and bounded by the current pass list. |
| Frame execution contract | Nullable/empty declaration-only pass name means a capture graph expects zero declaration-only rows. A nonempty name means an ordinary graph expects exactly one row with that exact name. |

The access enum is `Unknown`, `RenderTarget`, `DepthRead`, `DepthWrite`,
`ShaderResource`, `PixelShaderResource`, `NonPixelShaderResource`,
`UnorderedAccess`, `CopySource`, `CopyDest`,
`VertexAndNonPixelShaderResource`, and `Present`. `Unknown` is permitted as an
initial access but not as a pass read or write.

`RenderGraphTextureBinding` and `RenderGraphBackbufferBinding` are adjacent
value contracts, not builder inputs. The queue value is retained in `Passes()`;
the current serial compiler does not branch on it.

## Exact Compile Result

`RenderGraphCompileResult` has no `failed` field. It has exactly four members:

| Member | Fields and meaning |
|---|---|
| `transitions` | Each row has `passIndex`, graph `resource` handle, copied `nativeResource` token, `before`, `after`, and `subresource`. |
| `resourceLifetimes` | One row per declared resource with `resource`, `firstPass`, `lastPass`, and `used`. Unused external resources remain structured rows with `used=false`. |
| `transientAllocations` | Each used transient has `resource`, `poolSlot`, `firstPass`, `lastPass`, `descriptorCount`, `reused`, and `releasedAtFrameEnd`. |
| `transientDiagnostics` | `allocationCount`, `reuseCount`, `releaseCount`, `highWaterResources`, and `highWaterDescriptors`. |

The stale `createdThisCompile`, `reusedThisCompile`, materialization `failed`,
HRESULT, and failure strings belong to the separate backend-owned
`RenderGraphTransientMaterializationStats`. `dryRunValidatedPassCount` belongs
to `RenderGraphCallbackExecutionResult`. None is a compile-result field.

## Exact Derivation Rules

1. Compilation clears the supplied result, emits one lifetime row for every
   resource, and initializes each resource's all-subresource state from its
   declared initial access.
2. Passes are visited in insertion order. Within a pass every read is visited
   before every write, each in declaration order.
3. A use emits no transition when the tracked state is `Unknown` or already
   equals the requested access. An `Unknown` first use adopts the requested
   access. Otherwise compilation emits one row before the consuming pass and
   updates the tracked state.
4. A numeric subresource uses its exact override when present, otherwise the
   resource's all-subresource state. An override is removed when that
   subresource returns to the all-subresource state.
5. An all-subresource use first emits transitions for current numeric
   overrides, in stored subresource order, then clears the override list. If
   the all-subresource state is `Unknown`, the use becomes that state without a
   transition. If numeric overrides remain and the requested state differs from
   the all-subresource state, compilation is fatal rather than inventing a
   lossy mixed-state barrier.
6. The first use of a resource sets `firstPass` and `used`; every later use
   updates `lastPass`. Reads and writes participate equally.
7. Every transient must be used. Transients are considered in resource
   declaration order. A resource reuses the first occupied pool slot whose
   `lastPass` is strictly less than its `firstPass` and whose kind, format,
   dimensions, mip count, and four descriptor flags are all equal.
8. A transient's descriptor count is the number of selected descriptor flags.
   Every planned allocation is marked released at frame end. Diagnostics count
   allocations, reused rows, releases, and the maximum simultaneously live
   resources and descriptors over pass indices.

These are serial graph-compile rules. Queue scheduling, barrier coalescing,
backend object creation, and asynchronous execution are not inferred.

## Failure Semantics And Fixed Ceilings

There is no recoverable compile result. Public builder and compiler contract
violations use Lane F through `SB_FATAL`. The structured
`ValidateFrameExecutionContract()` path never fatals; it reports facts and
`IsValid()` combines them.

### Six named ceilings

| Ceiling | Boundary that succeeds | One-past behavior | Existing boundary test |
|---|---|---|---|
| Resources: 24 | 24 stored rows: unique resolved external names plus every transient declaration | The 25th stored resource is Lane-F fatal during `AddExternalResource` or `AddTransientResource`. | None |
| Passes: 24 | 24 pass declarations | The 25th pass is Lane-F fatal during `AddPass`. | None |
| Pass uses: 8 | Eight reads and, independently, eight writes per pass; a pass can therefore hold 16 total uses. | The ninth read or ninth write is Lane-F fatal in `RenderGraphResourceUseList::push_back`. | None |
| Subresource states: 8 | Eight active numeric overrides for one resource | Creating the ninth active override is Lane-F fatal during `Compile`. Returning an override to the all-state removes it and frees a row. | None |
| Transitions: 96 | Exactly 96 emitted transition rows | Emitting transition 97 is Lane-F fatal during `Compile`. | None |
| Transient allocations: 16 | Sixteen used transient resources | Planning the 17th used transient is Lane-F fatal during `Compile`. | None |

The compiler also has a 24-row local transient-pool array and a defensive
`Transient pool capacity exceeded` fatal. That path is unreachable through the
public graph: the 16-row transient-allocation ceiling terminates compilation on
the 17th used transient, before a public graph can need a 25th distinct pool
slot. RG4 must record this ordering, not manufacture an unreachable child case.

### Other public graph failures

| Operation | Lane-F condition | Current pure test |
|---|---|---|
| External declaration | Resolved name aliases a transient, or the same external name is associated with two different nonzero native tokens. | None |
| Transient declaration | Width, height, or mip count is zero; no descriptor need is selected. | None |
| Read/write declaration | `Unknown` pass access, invalid resource handle, invalid pass index, or a full read/write list. | Unknown read/write and one bad read resource/one bad write pass are covered. |
| Callback registration/execution | Invalid pass index; execution range outside the pass list; a callback record was poisoned/missing; an enabled callback pass declares neither a read nor a write. The code also has a defensive `size_t` addition-overflow check, but two `uint32_t` inputs cannot reach it on the supported x64 platform. | Callback without declarations is covered. |
| Compile | Mixed numeric state cannot be represented by a different all-subresource target; unused transient; subresource, transition, transient-allocation, or defensive pool capacity exhaustion. | Mixed-state and unused-transient cases are covered. |

The generic fixed-list helper also fatals on oversize reserve, resize, or push.
Graph-owned reserves request their exact capacities, and named builder/compiler
checks above are the semantic public boundaries RG4 should test.

## Execution-Contract Result

`RenderGraphExecutionContractResult` contains:

- `callbackPassCount`;
- `declarationOnlyPassCount`;
- `expectedDeclarationOnlyPassCount` (zero for null/empty input, otherwise one);
- `declarationOnlyNameMatches`, the conjunction of exact names for all
  declaration-only rows; and
- `allCallbacksEnabled`, the conjunction of callback enabled flags.

`IsValid()` requires the actual and expected declaration-only counts to match,
all declaration-only names to match, and all callbacks to be enabled. It does
not require a minimum callback count. Callback count is informational.

## Existing Pure Test Inventory

The following 16 functions are registered in the standalone DX12 architecture
CPU executable. Seven named child cases cover the fatal paths grouped by the
negative test functions.

| # | Existing test | What it actually pins |
|---:|---|---|
| 1 | `TestRenderGraphSkipsUnknownInitialTransition` | Unknown first use emits nothing; later incompatible use emits one exact transition. |
| 2 | `TestRenderGraphExplicitInitialStateTransitions` | Present -> RenderTarget -> Present transition order. |
| 3 | `TestRenderGraphTracksSubresourceTransitionsIndependently` | Two numeric subresources diverge independently, one returns, and the DX12 dry-run records the same subresource ids. |
| 4 | `TestRenderGraphAllowsUniformSpecificThenAllSubresourceTransition` | A numeric use equal to the all-state creates no override; a later all-resource transition succeeds. |
| 5 | `TestRenderGraphClearsSpecificStateWhenItReturnsToAllState` | One numeric override is removed before a later all-resource transition. |
| 6 | `TestRenderGraphRejectsMixedSpecificThenAllSubresourceTransition` | A different all-state request while a divergent override remains is fatal. |
| 7 | `TestRenderGraphRejectsUnknownPassAccess` | Unknown reads and writes are fatal. |
| 8 | `TestRenderGraphRejectsBadHandles` | One invalid resource and one invalid pass index are fatal. |
| 9 | `TestRenderGraphPlansTransientResourceLifetime` | One transient's allocation lifetime, descriptor count, release flag, and basic diagnostics. |
| 10 | `TestRenderGraphReusesCompatibleNonOverlappingTransientResources` | Two compatible disjoint transients share a slot and report one reuse. |
| 11 | `TestRenderGraphRejectsUnusedTransientResource` | An unused transient is fatal. |
| 12 | `TestRenderGraphExecutesCallbacksInPassOrder` | Callback/declaration-only counts and execution order. |
| 13 | `TestRenderGraphFrameEdgesKeepOnlyPresentDeclarationOnly` | External name identity, Present frame edge, null capture contract, extra declaration rejection, and disabled callback rejection. |
| 14 | `TestRenderGraphDryRunValidatesCallbacksWithoutExecuting` | Dry-run count increments without invoking payload code. |
| 15 | `TestRenderGraphDisabledCallbackDoesNotExecute` | Disabled callback count and zero execution. |
| 16 | `TestRenderGraphRejectsCallbackWithoutResourceDeclarations` | An enabled callback with no resource use is fatal. |

These tests compile `RenderGraph.cpp` directly in
`Dx12ArchUnitTests.vcxproj`. The main `SKULLBONEZ_TESTS.vcxproj` currently
contains neither `RenderGraph.cpp` nor a RenderGraph test translation unit.
Consequently `validate_tests.bat` and OpenCppCoverage see none of this execution,
even though hosted mandatory CPU validation does run the standalone executable
through the six-lane CPU umbrella.

## Binding Residual Work

### RG1: ordinary transition matrix and main-test routing

- Add subsystem-named `SkullbonezTests/TestRenderGraph.cpp` and the minimum
  project/filter entries needed to compile the production `RenderGraph.cpp`
  into `SKULLBONEZ_TESTS`.
- Hand-derive explicit all-subresource write->read, read->write,
  write->write, and read->read/no-transition cases.
- Pin an untouched external resource, Unknown and concrete external entry
  states, and the backbuffer ending at Present.
- For every case assert count, order, pass index, resource handle, copied native
  token where supplied, before/after access, and subresource. The standalone
  tests cover representative subsets, not this complete matrix.

### RG2: remaining subresource state properties

- Preserve the existing independent, uniform, return-to-all, and mixed-state
  fatal facts.
- Add a genuinely divergent multi-subresource graph that converges through an
  all-subresource use equal to the base state; assert each derived numeric
  transition and then prove no stale override affects the next all-resource
  use.
- Prove eight active overrides succeed and the ninth reaches the exact Lane-F
  capacity path.
- Pin deterministic transition order for multiple stored subresource states.

### RG3: remaining transient planning properties

- Add overlapping and nested lifetimes that cannot alias, alongside the
  already-covered compatible disjoint reuse.
- Cover exact compatibility dimensions: kind, format, width/height, mip count,
  and descriptor flags. Any difference prevents reuse.
- Assert every lifetime row including unused external `used=false`, pool-slot
  selection, first-row `reused=false`, exact allocation/reuse/release counts,
  descriptor counts, and resource/descriptor high-water for a multi-resource
  graph.
- Carry the existing unused-transient fatal into the main test lane. Do not
  assert backend `createdThisCompile`/`reusedThisCompile`; they are not compiler
  outputs.

### RG4: boundaries and execution contract

- For all six named ceilings, prove the boundary succeeds and use child
  processes for the one-past Lane-F diagnostic. Treat reads and writes as two
  independent eight-row lists.
- Record the 24-slot transient pool path as publicly unreachable because the
  16-allocation fatal wins first.
- Add empty-string capture validation, an ordinary graph missing Present, one
  wrong declaration-only name, one extra declaration-only row, and one disabled
  callback. Assert every structured count/boolean as well as `IsValid()`.
- Close through the mandatory CPU umbrella so the main doctests, instrumented
  coverage, and existing DX12 architecture tests all run from one merge gate.

## Review-Only Properties

Structured CPU tests can prove declaration and derivation facts. They cannot by
themselves prove:

- that no ordinary pass elsewhere emits a hand-written backend barrier;
- that the DX12 command list emits every compiled transition on-device;
- that Present, cold capture/readback, the development ImGui viewport copy, and
  shutdown/resize remain the only reviewed frame-transition exceptions;
- that upload, mip, dynamic-geometry, and acceleration-structure barriers stay
  with their resource owners;
- backend transient creation/reuse/failure (`RenderGraphTransientMaterializationStats`),
  descriptor ownership, or device failure HRESULT behavior; or
- future cross-queue scheduling or asynchronous ordering. The current compiler
  is intentionally serial even though pass declarations retain a queue enum.

The existing architecture tests do cover access-to-DX12-state mapping and
off-device dry-run records. Real barrier emission, resource lifetime, and image
correctness remain owned by the DX12 renderer and graphics-stress gates if
production Rendering source changes.

## Validation And Comment Ruling

RG0 changed Markdown only. No repository validation, build, test launch,
coverage run, baseline refresh, or touched-source comment audit was required.
RG1 is binding next. Its test/source-bearing files will require the normal
touched-file comment audit, and the completed plan will use the gate mapping in
the updated plan.

## Independent Review

Independent read-only review returned **ACCEPT** with no findings. It verified
the 16-test inventory and routing, exact compile-result ownership, all six
ceiling semantics, the unreachable transient-pool guard, RG1-RG4 allocation,
review-only boundaries, Related paths, date, and 1/15 ledger math against the
current source and validation scripts.

## Related

- `../../../SkullbonezSource/Rendering/RenderGraph.h`
- `../../../SkullbonezSource/Rendering/RenderGraph.cpp`
- `../../../Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp`
- `../../../tools/validate_all_cpu_tests.bat`
- `../../../tools/validate_coverage.bat`
- `../../Plans/TODO/render-graph-transition-coverage.md`
