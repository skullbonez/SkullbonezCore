# Look Lab Random Style Authoring

Date: 2026-08-01
Status: ACTIVE — 6/7 phases complete
Impact area: Runtime input and direction, Scene style values/serialization, Capture, UI status, tests, and style data
Owner: Runtime Direction look authoring
Priority: First

## Problem And Evidence

The engine already exposes a large presentation surface but requires the owner
to know which combination to ask for. There are 23 tracked
`SkullbonezData/styles/*.style.json` files, a schema-v1 standalone style format,
live style application, scene cinematic overrides, object-material rules, and
render consumers for sky, terrain, objects, water, lighting, shadows, fog,
volumetrics, god rays, bloom, grading, and related presentation modes. These
capabilities are individually usable, but there is no engine-owned way to
explore their combined design space by repeatedly asking for another coherent
look and then preserving the result.

The current keyboard table binds F5 to the performance histogram and F6 to the
memory overlay. Those bindings remain authoritative. F10 and F11 are currently
unbound and are owner-selected for Look Lab. The camera has a fixed 45-degree
perspective projection rather than an authored lens system; the owner explicitly
excluded lenses and all camera randomization from this plan.

The current style harness can apply `live.style.json`, and scene snapshots can
serialize complete scenes, but neither owns the requested interactive workflow:
a deterministic presentation-only reroll followed by a reusable standalone
style and matching screenshot. Uniformly randomizing every numeric field would
not solve that problem. It would mostly produce incoherent palettes, invisible
geometry, clipped highlights, opaque fog, pathological resource-quality
changes, and combinations that are surprising only because they are broken.

## Goal

Pressing F10 produces a new, broad, coherent, deterministic presentation look
for the active scene without reloading or changing simulation. Pressing F11
automatically writes the exact resolved look to a reusable schema-current style
inside a timestamp/seed directory, writes a human-readable text receipt, and
saves a matching screenshot. The output round-trips in a fresh process, the
active scene file remains untouched, and the saved result does not depend on
future generator behavior or inherited defaults.

## Binding Interaction Contract

| Input | Required behavior |
|---|---|
| F5 | Continue toggling the performance histogram exactly as today. |
| F6 | Continue toggling the memory overlay exactly as today. |
| F10 | On one keyboard-unblocked press edge, choose a new 64-bit authoring seed, resolve one coherent Look Lab candidate, and apply it live. |
| F11 | On one keyboard-unblocked press edge after F10, create one repository-root Look Lab bundle containing the exact standalone style, a text receipt, and one matching screenshot. |

F10 never polls or recompiles shader source. “Shader randomization” means
selecting only renderer-supported style/material branches already represented
by typed engine values. It does not create HLSL, alter shader bytecode, reload
assets, rebuild the scene, or mutate physics.

F11 writes one ignored directory below the repository-root `LookLab/` folder
using this collision-safe, Windows-safe name:

```text
LookLab/YYYY-MM-DD_HH-mm-ss_seed_<16-lowercase-hex-digits>/
  look.style.json
  look.txt
  look.png
```

`look.style.json` is the machine-authoritative reusable engine style; `look.txt`
is a human-readable receipt containing local timestamp and UTC offset, seed,
generator version, recipe, source scene path/display name, output filenames,
the complete flattened resolved cinematic/material key-value listing, and final
style/screenshot status. The text receipt is derived inspection output, not a
second reload parser; JSON remains authoritative. The JSON and receipt are
written through temporary siblings and renamed only after serialization
succeeds. The screenshot is captured after a completed rendered frame containing
that exact candidate, then the receipt is atomically updated with the final
capture result. A screenshot failure does not delete or misreport the valid
style; the folder remains with an honest partial-success receipt and diagnostic.
F11 before a successful F10 creates no directory and reports that there is no
candidate. The implementation adds root `LookLab/` to `.gitignore` so
exploration output never dirties the repository.

## Randomization Contract

LL0 owns the exhaustive current-source census. Every style-facing field and
enumerated mode is classified as randomized, derived, retained, or excluded,
with its consuming render stage and valid range. The implementation must obey
these rules:

1. **Independent authoring randomness.** Look Lab owns a separate deterministic
   generator. It never consumes or reseeds gameplay, scene, physics, Replay, or
   renderer sampling randomness.
2. **Resolved reproducibility.** The same seed and generator version produce
   the same candidate bytes. F11 writes all Look Lab-owned resolved fields and
   material rules, so a saved style remains identical if recipes or defaults
   change later.
3. **Coherent variation.** A candidate begins with a named internal art-direction
   recipe informed by the tracked style catalog, then applies bounded correlated
   variation. Palette roles are derived together in a perceptual color space;
   sun, sky, fog, terrain, water, object accents, exposure, bloom, and enabled
   effects must form one readable look rather than independent noise.
4. **Broad discovery.** Recipes cover the supported realistic, stylized,
   painterly, low-poly, neon, graphic, atmospheric, high-key, low-key, warm,
   cool, terrestrial, and space-facing families that current consumers can
   actually render. A deterministic seed matrix must demonstrate meaningful
   diversity without relying on a subjective screenshot count.
5. **Valid visibility.** Every value is finite and parser-valid. Coupled
   constraints preserve readable luminance/contrast, non-inverted fog ranges,
   bounded bloom/exposure, visible primary geometry, and compatible feature/mode
   combinations. A recipe may be dark, foggy, saturated, or abstract; it may not
   be accidentally blank or numerically invalid.
6. **Appearance, not quality policy.** Resource sizes, renderer capacity,
   shadow-map allocation, shader compilation, raytracing policy, window state,
   and performance/debug options are retained. The randomizer controls how the
   image looks, not how much memory or hardware work is authorized.
7. **Presentation-only application.** F10 may mutate active cinematic/style and
   object-material presentation. Camera pose/projection, object transforms,
   assets, simulation state, scene topology, physics settings, clocks, and the
   authored scene document remain exact.

## Ownership

- A focused Runtime Direction `LookLabController` owns the current candidate,
  authoring seed/generator version, save state, and the F10/F11 transaction. It
  is not a new Runtime context or service bag.
- A pure generator resolves a detached style candidate from a seed and typed
  recipe catalog. Tests invoke it without a window, renderer, filesystem, or
  active scene.
- Scene owns the standalone style snapshot value and writer because Scene
  already owns the style parser and schema. The writer accepts detached values
  and does not include Runtime.
- SceneController continues to own live application to cinematic state and
  object-material presentation. Look Lab does not reach into render stores.
- Capture owns screenshot execution. Look Lab submits a typed request and waits
  for the existing post-render completion result before finalizing the bundle
  receipt.
- Input publishes F10/F11 action values; App sequences them to the Direction
  owner. UI receives detached status text/value data and owns no authoring state.

## Non-Goals

- No focal length, FOV, aperture, focus distance, depth of field, camera pose,
  camera motion, or lens simulation.
- No change to F5, F6, F7, F8, F9, F2, or F3 behavior.
- No arbitrary HLSL generation, source editing, runtime shader compilation, or
  shader hot-reload through F10.
- No randomization of physics, gameplay, scene topology, transforms, asset
  identity, UI layout, debug overlays, resource capacities, or renderer quality
  budgets.
- No write to the currently loaded `.scene.json`, `engine.cfg`, existing curated
  style files, or tracked validation baselines. Generated output is confined to
  ignored root `LookLab/` directories.
- No prompt, modal naming dialog, or editor dependency on F11; saving is one key
  press with an automatic timestamp/seed name.
- No claim that every random look is aesthetically desirable. The contract is
  broad, coherent, valid exploration with exact reproducibility, not automated
  taste.

## Phases

- [x] **LL0 — Census the complete live style surface and lock the contract.**
  Inventory every field in `CinematicRenderConfig`, every supported sky/terrain/
  object/water and material mode, all object-material targeting semantics, all
  23 tracked styles, live-style application, scene-style merge/reset behavior,
  screenshot timing, root `LookLab/` bundle policy, receipt contents, and
  F5/F6/F10/F11 bindings. For every
  option record its actual consumer, valid parser range, runtime resource side
  effects, randomize/derive/retain/exclude ruling, and reason. Measure idle input
  and style-apply behavior before implementation. Lock the recipe families,
  coupled safety constraints, generator version, filename grammar, and exact
  output field set before production edits. Complete: the 80 cinematic atoms,
  14 material kinds, targeting rules, 23-style catalog, current merge/reset and
  capture flow, SplitMix64 generator-v1 contract, 14 recipes, exact output
  grammar, and pre-change input/style behavior are locked in
  `../../Reports/2026-08-01/look-lab-random-style-authoring-ll0-census.md`.

- [x] **LL1 — Build the pure deterministic candidate generator.** Introduce the
  detached candidate and focused generator using one explicitly specified 64-bit
  algorithm whose output is compiler/configuration independent. Implement typed
  art-direction recipes, correlated palette derivation, bounded feature and
  scalar variation, and final validity checks. Same seed/version must produce
  identical candidate bytes; a fixed seed matrix must cover every recipe family
  and supported style/material branch; planted invalid-range, incompatible-mode,
  black-frame, and cross-RNG controls must fail. Complete: Runtime Direction now
  owns the pure SplitMix64 generator-v1 candidate, Q12 Oklab palette derivation,
  fourteen typed recipes, explicit canonical byte encoding, comprehensive
  validity checks, and a 4,096-seed matrix covering every required recipe and
  supported render branch. Debug and Profile pin the same candidate fingerprint;
  evidence is in
  `../../Reports/2026-08-01/look-lab-random-style-authoring-ll1-generator.md`.

- [x] **LL2 — Add exact standalone style serialization.** Add the Scene-owned
  writer for a fully resolved schema-current `skullbonez.style.json` document.
  Preserve the established key vocabulary and parser bounds; do not bump the
  schema merely to record generator metadata because timestamp and seed already
  live in the directory name and receipt. Add the root `LookLab/` ignore rule.
  Prove serialize/parse equality for every field and material rule, stable key
  order/float formatting, byte-identical same-candidate output, exact receipt
  facts, collision-safe directory names, directory creation, atomic replace
  semantics, and bounded diagnostics for unwritable/invalid paths. Complete:
  Scene owns the self-contained ordered schema-v1 snapshot/writer, Core owns
  flushed temporary-sibling replacement, and Runtime Direction owns exclusive
  bundle naming plus derived atomic receipt revisions. Debug/Profile pin the
  same full JSON bytes and the production parser reconstructs every resolved
  field/material rule exactly; evidence is in
  `../../Reports/2026-08-01/look-lab-random-style-authoring-ll2-serialization.md`.

- [x] **LL3 — Add the focused live Look Lab owner.** Compose `LookLabController`
  in Runtime Direction, resolve F10 candidates, and apply them through the
  existing Scene style seam without reload. Retain only the current candidate and
  bounded status/save metadata. Prove immediate application, scene-reset/load
  clearing, UI/browser style-selection coherence, zero simulation-RNG reads, and
  exact preservation of camera, scene topology, transforms, assets, physics,
  clocks, and the authored scene path/content. Complete: Runtime Direction owns
  validated scene-resolved candidates and bounded status; App applies detached
  snapshots through a no-reload SceneController seam and restores process
  presentation before every runtime scene transition. Debug/Profile pass 11
  focused cases and 4,240 assertions; evidence is in
  `../../Reports/2026-08-01/look-lab-random-style-authoring-ll3-live-owner.md`.

- [x] **LL4 — Wire F10 reroll and the F11 bundle transaction.** Add explicit
  input actions and exact binding tests while pinning F5/F6 to their existing
  diagnostics. F11 creates one ignored timestamp/seed directory, writes the
  exact style and pending receipt first, requests one screenshot for the applied
  candidate, atomically finalizes `look.txt`, and publishes success/partial-
  failure paths only after the capture owner responds. Cover keyboard capture/
  focus edges, repeated presses, F11-before-F10, F10-during-pending-save, path
  collision, style/receipt failure, screenshot failure, shutdown/scene-load
  cancellation, and one complete bundle per accepted F11 action without
  duplicate writes or captures. Complete: Input owns exact keyboard-unblocked
  F10/F11 press actions while preserving F5/F6; App sequences one scene-local
  candidate and one save transaction; Capture owns a bounded typed post-render
  PNG queue executed after world/UI drawing and before Present. Pending saves
  reject rerolls and duplicate saves, scene transition/shutdown cancel the
  matching token, and receipt revisions publish final, partial-failure, or
  cancelled status only after Capture responds. Debug/Profile pass the focused
  transaction, PNG, and binding matrix; evidence is in
  `../../Reports/2026-08-01/look-lab-random-style-authoring-ll4-input-capture.md`.

- [x] **LL5 — Prove useful breadth, no idle cost, and reusable output.** Run a
  deterministic large seed census and report recipe/mode/feature distribution,
  palette/luminance/contrast envelopes, invalid count, and exact repeat hashes.
  Load saved output in a fresh process and prove it resolves the same cinematic
  and material values without generator/catalog/default dependence. Measure the
  input/render baseline with Look Lab idle and require no per-frame polling,
  filesystem access, allocation, shader compilation, scene reload, or material
  churn. Exercise all existing curated style files to prove compatibility.
  Complete: a 65,536-seed census produces 65,536 unique valid candidates with
  one exact Debug/Profile stream hash and reported recipe/mode/feature and
  palette envelopes. A distinct-process producer/consumer round-trip preserves
  complete style bytes without generator/catalog/default input, all 23 curated
  styles parse, and event-driven UI status publication removes Look Lab owner
  polling from the idle frame. Matching-condition timing is below LL0 in four
  passes and guarded idle runs exit cleanly; evidence is in
  `../../Reports/2026-08-01/look-lab-random-style-authoring-ll5-breadth-reuse-idle.md`.

- [ ] **LL6 — Perform visible validation and close.** In one waited interactive
  DX12 session, demonstrate repeated visibly distinct F10 looks and save a chosen
  F11 bundle. Inspect the complete JSON, text receipt, and PNG, reload the style
  in a fresh run, and compare the reapplied image/state. Run focused tests,
  style/parser and
  snapshot coverage, `validate_tests`, `validate_fast`, dependency validation,
  `validate_physics`, `validate_dx12_renderer`, the bounded graphics-stress gate,
  `validate_perf`, and `validate_full`. Existing Physics, DX12, Replay, and visual
  baselines must remain unchanged because no validated workload presses F10/F11.
  Audit every touched source-bearing file, run all seven ownership inventories,
  and obtain an independent read-only review of ownership, randomness,
  serialization, bundle/receipt failure atomicity, capture timing, input
  conflicts, idle cost, and test sensitivity.

## Dependencies And Decisions

- Look Lab is first in the active MASTER-PLAN order and runs before Contact
  Energy And Warm-Start Integrity.
- LL0 precedes source changes; a field without a real current consumer is not
  randomized merely because it exists in configuration or JSON.
- LL1 precedes live application and serialization so both consume one tested
  candidate contract rather than independently reconstructing values.
- LL2 precedes F11 wiring; the key handler never owns JSON or filesystem policy.
- LL3 precedes LL4; input dispatch cannot become the retained authoring owner.
- LL4 uses the existing Capture owner and one post-render completion. It may not
  add another backbuffer or file-writing owner. The receipt records the result;
  it does not own or duplicate the style values.
- No phase carries baseline-refresh authority. Any default, no-input visual or
  physics movement is a defect to repair, not a golden to normalize.

## Acceptance

The plan closes when F5/F6 remain exact, each F10 press applies a valid and
visibly useful deterministic presentation candidate without changing camera or
simulation, F11 creates one unique timestamp/seed `.style.json` and matching PNG
simulation, F11 creates one unique ignored root `LookLab/<datetime>_<seed>/`
bundle containing `look.style.json`, a complete human-readable settings/status
`look.txt`, and matching `look.png` without touching the active scene, saved
styles round-trip and reapply exactly in a fresh process, failures are atomic
and honestly reported, idle cost is zero within measurement resolution,
existing style content remains compatible, all mapped gates pass without
baseline refresh, touched comments are complete, and independent review finds
no blocking ownership or false-pass defect.

## Validation

- Exact input-binding tests pinning F5/F6 and adding F10/F11
- Pure generator same-seed, diversity, range, coupling, and negative controls
- Large deterministic seed census with zero invalid candidates
- Full-field style serialize/parse/reapply round-trip
- Atomic directory/JSON/receipt, collision, partial screenshot, cancellation,
  and repeat-press tests
- Simulation RNG/state, camera, scene topology, and source-file non-mutation tests
- Existing 23-style parser/application compatibility census
- Fresh-process saved-style reload and one waited F10/F11 DX12 demonstration
- `tools\validate_tests.bat`
- `tools\validate_fast.bat`
- `tools\validate_dependency_graph.bat`
- `tools\validate_physics.bat`
- `tools\validate_dx12_renderer.bat`
- `tools\run_graphics_stress.bat 1`
- `tools\validate_perf.bat`
- `tools\validate_full.bat`
- Seven ownership inventories and touched-source comment audit
- Independent read-only closure review
