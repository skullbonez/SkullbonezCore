# Contact Impact Audio Plan And Progress

Date: 2026-07-02
Status: Plan complete; Sound-tab flash/text follow-up validated
Impact areas: runtime, physics contact diagnostics/events, scene/assets, audio, project configuration, validation
Validation for this plan edit: Documentation/progress update only after continuation validation passed.

## Goal

Add material-aware contact impact audio:

- Play an impact sound when a meaningful contact is registered.
- Scale volume by distance from the listener/camera.
- Scale volume by impact strength.
- Enforce a minimum impact threshold so resting contacts do not chatter.
- Enforce cooldowns so persistent solver rows do not produce rapid-fire noise.
- Choose sound sets from contact material pairs such as metal/stone or wood/terrain when authored.
- Provide an in-game generic thud sample library so impact candidates can be auditioned and assigned without editing JSON.
- Keep audio asynchronous and non-authoritative: sound playback must never affect physics determinism.

## Current Source Facts

- There is no existing audio subsystem or checked-in audio asset set in the source tree. Searches for common playback APIs and audio file extensions found no production sound layer.
- The strongest contact source is `SkullbonezSource/Physics/PersistentContactSolver.cpp`, where object and terrain manifolds become persistent solver rows and later emit `PhysicsDebugContact` records with contact point, normal, penetration, and `normalImpulse`.
- `PhysicsWorld` already supports deterministic object narrowphase worker dispatch by collecting worker-local events and committing them in pair order. Contact audio should follow that spirit: collect compact immutable events, then consume them outside the solver.
- `SimulationSystem` can commit multiple `PHYSICS_FIXED_DT` physics ticks in one rendered frame. A robust audio bridge should drain contact events after each committed physics step, not only after the final frame-level `TickPhysics()` return.
- `CameraCollection::GetCameraTranslation()` exposes the active camera position that can act as the first listener position.
- Asset JSON currently has render `material` blocks plus physics values like `mass` and `restitution`. That render material is not a reliable audio material. Terrain contact manifolds already carry a `materialId` field, but object/object contacts do not appear to have an explicit contact material identity yet.

## Non-Goals

- Do not play one sound for every solver row every tick. Persistent contacts are expected to live across frames.
- Do not let audio timing, thread scheduling, or voice limits feed back into physics, replay, baselines, or scene state.
- Do not infer contact sound only from render color or render material mode. Add an explicit contact/audio material field with a default.
- Do not add per-frame dynamic allocation in the contact hot path.
- Do not use the engine `WorkerPool` as a long-lived audio mixer thread. The worker pool is a deterministic fork-join/job facility; audio should use the platform audio runtime and bounded queues.

## Proposed Design

### 1. Contact Audio Materials

Add explicit contact material identity to authored data:

```json
"contactMaterial": "metal"
```

Initial material names:

- `default`
- `metal`
- `stone`
- `wood`
- `earth`
- `water`
- `glass`

Use stable IDs internally after parsing, but keep names in scene and asset files. Treat missing values as `default` so existing scenes continue to load.
First-pass implementation also infers asset-instance contact material from existing asset `material.mode`/`kind` when `contactMaterial` is not explicit. Bare scene objects still default to `default` unless they author `contactMaterial`.

Material-pair lookup should be order-independent for object/object contacts:

```text
metal + stone -> impact.metal_stone
stone + metal -> impact.metal_stone
wood + earth  -> impact.wood_earth
unknown pair  -> impact.default
```

Each material pair or sound set should be able to define:

- sample list
- minimum impulse threshold
- cooldown milliseconds
- maximum distance
- base gain
- pitch randomization range
- max simultaneous voices
- optional light/medium/heavy impulse bands

### 2. Material Propagation

Add material identity without changing solver math:

- Parse optional `contactMaterial` from asset libraries and scene object entries.
- Store it in scene object state and the compatibility `GameModel`/body setup path.
- Copy it into collider/body records used by the physics step.
- Preserve terrain material identity through `TerrainContactManifold::materialId`.
- For object/object contacts, resolve both body/collider contact material IDs when building contact audio events.

Do not overload `Rendering::RenderMaterial`. Render material remains visual intent; contact material is gameplay/audio intent.

### 3. Contact Event Capture

Introduce a compact event shape, owned outside the solver hot math:

```cpp
struct ContactAudioEvent
{
    int bodyA;
    int bodyB;              // Terrain uses TERRAIN_BODY_INDEX.
    uint32_t featureId;
    uint32_t materialA;
    uint32_t materialB;
    Vector3 point;
    Vector3 normal;
    float normalImpulse;
    bool isTerrain;
};
```

Preferred event source:

- Build events from persistent contacts after solver impulses are known, near the existing debug-contact emission pass.
- Use `normalImpulse` for thresholding and impact loudness.
- Use `bodyA/bodyB/featureId/isTerrain` as the stable cooldown key.
- For terrain, use `bodyA + terrain material + featureId`.

Filtering rules:

- Drop events below the selected material-pair minimum impulse.
- Collapse multiple rows for the same body pair during one physics tick to the loudest representative event.
- Suppress events while the same key is inside its cooldown window.
- Allow stronger impulse spikes to retrigger after a shorter "override" cooldown if tuning wants heavy crashes to punch through.
- Cap total contact audio events per physics tick and voices per sound set.

### 4. Runtime Bridge

Add a runtime-side `ContactAudioEmitter` that:

- Owns cooldown state and per-step scratch buffers.
- Reads contact events after each physics step.
- Resolves material pair to a sound set.
- Computes listener attenuation from the copied listener position.
- Submits only small playback commands to the audio engine.

The runtime bridge should be called from the simulation after-step path whenever contact audio is enabled. Today `Run::AfterPhysicsStep()` is only wired for manipulator physics or replay capture; contact audio needs either:

- an always-on after-step callback that runs cheap no-op work when disabled, or
- a dedicated simulation post-step hook for optional systems.

Distance curve recommendation:

```text
distance = length(contactPoint - listenerPosition)
distanceT = clamp(1 - distance / maxDistance, 0, 1)
distanceGain = distanceT * distanceT
impactGain = saturate((normalImpulse - minImpulse) / impulseRange)
finalGain = baseGain * distanceGain * impactGain
```

Keep the first pass simple and tunable. More realistic inverse-square rolloff can come later if the game needs it.

### 5. Audio Engine

Add a small runtime audio service under `SkullbonezSource/Runtime/Audio/`.

Recommended backend:

- XAudio2 on Windows.
- Decode/load short `.ogg` samples at startup through vendored `stb_vorbis.c`.
- Maintain a bounded pool of source voices.
- Reuse bounded XAudio2 source voices directly from the runtime post-step hook. XAudio2 owns the asynchronous mixer thread; no custom thread is needed in the first pass.
- Copy listener state into the audio service; do not let the audio thread read camera or physics objects.

XAudio2 already owns real-time audio processing internally, so the game mostly needs a safe command queue and voice pool. A custom mixer thread is optional and should not be the first implementation unless XAudio2 integration demands it.

Project setup:

- Add the needed audio library dependency to all project configurations.
- Add source files to `SKULLBONEZ_CORE.vcxproj` and `.filters`.
- Add initial audio assets under `SkullbonezData/audio/impacts/`.
- Add an authored material-to-sound map, for example `SkullbonezData/audio/contact_audio.materials.json`.

## Sound Asset Sourcing

Preferred first-pass asset policy:

- Prefer CC0, public-domain-equivalent, or paid royalty-free sounds for anything intended to ship.
- Prefer CC0 for assets committed directly to this repository, because repo assets are easy to redistribute as standalone raw files.
- Avoid non-commercial, share-alike, GPL, or unclear licenses for runtime assets.
- Keep an attribution/source ledger at `SkullbonezData/audio/LICENSES.md` even when attribution is not required.
- Store source URL, author/vendor, license, download date, original filename, edited filename, and any required attribution text.
- Prefer short `.wav` PCM sources. Normalize, trim silence, and export engine-ready variants into `SkullbonezData/audio/impacts/`.
- Keep raw downloads out of the runtime asset folder unless their license explicitly allows redistribution in that form. For royalty-free libraries that allow use in a finished game but restrict standalone redistribution, commit only if the project license and distribution path make that safe.

Recommended sources:

| Source | Best use | License posture |
| --- | --- | --- |
| Sonniss GDC Game Audio Bundles | High-quality realistic impacts, debris, metal, stone, wood, glass | Royalty-free, commercially usable, no attribution required according to Sonniss bundle pages, but licensed for media production and restricted against standalone/raw redistribution. Good for packaged builds after license review; use CC0 first for checked-in starter assets. |
| OpenGameArt CC0 packs | Small practical starter set for wood, stone, metal, glass, item hits | Use CC0-filtered packs first. Avoid mixed-license packs unless each file is checked. |
| Freesound | Targeted gaps after the starter set, especially odd foley | Filter to CC0 for no-attribution assets. CC-BY can be used only if the attribution ledger and credits path are implemented. Avoid NC/SA for shipped runtime assets. |
| Kenney audio packs | Clean CC0 placeholders or stylized layers | Mostly useful for UI/stylized sounds, but safe for placeholders and simple layers. |
| Pixabay sound effects | Supplemental one-off sounds | License allows commercial use inside projects, but not standalone redistribution. Keep license records and avoid relying on it as the main runtime library. |
| Soundly | Paid/subscription search workflow for production SFX | Commercial use is allowed for projects made while subscribed, but raw library redistribution is not allowed. Keep account/license proof. |
| Boom Library and similar paid SFX libraries | Polished production impacts and material layers | Good final-quality source when budget matters less than consistency. Keep invoice/license proof and do not commit raw redistributable-looking library dumps without license review. |

Initial material shopping list:

- `default`: dull thud, short low transient.
- `metal`: clanks, rings, dull metal hits, scraped variants.
- `stone`: rock chips, stone-on-stone impacts, debris ticks.
- `wood`: hollow knocks, snaps, plank impacts.
- `earth`: dirt thumps, gravel scatter, sand/soil impacts.
- `water`: splash and wet slap layers for terrain/shoreline contacts.
- `glass`: small brittle ticks and heavier shatter-compatible hits.

### 6. Determinism And Replay Policy

Audio is presentation only:

- Physics baselines must remain byte-exact.
- Sound event ordering must not mutate physics, body stores, replay samples, or random gameplay state.
- Playback randomness should use an audio-only RNG seeded separately from physics.
- Validation/headless launches should be able to disable audio deterministically.
- Replay can either play sounds from replayed contact samples later, or remain silent until a dedicated replay-audio slice. Do not silently change replay artifact hashes for audio.

## Implementation Phases

### Phase 0 - Schema And Defaults

- [x] Add `contactMaterial` parsing and validation for assets and scene-authored objects.
- [x] Add a default material for generated scenes and legacy scenes.
- [x] Add stable name-to-ID lookup through existing `HashStr` tokens.
- [x] Add a material-pair sound-map file with initial defaults.
- [x] Confirm existing render `material` blocks remain untouched.

### Phase 1 - Audio Service Skeleton

- [x] Add `Runtime/Audio` source files with init, shutdown, OGG loading, and playback submission.
- [x] Add bounded XAudio2 source voice pooling.
- [x] Add project dependencies and filters.
- [x] Add launch switch to disable contact audio: `--no-contact-audio` / `--mute-contact-audio`.
- [x] Add a tiny manual smoke path that plays one known sound without physics.

### Phase 2 - Contact Event Extraction

- [x] Add a contact audio event buffer with reserved capacity.
- [x] Collect events after solver impulses are known from `PhysicsDebugContact`.
- [x] Include contact point, normal impulse, terrain flag, feature ID, and material IDs.
- [x] Avoid per-frame allocations in the audio candidate/cooldown path.
- [x] Keep audio consumption presentation-only and outside solver state mutation.

### Phase 3 - Cooldown, Threshold, And Attenuation

- [x] Implement minimum impulse thresholds by material pair.
- [x] Implement per-body-pair cooldowns.
- [x] Collapse multiple rows per pair to the strongest event per physics tick.
- [x] Implement listener-distance attenuation from camera/render listener position.
- [x] Add per-step candidate caps and per-sound-set voice caps.
- [x] Add tuning constants to the material-pair sound map.

### Phase 4 - Material Sound Selection

- [x] Add initial CC0 sound assets for default, metal, stone, wood/bark, glass, matte, and water.
- [x] Implement material-pair fallback with material-specific wildcard preference over `default/*`.
- [x] Add pitch/volume variation that is audio-only and does not affect physics determinism.
- [x] Add optional light/medium/heavy impulse bands.
- [x] Add dedicated earth/dirt samples instead of falling back to default/stone.
- [x] Follow-up: replace broad/material placeholder sounds with a generic CC0 thud library and a single active default thwack sample.
- [x] Follow-up: make `impact_thud_thwack_02.ogg` the active default impact sample.

### Phase 5 - UI, Diagnostics, And Tooling

- [x] Add a small diagnostic counter set: events seen, events rejected by threshold, rejected by cooldown, submitted, voices dropped.
- [x] Add optional UI/config controls for enable, master gain, max distance, and debug counters.
- [x] Add Sound-tab sample browsing with Play and Use actions for decoded contact-audio samples.
- [x] Add Sound-tab `Flash emitters` toggle for visible white contact-audio feedback.
- [x] Bound Sound-tab sample and material-set picker labels so long sound names cannot draw past the selector area.
- [x] Add startup console summary when the contact audio map loads.
- [x] Add CLI opt-out for validation/headless launches.

### Phase 6 - Validation

- [x] Targeted compile check: `tools\validate_build.bat Profile` passed in 8.1 seconds with 0 warnings / 0 errors after the C4701 third-party warning suppression.
- [x] Scene-load smoke: `Profile\SKULLBONEZ_CORE.exe --renderer dx12 --scene-load-only --scene SkullbonezData/scenes/concept_12_low_poly_art_style.scene.json` exited 0 in 2.6 seconds and printed `[audio] Contact audio loaded 9 sound set(s), 14 sample(s).`
- [x] `tools\validate_project_filters.bat` passed in 0.9 seconds after adding validator rules for runtime audio, audio data, and vendored stb items.
- [x] `tools\validate_fast.bat` passed in 34.7 seconds: format clean, project filters clean, runtime boundaries clean, Profile/Debug builds ready with 0 warnings / 0 errors.
- [x] Final PR gate: `tools\validate_full.bat` passed in 29.9 seconds. DX12 InfoQueue reported 0 validation errors, DX12 screenshots matched committed baselines, and `physics_regression_solver.csv` matched byte-exactly.
- [x] Physics validation is covered by the final full gate for this branch.
- [x] Final validation output recorded before commit in `TestOutput\contact_audio_validate_full.log`.
- [x] Continuation targeted compile check: `tools\validate_build.bat Profile` passed in 11.5 seconds with 0 warnings / 0 errors after adding smoke/config/band controls.
- [x] Continuation runtime boundary check: `tools\validate_runtime_boundaries.bat` passed in 5.1 seconds after reducing runtime `Cfg()` access count with config snapshots.
- [x] Continuation final PR gate: `tools\validate_full.bat` passed in 44.5 seconds. DX12 InfoQueue reported 0 validation errors, DX12 screenshots matched committed baselines, and `physics_regression_solver.csv` matched byte-exactly.
- [x] Continuation final manual smoke: `Profile\SKULLBONEZ_CORE.exe --contact-audio-smoke` exited 0 in 1.0 seconds, wrote `TestOutput/contact_audio_smoke.json`, and reported initialized=true, loaded=true, submitted=true, submittedVoices=1.
- [x] Follow-up targeted build: `tools\validate_build.bat Profile` passed in 8.4 seconds with 0 warnings / 0 errors after scoped formatting.
- [x] Follow-up direct smoke: `Profile\SKULLBONEZ_CORE.exe --contact-audio-smoke` exited 0 in 1.3 seconds and loaded 1 sound set / 38 samples with submittedVoices=1.
- [x] Follow-up final PR gate: `tools\validate_full.bat` passed in 38.6 seconds. DX12 InfoQueue reported 0 validation errors, DX12 screenshots matched committed baselines, and `physics_regression_solver.csv` matched byte-exactly.
- [x] Sound-tab visual follow-up: inspected `TestOutput\contact_audio_sound_tab.png` and `TestOutput\contact_audio_sound_tab_tall.png`; sample and material-set picker labels no longer overrun their row.
- [x] Sound-tab visual validation: `tools\validate_ui.bat` passed in 23.0 seconds. DX12 InfoQueue reported 0 validation errors, window containment reported 0 outside-window pixels, and UI screenshots passed.
- [x] Tool/source follow-up validation: `tools\validate_fast.bat` passed in 18.8 seconds with format, project filters, runtime boundaries, and ready-build checks clean.
- [x] Sound-tab flash final PR gate: `tools\validate_full.bat` passed in about 30 seconds. DX12 InfoQueue reported 0 validation errors, DX12 screenshots matched committed baselines, and `physics_regression_solver.csv` matched byte-exactly.

## Likely Files

| Area | Candidate files |
| --- | --- |
| Audio service | `SkullbonezSource/Runtime/Audio/*` |
| Runtime hook | `SkullbonezSource/Runtime/RunFrame.cpp`, `SkullbonezSource/Runtime/Run.h` |
| Simulation hook | `SkullbonezSource/Physics/SimulationSystem.*` |
| Contact extraction | `SkullbonezSource/Physics/PersistentContactSolver.cpp`, `PhysicsWorld.*`, `PhysicsEngine.*`, `PhysicsScene.*` |
| Material storage | `GameModel.*`, `ColliderStore.*`, scene object structs |
| Scene/asset parsing | `SkullbonezSource/Scene/TestSceneParser.cpp`, `TestScene.h`, `SceneSnapshotWriter.cpp` |
| Data | `SkullbonezData/audio/*`, `SkullbonezData/assets/*.assets.json`, scene JSON when needed |
| Build | `SKULLBONEZ_CORE.vcxproj`, `SKULLBONEZ_CORE.vcxproj.filters` |

## Risks And Decisions

- [x] First implementation consumes `PhysicsDebugContact` plus material lookup after impulses are known. Dedicated solver-side audio events remain optional future cleanup.
- [x] Terrain contacts use the object material plus `default` for the terrain side in the first pass. Per-cell or authored terrain material audio is deferred.
- [x] Contact audio is enabled by default when the backend and sound map load, with `--no-contact-audio` / `--mute-contact-audio` as deterministic validation/headless opt-outs.
- [x] Replay gets no dedicated replay-audio policy in this slice; live post-step contacts can play presentation audio without changing replay artifacts.
- [x] Existing asset material modes/kinds infer contact material. Bare generated/demo objects and legacy scene objects default to `default` unless authored otherwise.
- [x] Enable/master-gain/max-distance/debug-counter controls are config-backed through `engine.cfg` instead of a new in-game panel for this slice.
- [x] Rolling contacts are suppressed by treating an uninterrupted body pair as one continuing support contact unless it separates briefly or receives a strong impulse spike.
- [x] Sound-emitter flash uses submitted contacts only, so rolling, threshold, cooldown, distance, and voice-cap rejects do not flash.
- [x] White audio feedback uses a shader-side final-color blend through `material3.w`; it does not depend on base-color tint or material mode.

## Progress Ledger

- [x] 2026-07-02: Investigated contact source, runtime stepping, camera listener access, asset material shape, and audio absence.
- [x] 2026-07-02: Created this plan/progress tracker.
- [x] 2026-07-02: Created feature branch `contact-impact-audio`.
- [x] 2026-07-02: Added sound asset sourcing policy and candidate libraries.
- [x] 2026-07-02: Sourced starter CC0 OpenGameArt sounds and added `SkullbonezData/audio/LICENSES.md`.
- [x] 2026-07-02: Added XAudio2/stb_vorbis contact audio service, material map, cooldown/threshold/distance attenuation, and source voice pooling.
- [x] 2026-07-02: Added scene/asset `contactMaterial` parsing, asset material-mode inference, GameModel storage, collider-copy field, and snapshot preservation.
- [x] 2026-07-02: Wired post-physics-step contact audio submission from solved debug contacts.
- [x] 2026-07-02: Added `--no-contact-audio` / `--mute-contact-audio`.
- [x] 2026-07-02: Passed latest targeted Profile build and scene-load smoke.
- [x] 2026-07-02: Added project-filter validator coverage for new audio/runtime/stb project items.
- [x] 2026-07-02: Ran scoped formatting fixes required by validation, including pre-existing format drift in replay/UI files reported by the gate.
- [x] 2026-07-02: Passed `tools\validate_fast.bat`.
- [x] 2026-07-02: Passed `tools\validate_full.bat`.
- [x] 2026-07-02: Added `--contact-audio-smoke`, with a file-backed smoke report at `TestOutput/contact_audio_smoke.json`.
- [x] 2026-07-02: Added light/medium/heavy impulse bands to all material sound sets.
- [x] 2026-07-02: Added dedicated OpenGameArt CC0 earth/gravel/mud sounds and updated the license ledger.
- [x] 2026-07-02: Added `engine.cfg` contact audio controls: enabled, master gain, max-distance scale, and debug counters.
- [x] 2026-07-02: Passed targeted Profile build and direct contact-audio smoke after the completion pass.
- [x] 2026-07-02: Passed continuation `tools\validate_full.bat` and final direct smoke.
- [x] 2026-07-02: Replaced the active default with a generic thwack sample and added 38 CC0 thud candidates from OpenGameArt packs.
- [x] 2026-07-02: Added rolling-contact suppression so steady ball-ground contact does not replay every cooldown window.
- [x] 2026-07-02: Added Sound-tab sample browsing, preview, and runtime sample assignment.
- [x] 2026-07-02: Passed follow-up format, Profile build, contact-audio smoke, project-filter check, and `tools\validate_full.bat`.
- [x] 2026-07-02: Set `impact_thud_thwack_02.ogg` as the default selected impact sample and the default active material-map sample.
- [x] 2026-07-02: Added `Flash emitters` Sound-tab toggle, submitted-contact tracking, 100ms white object flash, and shader-side final-color blend.
- [x] 2026-07-02: Fixed Sound-tab selector label overruns with measured ellipsis clipping before the right-side selector buttons.
- [x] 2026-07-02: Updated UI scene tab parsing for the inserted Sound tab and adjusted UI screenshot validation to ignore runner status HUD outside the window under test.
- [x] 2026-07-02: Passed `tools\validate_ui.bat`, `tools\validate_fast.bat`, and `tools\validate_full.bat` for the Sound-tab flash/text follow-up.
- [x] Phase 0 complete.
- [x] Phase 1 complete.
- [x] Phase 2 complete.
- [x] Phase 3 complete.
- [x] Phase 4 complete.
- [x] Phase 5 complete.
- [x] Phase 6 validation complete.

## Handoff Notes

- Start with a narrow slice: schema defaults plus a disabled audio service skeleton are lower risk than touching solver contact rows first.
- For the first audible slice, use one default sample and one material pair. Prove threshold/cooldown behavior before adding a large material library.
- If the implementation touches source-bearing files, run the comment-style audit skill on every touched source file before reporting done.
- The validation formatter reported pre-existing replay/UI format drift. This branch includes scoped formatter-only cleanup for those reported files so the gate can pass.
