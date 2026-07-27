# Runtime Frame View Retirement FV2 Closure

Date: 2026-07-27
Plan phase: `runtime-frame-view-retirement` FV2
Result: PASS

## Result

All four reference aggregates are deleted:

- `RuntimeFrameHostView`
- `RuntimeFrameInteractionView`
- `RuntimeFrameSceneView`
- `RuntimeFramePresentationView`

`RuntimeFrameViews.h` now contains only `RuntimeUiTextFrameFacts`, a value-only
late-UI snapshot. The former `RuntimeStressController.h` and
`OperatorEditorFrameComposer.h` forwarding surfaces are deleted from source and
the Visual Studio project.

Wide input, automation, operator-UI, and stress sequences are private `Run`
coordinators under the owner-ratified direct-member exception. Delegated
`InputRouter` operations name their concrete owners explicitly; the widest
signatures contain 12 parameters, and no delegated operation receives the
complete frame surface. `Run::Execute` remains the short phase schedule and no
replacement context, service bag, callback pack, `Run&` parameter, or frame
transaction was introduced.

## Deletion And Governance Proof

- `rg -n 'RuntimeFrame(Host|Interaction|Scene|Presentation)View' SkullbonezSource SkullbonezTests`:
  zero rows.
- Aggregate governance: 1,172 candidates, 86 gated, 86 ruled, zero unruled.
  Five declaration anchors were updated to their current lines after the
  deleted view definitions shortened the files; no ruling changed.
- Extraction-scar inventory: one pre-existing ruled WorkerPool alias, zero
  unruled findings.
- Debug, Profile, and Automation x64 builds: PASS.

## Validation

- `tools\validate_fast.bat`: PASS in 129.7 seconds.
- `tools\validate_dx12_renderer.bat`: PASS; zero InfoQueue errors and all three
  committed screenshot comparisons accepted without baseline change.
- `tools\run_graphics_stress.bat 1`: PASS; 61.1-second bounded run completed
  without crash.

FV3 owns the independent whole-surface review and the complete final gate set.
