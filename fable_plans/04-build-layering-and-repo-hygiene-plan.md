# Build Layering And Repo Hygiene Plan

Date: 2026-07-06
Status: Proposed
Impact area: build system, file organization, git hygiene; no behavior change
Validation for this document: none (documentation-only)

## Problem

**Monolith build.** One `SKULLBONEZ_CORE.vcxproj` compiles all 156 .cpp files
(plus 14 textually-included .inl files). There is no library layering, so:

- Nothing can build or link in isolation — the physics-standalone goal
  (`physics-standalone-strict-goal-checklist.md`) is structurally impossible
  today, and plan 01's test project must compile sources in by hand.
- Dependency direction is unenforced; any file can include any other. The
  regex boundary checker exists precisely because the linker can't say no.

**Mega-files.** `RunInput.cpp` 3,580 lines (every tool/mode's input in one
file), `RunFrame.cpp` 3,426, `UI.cpp` ~3,400, `PhysicsWorld.cpp` 3,050,
`TestSceneParser.cpp` 2,956 (hand-rolled JSON parsing), `Init.cpp` 2,600+.
`Common.h` is a mega-header that defines engine-wide constants *and* the
global `Cfg()` accessor; the validation map's own rule — touch `Common.h`,
run `validate_full` — is an admission that its blast radius is "everything."

**Repo hygiene.** The pack is **542 MiB**. Commit `065bb64b` (2026-07-06)
checked an 81 MB disassembly dump (`Agentic/Temp/skullbonez_profile_disasm.txt`),
a `WinPixEventRuntime.dll`, and a copied build-output tree
(`Agentic/Temp/ProfilePrediction*/`) into history permanently. `Agentic/Temp`
is not ignored, so scratch artifacts keep landing in commits.

## Goal / Definition of Done

- Static libraries with an enforced one-way dependency chain:
  `SkullbonezMaths` → `SkullbonezCoreLib` (Core utils) → `SkullbonezPhysics`
  → `SkullbonezRendering` → runtime exe. Physics links and runs headless.
- No source file over ~1,500 lines without a written justification comment in
  its learning header; the current 3,000+ liners are decomposed.
- `Common.h` contains only what genuinely belongs everywhere; no service
  accessors (plan 02 deletes `Cfg()`), no domain constants.
- `Agentic/Temp/` is gitignored and empty in the tip tree; no build outputs,
  DLLs, or multi-MB dumps tracked outside `TestOutput/baselines` (which is
  intentional golden-file data).

## Phased slices

### Phase 1 — stop the bleeding (one sitting, do first)

- Add `Agentic/Temp/` to `.gitignore`; `git rm -r --cached` the tracked temp
  tree (tip-tree only — **no history rewrite**; see Open decision below).
- Add a lightweight pre-commit-oriented check to `tools/`: fail validation if
  a staged file exceeds a size threshold (e.g. 5 MB) outside an allowlist
  (`TestOutput/baselines`, `SkullbonezData`). Include the checker self-test.

### Phase 2 — extract `SkullbonezMaths.lib`

- `Maths/` has no upward dependencies — the cheapest proof of the layering
  mechanics (project refs, warning level, output dirs). The test project
  (plan 01) switches from compile-in to linking it.

### Phase 3 — `SkullbonezCoreLib` and `Common.h` split

- Split `Common.h`: engine constants move next to their owners
  (`PHYSICS_FIXED_DT` → physics constants header; render constants → render).
  Keep a slim `Common.h` for genuinely universal aliases during transition.
- Core utils (Timer, Log, Profiler, WorkerPool, allocation tracker) become a
  library beneath physics/rendering.

### Phase 4 — `SkullbonezPhysics.lib`, headless

- Physics + collision + solver link as a library with a tiny console driver
  (`physics_headless.exe`) that loads a scene sidecar and steps N ticks. This
  is the deliverable the standalone checklist wants, and it makes plan 01's
  determinism property test and plan 03's bundle stepping trivially hostable.

### Phase 5 — mega-file decomposition (rolling, one file per slice)

- `RunInput.cpp` → per-workspace input files (editor, replay, camera, launcher)
  matching the existing `Run*Tools.cpp` naming.
- `TestSceneParser.cpp` → schema-section parsers; or adopt a vendored minimal
  JSON reader and delete the hand-rolled tokenizer entirely (separate decision).
- `RunFrame.cpp`/`Init.cpp` shrink naturally as authoritative-plan-01
  (composition root) and plan 02 (service contexts) land; do not decompose
  those ahead of that work — sequence after.
- Replace `.inl`-into-anonymous-namespace composition with real TUs as files
  are touched (each `.inl` that becomes a `.cpp` with a narrow header removes
  one "must only be included from X" comment-enforced invariant).

## Open decision for the user (history rewrite)

Removing the 81 MB dump from the *tip* does not shrink the 542 MiB pack;
that requires history rewriting (`git filter-repo`), which repository rules
forbid without explicit authorization, and it invalidates all existing clones.
Options: (a) live with the pack size, (b) authorize a one-time filter-repo of
`Agentic/Temp/*` blobs with a coordinated re-clone. This plan takes no action
without that decision.

## Validation map

| Slice | Validation |
|-------|-----------|
| gitignore / checker | `validate_fast`, then run the changed script |
| Library extractions (2–4) | `validate_full` (build-system change is broad scope) |
| Headless physics driver | `validate_physics` + `validate_physics_deep` once |
| Mega-file splits | Per the file-to-validation map for the touched area |

## Risks

- vcxproj surgery is tedious and merge-hostile; do library extractions when
  the worktree is otherwise quiet and land them as single-purpose commits.
- Decomposing `RunInput.cpp` before the composition-root work risks moving
  code twice; the sequencing note in phase 5 exists for that reason.
- The `.inl` pattern currently gives the optimizer whole-TU visibility;
  converting hot .inl files (solver, prediction) to TUs should be paired with
  `validate_perf`.
