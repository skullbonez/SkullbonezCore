# Naming And Identity Debt — Make The Code Say What It Is

Date: 2026-07-18
Status: Active — 0/5 tasks
Branch: owner decision at N0 (feature branch; never directly on `main`)
Impact area: `SkullbonezSource/Scene/*`, `Rendering/GameModelRenderer.*`,
`Runtime/RuntimeTuning.*`, targeted `Run*`/`Runtime*` files, project files
and filters, includes across consumers
Owner: repository-wide naming policy

## Problem And Evidence (measured 2026-07-18 at `nightrunner-17th-july` tip, 06a17ff31)

The 2026-07-18 hostile review (finding 6) documents vestigial-identity debt
that survived twenty years of modernization:

- The production scene system is named **`TestScene`**: `Scene/TestScene.h`
  (670 lines), `TestScene.cpp`, and the `TestSceneParser*` family (~5,800
  lines). Every shipped scene in `SkullbonezData/scenes` loads through a
  type whose name says "test".
- `Rendering/GameModelRenderer.cpp/h` (864/111 lines) outlives `GameModel`,
  which no longer exists anywhere in the tree.
- `Runtime/RuntimeTuning.cpp/h` (1,359/285 lines) is a UI-to-everything
  mediator touching render, audio, physics, workers, and persistence, and
  wears "Tuning" — a noun the `AGENTS.md` Migration Cleanup rule flags as a
  migration spelling requiring owner/reason/deletion-condition naming it
  does not carry.
- The `Run*` / `Runtime*` prefix soup (40+ files: `Run.cpp`, `RunFrame`,
  `RunScene`, `RuntimeRenderer`, `RuntimeTools`, `RuntimeTuning`,
  `RuntimeViewModel`, …) encodes extraction history rather than domain.

## Goal

Production types carry production domain names. The scene system loses its
"Test" identity, the orphaned `GameModel` spelling is retired, the tuning
mediator is renamed (and, where the census shows a clean seam, its command
application moves to the owning subsystem), and the `Run*`/`Runtime*`
inventory receives explicit owner rulings: rename, keep-with-reason, or
defer.

## Non-Goals

- **Zero behavioral change and zero authored-data change.** C++ type/file
  renames must not alter any scene/asset/hull/config schema, key, version,
  or byte; no format migration is created or needed.
- No baseline, golden, screenshot, or coverage-floor refresh.
- No ownership redistribution beyond the optional RuntimeTuning seam N3
  records — this is a naming plan, not decomposition round 3.
- No mass mechanical rename of every `Run*`/`Runtime*` file in one sweep;
  only N4's owner-ruled subset moves.

## Tasks

- [ ] N0 — Rename census and ruling table. Inventory the four debt areas
  with per-file rename proposals (target names are domain nouns), the
  consumer-include blast radius per rename, and project/filter impact.
  Owner ratifies each target name and the branch before any edit.
  Evidence: dated census under `Agentic/Reports/`. Gate: none
  (documentation).
- [ ] N1 — Scene system rename. `TestScene` → the ratified production name
  (candidate: `AuthoredScene` or `Scene`; owner decides at N0), including
  the parser family, filenames, include paths, project/filter entries, and
  in-comment vocabulary. Authored `*.scene.json` files, schema keys, and
  versions are untouched; `git diff` over `SkullbonezData/` must be empty.
  Gate: `validate_full` (scene loading is in the broad gate's lanes).
- [ ] N2 — Retire the `GameModel` spelling. Rename `GameModelRenderer` to
  the ratified name matching what it renders today (census decides;
  candidate: `SceneModelRenderer` / `RenderInstanceRenderer`), update
  consumers, and delete any other surviving `GameModel` vocabulary the N0
  census finds (comments, capacity constant names, filters). Gate:
  `validate_fast`, plus `validate_dx12_renderer` + stress if any DX12-side
  file is touched per MASTER rule 10.
- [ ] N3 — RuntimeTuning rename and seam ruling. Rename to the ratified
  domain name (candidate: `OperatorCommandApplier` split per domain, or a
  single ratified name with recorded cohesion rationale). Where N0 found a
  clean seam (for example sound commands already delegating to
  `ContactAudioService` limits), move that application into the owning
  subsystem; otherwise record keep-reasons. No new bag types. Gate:
  `validate_full`.
- [ ] N4 — `Run*`/`Runtime*` ruled subset and closure review. Execute only
  the renames the N0 table ratified as high-value/low-radius; record
  keep-with-reason rulings for the rest so the debt is governed rather
  than silent. One independent review confirms renames are pure (no
  behavior, no authored-data, no ownership drift) and the ruling table is
  complete. Final gate: `validate_full`. Update MASTER-PLAN, SessionState,
  and delete this plan on closure.

## Dependencies And Decisions

- Runs **after** `scene-controller-decomposition-round-2` (both touch
  `Scene/*`; renames rebase on the split so moved files are named once).
- May run before or parallel to `small-findings-hardening`; coordinate on
  any shared DX12 files at merge.
- N0 owner decisions: every target name; whether `Run` (the composition
  root class) itself renames or receives a keep-with-reason ruling.
- Comment-standard note: renames of source-bearing files trigger the
  touched-file comment audit; glossary entries referencing old names must
  update in the same commit.

## Acceptance

- Zero occurrences of `TestScene` and `GameModel` spellings in
  `SkullbonezSource/` (tests may keep historical names only where the N0
  table records a reason).
- `RuntimeTuning` no longer exists as a name; its replacement carries
  either a domain name with recorded cohesion rationale or per-domain
  owners.
- The `Run*`/`Runtime*` ruling table covers every matching file with
  rename-or-reason.
- `git diff` over `SkullbonezData/` and `TestOutput/baselines/` is empty
  for the whole plan; all mapped gates pass from final source.
