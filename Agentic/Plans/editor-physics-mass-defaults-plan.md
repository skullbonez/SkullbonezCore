# Editor Physics Mass Defaults Plan

Date: 2026-06-20
Status: Draft implementation plan
Impact area: physics, editor placement, scene system, hull assets, tooling
Validation for this document-only change: none required

## Goal

Replace the editor's hard-coded dynamic-object masses with calculated masses
that scale with object size and hull volume. The default behavior should make
newly placed dynamic objects slightly lighter than the default water, so they
float while still visibly displacing and penetrating the water surface.

The target default is:

```text
default_density = 0.90 * default_water_density
default_water_density = 1.0 engine unit
```

For default water, this means a calm object should settle with roughly 90% of
its volume submerged before drag, waves, contacts, and approximation error.

## Current Problem

Editor placement currently assigns fixed masses:

| Object | Current mass behavior |
|--------|-----------------------|
| Ball | `6.0` regardless of radius |
| Sphere | `24.0` regardless of radius |
| Box | `24.0` regardless of half-extents |
| Generic hull/rock/root | `24.0` regardless of hull volume or scale |
| Tree parts | Hand-authored per-part masses |

This makes large editor-placed objects extremely low density. Since buoyancy is
based on displaced volume but gravity is based on mass, scaled objects can float
almost entirely above the water.

Relevant current paths:

- `SkullbonezSource/SkullbonezRunInput.cpp` editor placement creates balls,
  boxes, hulls, and tree parts.
- `SkullbonezSource/SkullbonezConvexHullShape.cpp` loads hull `volume`,
  `center_of_mass`, `unit_inertia`, and `projected_surface_area`.
- `SkullbonezSource/SkullbonezWorldEnvironment.cpp` applies gravity as
  `mass * gravity` and buoyancy as `-gravity * fluidDensity * submergedVolume`.
- `SkullbonezSource/SkullbonezSceneSnapshotWriter.cpp` already persists actual
  mass in editable scene state entries.

## Design Principles

1. Mass should be derived from physical volume and density, not object type
   magic numbers.
2. Hull volume should be computed accurately offline when possible.
3. Runtime scaling should preserve offline hull accuracy by multiplying volume
   and mass by `scaleX * scaleY * scaleZ`.
4. Saved editable scene state should store the actual mass used at placement
   time so old snapshots remain deterministic if defaults change later.
5. Authored scenes may still override mass explicitly for special cases.
6. If an authored hull scene entry omits mass, the loader should use the hull's
   default mass.

## Mass Formula

Use one shared helper for editor placement and any future placed physics object
path:

```text
sphereMass = density * (4/3 * pi * radius^3)
boxMass    = density * (8 * halfX * halfY * halfZ)
hullMass   = hullDefaultMass * scaleX * scaleY * scaleZ
```

Clamp all calculated masses to a small positive minimum before computing inverse
mass or inertia.

## Hull Asset Defaults

Extend the hull asset metadata with either:

```text
default_density 0.90
```

or:

```text
default_mass <volume * 0.90>
```

Preferred implementation:

- Store `default_density` for readability and future material tuning.
- Compute `default_mass = volume * default_density` after loading.
- Allow an optional explicit `default_mass` to override density where art or
  gameplay needs a hand-tuned value.

Fallback for existing hulls:

```text
if default_mass exists:
    use default_mass
else if default_density exists:
    use volume * default_density
else:
    use volume * 0.90
```

This makes old hull files float by default without requiring every asset to be
updated in the same commit, while still allowing a later offline refresh to
write explicit metadata for every hull.

## Offline Hull Mass Generation

Create or extend a hull authoring tool that:

1. Reads every `SkullbonezData/hulls/*.hull`.
2. Uses the existing accurate hull volume when present, or computes exact volume
   from the closed convex mesh if the file needs regeneration.
3. Writes `default_density 0.90` for generic floatable hulls.
4. Optionally supports material profiles:

| Profile | Density | Use |
|---------|---------|-----|
| float_default | `0.90` | Generic editor hulls, rocks, roots, decorative objects |
| wood | `0.65` to `0.85` | Trunks and roots when they should sit higher |
| foliage | `0.20` to `0.50` | Needle/leaf clusters if they become independent dynamic pieces |
| stone_sinking | `1.50+` | Explicit opt-in for rocks that should sink |

The immediate user-requested default is still "slightly lighter than water" for
all hulls. Material profiles are a future tuning hook, not a reason to keep the
current hard-coded `24.0` mass.

## Runtime Changes

### Shared Mass Helper

Add a small physics mass utility, for example:

```text
CalculateSphereMass(radius, density)
CalculateBoxMass(halfExtents, density)
CalculateHullMass(hull, scale)
CalculateInertiaForShape(shape, mass)
```

Keep it near physics or collision-shape code rather than burying the formulas
inside editor input handling.

### Editor Placement

Replace hard-coded masses in editor placement:

- Ball and sphere use volume-derived mass from their current radius.
- Box uses volume-derived mass from current half-extents.
- Hulls use hull default mass scaled by placement scale.
- Tree parts use each hull part's default mass unless the tree definition has an
  explicit multiplier or override.

This should also apply when the user drags placement scale before releasing the
mouse. The placed object's mass should match the final preview scale.

### Scaling Existing Objects

Editor gizmo scaling currently changes collision shape dimensions. Scaling a
dynamic object should also update:

- mass,
- inverse mass,
- rotational inertia,
- cached volume,
- projected surface area,
- any shape-specific volume data.

If changing mass while a body is moving proves too disruptive, the first
implementation can limit mass recalculation to placement and scene load, then
track gizmo mass refresh as a follow-up. The final target is still that scaled
physics objects have physically consistent mass.

### Convex Hull Scaling

Fix `ConvexHullShape::ScaleAxis()` so scaled hull volume preserves authored
accuracy. It should not replace volume with a bounding-box estimate after scale.

Preferred behavior:

```text
scale factor s on axis i:
    vertices scale as today
    bounding radius updates as today
    inertia extents update as today
    volume *= s
    projected surface area updates from scaled geometry or from a reasonable
    scaled-area approximation
    default_mass *= s
```

If `ScaleAxis()` is called three times, the final hull mass and volume naturally
pick up `scaleX * scaleY * scaleZ`.

## Scene Load And Save

### Editable Snapshots

Keep writing actual mass for:

- `ball_state`,
- `box_state`,
- `convex_hull_state`.

These are full dynamic state snapshots and should not be recomputed from new
defaults on load.

### Authored Scene Objects

For non-state authored objects:

- Keep explicit `mass` supported.
- Make `convexHull.mass` optional.
- If omitted, load the hull and use its default mass.
- Consider later making `box.mass` and `ball.mass` optional too, using the same
  calculated default path.

This gives hand-authored scenes a cleaner default while preserving authored
control when a scene needs a special heavy or light object.

## Acceptance Criteria

1. Newly placed dynamic balls, spheres, boxes, hulls, rocks, roots, and tree
   parts have mass based on volume and density.
2. Default editor-placed dynamic objects are slightly lighter than default water
   and visibly float with meaningful water penetration.
3. Scaling before placement changes mass proportionally to final volume.
4. Hull mass uses offline-authored volume, not bounding-box approximation.
5. Editable scene snapshots persist the actual calculated mass.
6. Authored convex hull scene entries can omit mass and receive hull default
   mass on load.
7. Existing scenes with explicit mass keep their current behavior.

## Implementation Phases

### Phase 1: Mass Utility And Editor Placement

Add the shared mass helper and use it for newly placed balls, spheres, boxes,
and hulls. Use fallback hull density `0.90` if no hull metadata exists.

Validation at PR gate:

```bat
tools\validate_physics.bat
```

### Phase 2: Hull Metadata

Extend hull parsing for `default_density` and optional `default_mass`. Add
accessors for default density/mass and update hull load diagnostics.

Validation at PR gate:

```bat
tools\validate_physics.bat
```

### Phase 3: Offline Hull Refresh Tool

Create or extend tooling to refresh hull mass metadata across
`SkullbonezData/hulls/*.hull`.

Validation at PR gate:

```bat
tools\validate_fast.bat
tools\validate_physics.bat
```

### Phase 4: Scene Parser Defaults

Make authored `convexHull.mass` optional and derive the default from the hull
asset when absent. Keep state object mass required.

Validation at PR gate:

```bat
tools\validate_physics.bat
```

Use `tools\validate_full.bat` if parser or scene-load changes broaden beyond
convex hull mass defaults.

### Phase 5: Existing Object Scaling Follow-Up

Update editor gizmo scaling so mass and inertia refresh when a dynamic object's
shape is resized after placement.

Validation at PR gate:

```bat
tools\validate_physics.bat
```

## Risks And Notes

| Risk | Mitigation |
|------|------------|
| Water density changes at runtime make objects float differently | This is expected physics behavior. Saved mass remains fixed; fluid density controls buoyancy. |
| Existing hand-authored scenes rely on light masses | Keep explicit mass authoritative. Only omitted mass uses defaults. |
| Hull `ScaleAxis()` volume changes affect validation baselines | Treat as physics behavior change and run the physics gate before commit/PR. |
| Tree foliage becomes too buoyant | Start with uniform `0.90`; add per-material density profiles only if the result looks wrong. |
| Inertia and mass drift apart during editor scaling | Include inertia refresh with the scaling follow-up. |

## Open Questions

| Question | Default answer |
|----------|----------------|
| Exact default density ratio? | Start at `0.90` for "slightly lighter than water." |
| Should rocks also float? | Yes by default per current user request; sinking rocks require explicit mass or a later material profile. |
| Should mass derive from current scene fluid density? | No. Mass is an object property; water density is a world property. |
| Should ball/box authored scene mass become optional now? | Not required for the first slice; consider after convex hull defaults are stable. |

## Definition Of Done

The work is complete when editor-created dynamic objects no longer use
hard-coded low masses, hull defaults are available from asset metadata or
fallback density, saved snapshots preserve actual mass, and the default placed
object behavior is visibly "slightly lighter than water" instead of barely
touching the surface.
