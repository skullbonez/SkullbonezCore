# Scene And Style Data Model Plan

Status: planning draft  
Created: 2026-06-11  
Scope: scene files, style files, render style config, material assignments, parser organization  
Implementation status: plan only, no code changes in this pass

## Goal

Clarify the data model boundary between:

- scene structure,
- physics/gameplay setup,
- camera/suite behavior,
- render style,
- material presets,
- per-object material assignments,
- live style reload.

The current style system is useful, but render look data is still routed through `CinematicRenderConfig` and object materials collapse into tint plus a mode float. This plan makes scene/style data easier to extend without turning scene files into code.

## Current Read

Scene files can define:

- physics options,
- frame count and seed,
- camera,
- terrain/water visibility,
- generated object counts,
- explicit balls/boxes,
- cinematic render toggles,
- many `cinematic_*` overrides,
- `style <name>` includes,
- `object_material` assignments.

Style files currently define:

- `cinematic_*` directives,
- `cinematic_style_modes`,
- terrain palette,
- water profile,
- object material overrides.

This is pragmatic, but the name "cinematic" now covers broader render style, and material data is not first-class.

## Desired Separation

### Scene Data

Scene data should answer:

- What objects exist?
- Where are they?
- What physics state do they have?
- What camera and runtime options should launch?
- Which style should apply?
- Which explicit overrides should apply?

### Style Data

Style data should answer:

- What should the world look like?
- What sky, post, terrain, water, and material presets apply?
- What object groups receive which visual materials?

### Material Data

Material data should answer:

- What base color, roughness, metallic, emissive, and stylization parameters define a named material?
- Which objects/groups use it?

### Runtime/UI Data

Runtime/UI state should answer:

- What has the user temporarily overridden?
- Which live style is selected?
- Which renderer/debug flags are active?

Do not serialize transient UI slider state into scene files unless explicitly saving a style/scene.

## Proposed Data Layers

```text
engine defaults
  -> engine.cfg
    -> included style file(s)
      -> scene-local style/render overrides
        -> command-line overrides
          -> live UI/runtime overrides
```

Clear precedence matters. The current code already has pieces of this. The plan is to document and enforce it.

## Style File Semantics

Style files should:

- be render-only,
- include other style files,
- define style params,
- define material presets,
- define object material assignments,
- avoid physics/collision/gameplay changes.

Style include:

```text
style _concept_base
```

Rules:

- Includes are applied in order.
- Later directives override earlier directives.
- Cycles should fail clearly.
- Missing style files should fail with path and including file.

## Scene File Semantics

Scene files may:

- include one or more styles,
- add explicit render overrides,
- add material assignments for objects in that scene,
- define physics and object data.

Scene-local overrides should win over style includes.

## Proposed New/Refined Directives

Keep current directives compatible. Add clearer aliases gradually.

### Render Style

Current:

```text
cinematic_rendering on
cinematic_style_modes 11 7 6 4
cinematic_style_grade 1.62 1.23 0.48
```

Future aliases:

```text
render_style on
style_modes sky=lowpoly terrain=lowpoly objects=lowpoly water=stylized_basin
post_grade saturation=1.62 contrast=1.23 vignette=0.48
```

Do not remove `cinematic_*` until all scenes/docs migrate.

### Material Definitions

Future:

```text
material chrome metal base=0.86,0.88,0.90 roughness=0.12 metallic=1.0 specular=0.95
material neon_cyan emissive base=0.02,0.95,1.00 emissive=0.02,0.95,1.00 strength=2.5
```

### Object Material Assignment

Current:

```text
object_material prefix:rock 0.18 0.20 0.24 stone
```

Future compatible forms:

```text
object_material prefix:rock stone tint=0.18,0.20,0.24
object_material prefix:chrome chrome
```

### Texture References

Future:

```text
terrain_texture textures/ground_snow.jpg
material_texture chrome textures/chrome_mask.png
```

## Parser Strategy

The parser should move toward directive schemas:

```cpp
struct DirectiveSpec
{
    const char* name;
    DirectiveDomain domain;
    DirectiveHandler handler;
    const char* usage;
};
```

Domains:

- scene,
- style,
- material,
- render,
- physics,
- camera,
- compatibility.

Benefits:

- Better error messages.
- Easier docs generation.
- Style files can reject physics-only directives.
- Scene files can allow both scene and style directives.

## Material Assignment Matching

Current matching supports:

- `all`,
- `balls`,
- `boxes`,
- exact object name,
- `prefix:<prefix>`.

Keep these. Add future selectors only when needed:

- `shape:sphere`,
- `shape:box`,
- `tag:<tag>`,
- `material:<oldName>`.

Do not add general regex selectors.

## Live Style Reload

Desired behavior:

1. User edits `live.style` or chooses style in UI.
2. Parser loads style into a style config.
3. Runtime applies style params and material assignments.
4. Object physics and transforms remain unchanged.
5. GPU resources only rebuild if texture/material resource references changed.

This requires clear separation between style data and scene object/physics data.

## Phase Plan

### Phase 1: Document Current Precedence

Tasks:

1. Write a reference doc or section explaining current precedence.
2. List allowed style directives.
3. List compatibility aliases.

Validation:

- Documentation only: no validation required.

### Phase 2: Parser Domain Tags

Tasks:

1. Tag directives by domain in parser tables.
2. Improve error messages.
3. Keep accepted syntax unchanged.

Validation:

- `tools\validate_fast.bat`.

### Phase 3: Render Style Wrapper

Tasks:

1. Add `RenderStyleConfig` wrapper or sub-structs around existing `CinematicRenderConfig`.
2. Keep current fields and directives compatible.
3. Start moving new fields into specific style structs.

Validation:

- `tools\validate_fast.bat` for pure plumbing.
- `tools\validate_renderers.bat` if visual behavior changes.

### Phase 4: Material Definitions

Tasks:

1. Add optional `material` directive in style files.
2. Keep existing `object_material` form.
3. Add named material assignment form.

Validation:

- `tools\validate_fast.bat`.
- Renderer validation if output changes.

### Phase 5: Style Reload Boundary

Tasks:

1. Ensure live style reload does not rebuild objects/physics.
2. Reapply material assignments.
3. Rebuild only required render resources.

Validation:

- `tools\validate_renderers.bat`.
- `tools\validate_full.bat` if scene lifecycle changes broadly.

## Validation Matrix

| Change | Validation |
|--------|------------|
| Docs only | No validation required |
| Parser schema/domain cleanup | `tools\validate_fast.bat` |
| Style precedence behavior | `tools\validate_fast.bat`, renderer validation if visible |
| Material directive parser | `tools\validate_fast.bat` |
| Live style reload render behavior | `tools\validate_renderers.bat` |
| Scene lifecycle changes | `tools\validate_full.bat` |

## Risks

| Risk | Mitigation |
|------|------------|
| Existing scenes fail to parse | Keep compatibility directives and add tests before removing aliases. |
| Style file changes physics accidentally | Domain-tag style directives and reject physics directives in style-only context. |
| Precedence becomes confusing | Document and log resolved style source in debug mode. |
| Material assignments lose names | Add material registry before shader payload migration. |
| Live reload rebuilds too much | Separate style data from scene object/physics data. |

## Success Criteria

- Scene files define worlds; style files define looks.
- Existing `cinematic_*` directives still work.
- Material names survive style parsing.
- Live style changes do not disturb physics/object state.
- Future render style fields do not all pile into one unstructured config.
