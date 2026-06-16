# Simulation Feature Ideas Plan

Date: 2026-06-16
Status: Basic idea plan
Impact area: future scene system, runtime UI, physics diagnostics, renderer polish, tests
Validation for this document-only change: none required

## Goal

Collect a small set of simulation feature ideas that would make SkullbonezCore
more fun to run, easier to debug, and better at showing off the DX12 renderer
and deterministic physics stack.

This is not an implementation queue yet. Treat it as a menu of focused future
plans that can each become a separate implementation task.

## Selected Ideas

### Deterministic Replay Lab

Build a replay workflow around fixed-step simulation history.

Core shape:

- Record compact simulation checkpoints and per-frame presentation samples.
- Pause, scrub, and step through historical frames.
- Branch from a selected frame by restoring the nearest checkpoint and replaying
  deterministic fixed ticks forward.
- Export a suspicious moment as a `.scene`, `.suite`, replay artifact, or
  SkullScope diagnostic case.
- Compare solver state, contact rows, sleep state, and render captures from the
  same recorded frame.

Why it fits:

- The engine already has fixed 120 Hz physics, scene seeds, fixed-step scenes,
  nudge snapshots, F2 scene snapshots, SkullScope, and pipeline debug stages.
- This turns determinism into an interactive debugging feature.
- `Agentic/Plans/replay-system-plan.md` already contains a deeper draft that
  can be resumed or refined.

Suggested first slice:

- Add an always-on bounded replay buffer for transform samples, camera state,
  and a simple timeline UI.
- Keep authoritative checkpoint/branch/export work for a later slice.

### Impact-Deforming Terrain

Add visual terrain response when bodies hit hard enough.

Core shape:

- Spawn impact decals, scorch marks, dust rings, cracks, or shallow dents at
  high-energy contacts.
- Keep the first version render-only so physics terrain remains deterministic
  and unchanged.
- Track impact energy from collision rows or SkullScope-style contact summaries.
- Add a dedicated test scene with falling balls, boxes, and bullets striking a
  flat or gently sloped surface.

Why it fits:

- Existing scenes already exercise bullets, terrain contact probes, and solver
  contacts.
- Render-only deformation can be dramatic without destabilizing physics.
- A later version could promote selected dents into real collision terrain only
  after the visual path is proven.

Suggested first slice:

- Add temporary circular impact decals or relief marks driven by object/terrain
  contact impulses.
- Cap decal count and reuse slots to avoid per-frame allocation.

### Marble Run Builder

Turn the cause/effect scene into an interactive contraption sandbox.

Core shape:

- Add authored ramps, funnels, catch trays, launchers, gates, switches, and
  target plates.
- Let scenes chain small events: ball hits switch, gate opens, cube tower
  collapses, another ball launches.
- Add simple scoring and timing: elapsed frames, targets hit, missed catches,
  chain completion time.
- Keep scene files text-based and deterministic.

Why it fits:

- `cause_effect_marble_run.scene` already points in this direction.
- The solver, broadphase, sleep islands, and debug overlays all get visible
  workout scenes.
- The feature naturally creates regression cases that are fun to watch.

Suggested first slice:

- Add a small set of scene-authored passive parts: fixed ramps, funnels, and
  target zones.
- Use existing fixed boxes and spheres before adding new gameplay objects.

### Material Lab

Make material authoring interactive enough to tune looks live.

Core shape:

- Use `material_authoring_contract.scene` as the main testbed.
- Expose object-group material controls in the UI: tint, roughness, metallic,
  specular, emissive, transmission, stylization, and named material presets.
- Support per-target editing for `all`, `balls`, `boxes`, `prefix:<name>`, and
  exact model names.
- Save accepted settings back as scene-local `object_material` lines or as a
  reusable `.style` file.

Why it fits:

- The runtime already supports `object_material` directives and live cinematic
  controls.
- The live style harness gives a precedent for changing look-dev values without
  restarting physics.
- This makes renderer improvements easier to inspect and compare.

Suggested first slice:

- Add a compact Material tab that edits the currently selected target group and
  writes a preview-only runtime override.
- Add Save Scene Defaults only after live overrides are stable.

### Collision Debug Theater

Make the existing physics debug views feel like an in-world explainer.

Core shape:

- Present contacts, normals, friction tangents, warm-start impulses,
  penetration depth, sleep eligibility, and solver stage records as readable
  world-space overlays.
- Add a pipeline stage timeline that matches the F7/F8 Catto stage cursor.
- Let the user focus one body or one contact pair and fade unrelated contacts.
- Show broadphase cells, candidate pairs, actual contacts, and sleep islands
  together without changing physics.

Why it fits:

- The engine already has physics debug overlays, SkullScope records, pipeline
  stages, broadphase visualization, and contact feature IDs.
- It would make solver investigations faster and make demos more legible.

Suggested first slice:

- Add a focused-contact mode that tracks one body or pair and displays row
  labels for feature id, impulse, penetration, and warm-start state.

### Shooting Range 2.0

Upgrade the shooting scenes into an interactive physics test chamber.

Core shape:

- Build from `shooting_range.scene`, `shooting_reaction_volley.scene`, and the
  bullet sweep scenes.
- Add moving targets, reaction scoring, target reset, ricochet plates, slow
  motion, seed replay, and optional camera-follow shots.
- Use high-speed bullets to exercise CCD, wake-up, object contacts, and solver
  response.
- Record hit timing and target reaction as validation-friendly data.

Why it fits:

- The existing bullet scenes already cover high-speed collision timing and
  target reactions.
- The feature is both a toy and a regression harness.
- It would pair well with replay and collision debug work.

Suggested first slice:

- Add target zones and score counters to the existing volley scene.
- Add one moving-target scene before adding ricochets or complex target logic.

## Suggested Priority

1. Material Lab
2. Collision Debug Theater
3. Shooting Range 2.0
4. Marble Run Builder
5. Deterministic Replay Lab
6. Impact-Deforming Terrain

The suggested order starts with tools that make later work easier to inspect.
Replay and deformation are larger systems and should get dedicated plans before
implementation.

## Implementation Notes

- Keep each idea as a separate branch and plan before coding.
- Avoid changing physics behavior while building visual/debug features unless
  that behavior change is the explicit goal.
- Prefer deterministic scene-authored examples over generated-only demos.
- Add scene-level probes early so features are easy to validate at PR time.
- Do not run repository validation while drafting plans or iterating casually.

## Future Validation Gates

When any of these ideas becomes code, use the narrowest PR-bound validation gate:

| Feature | Likely validation |
|---------|-------------------|
| Material Lab | `tools\validate_dx12_renderer.bat` |
| Collision Debug Theater | `tools\validate_physics.bat`; add `tools\validate_dx12_renderer.bat` if renderer/debug draw baselines change |
| Shooting Range 2.0 | `tools\validate_physics.bat` |
| Marble Run Builder | `tools\validate_physics.bat`; add `tools\validate_perf.bat` if many objects or broadphase stress are added |
| Deterministic Replay Lab | `tools\validate_full.bat` unless the implementation is narrowly scoped |
| Impact-Deforming Terrain | `tools\validate_dx12_renderer.bat`; add `tools\validate_physics.bat` only if deformation becomes physics-visible |
