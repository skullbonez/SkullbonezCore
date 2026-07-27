# Render Backend Service Bag Removal — RB2 Capabilities

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`

## Result

`RuntimeRenderBackendView` and `RequireBackbufferCapture()` are deleted.
Successful DX12 startup now supplies required concrete owners directly and
makes the three optional capability decisions at composition:

- raytracing is present only when startup diagnostics report DXR reflection;
- shader development is explicitly present for the successful DX12 backend,
  preserving the existing F9 and `--dev-shader-hot-reload` behavior;
- the ImGui renderer is a development-build capability supplied by reference
  when that build surface exists.

`Run` retains the required backbuffer capture owner by reference.
`RuntimeFramePresentationView` carries that required reference and an explicit
optional shader-development reference; it no longer carries any backend view.
`RenderResourceLifecycle` retains the explicit optional raytracing reference.
No capability absence is represented by an unchecked raw pointer.

## Mechanical Evidence

- `RuntimeRenderBackendView`: zero source or test rows.
- `RequireBackbufferCapture`: zero source or test rows.
- `RuntimeRenderHost.cpp`: deleted, including its now-empty project entries.
- Project-filter inventory: 785/785 project sources are filtered.
- Aggregate inventory: 1,170 candidates, 11 stated invariants, 0 signalled,
  84 ruled review rows, 0 unruled.
- Staged diff and formatting checks are clean.

## Behavioral Evidence

- `tools\validate_build.bat Automation`: PASS, zero warnings and errors.
- F9 shader-reload Automation probe: PASS. The
  `shader_hot_reload_f9.json` action was consumed at frame 10 with an `ok`
  interaction report and no failure.
- ImGui development-surface Automation probe: PASS. The 36-action
  `imgui_editor_stress.json` script completed with an `ok` report, no failure,
  and empty stderr.
- `tools\validate_fast.bat`: PASS; 416/416 doctests and
  2,409,556/2,409,556 assertions.
- `tools\validate_dx12_renderer.bat`: PASS, run
  `20260727T005928Z`; all three comparisons accepted without baseline refresh.
  `water_ball_test` was byte-identical, `solver_smoke` averaged 0.0003
  difference, and `space_three_body` averaged 0.4245, all below the 1.0 gate.
  Archived `dx12_validation.txt` contains zero errors.

## RB3 Handoff

The remaining phase is reconciliation and closure: audit every touched header,
obtain the independent no-replacement-bag ownership review, run the DX12 gate
three consecutive times, then complete graphics stress, full, and performance
validation without refreshing a baseline.
