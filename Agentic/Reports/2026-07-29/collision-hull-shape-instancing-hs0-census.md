# Collision Hull Shape Instancing HS0 Census And Decisions

Date: 2026-07-29
Plan: `Agentic/Plans/TODO/collision-hull-shape-instancing.md`
Task: HS0
Branch: `nightrunner-29th-JUL-26`
Impact: documentation only; no source, data, baseline, or golden changed.

## Outcome

The 145 committed scenes author 557 convex-hull colliders after deterministic
`assetInstances[]` expansion. Nineteen scenes contain hulls and the expanded
set resolves to 25 distinct tracked hull paths globally. The worst duplication
is `tornado_village_rampage.scene.json` at 162 total / 12 distinct. The three
acceptance scenes are `asd.scene.json` 51/12,
`convex_hull_stress_sleep.scene.json` 42/20, and
`convex_hull_stacking.scene.json` 34/20.

The plan's dated “33 distinct hull paths” statement was a raw-token count over
the 174 direct `convexHull`/`convexHullState` rows. Short editor tokens and
resolved path spellings were counted separately: 33 raw spellings normalize to
22 paths. Expanded asset instances add three used paths absent from that direct
set (`building_tri_roof.hull`, `disc_16.hull`, and
`pine_needle_cluster.hull`), yielding the authoritative 25. The repository
ships 35 hull files; ten are unused by committed scenes.

## Census Method

The walk used `git ls-files '*.scene.json'`; all 145 results are under
`SkullbonezData/scenes/`. It selected direct hull object rows, loaded the 18
definitions in all three tracked asset libraries, expanded each referenced
compound part in authored order, and normalized each identity to a
slash-normalized, case-folded repository path. Short editor tokens resolved by
their unique tracked hull basename. Zero scene assets or hull tokens were
unresolved or ambiguous.

| Scene | Total authored hull colliders | Distinct normalized paths |
|---|---:|---:|
| `aaa_ragdoll_clean_sky.scene.json` | 0 | 0 |
| `aaa_ragdoll_graphite_focus.scene.json` | 0 | 0 |
| `aaa_ragdoll_soft_pale.scene.json` | 0 | 0 |
| `aaa_ragdoll_sunset_showcase.scene.json` | 0 | 0 |
| `asd.scene.json` | 51 | 12 |
| `at_rest.scene.json` | 0 | 0 |
| `ball_on_ball_shadow.scene.json` | 0 | 0 |
| `ball_vs_ball_flight.scene.json` | 0 | 0 |
| `ball_vs_box_flight.scene.json` | 0 | 0 |
| `ball_vs_box_grounded.scene.json` | 0 | 0 |
| `box_crater_edge_repro.scene.json` | 0 | 0 |
| `box_flush_test.scene.json` | 0 | 0 |
| `box_only_rest.scene.json` | 0 | 0 |
| `box_pile_throw_300.scene.json` | 0 | 0 |
| `box_slide_surface_compare.scene.json` | 0 | 0 |
| `box_slope_test.scene.json` | 0 | 0 |
| `box_vs_ball_flight.scene.json` | 0 | 0 |
| `box_vs_ball_grounded.scene.json` | 0 | 0 |
| `bug.scene.json` | 0 | 0 |
| `bugz.scene.json` | 0 | 0 |
| `building_assets_showcase.scene.json` | 1 | 1 |
| `bullet_sweep_object.scene.json` | 0 | 0 |
| `bullet_sweep_terrain.scene.json` | 0 | 0 |
| `bullet_sweep_wall.scene.json` | 0 | 0 |
| `bullet_x_axis_cells.scene.json` | 0 | 0 |
| `buoyancy_inertia_orientation.scene.json` | 4 | 2 |
| `buoyancy_shoreline_lever.scene.json` | 2 | 1 |
| `cardinal_roll_test.scene.json` | 0 | 0 |
| `cause_effect_marble_run.scene.json` | 19 | 5 |
| `cinematic_shadow_map.scene.json` | 0 | 0 |
| `cinematic_volumetric.scene.json` | 0 | 0 |
| `collision_demo_ball_ball.scene.json` | 0 | 0 |
| `collision_demo_box_ball.scene.json` | 0 | 0 |
| `collision_demo_box_box.scene.json` | 0 | 0 |
| `concept_01_golden_hour_realism.scene.json` | 0 | 0 |
| `concept_02_brutal_industrial.scene.json` | 0 | 0 |
| `concept_03_studio_lighting_showcase.scene.json` | 0 | 0 |
| `concept_04_neon_cyberpunk.scene.json` | 0 | 0 |
| `concept_05_alien_planet.scene.json` | 0 | 0 |
| `concept_06_desert_storm.scene.json` | 0 | 0 |
| `concept_07_painterly.scene.json` | 0 | 0 |
| `concept_08_retro_future_2005.scene.json` | 0 | 0 |
| `concept_09_atmospheric_fog_world.scene.json` | 0 | 0 |
| `concept_10_ocean_world.scene.json` | 0 | 0 |
| `concept_11_scifi_test_chamber.scene.json` | 0 | 0 |
| `concept_12_low_poly_art_style.scene.json` | 0 | 0 |
| `concept_13_massive_scale.scene.json` | 0 | 0 |
| `concept_14_storm_front.scene.json` | 0 | 0 |
| `concept_15_photogrammetry_ground.scene.json` | 0 | 0 |
| `concept_16_tron_grid.scene.json` | 0 | 0 |
| `concept_17_dreamscape.scene.json` | 0 | 0 |
| `concept_18_nordic_winter.scene.json` | 0 | 0 |
| `concept_19_abstract_render_showcase.scene.json` | 0 | 0 |
| `concept_20_pixar_inspired.scene.json` | 0 | 0 |
| `convex_hull_collision.scene.json` | 8 | 6 |
| `convex_hull_stacking.scene.json` | 34 | 20 |
| `convex_hull_stress_sleep.scene.json` | 42 | 20 |
| `disc_edge_roll_test.scene.json` | 1 | 1 |
| `editor_brick_house_stability.scene.json` | 0 | 0 |
| `float_snap_test.scene.json` | 0 | 0 |
| `funnel_roll_test.scene.json` | 0 | 0 |
| `interaction_inspect_gizmo_harness.scene.json` | 0 | 0 |
| `interaction_replay_prediction_harness.scene.json` | 0 | 0 |
| `material_authoring_contract.scene.json` | 0 | 0 |
| `nature_hull_assets.scene.json` | 28 | 14 |
| `nbody_chaos_playground.scene.json` | 0 | 0 |
| `perf_1000.scene.json` | 0 | 0 |
| `perf_test.scene.json` | 0 | 0 |
| `physics_baseline.scene.json` | 0 | 0 |
| `physics_bench_solver.scene.json` | 0 | 0 |
| `physics_bench_solver_balls.scene.json` | 0 | 0 |
| `physics_bench_solver_boxes.scene.json` | 0 | 0 |
| `physics_bench_varied.scene.json` | 0 | 0 |
| `physics_regression_solver.scene.json` | 0 | 0 |
| `physics_roll.scene.json` | 0 | 0 |
| `physics_scale_1000.scene.json` | 0 | 0 |
| `physics_scale_200.scene.json` | 0 | 0 |
| `physics_scale_2000.scene.json` | 0 | 0 |
| `physics_scale_520.scene.json` | 0 | 0 |
| `physics_scale_sleepy_5000.scene.json` | 0 | 0 |
| `prediction_ragdoll_wall_200.scene.json` | 0 | 0 |
| `qqq.scene.json` | 3 | 1 |
| `ragdoll_box_pile_sleep.scene.json` | 0 | 0 |
| `ragdoll_fun.scene.json` | 21 | 11 |
| `ragdoll_playground.scene.json` | 0 | 0 |
| `ragdoll_sleep_island.scene.json` | 0 | 0 |
| `ragdoll_water_float.scene.json` | 0 | 0 |
| `replay_path_pool.scene.json` | 0 | 0 |
| `replay_prediction_simple.scene.json` | 0 | 0 |
| `replay_v2_generated_topology.scene.json` | 0 | 0 |
| `replay_v2_solver_one.scene.json` | 0 | 0 |
| `replay_velocity_four_ball.scene.json` | 0 | 0 |
| `roll_axis_test.scene.json` | 0 | 0 |
| `rolling_shadow_contact.scene.json` | 0 | 0 |
| `settle_test.scene.json` | 0 | 0 |
| `shadow_contact.scene.json` | 0 | 0 |
| `shadow_map_high.scene.json` | 0 | 0 |
| `shadow_map_plain.scene.json` | 0 | 0 |
| `shadow_map_ultra.scene.json` | 0 | 0 |
| `shadow_motion_stability.scene.json` | 0 | 0 |
| `shooting_range.scene.json` | 0 | 0 |
| `shooting_reaction_volley.scene.json` | 0 | 0 |
| `simdog.scene.json` | 3 | 1 |
| `skullbonezbase.scene.json` | 0 | 0 |
| `skyscraper_10_story.scene.json` | 0 | 0 |
| `solar_system.scene.json` | 0 | 0 |
| `solar_system_mars_slingshot.scene.json` | 0 | 0 |
| `solver_smoke.scene.json` | 0 | 0 |
| `space_field_200.scene.json` | 0 | 0 |
| `stacking.scene.json` | 0 | 0 |
| `standing_box_repro.scene.json` | 0 | 0 |
| `terrain_compare.scene.json` | 0 | 0 |
| `terrain_contact_probe_debug.scene.json` | 0 | 0 |
| `three_body_chaos.scene.json` | 0 | 0 |
| `three_body_figure_eight.scene.json` | 0 | 0 |
| `tornado_alley_showcase.scene.json` | 45 | 15 |
| `tornado_village_rampage.scene.json` | 162 | 12 |
| `transparent_ball_water_alpha.scene.json` | 0 | 0 |
| `tree_asset_stability.scene.json` | 12 | 10 |
| `tree_prefab_playground.scene.json` | 37 | 13 |
| `tree_types_showcase.scene.json` | 76 | 13 |
| `trees.scene.json` | 8 | 8 |
| `ui_blur_moved_off.scene.json` | 0 | 0 |
| `ui_blur_moved_on.scene.json` | 0 | 0 |
| `ui_blur_off.scene.json` | 0 | 0 |
| `ui_blur_on.scene.json` | 0 | 0 |
| `ui_controls.scene.json` | 0 | 0 |
| `ui_controls_bottom.scene.json` | 0 | 0 |
| `ui_controls_bottom_bg.scene.json` | 0 | 0 |
| `ui_controls_clip_scroll.scene.json` | 0 | 0 |
| `ui_min_size.scene.json` | 0 | 0 |
| `ui_min_size_bg.scene.json` | 0 | 0 |
| `ui_minimized.scene.json` | 0 | 0 |
| `ui_performance_histogram.scene.json` | 0 | 0 |
| `ui_physics_toggles.scene.json` | 0 | 0 |
| `ui_profiler_default.scene.json` | 0 | 0 |
| `ui_profiler_hierarchy.scene.json` | 0 | 0 |
| `ui_profiler_timeline.scene.json` | 0 | 0 |
| `ui_renderer_combo.scene.json` | 0 | 0 |
| `ui_scene_complete.scene.json` | 0 | 0 |
| `ui_scene_options.scene.json` | 0 | 0 |
| `ui_small_scroll.scene.json` | 0 | 0 |
| `ui_stress.scene.json` | 0 | 0 |
| `ui_water_combo.scene.json` | 0 | 0 |
| `water_ball_test.scene.json` | 0 | 0 |

## Exact Lifecycle Map

### Parse, reserve, and create

1. `AuthoredSceneParser::LoadAssetLibraries`,
   `ApplyAssetInstance`, and `ApplyAssetPrimitivePart` expand asset parts
   into ordinary scene hull vectors
   (`SkullbonezSource/Scene/AuthoredSceneParserAssets.cpp:463,583,984,1218`).
2. `SceneAuthoredSetup::SetUpSceneEntities` counts the expanded hull and
   hull-state vectors (`SceneAuthoredSetup.cpp:525`) and calls
   `SceneWorld::CommitPhysicsSceneCapacity` →
   `PhysicsEngine::ReserveAuthoredBodyCapacity` →
   `ColliderStore::ReserveShapeCapacity` →
   `RebindShapeReferences()` (`SceneWorld.cpp:350-391`,
   `PhysicsEngine.cpp:353-377`, `ColliderStore.cpp:163-169`).
3. Each hull loop resolves the token, calls
   `ConvexHullShape::TryLoadFromFile`, and enters
   `SceneWorld::TryCreateSceneEntity` →
   `PhysicsEngine::RegisterAuthoredBody` →
   `ColliderStore::CreateColliderRecord` →
   `AppendShape` (`SceneAuthoredSetup.cpp:719-837`,
   `SceneWorld.cpp:469-570`, `PhysicsEngine.cpp:560-618`,
   `ColliderStore.cpp:390-440,573-616`). Append-triggered relocation rebinds
   that shape kind before appending.

### Replace

Editor scale/shape commands build a value copy. The exact path is
`ResetEditorModelMotionAndWake` →
`PhysicsEngine::UpdateAuthoredBodyAndCollider` →
`ColliderStore::UpdateRecordForHandle` →
`ColliderStore::ReplaceShape`
(`EditorInteractionTools.cpp:728-748`, `PhysicsEngine.cpp:750-805`,
`ColliderStore.cpp:531-564,634-666`). Same-kind replacement overwrites one
row and reconstructs its const reference; kind-changing replacement appends
the new shape, removes the old kind row, and updates the record.

### Destroy

Editor delete/undo reaches `SceneWorld::DestroySceneEntity` →
`PhysicsEngine::DestroyAuthoredBody` →
`ColliderStore::DestroyColliderRecord` →
`RemoveShape` (`EditorHistory.cpp:223,248,580-588`,
`SceneWorld.cpp:590-650`, `PhysicsEngine.cpp:623-662`,
`ColliderStore.cpp:443-528,706-754`). Today RemoveShape swap-compacts one
shape per collider and remaps the one collider that owns the moved row. HS1 must
delete that single-owner assumption for shared hull indices.

### Rebind and Replay clone

`ColliderStore::RebindShapeReferences(kind)` walks every matching collider and
reconstructs its const pointer from the retained storage index
(`ColliderStore.cpp:350-387`). It runs after explicit reservation,
append-triggered relocation, and Replay cloning. The Replay chain is
`ReplayPredictionReserveOperations::SeedReplayPredictionEngineStorage` →
`PhysicsEngine::SeedReplayPredictionStorageFrom` →
`ColliderStore::CloneReplayPredictionStorageFrom` →
`CloneFixedListForReplayPrediction(m_hullShapes)` →
`RebindShapeReferences()` (`ReplayPredictionReserve.cpp:199-240`,
`PhysicsEngine.cpp:380-414`, `ColliderStore.cpp:126-147`).

## Mutation Audit

No narrowphase, solver, terrain, render, picking, diagnostics, snapshot, or
Replay consumer can mutate stored hull geometry. `CollisionShapeReference`
contains `const ConvexHullShape*`, both `GetShapeIf` overloads return const
pointers, and observed consumers borrow const references
(`CollisionShape.h:76-148`).

Cold post-load mutations do exist:

- `EditorObjectPlacement.cpp:346-349` and
  `EditorPlacementAssets.cpp:2114-2118` copy a cached/loaded hull and call
  `ScaleAxis` on the copy before creation.
- `ScaleCollisionShapeAxis` (`CollisionShape.h:258-263`) copies and scales a
  hull for replacement.
- `ColliderStore::ReplaceShape` whole-assigns a replacement value into a
  stored row; no consumer mutates through a reference.

A path-only key is therefore invalid because it can alias unit and scaled
payloads.

## Binding HS1 Decisions

### Authored identity key

Use a cold `HullShapeIdentity` made from the normalized resolved authored path
plus the validated IEEE-754 bit patterns of the exact cumulative X/Y/Z authored
scale. Scene JSON hulls use exact unit scale. Equality compares those identity
fields, never pointers or hull vertex/face floats.

Editor placement and axis-scaling commands must propagate their exact
path-plus-scale variant. A caller that cannot prove both fields is explicitly
non-shareable and receives a unique cold identity; it must not silently fall
back to path-only sharing. This preserves procedural/test values while making
all committed authored rows eligible for deterministic sharing.

### Mid-scene release policy

Retain identity-keyed hull rows until the scene store is cleared. Destroying the
last collider using a hull does not decrement a hot refcount, erase, or compact
the row; a later create of the same identity reuses its stable index. This makes
shared indices and surviving references stable through editor delete/undo.

Editor-created unique scale variants therefore occupy monotonic scene-lifetime
capacity and remain visible in high-water diagnostics. Exhaustion fails loudly
through the fixed-capacity policy. HS1 must add no runtime growth, hidden
refcount, or narrowphase lookup.

## HS1 Handoff

HS1 changes initial hull reservation from total colliders to distinct shareable
identities while keeping honest headroom for non-shareable/editor variants. It
must rename `PhysicsCapacityReason::HullColliders` to describe distinct hull
variants, preserve shared indices through rebind and Replay clone, and leave
sphere/box behavior unchanged.

## Validation

Documentation-only task; `AGENTS.md` requires no repository validation.
Evidence is the repeated 145-file census, zero unresolved identities,
repository-relative link checks, and `git diff --check`. No baseline, golden,
schema, config, source, or runtime artifact changed.
