# Interactive Grass For Legacy Rendering

Date: 2026-09-17
Status: Active by owner direction 2026-09-17; 6/7 phases complete; IG6 terminal gate in progress.
Owner: Runtime render composition, terrain presentation, Rendering/DX12 resources
Impact area: grass geometry/materials, visual interaction history, terrain, replay presentation, performance and automation
Commit name: `INTERACTIVE_GRASS`

## Owner Direction And Scope

Replace the impression of a flat grass texture in legacy mode with short grass
that folds beneath rolling objects and gradually stands back up after they
leave. The owner referenced the visual effect in newer Rocket League footage;
that is an appearance reference, not a claim about that game's implementation.

Activated for the combined Night Runner PR. Grass is enabled only in the demo
scene; other production scenes keep it disabled. Dedicated automation fixtures
may explicitly enable it to prove acceptance. Preserve Cine and Split Future.
Follow the repository orchestrator workflow.

Deliver a visual-only grass layer that works with ordinary/legacy rendering,
without requiring cinematic rendering or a particular cinematic style number.
Keep the existing terrain surface and texture beneath the blades and at long
distance. Grass must not change colliders, friction, solver output, sleep,
prediction trajectories or authoritative Physics state.

Start in a dedicated grass interaction fixture. Establish the rollout/default
policy in IG0: existing scenes must not silently acquire grass on water, rock,
solid showcase floors or other ineligible surfaces. Provide an Off setting that
restores their existing terrain rendering. Do not bundle a Cine cleanup.

## Dated Evidence And Integration Points

Planning inspection on 2026-09-17 at HEAD `9d919a281`, with unrelated Split
Future renderer changes staged in the shared checkout. These are integration
references, not a completed audit or runtime acceptance:

| Existing source | Relevance |
|---|---|
| `SkullbonezSource/World/Terrain.cpp` and `Terrain.h` | Own terrain geometry, flat slopes and height-map representation; grass roots must agree with the actual terrain surface. |
| `SkullbonezSource/World/TerrainEditing.cpp` | Terrain edits must invalidate affected roots and interaction tiles without retaining marks at an obsolete height. |
| `SkullbonezData/shaders/lit_textured.hlsl` | Current terrain shading samples grass albedo; existing style-specific terrain treatments must remain separate from vegetation eligibility. |
| `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp` and `RuntimeRenderer.PairedViews.cpp` | Compose ordinary/cinematic world rendering and multiple views; add grass without advancing its state once per view. |
| `SkullbonezSource/Rendering/RenderInstanceRenderer.h` and `RenderInstanceStore.h` | Existing instancing facilities to evaluate for compact grass placement/draw data. Reuse suitable generic capabilities without putting vegetation into Physics body storage. |
| `SkullbonezSource/Rendering/RenderGraph.h` and `DX12/Dx12RenderGraphExecutor.h` under `SkullbonezSource/Rendering/` | Resource lifetime, compute/draw dependencies and texture transitions for the interaction field. |
| `SkullbonezSource/Runtime/Replay/ReplayCapturePackets.h`, `ReplayTimelinePackets.h` and `ReplayV2Artifact.h` | Existing recorded state and selected-time contracts to audit before choosing grass history/reconstruction storage. |
| `tools/skarness.py` and `Agentic/Skills/skarness/SKILL.md` | Native input, state observations, screenshots and identity-bound behavioral proof. |

At activation verify what foliage geometry, placement masks and reusable GPU
field support actually exist. Do not assume the texture reference proves that
all other vegetation paths are absent. Record hardware, driver, resolution,
scene scale, terrain extent and available history before finalizing budgets.

## Proposed Rendering And Interaction Design

### Grass geometry and placement

- Draw small instanced blade meshes or clumps near the camera. Use enough
  vertical segments to bend into a curve, with fixed roots and progressively
  displaced tips; translating a rigid upright card is insufficient.
- Place roots deterministically from terrain coordinates and a scene seed.
  Align to the sampled terrain normal. Vary height, width and orientation
  within bounded authored ranges to avoid obvious rows or identical tufts.
- Use an explicit vegetation/material mask and slope/water exclusions. Avoid
  guessing grass eligibility from the green channel of an albedo texture.
- Use spatial tiles, view culling, distance detail reduction and a smooth
  transition to the existing grass texture. Preserve coverage through LOD
  changes and inspect silhouettes for shimmer and popping.
- Use consistent ordinary-mode lighting and deformed normals. Start with
  simple opaque blade geometry where practical; compare alpha-cutout clumps
  only if they improve measured cost and appearance. Avoid an unmeasured
  transparent overdraw solution.
- Shadow rendering must use compatible deformation and bounds when enabled.
  Limit distant grass shadows independently; no requirement for expensive
  per-blade self-shadow simulation. Optional subtle wind is secondary to the
  requested contact effect and must diminish beneath compressed objects.

### Contact footprints and persistent bending

- Runtime composes bounded, detached interaction values from coherent body
  poses/shapes and terrain samples: stable object identity, scene generation,
  simulation tick, ground footprint, bend direction and compression amount.
  Rendering consumes generic values and must not include Physics or Replay
  owners to discover bodies on its own.
- Use a world-space interaction texture or bounded tile atlas containing bend
  direction and compression. A vertex shader samples it to bend blades. The
  field provides persistence after the object has left; current-position-only
  displacement does not satisfy recovery/trail acceptance.
- Cover sphere and oriented box footprints first. Gate stamping by distance
  to the grass layer/terrain, including slope and contact height. A projected
  bounding rectangle from a high airborne body must not flatten the ground.
- Sweep eligible footprints between consecutive simulation samples to avoid
  dotted trails at speed. End the sweep on teleport, reset, scene generation
  change or identity discontinuity. Do not connect unrelated positions.
- Resting and sleeping objects continue holding grass down; relying only on
  newly emitted collision events would allow grass to grow through them.
- Define deterministic overlap/composition order and saturation. Opposing
  tracks must not cancel compression or create unbounded bend vectors.
- Use a finite recovery duration, initially tunable over roughly 2-5 seconds.
  Define the curve in simulation seconds and a precise fully-recovered time.
  A finite-support curve allows bounded history reconstruction; an endless
  exponential tail would not have an exact finite reconstruction window.
- Keep tiles anchored to world coordinates. Camera movement must neither drag
  tracks nor erase a trail that should still exist when revisited. Define
  tile eviction/reconstruction, resolution, borders and bounded overflow policy
  before implementation; document visible degradation if capacity is exceeded.

### Time, reset and replay

- Advance interaction history from the selected simulation time, not wall-clock
  time or the number of render passes. Pause freezes recovery; a single step
  advances exactly the supported simulation interval. Multiple views and
  reflections consume the same field version without advancing it again.
- Scene reset/reload clears transient grass state and reapplies current held
  footprints. Deleted objects stop contributing; old dense indices cannot
  transfer an imprint to a new identity. Changing terrain invalidates only
  the affected region where possible.
- On reverse play, seeks or branching, reconstruct grass at the selected tick
  from a checkpoint plus bounded footprint history, or from the complete
  finite recovery window. Never run a decay update with negative delta time.
- IG0 must prove whether recorded poses/shapes are sufficient to reproduce
  footprint samples. If not, record the minimum derived interaction stream
  through the Replay owner with bounded storage and owned format migration.
  Rendering retains no independent replay authority or mutable body pointers.
- A recording beginning mid-track needs initial field/history coverage or a
  clearly defined initial grass state. Older recordings without the required
  evidence use an explicit fallback; never silently display present-day tracks
  at a historical tick or claim faithful reconstruction with missing history.
- Prediction ghosts do not stamp the current grass. Solver comparison views
  must use their own recording/time context rather than sharing unrelated
  interaction history. No grass state is added to Physics snapshots.

## Ownership, Capacity And Cost

Runtime owns interaction eligibility and mapping from scene identity/time to
presentation values. The grass presentation owner enforces placement, field
version and reconstruction rules. Rendering/DX12 owns GPU resources, uploads,
barriers, draw submission and device lifetime. UI, if extended, emits typed
settings commands; it does not retain a second copy of active grass state.

Preallocate and cap placement tiles, instances, interaction stamps, history,
uploads and GPU textures. Document owner, units, lifetime, capacity, overflow
behavior and counters. No per-blade CPU simulation, heap growth in steady
frames, CPU readback for ordinary rendering, or new per-body Physics fields.
Large-world and four-view behavior must fit the declared budget.

Expose Off/Low/High quality and bounded density, draw distance, blade height,
bend strength and recovery duration as appropriate. Changes take effect through
one owner; save only through explicit existing persistence actions. Any new
authored scene/config fields require the owning format version and migrations,
including legacy/current/future-format and writer tests.

IG0 sets numeric CPU/GPU/memory budgets on the user's actual hardware. A useful
initial target to evaluate is no more than about 1 ms additional GPU time and
0.25 ms CPU time at 1080p for the standard grass fixture; this is a proposed
budget, not a performance claim. Measure p50/p95, draw/instance counts, shaded
coverage, texture/history memory and upload volume with the feature off/on.
Report four-view and dense-object stress separately. Lower quality by reducing
bounded detail, not by dropping required contact/recovery correctness silently.

The earlier 1-2 day estimate was for a visible prototype. Production scope also
includes terrain masks, DX12 lifetime, replay reconstruction, quality controls
and regression closure; re-estimate after IG0 rather than treating that initial
estimate as a delivery commitment.

## Phases

- [x] **IG0 - Audit and settle contracts.** Confirm existing grass/render paths,
  terrain sampling and masks, object-to-footprint extraction, time ownership,
  replay evidence and capacity requirements. Decide rollout defaults, finite
  recovery curve, supported bounds and numeric budgets. Record decisions and
  the exact native acceptance matrix before writing production code.
- [x] **IG1 - Render rooted grass in legacy mode.** Add bounded instanced blades,
  terrain-conforming placement, material masks and a dedicated fixture. Keep
  ordinary terrain and Off behavior intact. Establish shader bake, depth,
  lighting and screenshot evidence; add native observability/control seams.
- [x] **IG2 - Add object flattening and recovery.** Implement bounded world-space
  stamps/field updates, sphere and oriented-box footprints, swept movement,
  held pressure, finite recovery and blend rules. Cover fast movement, slopes,
  overlaps, sleeping objects and airborne negative controls with focused tests.
- [x] **IG3 - Integrate time and lifecycle.** Bind pause/step/reset, generation
  changes, removal, terrain edits and multi-view consumption. Implement selected-
  tick replay reconstruction and explicit old/missing-history behavior. Prove
  seek/reverse/branch isolation and keep prediction ghosts from stamping grass.
- [x] **IG4 - Finish quality and performance.** Add distance detail, culling,
  transitions, compatible shadows, quality controls and persistence migrations.
  Meet the IG0 budgets, exercise atlas edges/eviction and capacity boundaries,
  and verify no steady-state growth or device-resource leaks.
- [x] **IG5 - Complete native acceptance and visual tuning.** Exercise every
  matrix row through Skarness, inspect screenshots/videos, tune short-turf
  appearance, recovery and contact readability. Confirm other materials/scenes
  and replay controls retain their declared behavior; record any default rollout
  decision and the approved hardware/settings evidence.
- [ ] **IG6 - Terminal validation and closure.** Complete cumulative mapped
  gates, ownership/visual/performance review and final fixes. Run the terminal
  plan-completion gate once, leave Profile built with a no-op rebuild witness,
  reconcile MASTER-PLAN/SessionState and remove the completed plan according to
  repository lifecycle. Do not mark closure while required evidence is missing.

## Required Behavioral Proof

| Scenario | Acceptance |
|---|---|
| Slow rolling ball and translating/rotating box | Roots stay fixed; footprint and bend follow actual body motion; visible continuous flattened trail. |
| Fast ball and teleport/reset | No dashed trail between valid samples; no long false streak across discontinuities. |
| Resting and sleeping bodies | Grass remains held below the object, then recovers after it is lifted/removed. |
| Airborne object and prediction-only ghost | No imprint on the current ground. |
| Object leaves a patch | Compression decreases according to the chosen curve and reaches zero at its declared recovery time; screenshots at immediate, midpoint and recovered states. |
| Different render rates, pause and one tick | Same grass state at the same simulation tick within declared GPU tolerance; pause holds and one tick advances once. |
| Seek backward/forward, reverse and branch | Same selected-time state as straight playback; future/live tracks cannot leak into the past or a different recording. |
| Slopes, height maps and edited ground | Correct root heights and footprint eligibility; no floating roots, below-ground blades or old-height marks. |
| Camera pans away/back, tile borders and four views | World-fixed tracks and seamless field sampling; eviction/history policy proven; no duplicate recovery updates. |
| Low/High/Off, density and distance changes | Smooth transitions, valid bounds/shadows, stable memory; Off restores the original terrain path. |
| Repeated overlap, capacity pressure, scene reload | Bounded values/work, explicit capacity behavior and no stale identities/resources. |
| Feature off/on with identical Physics inputs | Same authoritative body/contact/sleep and prediction output; the visual effect never changes simulation. |

Preserve commands, executable/source identity, selected tick/body IDs, settings,
field/stamp counters, timing and native captures under
`TestOutput/skarness/interactive-grass/`. A command acknowledgement or a grass
screenshot alone does not prove persistent deformation or replay correctness.

## Validation When Activated

During implementation compile affected payloads and run narrow meaningful
subsystem tests. Reserve heavy suites and final review for terminal closure,
subject to mandatory earlier commit/push gates. Documentation creation now
runs none of these commands.

Map the final diff to the current AGENTS.md. Expected commands include:

```bat
python tools\bake_shaders.py --check
tools\validate_build.bat Automation
tools\validate_fast.bat
tools\validate_dx12_renderer.bat
tools\run_graphics_stress.bat 1
tools\validate_perf.bat
tools\validate_physics.bat
tools\validate_replay_visual_fidelity.bat
tools\validate_replay_v2_artifact.bat
tools\agent_validate.bat --plan-completion
```

Use Skarness capabilities and extend missing grass controls/observations under
the repository's normal rules. Add CPU tests for pure footprint/recovery/history
rules and native tests for actual GPU output; include empty-field, no-contact,
pause, wrong-generation and absent-history negative controls. Register the
new cases in ordinary validation owners. Add UI and authored-format gates if
those surfaces change; do not duplicate checks already run by the final
umbrella without a new failure or unresolved concern.

Keep approved baselines and archived recordings unchanged unless their owned
transition policy is explicitly satisfied. Record inherited failures separately.
Shader-set provenance mismatches are not authorization to refresh replay goldens.
This rendering plan does not inherit the broader authority of a Physics plan.

## IG0 implementation contract - 2026-09-17

The integration checkout is nightrunner-17th-SEP-26, rooted at c686f653b
with the preserved Catto patch. Automation compilation passed with zero
warnings/errors before new feature code. Hardware is NVIDIA GeForce RTX 3080,
driver 32.0.15.9186, desktop 2560x1440. Measure the standard acceptance fixture
at 1920x1080 and four-view at the desktop size; results remain pending.

Owner rollout: only the generated Demo enables grass by default. Authored
production scenes remain disabled; explicit test fixtures may enable it.
Eligibility is an explicit vegetation setting plus terrain bounds, water
height and a maximum slope, never texture colour. Existing terrain remains.

Provisional numerical choices: 0.35 engine-unit blade height, 3 seconds finite
linear recovery (interactive range 2-5 seconds), 120 Hz tick time, and 0.5-unit
field cells. Each cell stores its release tick, bend direction and stable source
identity. A stamp of compression c at tick t contributes release=t+c*duration;
maximum release wins. Compression is max(0,(release-selectedTick)/duration).
This gives an exact recovery endpoint, bounded overlapping pressure, stable
identity tie-breaking, and no cancellation from opposing tracks. A later stamp
can never reduce existing pressure. No wall clock enters field updates.

Sphere and oriented-box occupancy is evaluated by intersecting the root-to-tip
segment with the actual shape. A high airborne object cannot affect the root.
Footprints carry full orthonormal box axes, not world-axis bounding rectangles.
Sweeps must come from adjacent same-identity samples; discontinuities end them.

Implementation separates Physics-owned shape/pose values, Runtime footprint
composition/time reconstruction, and generic Rendering instance submission.
The planned field is a bounded world-tile atlas. GPU blades share segmented
geometry and deform from field values; no per-blade CPU integrator or Physics
body field is introduced. Final tile/instance/upload budgets are not yet settled.

Retained ReplayPresentationSample already supplies stable identities, full
orientation, position and simulation seconds. Collider geometry is scene-owned;
shape/topology changes and recordings without sufficient history require an
explicit missing-history state. Replay remains the source of retained time:
any grass working field is a discardable cache reconstructed from the complete
finite window, not a new durable timeline. Required pose-window access and
shape-lifetime proof remain IG0 work before it can be marked complete.

Native matrix is the Required Behavioral Proof table above, including all
negative controls. Initial performance targets remain additional GPU p95 <=1 ms
and CPU p95 <=0.25 ms at 1080p; they are targets, not measured claims. Counters
must expose field availability, selected tick, context identity, tile/instance
capacity and drops. No phase is accepted by this contract note alone.

## Exact root and visual-relief eligibility decision - 2026-09-18

Each eight-blade patch now carries the exact terrain height at every seeded
blade root. Four-corner interpolation was insufficient for arbitrary grid
spacing. The generic instance grows from 26 to 30 floats (nine attributes);
both four-view owners still fit the existing combined 24 MiB assertion.
The terrain samples are cached with placement and terrain revision, while
pressure updates reuse the stored roots. Shader seed arithmetic matches the
integer CPU placement function.

Cinematic visual-only terrain relief is explicitly ineligible: it moves the
rendered surface away from the Physics ground. Grass is disabled while that
relief is enabled with nonzero strength, with the rule stated in the Render
control tooltip. This preserves existing cinematic terrain instead of drawing
roots at unrelated physical heights. Ordinary Demo and zero-relief cinematic
Demo remain eligible; authored scenes still require validation fixture opt-in.

## Accepted implementation and native evidence - 2026-09-18

IG0-IG5 are accepted. IG6 remains open until the terminal gate and integrated
PR checks pass. The independent grass review has no remaining correctness
blockers after endpoint continuity, recording epoch, and recording-gap fixes.

The final default height is .22 engine units. The two fixed caches together
occupy 22,820,400 bytes (24 MiB ceiling), with 65,536 world cells per owner,
64 bounded hash probes, 131,072 root tests per simulation sample, four cached
views per owner and at most 16,384 eight-blade patches per view. Each patch
uploads 30 floats. Saturation records drops and marks faithful reconstruction
unavailable; it never grows storage. Placement and interaction remain detached
from Physics body storage. Off skips placement/drawing while preserving the
small live history update so enabling grass does not invent a fresh past.

Replay outer v6 retains shape dimensions and terrain identity with integrity
checks, plus an explicit per-body sweep-continuity flag. Continuity compares the
prior recorded identity/pose to the current fixed step's previous endpoint;
ResetPoseHistory's valid collapsed pose is not evidence of continuous motion.
Recorder reset/configuration increments the live recording epoch and historical
revision at ReplayTimeline. Loading another artifact only changes the historical
revision. Recording stop/resume preserves retained data; the first resumed
sample marks grass evidence unavailable, so any recovery window spanning the
gap uses upright grass. Reconstruction resumes after a fully covered window.
Scene/application frame numbers are never used as Physics tick adjacency.

Native evidence under TestOutput/skarness/interactive-grass:
- acceptance-12: identity, recovery endpoint, pause, quality, live/retained/loaded
  parity, four-view consumption and zero gameplay allocation violations.
- edges-final-03: the original 16 edge cases passed before the newly added hidden
  case exposed a test endpoint shortcut. The corrected hidden-02 separately
  proves live/retained/loaded compression and identity equality.
- heightmap-01: irregular 4.1-unit grid with exact seeded blade root sampling;
  cinematic-shadows-01 and cinematic-relief-01: cast geometry and relief exclusion.
- recording-gap-01, non-lockstep-01, retention-reset-01, short-teleport-01:
  missing-window recovery, multiple Physics ticks per application frame,
  recording reset parity and short-discontinuity isolation all pass.

Screenshots of terrain roots, contact flattening and cinematic grass were
inspected. Grass casts its deformed/faded geometry into enabled terrain shadow
maps. It does not receive object/terrain shadows or perform blade self-shadowing.
Cinematic visual-only relief remains ineligible when nonzero.

performance-final-02 (RTX 3080, driver 32.0.15.9186) records 220 timing samples
per mode. Standard 1920x1080 CPU p95 sum is .1561 ms against .25 ms; grass GPU
p95 is .0246 ms against 1 ms. Four-view 2560x1440 CPU p95 sum is .9803 ms.
Its .0174 ms GPU marker reports the last pane, not aggregate four-view GPU cost.
Paused, dense-100 and Off measurements are retained in the same results.json;
Off placement/draw CPU is zero, with .0105 ms p95 live history maintenance.

The ordinary Skarness gate owns the new acceptance and edge scripts. Shader
freshness passes all 56 stages. Continuity delta/codec corruption/legacy and
recording epoch tests pass with 114 assertions across three focused cases.
No archived replay or visual golden was refreshed.
