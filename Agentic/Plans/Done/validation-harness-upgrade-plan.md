# Validation Harness Upgrade Plan

Status: planning draft  
Created: 2026-06-11  
Scope: renderer validation, artifacts, concept validation, DX12 logs, perf budgets, reporting  
Implementation status: plan only, no code changes in this pass

## Goal

Upgrade the validation harness so rendering, shader, material, post, water, and DX12 changes produce clearer artifacts and less ambiguous failure reports.

The current scripts are already the source of truth. This plan improves diagnostics around them without weakening validation expectations.

## Current Validation Expectations

From repository instructions:

- Validation scripts are pre-commit/PR gates, not normal as-you-go checks.
- Documentation only: no validation required.
- Shader or render backend changes: `tools\validate_renderers.bat`.
- Broad or uncertain scope: `tools\validate_full.bat`.
- DX12 validation errors must be zero.
- All three renderers must produce visually identical output with average pixel diff below threshold.
- Do not claim validation success without command output.

These rules stay.

## Current Pain Points

- Renderer validation pass/fail is not always enough to locate the problem.
- Cross-renderer diff artifacts are not designed for quick visual triage.
- Concept scenes add many style/material cases not necessarily covered by older render tests.
- DX12 validation log checks should be hard to overlook.
- Performance budgets for render hot-path changes are not tied to specific render features.
- Per-pass captures are not available for debugging post/water/shader changes.

## Upgrade Areas

### 1. Renderer Diff Artifacts

Add generated artifacts for renderer comparisons:

- final screenshot per renderer,
- side-by-side composite,
- absolute diff heatmap,
- amplified diff heatmap,
- JSON summary.

Summary fields:

```json
{
  "scene": "...",
  "rendererA": "gl",
  "rendererB": "dx12",
  "averageDiff": 2.5,
  "maxDiff": 41,
  "pixelsOver10": 1234,
  "status": "pass"
}
```

### 2. DX12 Validation Log Gate

Make `validate_renderers` explicitly print:

```text
DX12 validation errors: 0
```

If the log file is missing, the script should say whether DX12 validation was unavailable or not run.

### 3. Concept Scene Suite

Add a concept validation mode once material/style work stabilizes:

```bat
tools\validate_select.bat concepts
```

or:

```bat
tools\validate_concepts.bat
```

This should run a curated subset first, not all 20 scenes every quick validation.

Suggested tiers:

- `concept_smoke`: 3 scenes covering golden hour, low-poly, neon.
- `concept_core`: 8 representative scenes.
- `concept_full`: all 20 scenes.

### 4. Per-Pass Capture Mode

For debugging only:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene ... --capture-passes
```

Artifacts:

- reflection,
- scene color,
- scene depth,
- volumetric,
- final.

This should not run in normal fast validation unless explicitly requested.

### 5. Shader Contract Check

Add a validation helper:

```bat
tools\validate_shaders.bat
```

Possible checks:

- every active shader has expected backend files,
- GLSL uniforms match manifest,
- HLSL cbuffer/textures match manifest,
- texture slots match shader contract,
- legacy shader files are listed as legacy or active.

This can start as a Python script called from `validate_fast` or `validate_renderers` later.

### 6. Render Perf Budgets

For render/material hot path changes:

- record draw calls,
- frame render CPU time,
- GPU marker times when available,
- instance upload bytes,
- DX12 descriptor copies,
- upload buffer peak.

Do not make all budgets hard failures immediately. Start with reporting, then add thresholds for stable scenes.

## Proposed Artifact Layout

```text
TestOutput/
  validation/
    renderers/
      <timestamp-or-run-id>/
        manifest.json
        gl_final.png
        dx11_final.png
        dx12_final.png
        gl_vs_dx11_heatmap.png
        gl_vs_dx12_heatmap.png
        summary.json
        dx12_validation.txt
    passes/
      <scene>/
        gl/
        dx11/
        dx12/
```

Keep existing output paths compatible. Add richer artifacts under a new directory if needed.

## Script Behavior Rules

1. Scripts should print the command they run.
2. Scripts should print key artifact paths.
3. Scripts should print concise pass/fail summaries.
4. Scripts should preserve raw logs.
5. Scripts should fail when required artifacts are missing.
6. Scripts should not hide DX12 validation errors.
7. Scripts should avoid changing baselines automatically.

## Phase Plan

### Phase 1: Artifact Manifest

Tasks:

1. Add manifest JSON to renderer validation output.
2. Include scene, renderer, viewport, command, and artifact paths.
3. No image-processing behavior change.

Validation:

- Changed validation script: `tools\validate_fast.bat`, then run changed script.

### Phase 2: Heatmap Generation

Tasks:

1. Add heatmap/side-by-side generator.
2. Write summary JSON.
3. Print paths on failure or near-threshold result.

Validation:

- Run changed script directly.
- `tools\validate_renderers.bat`.

### Phase 3: DX12 Log Gate

Tasks:

1. Ensure `dx12_validation.txt` is produced or explicitly marked unavailable.
2. Count errors.
3. Fail renderer validation on nonzero errors.

Validation:

- `tools\validate_renderers.bat`.

### Phase 4: Shader Contract Validation

Tasks:

1. Add shader manifest/check script.
2. Check high-risk shader families first.
3. Keep warnings separate from hard errors until manifests are complete.

Validation:

- `tools\validate_fast.bat`, then run shader check directly.

### Phase 5: Concept Validation Tier

Tasks:

1. Define concept smoke/core/full suites.
2. Add script entry points.
3. Avoid making all 20 scenes part of every fast render validation at first.

Validation:

- New script direct run.
- `tools\validate_renderers.bat` if integrated.

### Phase 6: Per-Pass Capture Harness

Tasks:

1. Add opt-in runtime pass capture.
2. Compare pass outputs by renderer.
3. Report first divergent pass.

Validation:

- `tools\validate_renderers.bat`.

## Validation Matrix

| Change | Validation |
|--------|------------|
| Docs only | No validation required |
| Validation script mechanics | `tools\validate_fast.bat`, then run changed script |
| Renderer validation output changes | `tools\validate_renderers.bat` |
| Shader validation script | `tools\validate_fast.bat`, then direct script run |
| Pass capture runtime changes | `tools\validate_renderers.bat` |
| Perf budget/report changes | `tools\validate_perf.bat` if perf path touched |

## Risks

| Risk | Mitigation |
|------|------------|
| Artifact generation makes validation slow | Generate heavy artifacts only on failure or opt-in. |
| Scripts become hard to read | Keep concise summary and store raw detail in files. |
| Concept suite becomes too expensive | Use smoke/core/full tiers. |
| Shader contract check blocks incomplete manifests | Start as warnings for non-manifested shaders. |
| DX12 validation missing is mistaken for success | Print explicit unavailable/missing status. |

## Success Criteria

- Renderer validation failures produce useful artifacts.
- DX12 validation status is always visible.
- Shader contract drift can be checked before runtime.
- Concept scenes can be validated in tiers.
- Perf-sensitive render changes report relevant render metrics.
