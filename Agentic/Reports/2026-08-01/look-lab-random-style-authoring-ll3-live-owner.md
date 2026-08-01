# Look Lab Random Style Authoring — LL3 Live Owner

Date: 2026-08-01
Plan: `Agentic/Plans/TODO/look-lab-random-style-authoring.md`
Phase: LL3 complete

## Outcome

Runtime Direction now owns one focused `LookLabController`. It resolves the
pure generator output against the active scene's retained presentation facts,
publishes only a fully validated candidate, exposes a detached complete style
snapshot, and retains fixed-capacity operator status. App sequences that value
through the existing SceneController style boundary; the controller retains no
Scene, UI, renderer, Capture, filesystem, simulation, or random-state pointer.

`SceneController::ApplyStandaloneStyle` applies the complete cinematic and
material snapshot directly to the live scene. It performs no scene reload or
file round-trip, clears curated-browser selection, and leaves scene mutation
authority with SceneController. Partial authored styles continue to use their
existing defaults-merge path; the new standalone seam deliberately replaces
the full authorable presentation surface.

## Resolution And Preservation

- SplitMix64 remains the only Look Lab random stream. A C runtime RNG sentinel
  proves resolution does not consume shared process randomness.
- Basin center, radii, and feather are copied exactly because they are scene
  geometry in scene units. A scene with water disabled remains water-disabled.
- Shadow allocation, filtering, bias, coverage, and geometry-participation
  values are copied exactly. Generator fog distances are scaled only from the
  retained shadow coverage and remain within the locked parser constraints.
- The resolution seam accepts only `CinematicRenderConfig`. It has no authority
  to observe or mutate camera pose/projection, scene topology, transforms,
  assets, physics, clocks, authored path/content, or Scene RNG state.
- Live application mutates only entity render materials, cinematic presentation
  fields/masks, and browser presentation selection. It does not rebuild Scene,
  physics, assets, transforms, or queues.

Look Lab presentation is scene-local. Before every runtime scene-transition
entry, App restores the process cinematic baseline and clears the candidate.
The controller also observes the scene lifecycle packet idempotently, so a
completed clear cannot leave retained status behind. Save-only requests do not
trigger this cleanup; the Scene request owner exposes a read-only transition
query so App can clean sibling state before `ExecutePending` enters teardown.

## Focused Proof

`SkullbonezTests/TestLookLabController.cpp` proves:

- generator/process-RNG isolation;
- exact basin, water, shadow-quality, and shadow-geometry retention;
- bounded fog scaling and final candidate validation;
- exact detached standalone-snapshot publication;
- bounded status transitions and prior-candidate preservation on rejection;
- direct and lifecycle-driven clearing, including idempotent observation.

`SkullbonezTests/TestOwnerRequestQueues.cpp` proves the scene transition query
is false for an empty/drained queue and true when a transition is pending.

Debug and Profile each pass the focused `Look Lab*,SceneRequestQueue*` selection:
11 test cases and 4,240 assertions, with no failures. Debug, Profile, and
Automation solution configurations build successfully; all compiles complete
without errors.

## Governance And Comment Audit

- Format validation passes for all 586 C++ files; all repository-relative
  `Related:` paths resolve.
- Dependency proof/repository scan passes: 27 include rules, one content rule,
  one project rule, zero findings.
- Project-filter validation passes: 802 project items and 802 filter items,
  zero errors.
- Build-configuration consistency reports 1,714 compile rows, 67 shared source
  files, 134 exact ruled divergences, zero dropped inheritance, and zero
  blocking diagnostics.
- Function complexity: 40/40 triggered bodies ruled.
- Authority-free aggregates: 86/86 gated rows ruled.
- Wide signatures: every signature at the 12-parameter review trigger is ruled.
- Extraction scars: the one pre-existing WorkerPool row remains ruled.
- Glossary: 584 files, 979 unique definitions, zero duplicates or drift.
- Reachability: 89 current rows, all ruled, zero blocking diagnostics. The
  unrooted `Run::ApplyLookLabSeed` seam carries an exact repair-plan ruling
  because LL4 supplies its F10 production root.
- `tools\\validate_fast.bat` passes in 398.1 seconds and
  `tools\\validate_full.bat` passes in 591.7 seconds without baseline refresh.

The required touched-source comment audit inspected 13/13 files with zero
deferred or unchecked files. Learning summaries identify ownership/flow,
LookLab status names its invariant and focused test, lifecycle and retained
value comments sit beside the risky operations, and post-change ownership
claims match the live call paths. This is a touched-file pass, so no subsystem
checklist file is required.

## Phase Boundary

LL3 deliberately leaves the application entry unbound. LL4 owns the explicit
F10/F11 input actions, the production reachability root, and the bundle/capture
transaction. This preserves the plan's rule that input dispatch cannot become
the retained Look Lab owner.
