# Render Backend Service Bag Removal — RB1 Bindings

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`

## Result

`RuntimeRenderBackendView` no longer crosses the startup composition boundary.
`Run`, `RuntimeRenderer`, `RenderResourceLifecycle`,
`RuntimeFramePresentationView`, input execution, and both stress paths receive
only the concrete owners or exact optional capabilities they use.

`Run` defers renderer construction until `BindRenderBackend`, whose eleven
concrete operands remain below the repository limit. `RuntimeRenderer` exposes
the already-retained backend authority through typed accessors. The duplicate
seven-pointer `BackendEpochOwners` bag was deleted; `RenderResourceLifecycle`
now retains concrete references directly.

The startup-only `RuntimeRenderBackendView` remains for RB2, where composition
will publish required and optional capabilities without this transitional type.

## Mechanical Evidence

- `m_renderBackendView`: zero rows below `Runtime/App`.
- `BackendEpochOwners`: zero rows.
- `RuntimeRenderBackendView`: confined to `Runtime/App/Init.cpp` and
  `Runtime/Render/RuntimeRenderHost.*`.
- Wide-signature inventory: 4,750 operations, maximum arity 12, zero operations
  above 12.
- Aggregate inventory: 1,171 candidates, 11 stated invariants, 0 signalled,
  84 ruled review rows, 0 unruled.

## Validation

- `tools\validate_build.bat Profile`: PASS, zero warnings and errors.
- `tools\validate_fast.bat`: PASS; 416/416 doctests and
  2,409,556/2,409,556 assertions.
- `tools\validate_dx12_renderer.bat`: PASS, run
  `20260727T003740Z`; all three comparisons accepted without baseline refresh.
  Archived `dx12_validation.txt` contains zero errors.
- `tools\run_graphics_stress.bat 1`: PASS; 62.2-second bounded command,
  59.631-second shutdown sample at frame 17, clean renderer shutdown, empty
  stderr, and timeout-owned exit.
