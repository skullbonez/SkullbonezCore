# Renderer Parity Debugging Plan

Status: planning draft  
Created: 2026-06-11  
Scope: GL/DX11/DX12 screenshot parity, pass capture, heatmaps, debug reporting  
Implementation status: plan only, no code changes in this pass

## Goal

Turn renderer parity from a final validation result into an inspectable debugging workflow.

The current project already treats GL, DX11, and DX12 as equal outputs and validates average pixel diffs. The next step is a "parity debugger" that helps answer:

- Which renderer diverged?
- Which pass first diverged?
- Is the difference color, depth, UV, texture filtering, blend, projection, or material data?
- Can the divergence be reproduced from a small scene/camera/config artifact?

## Current Read

Strengths:

- The engine supports GL, DX11, and DX12.
- Validation scripts already compare visual outputs.
- Scenes and suites provide deterministic launch inputs.
- Screenshot capture exists through backend APIs.
- Runtime renderer switching exists.
- The project has strict expectations for DX12 validation errors and cross-renderer pixel diffs.

Limitations:

- Validation tells that images differ, not why.
- There is no per-pass capture artifact.
- There is no heatmap output for quick visual inspection.
- There is no pixel query linking a divergent pixel to material/pass/depth state.
- Renderer-specific state logs are not unified.

## Target Tooling Layers

### Layer 1: Existing Whole-Frame Diff

Keep current validation as the source of truth:

- capture final backbuffer for each renderer,
- compute average/max pixel diff,
- fail above threshold.

Enhancement:

- Always write a concise diff summary JSON.
- Include renderer names, scene path, config, viewport, commit, and timestamp.

### Layer 2: Heatmap Artifact

For every failed or near-threshold comparison:

- write absolute-difference heatmap PNG,
- write amplified heatmap PNG,
- write side-by-side composite PNG,
- write max-diff pixel coordinates.

This can be a script-level addition before engine changes.

### Layer 3: Pass Capture

Add optional pass capture points:

- reflection color,
- scene color before water,
- scene color after water,
- scene depth,
- volumetric light,
- final tonemap.

Do not capture every pass by default. Enable with a flag such as:

```text
--capture-passes
--capture-pass-dir TestOutput/pass_captures/<scene>/<renderer>
```

### Layer 4: Pixel Probe

Future feature:

```text
tools\renderer_probe.bat <capture-set> x y
```

Output:

- final RGB by renderer,
- depth by renderer,
- pass where pixel first diverges,
- material/object ID if available,
- texture slot/sample source if instrumented,
- shader name/pass name.

This requires render IDs or pass debug buffers later. Do not start here.

## Proposed Artifacts

### Capture Manifest

```json
{
  "scene": "SkullbonezData/scenes/example.scene",
  "suite": "SkullbonezData/scenes/render_tests.suite",
  "viewport": [1280, 720],
  "frame": 120,
  "fixedStep": true,
  "renderers": ["gl", "dx11", "dx12"],
  "captures": [
    {
      "renderer": "gl",
      "pass": "final",
      "path": "gl_final.png"
    }
  ]
}
```

### Diff Summary

```json
{
  "base": "gl",
  "candidate": "dx12",
  "pass": "final",
  "averageDiff": 2.4,
  "maxDiff": 37,
  "maxPixel": [812, 312],
  "status": "pass"
}
```

## Engine Hooks Needed

### Named Passes

Renderer parity debugging benefits from `Agentic/Plans/Done/render-pipeline-extraction-plan.md`.

Each pass should have a stable name:

- `reflection`
- `sky`
- `objects`
- `terrain`
- `shadows`
- `water`
- `debug`
- `volumetric`
- `tonemap`
- `final`

### Capture Points

Add a capture interface:

```cpp
class RenderCaptureSink
{
public:
    bool IsEnabledForPass(const char* passName) const;
    void CaptureColor(const char* passName, uint32_t textureHandle);
    void CaptureBackbuffer(const char* passName);
};
```

For v1, backbuffer/final capture only can stay script-owned. Per-pass texture capture requires backend support for FBO/texture readback.

### Renderer State Snapshot

Log key state at capture:

- renderer,
- pass,
- viewport,
- current target format,
- depth enabled/write,
- blend enabled/factors,
- cull enabled,
- shader base name,
- bound texture handles/slots.

DX12 should include:

- current RTV format,
- root signature version/name,
- PSO hash,
- resource state transitions if available.

## Phase Plan

### Phase 1: Script Heatmaps

Tasks:

1. Add or extend a Python comparison helper.
2. Generate:
   - average/max diff,
   - heatmap PNG,
   - side-by-side PNG,
   - JSON summary.
3. Integrate into renderer validation logs without changing pass behavior.

Validation:

- Tool changes: `tools\validate_fast.bat`, then run the changed tool/script directly.

### Phase 2: Stable Capture Manifests

Tasks:

1. Emit a capture manifest for renderer validation runs.
2. Include exact commands and output artifact paths.
3. Keep JSON small and deterministic.

Validation:

- `tools\validate_renderers.bat` if validation scripts change.

### Phase 3: Optional Per-Pass Capture Flag

Tasks:

1. Add `--capture-passes` runtime flag.
2. Capture FBO-backed pass textures first:
   - reflection,
   - scene HDR,
   - volumetric,
   - final.
3. Avoid expensive readbacks unless flag is enabled.

Validation:

- `tools\validate_renderers.bat`.
- `tools\validate_perf.bat` only if capture hooks affect normal non-capture path.

### Phase 4: Pass Diff Report

Tasks:

1. Run the same scene on all renderers with pass capture enabled.
2. Compare each pass by name.
3. Report first divergent pass.

Validation:

- New tool direct run plus `tools\validate_renderers.bat`.

### Phase 5: Pixel Probe

Tasks:

1. Add optional object/material ID debug buffer for selected passes.
2. Add depth capture.
3. Add pixel query command.
4. Print compact report.

Validation:

- `tools\validate_renderers.bat`.

## User-Facing Commands

Potential future commands:

```bat
tools\validate_renderers.bat --artifacts
tools\renderer_diff.bat TestOutput\captures\scene_a
tools\renderer_diff.bat --pass water TestOutput\captures\scene_a
tools\renderer_probe.bat TestOutput\captures\scene_a 812 312
```

Keep current validation commands compatible. Add optional commands rather than changing the main habit abruptly.

## Validation Matrix

| Change | Validation |
|--------|------------|
| Plan/docs only | No validation required |
| Diff script only | `tools\validate_fast.bat`, then run script directly |
| Renderer validation script output changes | `tools\validate_renderers.bat` |
| Runtime capture flag | `tools\validate_renderers.bat` |
| Backend readback changes | `tools\validate_renderers.bat`, DX12 validation log check |
| Capture overhead changes | `tools\validate_perf.bat` if normal path is touched |

## Risks

| Risk | Mitigation |
|------|------------|
| Capture readback changes timing | Keep capture opt-in and off during normal validation unless needed. |
| Per-pass captures perturb DX12 state | Use explicit resource transitions and validate zero DX12 errors. |
| Artifact explosion | Capture only selected passes and only failed/near-threshold comparisons by default. |
| Heatmap thresholds hide subtle issues | Report average, max, and count of pixels over thresholds. |
| Pixel probe becomes too invasive | Defer debug ID buffers until pass capture is already useful. |

## Success Criteria

- A failed renderer parity run produces heatmaps and concise JSON.
- A future agent can identify first divergent pass without manual image hunting.
- Capture tools do not affect normal render output.
- DX12 validation remains zero-error.
