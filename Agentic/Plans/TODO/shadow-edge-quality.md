# Shadow Edge Quality

Date: 2026-07-09 (consolidated from `In_Progress/shadow-edge-quality-plan.md`
+ its progress file)
Status: Planned — ~5% complete (inventory done; no source/shader work started)
Impact area: DX12 renderer, shaders, shadow config

## Problem

Player-scale and object-scale shadows look pixelated: receivers see too little
shadow-map detail and the filter is a square of point-sampled depth
comparisons (radius clamped to 3). Visual target: readable caster shape, soft
feathered edge, no block steps, stable under camera motion.

Current implementation (verified): `ShadowPass` builds two maps in
`RunPasses.cpp` — a broad terrain-centered frame and a tight nearby-object
frame; terrain receives only the broad frame; `lit_textured.hlsl` /
`lit_textured_instanced.hlsl` do manual square PCF; defaults are 2048 map,
radius 1, softness ~1.0.

## Non-goals

No GL/DX11 paths; no hiding the issue behind resolution alone; no per-frame
heap growth in render hot paths.

## Phases

| ID | Phase | Status |
|----|-------|--------|
| S0 | Baseline aliasing + acceptance scenes (one silhouette scene, one stress scene; capture frame + shadow-depth previews; record map settings). Acceptance: no visible stair steps at normal distance, soft edge preserving silhouette shape, no crawling under orbit/zoom, no acne or peter-panning. | Pending |
| S1 | Feed the tight object shadow map into terrain receivers: second optional shadow binding (deliberate slot/root-signature expansion — do **not** steal t4, it is the object material table), combine broad+tight by darker-wins, explicit lifetime in `ShadowPassResources`, clear disabled bindings. | Pending |
| S2 | Replace square PCF with stable filtering: Poisson disk 12–16 taps with small stable per-pixel rotation, radius driven by softness × texel size; contact-hardening PCSS only after Poisson is stable; consider DX12 comparison sampler / `SampleCmp` (confirm `R24_UNORM_X8_TYPELESS` SRV compatibility first). | Pending |
| S3 | Texel snapping for light-space centers; quality presets (High: 4096 + Poisson; Ultra: 8192) only after filter behavior is proven; retune bias. | Pending |
| S4 | Decide whether cascaded/clipmap sun shadows are still needed (2–3 camera-centered cascades with stable snapping and cross-fade) — plan-only unless S1–S3 leave broad terrain aliasing. | Pending |

## Likely files

`RunPasses.cpp`, `Runtime/Render/RuntimeRenderPasses.h`, `Rendering/Shadow.h`;
`SkullbonezData/shaders/lit_textured*.hlsl`; DX12 binding ABI in
`RenderBackendDX12.*` if adding a second map; config/scene knobs; visual
baselines only on intentional accepted changes.

## Risks

Root-signature changes can break all lit shaders — expand the slot contract
deliberately. Higher tap counts cost GPU — run `validate_perf` beyond the
current kernel budget. Soft filtering hides peter-panning until motion — test
moving shadows.

## Validation

`tools\validate_dx12_renderer.bat` per implementation slice; add
`tools\validate_perf.bat` when sample counts or hot paths materially change.
