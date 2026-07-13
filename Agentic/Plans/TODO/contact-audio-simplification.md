# Contact Audio Simplification — One Honest Thud

Date: 2026-07-13
Status: Live — 5/6 tasks complete (T6 blocked on a Windows machine for the
`validate_full` PR gate; all Linux-runnable gates have passed)
Impact area: contact audio service, Sound tab UI, runtime tuning commands,
engine config registry, runtime diagnostics, config unit tests
Owner: presentation audio (ContactAudioService)

> Note: this plan is intentionally NOT registered in
> `Agentic/Plans/MASTER-PLAN.md` (owner instruction 2026-07-13: another agent
> owns the ledger right now). Commits for this plan use ordinary commit
> subjects and do not claim MASTER progress.

## Problem And Evidence

The owner's requirement is one sentence: *when two bodies collide with enough
energy, play a thud whose volume scales with the collision energy and the
listener distance. Rolling and resting contact stays silent.*

What exists instead is a ~2,100-line decision engine
(`Runtime/Audio/ContactAudioService.cpp`) with two competing input paths and
roughly 30 live-tunable parameters:

1. **"Simple linear mode" is on by default and is the false-positive machine.**
   `RunFrame.cpp` (`ExecuteContactAudioPostStep`) skips solver contacts
   entirely when it is enabled and emits a sound whenever any body's velocity
   changes by more than ~2 m/s in a step. That measures *acceleration*, not
   *collision*: a ball rolling over bumpy terrain, pile jostle, and solver
   corrections all trigger it. It also fabricates `materialB = default` and
   `bodyB = -1`, so material pairing is fiction in the default path. Not
   fixable by tuning; the input signal is wrong.
2. **The contact path deliberately plays rolling sounds.** The "rolling lane"
   (`PlayRollingCandidate`) submits roll/slide sounds for slipping contacts,
   with its own dB level, distance, burst budget, and re-arm timer. The owner
   does not want rolling audio (and has no rolling sample).
3. **Compensating machinery hides the two real knobs.** Six perceptual
   "kinds", ~15 rejection reasons, a 4,096-entry cooldown table with O(n)
   linear scan per contact inside the fixed step, spike-ratio override
   cooldowns, terrain-specific re-arm gaps, 100 ms burst windows, per-body
   burst budgets, and three different "how big was this" definitions
   (`ContactStrength` vs `ContactCandidateRank` vs `ContactImpactScore`).
4. The physics side already exports the honest signal: per-row solved
   `normalImpulse` plus `preSolveClosingSpeed` / `preSolveSlipSpeed`
   (`Physics/PhysicsDebugData.h`, captured pre-solve in
   `Physics/PersistentContactSolver.cpp`). A rolling or resting contact has
   near-zero closing speed on every later step, so **no physics change is
   needed** — the gate below rejects rolling for free, and physics
   determinism is untouched.

## Goal

One emit rule, two user-facing knobs:

- **Emit rule:** a contact becomes a thud only when
  `preSolveClosingSpeed >= MIN_CLOSING_SPEED` (fixed constant — rolling and
  resting rows fail this) **and**
  `impactEnergy = 0.5 * normalImpulse * preSolveClosingSpeed >= minImpactEnergy`
  (the user threshold) **and** the body pair is not inside a short fixed
  cooldown window (~150 ms, pair-keyed, no featureId) **and** the contact is
  within the sound set's authored `maxDistance` of the listener.
- **Volume:** `masterGain * setBaseGain * distanceFalloff * energyGain`,
  where `distanceFalloff = (1 - d/maxDistance)^2` and `energyGain` maps
  impact energy above the threshold through a fixed range with a
  square-root perceptual curve. Light air-taps come out soft; ground slams
  come out loud, automatically.
- **Sound tab:** enable toggle, Volume slider, Thud-threshold slider, and the
  existing sample library picker (prev/next/play/use) so the owner can keep
  auditioning thud samples. One read-only stats line. Nothing else.
- **Material sets stay.** `contact_audio.materials.json` keeps loading as-is
  (sets, samples, maxDistance, baseGain, pitch range are honored; `bands`
  and per-set impulse/cooldown tuning fields are ignored by the loader).
  The authored JSON file is not modified.

## Non-Goals

- No rolling audio. A single commented hook point in the new decision core
  marks where a rolling lane would classify slip contacts if ever wanted.
- No per-set / per-band live tuning UI, no flash-mode selector, no debug
  counter printing, no burst windows, no body budgets, no spike overrides.
- No physics source changes: `PhysicsDebugData.h`, `PersistentContactSolver`
  and all solver state are untouched, so the physics baseline cannot move.
- No new inheritance, no allocation-policy exceptions: bounded scratch
  vectors reserved at startup, same as today.

## Tasks

- [x] T1 — Rewrite `ContactAudioService` decision core: delete simple linear
  mode, rolling lane, classification kinds, spike/override cooldowns, burst
  windows, and body budgets; implement the single emit rule + energy gain
  above; trim `ContactAudioEvent`, `ContactAudioDecision`,
  `ContactAudioStats`, and the public API (new `SetMinImpactEnergy` /
  `MinImpactEnergy`); keep XAudio2 backend, voice pooling, ogg decode,
  material-set resolve, sample library, preview/assign, `PlaySmokeImpact`.
  Verbose teaching comments for a reader who does not know audio.
- [x] T2 — Simplify the feed + flash in `RunFrame.cpp`
  (`ExecuteContactAudioPostStep`): always feed solver debug contacts, delete
  the simple-mode branch, flash emitted contacts only (no mode selector),
  delete the debug-counter print and `RunTimerState::contactAudioStatsLogTime`.
- [x] T3 — Shrink the UI surface: `UICommands.h` (`UISoundParam` →
  MasterGain/MinImpactEnergy; drop band params, toggles for simple/flash/debug
  counters), `UITabSound.h/.cpp` minimal tab (toggle, 2 sliders, sample
  picker, set picker for sample assignment, stats line),
  `RuntimeViewModel.h/.cpp` + `UI.h` + `RunUiTextPass.cpp` +
  `UIFrameComposition.cpp` snapshot/frame-data trim,
  `RuntimeTuning.cpp` command application trim.
- [x] T4 — Config schema v2: `ContactAudioConfig` becomes
  `{ enabled, masterGain, minImpactEnergy }`; registry rows updated and
  `kExpectedConfigSettingCount` re-counted; `ENGINE_CONFIG_FORMAT_VERSION`
  bumped 1 → 2 with the deterministic v1→v2 migration (drop removed
  `contact_audio_*` keys) added to `tools/migrate_data_formats.py`
  (`CONFIG_VERSION` = 2 + self-tests); committed `SkullbonezData/engine.cfg`
  upgraded; `SceneRuntimeDefaults` writer stamps current automatically;
  `SkullbonezTests/TestConfig.cpp` legacy/current/future/writer cases updated.
- [x] T5 — Update `Run.cpp` config application, `Init.cpp`
  `--contact-audio-smoke` path, and `RuntimeDiagnostics.h/.cpp`
  decision/step-stat logging to the trimmed structs; reconcile
  `tools/allocation_policy_allowlist.json` rows for the audio service if the
  scan output changes.
- [ ] T6 (partial: Linux gates passed 2026-07-13; Windows `validate_full` still owed before merge) — Validation + handoff: run the Linux-runnable gates here
  (`python tools/check_allocation_policy.py --self-test` and `--repo .`,
  `python tools/migrate_data_formats.py --check`,
  `python tools/validate_project_filters.py` if file lists change); record
  that the Windows PR gate (`tools\validate_full.bat`, because Run*/Config/
  tests all move) is still owed before merge, since this container cannot
  build MSVC/XAudio2.

## Validation Map

| Change | Gate |
|---|---|
| `Run*`, `Config*`, `Common`-adjacent breadth | `tools\validate_full.bat` on Windows before merge (owed) |
| `SkullbonezTests/TestConfig.cpp` | covered by the umbrella (`validate_tests` lane) |
| Allocation policy | `python tools/check_allocation_policy.py --self-test` + `--repo .` (runnable in this container) |
| Authored data migration | `python tools/migrate_data_formats.py --check` (runnable in this container) |
| Physics | untouched — no physics gate required by this plan |

## Design Notes For The Implementer

- **Impact energy** `0.5 * J * v_close` (impulse × closing speed) has joule
  units and approximates the kinetic energy the contact absorbed. Default
  threshold 125 J preserves today's effective default sensitivity
  (`minImpactScore` 250 × the removed 0.5 factor).
- **Cooldown key is the body pair only.** The old key mixed in `featureId`,
  which changes continuously while a body rolls or a manifold rotates, so
  "ongoing contact" detection kept re-arming. Pair-only keys make the 150 ms
  window actually mean "this pair just thudded".
- **Sort candidates by energy, loudest first,** before submitting, so voice
  slots go to the biggest hits when a wall of boxes lands in one step.
- The decision/stat records stay (trimmed) because SkullScope `_DEBUG`
  logging (`RuntimeDiagnostics::LogContactAudioDecision`) is how "why didn't
  I hear that" gets answered without raw log ingestion.
