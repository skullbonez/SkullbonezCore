# Contact Audio Perceptual Model Plan

Date: 2026-07-03
Status: Complete; moved to Done on 2026-07-04
Impact areas: runtime audio, physics contact diagnostics, SkullScope, UI, scene/audio data, tests
Validation: see Progress Ledger for the final `tools\validate_full.bat` gate

## Goal

Replace the current "solved contact impulse becomes a possible sound" model with
a perceptual contact-audio model:

- Emit sounds from real audible contact events, not from every solver impulse or
  propagated wall force.
- Keep rolling, resting, and minor settling mostly silent.
- Let high-energy brick landings produce many thuds during a collapse without
  turning into a rapid counter-like stream.
- Make ball-to-wall hits audible at the local contact patch without lighting or
  sounding the whole wall.
- Keep all tuning live in the Sound tab where practical.
- Make the system explain itself through SkullScope so we can answer "why did
  this sound play?" and "why was this candidate rejected?"
- Preserve physics determinism: audio is presentation-only and never feeds back
  into solver state, replay hashes, or baselines.

The practical target is not physically exact modal sound synthesis. The target is
a robust game-audio event reducer that uses physics facts, material pairs, and
perceptual budgeting to choose the small set of sounds a player expects.

## References And Lessons

Primary references:

- GDC Vault, "Tunes of the Kingdom: Evolving Physics and Sounds for The Legend
  of Zelda: Tears of the Kingdom"
  https://gdcvault.com/play/1034667/Tunes-of-the-Kingdom-Evolving
- Game Developer interview, "Did you know Tears of the Kingdom has a physics
  engine for sound?"
  https://www.gamedeveloper.com/audio/tears-of-the-kingdom-s-sound-designers-unintentionally-made-a-physics-engine-for-sound-
- Audiokinetic, "Impacter and Unreal | Controlling the Impacter Plug-in Using
  Game Physics"
  https://www.audiokinetic.com/en/community/blog/impacter-and-unreal-controlling-the-impacter-plug-in-using-game-physics
- Audiokinetic documentation, "Impacter"
  https://www.audiokinetic.com/en/public-library/2025.1.8_9170/?id=impacter_plug_in_source&source=Help
- GearBlocks, "Collision exposition"
  https://www.gearblocksgame.com/2018/07/03/collision-exposition/
- van den Doel, Kry, and Pai, "FoleyAutomatic: Physically-based Sound Effects
  for Interactive Simulation and Animation"
  https://www.cs.mcgill.ca/~kry/pubs/foleyautomatic/foleyautomatic.pdf
- Zheng and James, "Toward High-Quality Modal Contact Sound"
  https://www.cs.cornell.edu/projects/Sound/mc/

Best-practice translation for SkullbonezCore:

- Physics should provide contact facts; audio should classify and curate them.
- Solver impulses are not sound events by themselves. They can include support,
  constraint stabilization, propagation through stacks, and persistent contacts.
- A useful collision sound is normally an attack/transient plus optional body or
  surface resonance. For us that maps to "brick thud attack" plus "large object
  or surface body" layers rather than one raw sample per contact row.
- Contact callbacks or contact rows need discard/merge/pool stages before they
  touch the audio backend.
- Material and surface rules scale better than hand-authored combinations for
  every object.
- Debug visibility is part of the system. Without candidate/rejection traces, we
  tune by ear and guess wrong.

## Current Source Facts

The completed first contact-audio plan is:

- `Agentic/Plans/Done/contact-impact-audio-plan.md`

Current implementation pieces include:

- `SkullbonezSource/Runtime/Audio/ContactAudioService.h/.cpp`
- `SkullbonezData/audio/contact_audio.materials.json`
- `SkullbonezData/audio/impacts/*.ogg`
- `SkullbonezSource/UI/UITabSound.h/.cpp`
- Runtime contact-audio diagnostics in `SkullbonezSource/Runtime/RuntimeDiagnostics.*`
- SkullScope core trace support in `SkullbonezSource/Core/SkullScope.*`
- Physics contact sources around `SkullbonezSource/Physics/PersistentContactSolver.*`
  and `SkullbonezSource/Physics/PhysicsWorld.*`

Known current behaviors from user testing:

- The 200-box ragdoll wall scene can still overrepresent sound and flash events.
- Ball-to-wall impact can cause force propagation through the wall that looks
  like many bricks emitted sound, even when the audible contact should be local.
- Rolling and settling must stay suppressed unless a later slice adds dedicated
  rolling/sliding loops.
- The default impact direction is a short generic thud/thwack, not foliage,
  debris wash, or brittle scatter.
- The Sound tab already exposes threshold, cooldown, distance scale, sample
  selection, flash emitters, and burst voices per 100 ms.

## Core Model

Use a four-stage pipeline:

```text
physics contact facts
    -> contact-audio classifier
    -> event reducer and perceptual budget
    -> material-aware playback commands
```

Do not let the audio service consume unclassified solver rows directly.

### Contact Fact

Collect immutable facts after solver work is complete, but before audio
classification. The exact type name can change, but the data should cover:

```cpp
struct ContactAudioFact
{
    uint64_t stepIndex;
    int bodyA;
    int bodyB;
    uint32_t featureId;
    uint32_t materialA;
    uint32_t materialB;
    Vector3 point;
    Vector3 normal;
    float normalImpulse;
    float tangentImpulse;
    float relativeNormalSpeed;
    float relativeTangentSpeed;
    float contactAgeSeconds;
    bool isNewContact;
    bool isTerrain;
    bool bodyAWokeThisStep;
    bool bodyBWokeThisStep;
    bool bodyAWasMoving;
    bool bodyBWasMoving;
};
```

Important distinction:

- `normalImpulse` says the solver applied a constraint force.
- `relativeNormalSpeed` at the contact point says whether two surfaces actually
  struck each other.

The latter should become a first-class sound input. It is the main guard against
"the whole wall made noise because force propagated through it."

### Contact Classifier

Classify each fact before sample selection:

| Kind | Meaning | First-pass action |
| --- | --- | --- |
| `impact` | New or rearmed contact with meaningful closing speed and impulse | Candidate thud |
| `heavy_landing` | Dynamic body hits terrain/support with high score | Candidate thud, likely stronger band |
| `support` | Persistent contact holding weight or stacked support | Reject |
| `roll` | Tangential motion plus angular motion, low normal attack | Reject for now |
| `slide` | Sustained tangential motion, low normal attack | Reject for now |
| `settle` | Low-energy continuation after landing | Reject |
| `propagated_impulse` | Solver impulse without local contact attack | Reject |

Default rejection policy:

- Reject if `relativeNormalSpeed` is below the material-pair closing-speed
  threshold unless this is an explicitly rearmed high-impulse landing.
- Reject if the contact age is above the impact window and there was no
  separation/rearm gap.
- Reject if both bodies were effectively stationary before the solver step.
- Reject if the contact belongs to a stable support island and the energy delta
  is below the settle threshold.
- Reject roll/slide candidates until dedicated looped sound support exists.

### Event Reducer

Reduce classified candidates before playback:

- Merge by contact patch:
  `body pair + material pair + feature/contact patch + 100 ms window`.
- For many contacts between the same two objects, keep the highest score and
  optionally accumulate energy into that one candidate.
- For a large collapse, keep many independent body-ground and body-body impacts,
  but cap them by score and distance before they reach the audio backend.
- Use one deterministic sort key before random sample choice:
  score descending, distance ascending, stable body/contact ids.
- Apply the existing burst cap after merge/reduce, not before classification.

Reducer outputs should be small:

```cpp
struct ContactSoundCandidate
{
    uint64_t stepIndex;
    uint32_t eventId;
    ContactSoundKind kind;
    uint32_t materialA;
    uint32_t materialB;
    Vector3 point;
    float score;
    float gain;
    float pitch;
    uint32_t selectedSet;
    uint32_t selectedBand;
};
```

### Scoring

Use a simple score first:

```text
attack = max(relativeNormalSpeed - minClosingSpeed, 0)
impulse = max(normalImpulse - minImpulse, 0)
distance = listener attenuation after max-distance scale
body = optional effective-mass/body-size weight

score = attackWeight * attack + impulseWeight * impulse + bodyWeight * body
audibleScore = score * distance
```

Candidate tuning should be material-aware. A rubber ball, wood block, brick, and
metal chunk should not share one global threshold forever.

### Sound Layers

Keep the first playable version sample-based, but organize it like a layered
system:

- `attack`: short thud, knock, slap, clack, or crack at the impact moment.
- `body`: optional low resonance by object size/material for heavy impacts.
- `surface`: optional dirt, stone, wood, metal, or water contact color.

For now, emit only one attack sample unless a candidate is strong enough for a
second low-frequency body layer. This keeps the system practical while leaving a
path toward richer material sound.

Do not copy Rocket League sounds. Use the target as a reference for shape:
short, punchy, low-mid thud with a clean attack, not brittle debris and not a
leafy/foliage tail.

## SkullScope Diagnostics

Yes: sound information should be queryable in SkullScope.

Add compact contact-audio rows for:

- raw fact count per step, emitted as `contact_audio_frame` aggregate rows
- reduced patch-candidate count and merged patch-fact count
- rejected count by reason from per-verdict `contact_audio` rows
- emitted count
- dropped count by voice cap/burst cap/distance/threshold
- top N emitted events with body ids, material pair, score, selected sample, and
  listener distance

Proposed query names:

```bat
tools\physics_query.bat <trace> contact-audio-summary
tools\physics_query.bat <trace> contact-audio-events --frame <n> --limit 40
tools\physics_query.bat <trace> contact-audio-rejections --reason propagated_impulse --limit 40
tools\physics_query.bat <trace> contact-audio-body --body <id> --limit 40
tools\physics_query.bat <trace> contact-audio-timeline --window-ms 100
```

The debugging loop for the wall scene should be:

1. Run the scene with fixed step, audio diagnostics, and a larger distance scale
   so camera placement is not the bottleneck.
2. Query the sound summary.
3. Query the frame window around ball impact.
4. Query the frame window around wall/ground collapse.
5. Compare emitted events to white flashes and audible playback.

Final handoffs that use SkullScope must report the trace command, query commands,
raw artifact sizes, and bounded GPT-read query output size, per `AGENTS.md`.

## Sound Tab Work

Keep Sound-tab controls user-facing and practical:

- Existing: enable, flash emitters, sample library, material set selection,
  thresholds, cooldown, distance scale, burst voices per 100 ms.
- Add classifier controls only when they are stable enough to tune:
  - min local closing speed
  - impact contact-age window
  - rearm separation gap
  - stable-support settle threshold
  - propagated-impulse reject threshold
  - max emitted candidates per body per 100 ms
- Add a diagnostics mode selector for flash:
  - `Emitted only` default
  - `Candidates`
  - `Rejected`
  - `Off`
- Keep `Emitted only` as the gameplay default so the screen does not turn white
  from rejected or propagated events.

## Implementation Phases

### Phase 0 - Baseline Trace And Counts

- [x] Pick the main repro scene: `prediction_ragdoll_wall_200.scene.json` if
  that is the exact 200-box wall case, otherwise record the correct scene name.
- [x] Add or reuse a deterministic launch that lets the wall collapse long
  enough to cover ball impact, ragdoll-ground, wall collapse, and final settling.
- [x] Run with distance scale high enough that nearby camera placement is not
  required for audible events.
- [x] Capture current raw contact-audio counters and total emissions.
- [x] Record expected user target bands:
  - ragdoll hit: about 1 event
  - ragdoll-ground: a few events
  - ball-to-wall: local contact patch, roughly 1 to 8 events
  - wall collapse/landings: many real brick impacts, roughly 300 to 400 total
    emissions across the collapse window, not hundreds per second forever
  - final settling: near silent

Deliverable:

- [x] A short note in this plan or a report under `Agentic/Reports/` with current
  emitted counts and obvious false positives.

Validation:

- [x] No formal gate for trace-only investigation.
- [x] If SkullScope query tooling changes, use the matching physics-query gate.

### Phase 1 - Contact Fact Upgrade

- [x] Add local contact-point relative velocity facts to the audio candidate
  path.
- [x] Preserve body/material ids and terrain flags.
- [x] Track contact age/rearm state using stable pair/feature keys.
- [x] Keep allocations bounded and out of solver hot loops.
- [x] Confirm physics CSV baselines do not change from presentation-only facts.

Validation:

- [x] `tools\validate_physics.bat` if physics/contact code changes.
- [x] `tools\validate_full.bat` if runtime and UI are touched in the same slice.

### Phase 2 - Classifier And Rejection Reasons

- [x] Implement `impact`, `heavy_landing`, `support`, `settle`,
  `roll_slide`, and `propagated_impulse` classification. Separate roll and
  slide playback remains deferred until looped contact sound support exists.
- [x] Require local closing speed for ordinary impacts.
- [x] Suppress propagated impulses that do not have local attack motion.
- [x] Keep rolling and sliding rejected until dedicated loop/slide sounds exist.
- [x] Expose rejection counters in diagnostics.

Acceptance:

- [x] Ball hitting the middle of a wall does not emit from every brick.
- [x] A stationary brick receiving solver force through the wall does not flash
  white unless it has a local audible contact.
- [x] Rolling balls do not replay thuds on cooldown.

Validation:

- [x] `tools\validate_physics.bat`.
- [x] `Debug\SKULLBONEZ_CORE.exe --contact-audio-smoke`.

### Phase 3 - Reducer And Perceptual Budget

- [x] Merge candidates by contact patch and material pair in a 100 ms window.
- [x] Rank by audible score after distance attenuation.
- [x] Apply burst voices after classification and merge.
- [x] Add per-body and global caps so one object cannot monopolize the frame
  unless it is truly the loudest event. Current acceptance evidence does not
  justify a separate cluster cap.
- [x] Keep the existing Sound-tab burst slider as the final global limiter.

Acceptance:

- [x] The wall collapse has many thuds during real landings, but no long tail of
  minor settle chatter.
- [x] The 200-box scene lands in the target order of magnitude, not thousands of
  emissions from one collapse.

Validation:

- [x] `tools\validate_full.bat` because this touches runtime audio behavior.

### Phase 4 - SkullScope Contact-Audio Queries

- [x] Emit compact SkullScope rows for facts, candidate verdicts, reducer
  decisions, and final submissions.
- [x] Extend `tools\physics_query.py` with bounded contact-audio queries.
- [x] Add regression coverage for at least one query using a small deterministic
  trace.
- [x] Document query commands in `Agentic/Reference/physics-query-reference.md`
  if the query surface becomes permanent.

Acceptance:

- [x] We can answer "how many contact sounds emitted in this scene?" from a
  query without reading raw NDJSON into the model.
- [x] We can list top emitters and rejection reasons for the wall impact frame.

Validation:

- [x] `tools\validate_physics_deep.bat` if SkullScope/query baselines change.
- [x] `tools\validate_fast.bat` for Python/tool changes if no deep baseline
  changes are required.

### Phase 5 - Material Layers And Better Samples

- [x] Split material sets into attack/body/surface intent where the map needs
  more richness.
- [x] Keep the default selected thud as the short generic impact unless replaced
  by a better licensed thud.
- [x] Do not add a production-quality dull ball-kick-style thud source until a
  clearly redistributable source exists.
- [x] Keep `SkullbonezData/audio/LICENSES.md` current for any new files. This
  slice adds no new sample files.

Acceptance:

- [x] Stone, metal, wood, and soft material contacts choose different dry attack
  recipes in the material lab.
- [x] Default scene audio stays thuddy and dry, without foliage/debris tails.

Validation:

- [x] `tools\validate_full.bat` for audio data plus runtime playback changes.

### Phase 6 - Sound Tab Tuning Polish

- [x] Add stable classifier/reducer sliders only after the defaults are useful.
- [x] Add flash diagnostics mode if it helps tune emitted/candidate/rejected
  differences.
- [x] Keep labels bounded; no selector text overrun.
- [x] Add UI screenshot validation if the Sound tab layout changes materially.

Validation:

- [x] `tools\validate_ui.bat` for material Sound-tab layout changes.
- [x] `tools\validate_full.bat` if runtime behavior also changes.

## Acceptance Scenes

| Scene | Expected sound behavior |
| --- | --- |
| 200-box ragdoll wall | Local ragdoll hit, local ball-wall contact, many brick landings, quiet settling |
| Rolling ball on terrain | No repeated thud spam |
| Single box drop | One primary thud, optional small bounce thud, then silence |
| Brick pile collapse | Many high-score impacts, merged/reduced enough to avoid chatter |
| Material lab | Different material pairs choose different sets and bands |
| Bullet or fast ball into wall | Local contact patch only, no wall-wide propagated audio |

## Likely Files

| Area | Files |
| --- | --- |
| Audio classifier/reducer | `SkullbonezSource/Runtime/Audio/ContactAudioService.h/.cpp` |
| Contact facts | `SkullbonezSource/Physics/PersistentContactSolver.*`, `SkullbonezSource/Physics/PhysicsWorld.*` |
| Runtime diagnostics | `SkullbonezSource/Runtime/RuntimeDiagnostics.*` |
| SkullScope trace | `SkullbonezSource/Core/SkullScope.*` |
| Query tooling | `tools/physics_query.py`, `tools/physics_query.bat`, `tools/check_physics_query_regression.py` |
| Sound tab | `SkullbonezSource/UI/UITabSound.h/.cpp`, `SkullbonezSource/UI/UI.*`, `SkullbonezSource/UI/UICommands.h` |
| Runtime UI bridge | `SkullbonezSource/Runtime/RunInput.cpp`, `SkullbonezSource/Runtime/RunUiTextPass.cpp`, `SkullbonezSource/Runtime/RuntimeViewModel.h` |
| Audio data | `SkullbonezData/audio/contact_audio.materials.json`, `SkullbonezData/audio/impacts/*`, `SkullbonezData/audio/LICENSES.md` |
| Reference docs | `Agentic/Reference/physics-query-reference.md`, this plan |

## Risks And Decisions

- Decision: Treat local contact-point relative velocity as required evidence for
  ordinary impact sound. Solver impulse alone is not enough.
- Decision: Keep roll/slide silent until a separate looped sound model exists.
  Fake thud loops are worse than silence.
- Decision: Use the Sound-tab burst slider as a final output cap, not as the
  primary anti-spam mechanism.
- Decision: Add SkullScope audio diagnostics before deep tuning so the wall scene
  becomes measurable.
- Risk: Adding too many sliders too early can make the Sound tab confusing. Start
  with debug-only counters and promote only the useful controls.
- Risk: Physics diagnostics changes can affect deep baselines. Update SkullScope
  baselines only from final Debug artifacts and rerun the matching gate.
- Risk: Sample choice can mask model bugs. Tune with a dry generic thud first,
  then layer richer material sounds after the event counts are right.

## Progress Ledger

- [x] 2026-07-03: Created this plan from the current contact-audio failure cases,
  the completed first contact-audio plan, and external game-audio references.
- [x] 2026-07-03: Implemented the first perceptual reducer slice on
  `codex/contact-audio-perceptual-model`: contact history now updates for every
  observed body pair during `SubmitContact`, including burst-skipped frames, and
  playback rejects ongoing object/object contacts as `ongoing_object_contact`
  instead of letting propagated impulse spikes sound or flash the whole wall.
  Existing pre-solve closing/slip speed remains the local impact evidence for
  new contacts. Focused checks: Profile and Debug builds passed with 0 warnings,
  `--contact-audio-smoke` submitted one sound, and `physics_roll.scene.json` ran
  600 Debug/SkullScope frames with exactly one submitted contact-audio event.
  The specific `prediction_ragdoll_wall_200.scene.json` probe did not complete
  on this machine: both a 1200-frame diagnostic run and scene-load-only run were
  stopped by PID after producing no trace/startup output.
- [x] 2026-07-03: Added the second contact-audio classifier/reducer slice:
  contact candidates now collapse by body pair, feature id, and material pair;
  the ranked burst selector breaks ties by listener distance and stable key; a
  bounded per-body burst budget rejects monopolizing candidates as `body_budget`;
  the global burst cap records skipped candidates as `burst_budget`; and
  low-attack/slip or support-transfer rows now report `roll_or_slide`, `settle`,
  `propagated_impulse`, or `below_min_impact_score` instead of a single generic
  threshold reason. The Sound tab now cycles a render-only flash mode across
  Off, Emitted, Candidates, and Rejected. Validation: `git diff --check`;
  `python tools\check_runtime_boundaries.py --repo .`; `tools\validate_build.bat Profile`
  with log `TestOutput\agent_build_profile_contact_audio_classifier_flash_mode.log`
  passed with 0 warnings and 0 errors; `Profile\SKULLBONEZ_CORE.exe --contact-audio-smoke`
  with log `TestOutput\agent_contact_audio_smoke_classifier_flash_mode.log`
  submitted one voice from one event; and `tools\validate_full.bat` with log
  `TestOutput\agent_validate_full_contact_audio_classifier_flash_mode.log` passed
  with DX12 InfoQueue 0 errors, screenshots matching committed baselines, and
  `physics_regression_solver.csv` matching byte-exactly.
- [x] 2026-07-03: Added the Phase 4 contact-audio SkullScope query surface over
  existing `type:"contact_audio"` verdict events: `contact-audio-summary`,
  `contact-audio-events`, `contact-audio-rejections`, `contact-audio-body`, and
  `contact-audio-timeline`. The existing `audio` command remains a summary
  alias. Regression coverage now includes contact-audio summary/events,
  propagated-impulse rejections, the `roll_a` body query, and timeline buckets
  in `tools\check_physics_query_regression.py`; the SkullScope reference docs
  list the permanent commands. Validation: `python -m py_compile
  tools\physics_query.py tools\check_physics_query_regression.py`;
  `python tools\check_physics_query_regression.py` passed with exact baseline
  match; `tools\validate_fast.bat` passed; `tools\validate_physics_deep.bat`
  passed after refreshing `physics_query_varied.json` and the stale known
  stacking issue signature from final Debug artifacts.
- [x] 2026-07-03: Captured the 200-brick/ragdoll contact-audio baseline with
  SkullScope. Command:
  `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --scene SkullbonezData\scenes\aaa_ragdoll_sunset_showcase.scene.json --fixed-step --frames 1800 --vsync off --shadows off --cinematic off --no-water --physics-diag Debug\contact_audio_ragdoll_wall_200.physicsdiag.ndjson`.
  At capture time, the default cinematic/water render path hit an unrelated DX12
  debug-layer transition break in `EmitDx12RenderGraphTransitionBarrier`, so this
  physics evidence disables cinematic water rendering while keeping the authored
  physics scene and 200-brick asset intact. The run completed frames 0..1799 with
  211 bodies and DX12 InfoQueue 0 errors. Contact-audio query counts:
  205,287 verdict rows, 191 submitted voices, and 205,096 rejected rows. Decision
  counts: `ongoing_object_contact` 67,662, `settle` 51,484,
  `candidate_collapsed` 37,780, `candidate_cap` 30,697, `below_min_impulse`
  11,460, `below_min_impact_score` 2,482, `roll_or_slide` 2,228,
  `propagated_impulse` 1,222, `cooldown_ongoing` 74, `gain_floor` 4, and
  `distance` 3. Hot frames submitted at most 7 voices; the initial striker/ragdoll
  impact at frame 20 submitted 3 heavy hits.
- [x] 2026-07-04: Fixed the unrelated DX12 default cinematic/water abort seen in
  `aaa_ragdoll_sunset_showcase.scene.json`. Graph-owned transition and UAV
  barriers now reopen the DX12 command list when they are the first command after
  `Present()` or a mid-frame drain, so `FramebufferDX12::Bind()` no longer emits
  a `ResourceBarrier` into a closed list. Evidence: before-fix CDB repro stopped
  on `COMMAND_LIST_CLOSED` in `EmitDx12RenderGraphTransitionBarrier`; after-fix
  CDB repro command
  `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --scene SkullbonezData\scenes\aaa_ragdoll_sunset_showcase.scene.json --fixed-step --frames 10 --vsync off --shadows off`
  exited cleanly with the default cinematic/water path; `tools\validate_dx12_renderer.bat`
  passed with DX12 InfoQueue 0 errors and screenshots matching baselines.
- [x] 2026-07-03: Added compact contact-audio reducer aggregate rows and query
  support. New Debug SkullScope traces write one `contact_audio_frame` row per
  physics step with raw facts, reduced patch candidates, merged patch facts,
  queue overflow, burst-window skip, budget rejection, quiet rejection, and
  submitted voice counts. `contact-audio-summary` now includes
  `frameAggregateTotals` and `frameAggregateHotspots` for those rows while the
  existing verdict/event/rejection/body/timeline queries keep their
  per-candidate semantics. New traces rename reducer-internal verdicts from
  `candidate_collapsed`/`candidate_cap` to `patch_merged`/`patch_queue_full`.
  The Sound tab runtime row now presents the reducer as facts, patches, merged,
  played, quiet, and budget counts. Validation: `tools\validate_build.bat Debug`
  passed with 0 warnings and 0 errors; `Debug\SKULLBONEZ_CORE.exe
  --contact-audio-smoke` submitted one voice; `python -m py_compile
  tools\physics_query.py tools\check_physics_query_regression.py` passed;
  `python tools\check_physics_query_regression.py --update` refreshed
  `TestOutput/baselines/physics_query_varied.json` from the final Debug
  executable; `python tools\check_physics_query_regression.py` passed exact
  baseline match; `tools\validate_format.bat`, `tools\validate_fast.bat`, and
  `tools\validate_physics_deep.bat` passed. The refreshed small trace summary
  reports 15,673 facts,
  15,612 reduced patch candidates, 61 merged patch facts, 9,028 burst-window
  skipped patch candidates, and 59 submitted voices.
- [x] 2026-07-04: Added explicit perceptual kind reporting to contact-audio
  verdict diagnostics. Each `contact_audio` row now carries `kind`, with
  summaries and timeline buckets reporting `kindCounts`. The current classes are
  `impact`, `heavy_landing`, `support`, `settle`, `roll_slide`, and
  `propagated_impulse`; `reason` remains the pass/reject explanation. The
  refreshed `physics_query_varied` summary reports 28 heavy landings, 88
  impacts, 679 support rows, 5,375 settle rows, 236 roll/slide rows, and 239
  propagated impulses. Validation: `tools\validate_format.bat` passed;
  `tools\validate_build.bat Debug` passed with 0 warnings and 0 errors;
  `Debug\SKULLBONEZ_CORE.exe --contact-audio-smoke` submitted one voice;
  `python -m py_compile tools\physics_query.py tools\check_physics_query_regression.py`
  passed; `python tools\check_physics_query_regression.py --update` refreshed
  `TestOutput/baselines/physics_query_varied.json`; `python
  tools\check_physics_query_regression.py` passed exact baseline match;
  `tools\validate_fast.bat` and `tools\validate_physics_deep.bat` passed.
- [x] 2026-07-04: Polished the Sound tab wording around the perceptual reducer:
  the runtime counter line is now labeled `Reducer`, global contact-audio
  controls sit under `Classifier`, material tuning sits under `Material Recipe`,
  and stale `sleep` wording in cooldown controls is replaced with pair-cooldown
  and spike-rearm terminology. Validation: `tools\validate_ui.bat` passed.
- [x] 2026-07-04: Added material-aware dry attack recipes and a deterministic
  material lab scene. `SkullbonezData/audio/contact_audio.materials.json` now
  keeps the generic dry thud as `attack.default_dry_thud` and adds stone, metal,
  wood/bark, wood, and soft/foliage recipes using the existing licensed sample
  library. `SkullbonezData/scenes/contact_audio_material_lab.scene.json` drops
  separated stone, metal, wood, and foliage blocks so material routing can be
  proven without a huge collapse trace. Smoke evidence:
  `Debug\SKULLBONEZ_CORE.exe --contact-audio-smoke` loaded 6 sound sets and 38
  samples, then submitted 1 voice from 1 event.
- [x] 2026-07-04: Completed the acceptance scene sweep with bounded SkullScope
  queries. Material lab command:
  `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --scene SkullbonezData\scenes\contact_audio_material_lab.scene.json --physics-diag Debug\contact_audio_material_lab.physicsdiag.ndjson --fixed-step --frames 300 --vsync off --shadows off`.
  The trace produced 110 contact-audio verdicts and 10 submitted voices; the
  submitted rows selected `attack.stone_dry`, `attack.metal_dry`,
  `attack.wood_dry`, and `attack.soft_surface_dry`. Rolling command:
  `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --scene SkullbonezData\scenes\physics_roll.scene.json --physics-diag Debug\contact_audio_roll.physicsdiag.ndjson --fixed-step --frames 300 --vsync off --shadows off`.
  The roll trace produced exactly 1 submitted heavy landing and no later thud
  spam. Showcase command:
  `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --scene SkullbonezData\scenes\aaa_ragdoll_sunset_showcase.scene.json --physics-diag Debug\contact_audio_showcase.physicsdiag.ndjson --fixed-step --frames 1800 --vsync off --shadows off`.
  The full cinematic/water path completed cleanly and produced 205,287
  contact-audio verdict rows, 191 submitted voices, 205,096 rejections, and a
  hottest frame of 7 submitted voices. Frames 1500..1800 produced only 8
  submitted voices, and the final 100 ms window produced 0 submitted voices.
  A complete frame-630 query returned 7 submitted events, and a complete
  frame-50 query showed a propagated impulse rejected with `flashEligible=false`.
  Raw SkullScope artifacts were not read into the model; bounded query output
  read by GPT totaled 140,068 characters / 280,166 captured bytes across 15
  query files.
  Raw artifact sizes:
  material lab trace 1,802,868 bytes, material lab SQLite cache 1,196,032 bytes;
  rolling trace 672,815 bytes, rolling SQLite cache 585,728 bytes; showcase
  trace 886,977,038 bytes, showcase SQLite cache 566,759,424 bytes.
  Query output accounting:
  `tools\physics_query.bat Debug\contact_audio_material_lab.physicsdiag.ndjson contact-audio-summary`
  read 21,164 chars / 42,330 bytes;
  `tools\physics_query.bat Debug\contact_audio_material_lab.physicsdiag.ndjson contact-audio-events --submitted yes --limit 40`
  read 6,088 chars / 12,178 bytes;
  `tools\physics_query.bat Debug\contact_audio_material_lab.physicsdiag.ndjson contact-audio-timeline --window-ms 100`
  read 3,102 chars / 6,206 bytes;
  `tools\physics_query.bat Debug\contact_audio_roll.physicsdiag.ndjson contact-audio-summary`
  read 1,897 chars / 3,796 bytes;
  `tools\physics_query.bat Debug\contact_audio_roll.physicsdiag.ndjson contact-audio-events --submitted yes --limit 20`
  read 933 chars / 1,868 bytes;
  `tools\physics_query.bat Debug\contact_audio_roll.physicsdiag.ndjson contact-audio-rejections --reason roll_or_slide --limit 20`
  read 384 chars / 770 bytes;
  `tools\physics_query.bat Debug\contact_audio_showcase.physicsdiag.ndjson contact-audio-summary`
  read 27,260 chars / 54,522 bytes;
  `tools\physics_query.bat Debug\contact_audio_showcase.physicsdiag.ndjson contact-audio-events --submitted yes --limit 40`
  read 23,310 chars / 46,622 bytes and reported `truncated=true`;
  `tools\physics_query.bat Debug\contact_audio_showcase.physicsdiag.ndjson contact-audio-timeline --window-ms 100`
  read 9,334 chars / 18,670 bytes and reported `truncated=true`;
  `tools\physics_query.bat Debug\contact_audio_showcase.physicsdiag.ndjson contact-audio-rejections --reason propagated_impulse --limit 20`
  read 10,953 chars / 21,908 bytes and reported `truncated=true`;
  `tools\physics_query.bat Debug\contact_audio_showcase.physicsdiag.ndjson contact-audio-events --frame 630 --submitted yes --limit 10`
  read 4,358 chars / 8,718 bytes;
  `tools\physics_query.bat Debug\contact_audio_showcase.physicsdiag.ndjson contact-audio-rejections --frame 50 --reason propagated_impulse --limit 10`
  read 920 chars / 1,842 bytes;
  `tools\physics_query.bat Debug\contact_audio_showcase.physicsdiag.ndjson contact-audio-timeline --window-ms 100 --frames 1500:1800`
  read 6,782 chars / 13,566 bytes and reported `truncated=true`;
  `tools\physics_query.bat Debug\contact_audio_showcase.physicsdiag.ndjson contact-audio-summary --frames 1500:1800`
  read 22,913 chars / 45,828 bytes;
  `tools\physics_query.bat Debug\contact_audio_showcase.physicsdiag.ndjson contact-audio-timeline --window-ms 100 --frames 1788:1799`
  read 670 chars / 1,342 bytes. Broad truncated queries were used only for
  orientation; complete narrow queries and summaries carry the acceptance claims.
- [x] 2026-07-04: Final gate for the material/audio-data and acceptance-scene
  slice passed: `tools\validate_full.bat` with log
  `TestOutput\agent_validate_full_contact_audio_material_acceptance.log`.
  Metadata and runtime-boundary checks passed, Profile and Debug builds passed
  with 0 warnings and 0 errors, DX12 validation reported 0 InfoQueue errors with
  screenshots matching committed baselines, and `physics_regression_solver.csv`
  matched the committed baseline byte-exactly.
- [x] 2026-07-04: The final performance gate passed after the end-of-slice
  rubber-duck review flagged cache/perf evidence as incomplete.
  `tools\validate_perf.bat` with log
  `TestOutput\agent_validate_perf_contact_audio_material_acceptance.log` rebuilt
  Profile and Debug with 0 warnings and 0 errors, ran DX12 and
  `physics_bench` perf scenes, passed absolute budgets, and reported no
  regressions. DX12 frame timing: avg 1.1527 ms, p50 1.0793 ms, p99 1.6427 ms,
  p99.9 1.9809 ms. Physics bench frame timing: avg 0.6948 ms, p50 0.6283 ms,
  p99 1.0732 ms, p99.9 1.3987 ms.
- [x] Phase 0 baseline trace and counts.
- [x] Phase 1 contact fact upgrade.
- [x] Phase 2 classifier and rejection reasons. Complete for impact-style
  one-shot sounds; a future separate roll-vs-slide split belongs with dedicated
  looped sound support.
- [x] Phase 3 reducer and perceptual budget. The current evidence does not
  justify adding a separate cluster cap beyond the implemented patch merge,
  deterministic ranking, global burst cap, and per-body budget.
- [x] Phase 4 SkullScope contact-audio queries.
- [x] Phase 5 material layers and better samples.
- [x] Phase 6 Sound-tab tuning polish.
