# Contact Audio Perceptual Model Plan

Date: 2026-07-03
Status: Draft implementation plan
Impact areas: runtime audio, physics contact diagnostics, SkullScope, UI, scene/audio data, tests
Validation for this document-only change: none required

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

- raw fact count per step
- classified candidate count per kind
- rejected count by reason
- merged count by merge key
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

- [ ] Pick the main repro scene: `prediction_ragdoll_wall_200.scene.json` if
  that is the exact 200-box wall case, otherwise record the correct scene name.
- [ ] Add or reuse a deterministic launch that lets the wall collapse long
  enough to cover ball impact, ragdoll-ground, wall collapse, and final settling.
- [ ] Run with distance scale high enough that nearby camera placement is not
  required for audible events.
- [ ] Capture current raw contact-audio counters and total emissions.
- [ ] Record expected user target bands:
  - ragdoll hit: about 1 event
  - ragdoll-ground: a few events
  - ball-to-wall: local contact patch, roughly 1 to 8 events
  - wall collapse/landings: many real brick impacts, roughly 300 to 400 total
    emissions across the collapse window, not hundreds per second forever
  - final settling: near silent

Deliverable:

- [ ] A short note in this plan or a report under `Agentic/Reports/` with current
  emitted counts and obvious false positives.

Validation:

- [ ] No formal gate for trace-only investigation.
- [ ] If SkullScope query tooling changes, use the matching physics-query gate.

### Phase 1 - Contact Fact Upgrade

- [ ] Add local contact-point relative velocity facts to the audio candidate
  path.
- [ ] Preserve body/material ids and terrain flags.
- [ ] Track contact age/rearm state using stable pair/feature keys.
- [ ] Keep allocations bounded and out of solver hot loops.
- [ ] Confirm physics CSV baselines do not change from presentation-only facts.

Validation:

- [ ] `tools\validate_physics.bat` if physics/contact code changes.
- [ ] `tools\validate_full.bat` if runtime and UI are touched in the same slice.

### Phase 2 - Classifier And Rejection Reasons

- [ ] Implement `impact`, `heavy_landing`, `support`, `roll`, `slide`, `settle`,
  and `propagated_impulse` classification.
- [ ] Require local closing speed for ordinary impacts.
- [ ] Suppress propagated impulses that do not have local attack motion.
- [ ] Keep rolling and sliding rejected until dedicated loop/slide sounds exist.
- [ ] Expose rejection counters in diagnostics.

Acceptance:

- [ ] Ball hitting the middle of a wall does not emit from every brick.
- [ ] A stationary brick receiving solver force through the wall does not flash
  white unless it has a local audible contact.
- [ ] Rolling balls do not replay thuds on cooldown.

Validation:

- [ ] `tools\validate_physics.bat`.
- [ ] `Profile\SKULLBONEZ_CORE.exe --contact-audio-smoke`.

### Phase 3 - Reducer And Perceptual Budget

- [ ] Merge candidates by contact patch and material pair in a 100 ms window.
- [ ] Rank by audible score after distance attenuation.
- [ ] Apply burst voices after classification and merge.
- [ ] Add per-body and per-cluster caps so one object cannot monopolize the
  frame unless it is truly the loudest event.
- [ ] Keep the existing Sound-tab burst slider as the final global limiter.

Acceptance:

- [ ] The wall collapse has many thuds during real landings, but no long tail of
  minor settle chatter.
- [ ] The 200-box scene lands in the target order of magnitude, not thousands of
  emissions from one collapse.

Validation:

- [ ] `tools\validate_full.bat` because this touches runtime audio behavior.

### Phase 4 - SkullScope Contact-Audio Queries

- [ ] Emit compact SkullScope rows for facts, candidate verdicts, reducer
  decisions, and final submissions.
- [ ] Extend `tools\physics_query.py` with bounded contact-audio queries.
- [ ] Add regression coverage for at least one query using a small deterministic
  trace.
- [ ] Document query commands in `Agentic/Reference/physics-query-reference.md`
  if the query surface becomes permanent.

Acceptance:

- [ ] We can answer "how many contact sounds emitted in this scene?" from a
  query without reading raw NDJSON into the model.
- [ ] We can list top emitters and rejection reasons for the wall impact frame.

Validation:

- [ ] `tools\validate_physics_deep.bat` if SkullScope/query baselines change.
- [ ] `tools\validate_fast.bat` for Python/tool changes if no deep baseline
  changes are required.

### Phase 5 - Material Layers And Better Samples

- [ ] Split material sets into attack/body/surface intent where the map needs
  more richness.
- [ ] Keep the default selected thud as the short generic impact unless replaced
  by a better licensed thud.
- [ ] Add a production-quality dull ball-kick-style thud source only with clear
  redistribution rights.
- [ ] Keep `SkullbonezData/audio/LICENSES.md` current for any new files.

Acceptance:

- [ ] Brick-ground and ball-brick sound different enough to read as different
  events.
- [ ] Default scene audio stays thuddy and dry, without foliage/debris tails.

Validation:

- [ ] `tools\validate_full.bat` for audio data plus runtime playback changes.

### Phase 6 - Sound Tab Tuning Polish

- [ ] Add stable classifier/reducer sliders only after the defaults are useful.
- [ ] Add flash diagnostics mode if it helps tune emitted/candidate/rejected
  differences.
- [ ] Keep labels bounded; no selector text overrun.
- [ ] Add UI screenshot validation if the Sound tab layout changes materially.

Validation:

- [ ] `tools\validate_ui.bat` for material Sound-tab layout changes.
- [ ] `tools\validate_full.bat` if runtime behavior also changes.

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
- [ ] Phase 0 baseline trace and counts.
- [ ] Phase 1 contact fact upgrade.
- [ ] Phase 2 classifier and rejection reasons.
- [ ] Phase 3 reducer and perceptual budget.
- [ ] Phase 4 SkullScope contact-audio queries.
- [ ] Phase 5 material layers and better samples.
- [ ] Phase 6 Sound-tab tuning polish.
