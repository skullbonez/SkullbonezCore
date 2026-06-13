# Material System V1 Implementation Plan

Status: planning draft  
Created: 2026-06-11  
Scope: render material data, scene/style material directives, object shader inputs, compatibility migration  
Implementation status: plan only, no code changes in this pass

## Goal

Add a small backend-neutral material system that replaces overloaded tint/mode behavior while preserving existing scenes and styles.

This is not a full material graph. The target is a compact, deterministic CPU material layer that supports the concept scenes and cleans up the shader architecture. DX12 is the canonical production renderer, but material authoring should stay independent of D3D12 handles so a future Vulkan or Metal backend can consume the same scene/style data.

## Current Read

Current object material flow:

1. Style files contain directives like:
   - `object_material balls 1.0 1.0 1.0 beachball`
   - `object_material prefix:chrome 0.86 0.88 0.90 chrome`
   - `object_material prefix:pineleaf 0.045 0.20 0.040 pine`
2. The scene parser maps the mode token to a float.
3. `SkullbonezRunScene.cpp` applies that to matching `GameModel` objects through `SetRenderTint`.
4. The render helper packs tint RGB and mode/override into one instance `vec4`.
5. `lit_textured_instanced` interprets `vTint.a` and `uObjectStyle` to choose behavior.

This is clever but too implicit:

- There is no renderer material ID on `GameModel`.
- Tint alpha encodes unrelated ideas.
- Material names disappear before rendering.
- Shader logic contains material families as magic integers.
- The system cannot easily support roughness, metallic, emissive, alpha, or texture mode.

## Design Principles

1. Keep material data backend-neutral.
2. Preserve old scene/style syntax at first.
3. Keep batching intact.
4. Avoid DX12 root-signature changes in v1 unless the feature explicitly requires a GPU material table.
5. Prefer explicit material params over shader hard-codes.
6. Keep physics materials separate from render materials.
7. Keep UI/debug shaders out of the material system.
8. Keep D3D12 descriptors, Vulkan descriptors, and Metal resources out of scene/style authoring.

## Data Model

### Render Material Kind

```cpp
enum class RenderMaterialKind : uint8_t
{
    Textured = 0,
    Matte = 1,
    Metal = 2,
    Emissive = 3,
    Glass = 4,
    Toon = 5,
    LowPoly = 6,
    Shadow = 7,
    Foliage = 8,
    Bark = 9,
    Stone = 10,
    Ridge = 11,
    Shore = 12,
    Pine = 13
};
```

These names match existing parser material aliases so migration is straightforward.

### Render Material Params

```cpp
struct RenderMaterial
{
    char name[32];
    RenderMaterialKind kind;

    float baseR;
    float baseG;
    float baseB;
    float alpha;

    float roughness;
    float metallic;
    float specular;
    float transmission;

    float emissiveR;
    float emissiveG;
    float emissiveB;
    float emissiveStrength;

    float stylization;
    float textureMode;
    float normalMode;
    float reserved;
};
```

V1 can be smaller if needed. The key is to stop using one overloaded float.

### Object Assignment

Add a render material assignment separate from physics:

```cpp
struct RenderMaterialAssignment
{
    uint16_t materialIndex;
    float tintR;
    float tintG;
    float tintB;
};
```

Possible home:

- Short term: `GameModel` fields.
- Better long term: a render snapshot or render component owned outside physics.

Short-term `GameModel` fields are acceptable if kept explicitly render-only and not used by solver/collision code.

## Material Presets

Start with CPU presets:

| Name | Kind | Purpose |
|------|------|---------|
| `texture` / `beachball` | Textured | Current sphere texture or procedural beachball mode. |
| `matte` / `solid` | Matte | Tinted diffuse material. |
| `metal` / `chrome` | Metal | Strong specular/reflection-like response. |
| `emissive` / `neon` | Emissive | Bloom-driving color output. |
| `glass` | Glass | Fresnel and high specular response. |
| `toon` / `pixar` | Toon | Soft stylized bands. |
| `lowpoly` | LowPoly | Faceted/quantized response. |
| `shadow` / `black` | Shadow | Dark rim material. |
| `foliage` / `leaf` | Foliage | Leaf-like lighting. |
| `bark` / `trunk` | Bark | Wood/trunk response. |
| `stone` / `rock` | Stone | Cool faceted stone. |
| `ridge` / `distant` | Ridge | Distant terrain/object response. |
| `shore` / `sand` | Shore | Sand/shore material. |
| `pine` / `conifer` | Pine | Pine needle material. |

## Scene And Style Syntax

Keep current syntax:

```text
object_material <target> <r> <g> <b> <mode>
```

Interpret `<mode>` as:

- material preset name, or
- legacy numeric material mode.

Future syntax can add named material definitions:

```text
material chrome_ball metal 0.86 0.88 0.90 roughness=0.18 metallic=1.0 specular=0.95
object_material prefix:chrome chrome_ball
```

Do not require this future syntax for v1.

## Instance Payload Options

### Option A: Packed Material Params Per Instance

Payload:

```text
mat4 model
vec4 material0  // base rgb, kind or alpha
vec4 material1  // roughness, metallic, specular, emissiveStrength
vec4 material2  // emissive rgb, flags/style
```

Pros:

- No root signature change.
- No GPU material table.
- Simple implementation for current backends and a low-risk DX12 migration step.
- Good for current object counts.

Cons:

- More instance bandwidth.
- Repeats material data for many objects with same material.

Recommendation: use this for v1 unless profiling or material complexity proves the need for an earlier DX12 GPU material table.

### Option B: Material Index Plus GPU Table

Payload:

```text
mat4 model
vec4 tintAndMaterialIndex
```

Pros:

- Less per-instance data.
- Scales better with repeated materials.

Cons:

- Requires uniform array/material texture/structured buffer design.
- More backend-specific work.
- Likely DX12 root signature/resource contract changes.

Recommendation: defer until v1 proves the need, then make DX12 the first implementation while keeping the CPU material registry and scene/style syntax unchanged.

## Shader Mapping

The object shader should map material kind to behavior:

| Kind | Shader Behavior |
|------|-----------------|
| Textured | Sample diffuse texture or procedural texture mode. |
| Matte | Diffuse response using base color and roughness. |
| Metal | High specular, colored reflection approximation. |
| Emissive | Base color plus emissive contribution before tonemap. |
| Glass | Fresnel edge, reduced diffuse, strong highlight. |
| Toon | Banded diffuse, soft rim. |
| LowPoly | Quantized normals/light bands. |
| Foliage | Hemispherical leaf lighting. |
| Bark | Grain/noise response. |
| Stone | Faceted cool material. |
| Pine | Needle-specific green response. |

Keep current functions as starting point. Rename comments and variables so the material meaning is explicit.

## Migration Plan

### Phase 1: CPU Material Types

Tasks:

1. Add render material enum and struct.
2. Add preset table.
3. Add conversion from existing material mode tokens to enum/preset.
4. Keep current `SetRenderTint` behavior untouched.

Validation:

- `tools\validate_fast.bat` if parser/config only.

### Phase 2: GameModel Render Assignment

Tasks:

1. Add render-only material assignment fields or render component.
2. Preserve `GetRenderTint` compatibility.
3. Apply existing `object_material` directives to the new assignment.
4. Add debug logging for unknown material names.

Validation:

- `tools\validate_fast.bat`.
- `tools\validate_renderers.bat` if rendering uses the new fields.

### Phase 3: Instance Payload Expansion

Tasks:

1. Update object instance data builders.
2. Update GL instanced attribute layout.
3. Update DX11 instanced input layout.
4. Update DX12 instanced input layout.
5. Update `lit_textured_instanced.vert` and `.hlsl`.

Validation:

- `tools\validate_renderers.bat`.
- `tools\validate_perf.bat` if instance upload/batch cost changes materially.

### Phase 4: Object Shader Material Params

Tasks:

1. Replace `vTint.a` mode decoding with explicit params.
2. Keep old behavior through conversion before shader.
3. Replace "directional light means procedural beachball" with material texture mode.
4. Add emissive output support.
5. Keep non-cinematic path visually compatible.

Validation:

- `tools\validate_renderers.bat`.

### Phase 5: Style Authoring Upgrade

Tasks:

1. Add optional material definition syntax.
2. Let style files define named presets.
3. Let scene files override style materials.
4. Add material assignment summary in debug/log output.

Validation:

- `tools\validate_fast.bat` for parser-only work.
- `tools\validate_renderers.bat` if authored visual output changes.

## Compatibility Requirements

- Existing `.scene` and `.style` files must still parse.
- Existing numeric material modes must still map to the same visual family.
- Existing tint-only behavior must remain available.
- Texture/beachball default must not regress.
- Physics contact material IDs must not be conflated with render materials.

## Validation Matrix

| Change | Validation |
|--------|------------|
| Docs only | No validation required |
| Parser token mapping only | `tools\validate_fast.bat` |
| GameModel render fields only | `tools\validate_fast.bat` unless visible output changes |
| Instance layout changes | `tools\validate_renderers.bat` |
| Shader material behavior | `tools\validate_renderers.bat` |
| Instance upload/batch hot path changes | `tools\validate_renderers.bat` plus `tools\validate_perf.bat` |

## Risks

| Risk | Mitigation |
|------|------------|
| Object batching breaks | Update the active DX12 layout first, and keep any still-supported comparison backend layouts in the same tightly scoped slice. |
| Material contract leaks DX12 details | Keep D3D12 descriptors and root-signature details inside renderer code, not scene/style material records. |
| Material data gets mixed into physics | Use render-specific names and avoid existing physics `materialId`. |
| Payload grows too much | Validate perf, then consider material table v2. |
| Old style files change appearance | Preserve legacy token mapping and compare baselines. |

## Success Criteria

- Material names survive from style parsing to render setup.
- `lit_textured_instanced` receives explicit material params.
- Existing material modes remain compatible.
- Concept scenes can assign multiple visual material families in one scene.
- DX12 renders the material families consistently under screenshot validation, with GL/DX11 comparison maintained only while those backends remain active.
