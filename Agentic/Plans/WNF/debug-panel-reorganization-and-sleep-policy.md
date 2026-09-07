# Debug Panel Reorganization And Sleep Policy Controls

Date: 2026-08-30
Status: WNF - owner-requested plan; 0/6 phases complete
Owner: Runtime UI and Physics sleep controller
Impact area: in-game debug panel, Runtime command routing, Physics sleep policy, terrain contact policy, automation scenes, tests, and documentation

## Owner Request

Reduce the main debug display from eleven crowded top-level tabs to a smaller
set grouped by operator task. Add a dedicated Sleep tab for changing live sleep
settings, including the terrain angle above which a ball is not allowed to sleep
and should remain active so gravity can make it roll.

This file is a parked design in `WNF/`. Implement it through
`Agentic/Skills/orchestrator/SKILL.md` only after the owner moves or selects the
plan for implementation.

The current working tree contains owner-owned Physics sleep work for persistent
simulation islands. Do not edit, discard, stage, or build on those files until
that work is stable or the owner explicitly approves the overlap. Recheck all
planned Physics files against the final island-sleep implementation before
starting this plan.

## Current Panel

The current top bar contains eleven tabs:

1. `Prof` - worker controls, timing hierarchy, percentiles, and timeline spans.
2. `Scene` - scene and replay selection, reset/save actions, run status, simulation speed, and continuous forecast.
3. `Edit` - editor/place/static modes, object selection, viewport state, and history depth.
4. `Phys` - physics overlays, pipeline inspection, ray tools, gravity, friction, sleep enable, and tornado controls.
5. `Opt` - lockstep, terrain/water visibility, shadows, presentation status, time scale, and model count.
6. `Render` - ordinary lighting, environment, shadows, water, materials, visibility counts, and prediction-path style.
7. `Targets` - render-target metadata, selection, and preview.
8. `Ctrl` - seed, generated ball/box counts, and fluid settings.
9. `Sky` - sky passes, sun direction/colour, clouds, rays, and grading.
10. `Cine` - cinematic mode, passes, grading, sun/sky/clouds, bloom, terrain, water, basin, and fog.
11. `Mem` - replay memory policy, process/subsystem memory, store capacity, and reserve growth.

The main organization problems are:

- time scale is editable in more than one place;
- shadows, water, sun, sky, exposure, gamma, and grading are repeated across tabs;
- Physics mixes simulation tuning, gameplay forces, test tools, and debug drawing;
- the Sleep policy control is only an enable toggle among unrelated Physics overlays;
- Memory mixes live policy controls with diagnostic tables; and
- abbreviated tab labels make the crowded top bar harder to scan.

## Target Navigation

Use five top-level tabs. Within a tab, use named collapsible sections or a short
left-side section list. Do not replace eleven top-level tabs with eleven hidden
secondary tabs.

| New tab | Sections and relocated content |
|---|---|
| `Run` | Scene selection and replay; run status; reset/default actions; simulation speed; generated scene seed and object counts; fluid settings; editor controls; continuous forecast. |
| `Physics` | Gravity and material tuning; ray test and launcher; tornado field and its visual controls. |
| `Sleep` | Sleep enable, live thresholds, maximum sleeping terrain slope, reset/wake actions, and sleep diagnostics. |
| `Visuals` | Ordinary render settings; consolidated atmosphere/cinematic controls; scene visibility and water debug modes; render-target preview. |
| `Diagnostics` | Profiler and timeline; memory and replay-memory policy; physics overlays, pipeline inspection, debug alpha, and contact linger. |

### Consolidation Rules

- One live value has one interactive control. Other pages may show a read-only
  summary or a link-style action that opens the owning section.
- Keep only one time-scale control under `Run`.
- Keep ordinary and cinematic shadow controls visually distinct only when they
  truly mutate different owners. Do not show duplicates for the same value.
- Merge the duplicated Sky and Cine sun, cloud, exposure, gamma, saturation,
  contrast, and vignette rows into one `Atmosphere and grade` section.
- Keep terrain/water visibility and render-only debug modes under `Visuals`.
- Move physics overlay toggles and pipeline stepping to `Diagnostics`, while
  leaving behavior-changing world, friction, ray, and tornado controls under
  `Physics`.
- Reduce the footer to frame/status readouts and controls that must remain
  globally available. Move `Perf`, `Timeline`, `Hitboxes`, and `Blur` to their
  owning tabs unless testing proves a global shortcut is still useful.
- Preserve familiar full labels. Use `Physics`, `Sleep`, `Visuals`, and
  `Diagnostics` rather than `Phys`, `Opt`, `Cine`, or `Mem`.

## Sleep Tab

### Layout

The first visible block is a compact status and action row:

- `Sleep enabled` toggle;
- awake, sleeping, and fixed body counts;
- `Wake all` button, which clears sleep state and deactivation progress through
  an explicit Physics operation; and
- `Reset startup values` button.

The `Eligibility` section contains these live controls:

| Control | Meaning | Initial UI range |
|---|---|---:|
| `Linear speed` | Maximum body linear speed that can count as quiet. | `0.0..5.0 m/s` |
| `Angular speed` | Maximum body angular speed that can count as quiet. | `0.0..5.0 rad/s` |
| `Quiet duration` | Consecutive fixed steps required before sleep. Show the equivalent seconds beside it. | `1..600 steps` |
| `Max sleeping slope` | Maximum terrain angle from horizontal on which a terrain-contacting dynamic body may sleep. | `0.0..90.0 degrees` |

Use the existing defaults for linear speed, angular speed, and quiet duration:
`0.5 m/s`, `0.3 rad/s`, and `30` fixed steps. The first implementation should
default `Max sleeping slope` to `90 degrees` so existing scenes keep their prior
behavior until the owner deliberately chooses a new default. A later owner
decision may select a stricter default after scene evidence is reviewed.

The `Derived limits` section is read-only. Show the effective object penetration
limit, terrain penetration limit, correction-speed limit, fixed-step frequency,
and quiet duration in seconds. These values are coupled to contact epsilon,
solver slop, terrain slop, or the linear speed setting and should not initially
be presented as independent tuning controls.

The `Observation` section shows:

- total, stable, eligible, and ready-to-sleep island counts;
- minimum, average, and maximum body deactivation progress;
- reset-reason counts for motion, terrain inhibition, contact topology,
  contact stability, and point-joint error;
- count of bodies currently inhibited by steep terrain; and
- toggles for sleep state, island identity, and sleep-block reason drawing.

### Live Editing Behavior

- Sliders preview while dragging and send one typed command on release, matching
  the existing GameUI slider behavior.
- A committed setting applies at the next fixed step.
- Changing a threshold does not silently wake everything or clear current
  deactivation progress. The next fixed step re-evaluates each body under the
  new values.
- `Wake all` is the explicit clean-experiment action. It wakes all dynamic bodies
  and clears their deactivation progress in canonical body order.
- Live values survive ordinary scene reset and scene changes, matching the
  current sleep-enable behavior. They reset on process restart.
- The first implementation does not write the process configuration file.
  Persistent `Save CFG` support is a separate, explicit change with atomic file
  replacement and clear destination text.

## Maximum Sleeping Terrain Slope

### User Meaning

`Max sleeping slope` is measured in degrees from horizontal:

- `0 degrees` permits terrain-supported sleep only on flat terrain;
- `30 degrees` permits sleep on slopes up to and including 30 degrees;
- a contact above 30 degrees is sleep-inhibited; and
- `90 degrees` disables this exclusion for ordinary upward-facing terrain and
  preserves current behavior.

The exact boundary is inclusive: a terrain slope equal to the configured value
may pass this gate, while a greater slope cannot.

This value is not one of the existing box-face alignment angles. The current
approximately 18-degree, 30-degree, and 8-degree constants compare a box or hull
face with the local terrain plane to reject unstable edge/point support. The new
setting compares the terrain plane itself with world up. Both checks apply to a
box or hull; the new slope check also gives spheres and capsules an explicit
terrain-grade sleep rule.

### Physics Rule

For every valid terrain manifold, compare its normalized terrain normal with
world up. The implementation should compare the normal's up component with a
precomputed cosine threshold:

```text
terrain may sleep when normal dot worldUp >= cos(max sleeping slope)
```

Convert the configured degree value to the comparison threshold only when the
configuration or live UI command is applied. Do not call inverse trigonometric
functions per contact or per body. Use the repository's deterministic math
policy and add below/equal/above boundary tests for the stored comparison value.

A non-finite or invalid terrain normal fails the gate and inhibits sleep.

Above the threshold, Physics marks the body terrain-inhibited and keeps it in
active simulation. Do not add a synthetic downhill impulse or torque. Gravity,
friction, and the existing terrain contact solver remain responsible for the
actual rolling motion. A focused inclined-plane scene must nevertheless prove
that a sphere above the threshold remains awake and gains downhill motion under
ordinary gravity, while the same sphere below the threshold can settle and
sleep.

Do not change friction, damping, gravity, restitution, solver iterations, the
box/hull footprint classifier, or the existing sleep thresholds as part of this
setting.

## Runtime And Ownership Design

### Physics

- Add `maximumTerrainSleepSlopeDegrees` to the authored/Core sleep
  configuration and to the Physics-owned `SleepSettings` value.
- Keep the normalized/precomputed comparison value inside Physics-owned runtime
  policy. UI and Runtime must not classify terrain contacts.
- Add a narrow PhysicsEngine operation for applying a complete normalized
  `SleepSettings` value and a read-only operation for retrieving the current
  value.
- Applying settings updates both the fixed-step runtime snapshot and any
  controller value used to seed newly created bodies.
- Add an explicit Physics operation for waking all dynamic bodies and clearing
  deactivation progress. Do not implement this by toggling sleep off and on at
  the UI layer.
- Keep all new per-frame diagnostic summaries derived from existing Physics-owned
  rows. Do not add a field to `PhysicsBodyRecord` or another hot body store.

### Runtime Commands

- Add a dedicated sleep-policy command value rather than extending the already
  crowded physics-debug command with more unrelated flags.
- Normalize every requested value once at the App command boundary.
- Convert the Runtime/UI command value to `Physics::SleepSettings` only at the
  application point.
- Record command acceptance so automation can prove the intended value was
  applied.
- Preserve the complete live sleep settings across ordinary scene-load and
  reset transactions, not only the enabled bit.

### UI Projection

- Project one detached `UISleepPolicyFrameView` containing current settings,
  status counts, derived limits, and bounded reset-reason summaries.
- The GameUI page consumes only this detached value and emits typed commands. It
  must not borrow Physics owners, spans, controller storage, or Core config.
- Keep draw bounds and hit testing based on the same layout functions.
- Retain preview values only for active sliders; the frame view remains the
  source of committed state.

### Automation Compatibility

The existing `ui.tab` scene strings and numeric tab ordinals are test-facing
compatibility data. Preserve old strings as aliases:

- `profiler` and `memory` open the corresponding Diagnostics section;
- `scene`, `options`, `controls`, and `editor` open the corresponding Run section;
- `physics` opens Physics;
- `render`, `targets`, `sky`, and `cinematic` open the corresponding Visuals section;
- add `sleep` for the new Sleep page.

Do not silently reinterpret an old automation scene as an unrelated section.
Update the parser tests to pin every old alias and every new top-level name.

## Phases

- [ ] **DPS0 - Pin the current panel and slope semantics.** Add focused tests for
  all current tab-string aliases and a control inventory that identifies the one
  owner of every duplicated value. Add pure Physics tests for terrain slope at
  below, equal, and above the requested angle, invalid normals, 0 degrees, and
  90 degrees. Plant a sphere-on-incline negative control showing that current
  Physics has no authored terrain-grade sleep threshold.
- [ ] **DPS1 - Replace the eleven-tab shell with five grouped pages.** Introduce
  `Run`, `Physics`, `Sleep`, `Visuals`, and `Diagnostics`; relocate existing
  controls without changing their commands; remove duplicate interactive rows;
  retain old automation strings as section aliases; and keep scroll, clipping,
  combos, and pointer capture correct at minimum window size.
- [ ] **DPS2 - Add live sleep-policy values and commands.** Project the current
  complete sleep settings, add preview/commit widgets, normalize commands at the
  App boundary, apply them through PhysicsEngine, preserve them across scene
  changes, and add explicit `Wake all` and `Reset startup values` operations.
- [ ] **DPS3 - Add maximum sleeping terrain slope.** Add the Core config key,
  Physics runtime value, deterministic cold conversion, terrain-manifold gate,
  steep-slope inhibition reason, and sphere/box/hull boundary tests. Prove the
  gate changes only sleep eligibility and never adds forces or changes contact
  solver settings.
- [ ] **DPS4 - Add Sleep observations and visual coverage.** Project island/body
  counts, deactivation progress, reset reasons, and steep-slope inhibition.
  Add or extend sleep overlays, deterministic screenshot scenes for all five
  pages, focused scroll/min-size captures, and the inclined-sphere scene.
- [ ] **DPS5 - Run terminal validation and review.** Run focused tests while
  iterating, then the required UI, Physics, Replay, dependency, allocation,
  source-design, build-configuration, and full validation at the end. Run the
  inclined-sphere and existing sleep scene lanes twice and with 0/1/4 Physics
  workers. Obtain an independent read-only review before marking the plan done.

## Required Behavior

### Panel

- Exactly five top-level tabs are visible at the normal debug-panel width.
- Every current control remains available in one logical location unless the
  plan explicitly removes a duplicate of the same live value.
- No old scene automation tab string fails or opens the wrong section.
- Open combos, sliders, scrolling, and overlays retain pointer ownership and do
  not click through after controls move.
- The panel remains readable at the existing minimum-size and clipped-scroll
  fixtures.

### Sleep Policy

- Displayed values match the committed Physics settings every frame.
- Slider previews never mutate Physics until commit.
- A committed value is normalized once and is visible on the next frame.
- Live values survive scene reset and scene navigation but not process restart.
- `Wake all` clears deactivation progress and leaves sleep enabled.
- `Reset startup values` restores the values captured from startup configuration,
  including the maximum sleeping terrain slope.

### Terrain Slope

- At an exact configured boundary, the terrain contact may pass the slope gate.
- A greater terrain angle inhibits sleep for spheres, boxes, hulls, and other
  dynamic terrain-contact shapes while leaving their ordinary contact solve
  unchanged.
- A sphere above the threshold remains awake and develops downhill motion under
  ordinary gravity in the focused scene.
- The same scene below the threshold can reach the usual complete sleep criteria.
- A 90-degree default reproduces current behavior before the owner selects a new
  default.
- Results, sleep transitions, and diagnostic reasons are byte-identical across
  repeated clean runs and 0/1/4 Physics worker settings.

## Validation Map

| Change | Required final evidence |
|---|---|
| Tab reorganization | GameUI layout/input tests; parser alias tests; deterministic page screenshots; `tools\validate_ui.bat` |
| Sleep commands and persistence | command normalization/application tests; scene reset/navigation preservation tests |
| Terrain slope setting | pure boundary tests; sphere/box/hull terrain tests; inclined-sphere scene; `tools\validate_physics.bat` |
| Sleep diagnostics | projection tests; overlay screenshots; replay capture/restore checks if diagnostic state touches serialized Physics values |
| Dependency and source ownership | `tools\validate_dependency_graph.bat`; compiler-backed source-design and build-configuration checks |
| Complete implementation | focused fast checks during phases; terminal `tools\validate_full.bat`; independent read-only review |
| This documentation-only plan | `git diff --check`; no repository validation required |

Do not refresh a Physics, Replay, UI, screenshot, or performance baseline merely
to make the implementation pass. Any intended baseline change requires separate
owner review after the behavioral and visual evidence is available.

## Expected Files

UI and Runtime:

- `SkullbonezSource/Runtime/UI/GameUI/UI.h`
- `SkullbonezSource/Runtime/UI/GameUI/UI.cpp`
- `SkullbonezSource/Runtime/UI/GameUI/UIWindowInteractionOwner.h`
- `SkullbonezSource/Runtime/UI/GameUI/UIWindowInteractionOwner.cpp`
- current `UITab*.h/.cpp` files that are retained as grouped sections
- new `SkullbonezSource/Runtime/UI/GameUI/UITabSleep.h`
- new `SkullbonezSource/Runtime/UI/GameUI/UITabSleep.cpp`
- `SkullbonezSource/Runtime/UI/OperatorUiProjection.h`
- `SkullbonezSource/Runtime/UI/OperatorUiProjection.cpp`
- `SkullbonezSource/Runtime/Interaction/OperatorUiCommands.h`
- `SkullbonezSource/Runtime/App/OperatorCommandBoundaryPolicy.h`
- `SkullbonezSource/Runtime/App/OperatorCommandApplication.cpp`
- scene-load/reset preservation values under `SkullbonezSource/Runtime/Scene/`

Physics and configuration:

- `SkullbonezSource/Core/Config.h`
- `SkullbonezSource/Core/Config.cpp`
- `SkullbonezSource/Physics/PhysicsRuntimeSettings.h`
- `SkullbonezSource/Physics/PhysicsEngine.h`
- `SkullbonezSource/Physics/PhysicsEngine.cpp`
- `SkullbonezSource/Physics/PhysicsWorld.h`
- `SkullbonezSource/Physics/PhysicsWorld.cpp`
- `SkullbonezSource/Physics/TerrainSupportClassifier.h`
- `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp`
- the final post-island-work sleep controller and diagnostic view files

Tests and fixtures:

- GameUI, command, projection, scene-parser, and sleep-controller tests under
  `SkullbonezTests/`
- deterministic UI scenes under `SkullbonezData/scenes/`
- a focused inclined-sphere terrain scene and analyzer under
  `SkullbonezData/scenes/` and `tools/`
- project files/filters for any new `.cpp` source or test translation unit

## Review Questions

1. Are there exactly five top-level tabs without recreating the old tab count as
   hidden secondary navigation?
2. Does every live value have one interactive owner and one command path?
3. Is `Max sleeping slope` clearly measured from horizontal and kept separate
   from box/hull face-alignment policy?
4. Does a steep terrain contact inhibit sleep without injecting artificial
   force, torque, damping, or friction?
5. Do spheres above the threshold remain active and roll downhill under existing
   gravity/contact behavior?
6. Are live settings normalized once, applied at the next fixed step, and
   preserved across scene changes?
7. Did the implementation avoid a new hot-body field, per-step allocation,
   upward include, or second sleep-policy owner?
8. Do old automation tab strings and deterministic UI scenes still select the
   intended controls?
9. Do repeated and worker-count runs produce identical sleep decisions and
   diagnostics?
10. Do the screenshots show a visibly simpler panel with no clipped, duplicated,
    or misleading controls?
