# Render Graph Transition Coverage RG1 Ordinary Transitions

Date: 2026-08-02
Phase: RG1 complete
Impact area: Rendering render graph, main CPU tests, instrumented coverage
Production behavior change: None

## Outcome

Production `SkullbonezSource/Rendering/RenderGraph.cpp` now compiles directly
into `SKULLBONEZ_TESTS`, alongside a subsystem-named
`SkullbonezTests/TestRenderGraph.cpp`. This moves the pure compiler contract
into both the ordinary doctest lane and OpenCppCoverage without changing the
RenderGraph implementation or removing the 16 complementary standalone DX12
architecture cases.

Four hand-authored graphs produce 47 assertions. Their expected rows are
derived from declared initial state and pass order rather than copied compiler
output:

1. Three resources jointly prove write-then-read, read-then-write, and
   write-then-write transitions in read-before-write declaration order. Unknown
   first uses and a concrete same-state read emit no row.
2. Two identical reads from a concrete `CopyDest` entry emit only the first
   `CopyDest -> PixelShaderResource` transition.
3. An untouched external resource emits no transition while a used depth
   resource emits its exact `DepthWrite -> DepthRead` row.
4. A Present backbuffer emits `Present -> RenderTarget`, suppresses a repeated
   render-target write, and returns through `RenderTarget -> Present`.

Every emitted row checks count before indexing, pass index, resource identity,
copied native-resource token, before/after access, and all-subresource scope.

## Test Integration And Build Ownership

`SKULLBONEZ_TESTS.vcxproj` and its filter file each contain one entry for the
new test and one for production `RenderGraph.cpp`. The source is shared with the
engine and standalone architecture target. The build-configuration inventory
therefore records exact current fingerprints for its intentional differences:

- exception handling:
  `cef6efc2f1ebf34ec9315d8ce048e83fe4666e674a928ae930855f33f91bfd09`;
- preprocessor definitions:
  `b2ce12953a519d9255ebe44815f61f80cbb2d8e92a29a38e83756daefd8df39d`.

`RenderGraph.cpp` contains no conditional-compilation branch. The executable
role macros and `/EHsc` difference therefore do not change the pure algorithm.
The strict inventory reports 1,726 compile rows, 326 files, 68 shared files,
136 intentional divergent pairs, zero dropped list inheritance, and zero
blocking findings.

## Validation

- `tools\validate_build.bat Profile`: PASS with the new test and production
  graph source compiled and linked.
- `Profile\SKULLBONEZ_TESTS.exe --test-case="Render graph*"`: PASS, 4/4 cases
  and 47/47 assertions.
- `tools\validate_build.bat Debug`: PASS with zero warnings or errors.
- `Debug\SKULLBONEZ_TESTS.exe --test-case="Render graph*"`: PASS, 4/4 cases and
  47/47 assertions.
- Build-configuration self-tests and strict inventory: PASS, zero blockers.
- `tools\validate_format.bat`: PASS; formatting and 2,036 repository-relative
  `Related` paths are clean.
- `tools\validate_tests.bat`: PASS in 44.9 seconds; project/filter integration,
  Profile build, and the full doctest harness are clean.
- `tools\validate_coverage.bat`: PASS in 74.1 seconds; every ratified subsystem
  floor passes, whole instrumented product output is 23,853/30,812 lines
  (77.41%), and `RenderGraph.cpp` now contributes 143/459 lines (31.15%).
- `tools\validate_fast.bat`: PASS in 402.3 seconds; 804/804 production project
  and filter rows, dependency proof, all seven governance inventories,
  Profile/Debug builds, and tests are clean.
- Touched-source comment audit: 1/1 checked, zero deferred.

No DX12 renderer or graphics-stress gate was triggered because no production
Rendering source changed. No baseline, golden, screenshot, schema, or coverage
floor was refreshed.

## Independent Review

Independent read-only review returned **ACCEPT** with no findings. It verified
all four hand-derived graphs against the compiler traversal, every row field and
index guard, singular project/filter entries, exact build fingerprints, all
seven governance inventories, ownership questions, Related paths, and the 1/1
comment audit. It found no RG2-RG4 scope, captured output, device dependency,
fatal case, backend double, baseline change, or production edit.

## Related

- `../../../SkullbonezTests/TestRenderGraph.cpp`
- `../../../SkullbonezSource/Rendering/RenderGraph.h`
- `../../../SkullbonezSource/Rendering/RenderGraph.cpp`
- `../../Plans/TODO/render-graph-transition-coverage.md`
- `render-graph-transition-coverage-rg0-census.md`
