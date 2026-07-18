# Naming And Identity Debt — Make The Code Say What It Is

Date: 2026-07-18
Status: Active — 4/5 tasks
Branch: `nightrunner-17th-july` (ratified at N0; never directly on `main`)
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

- [x] N0 — Rename census and ruling table. Inventory the four debt areas
  with per-file rename proposals (target names are domain nouns), the
  consumer-include blast radius per rename, and project/filter impact.
  Owner ratifies each target name and the branch before any edit.
  Evidence: dated census under `Agentic/Reports/`. Gate: none
  (documentation).
- [x] N1 — Scene system rename. `TestScene` → the ratified production name
  (candidate: `AuthoredScene` or `Scene`; owner decides at N0), including
  the parser family, filenames, include paths, project/filter entries, and
  in-comment vocabulary. Authored `*.scene.json` files, schema keys, and
  versions are untouched; `git diff` over `SkullbonezData/` must be empty.
  Gate: `validate_full` (scene loading is in the broad gate's lanes).
- [x] N2 — Retire the `GameModel` spelling. Rename `GameModelRenderer` to
  the ratified name matching what it renders today (census decides;
  candidate: `SceneModelRenderer` / `RenderInstanceRenderer`), update
  consumers, and delete any other surviving `GameModel` vocabulary the N0
  census finds (comments, capacity constant names, filters). Gate:
  `validate_fast`, plus `validate_dx12_renderer` + stress if any DX12-side
  file is touched per MASTER rule 10.
- [x] N3 — RuntimeTuning rename and seam ruling. Rename to the ratified
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

## Progress Evidence

- N0 evidence (2026-07-18):
  `Agentic/Reports/2026-07-18/naming-and-identity-n0-census.md` ratifies
  `AuthoredScene` / `AuthoredSceneParser`,
  `Rendering::RenderInstanceRenderer`, and `OperatorCommandApplier` on
  `nightrunner-17th-july`. It measures the 34-file/533-occurrence scene blast
  radius, 8-file/28-occurrence renderer radius, and 17-file/21-occurrence
  command-module radius; records project/filter/tool impacts; and rules all
  62 tracked `Run*` / `Runtime*` source files. N4 is limited to the three files
  in `RunDemoDirector.*` and `RunUiTextPass.cpp`. The pre-plan git-index
  manifests for 347 `SkullbonezData/` files and 24 baseline files are recorded
  for exact N1-N4 reconciliation. N0 is documentation-only; no repository
  validation was required.
- N1 evidence (2026-07-18): the eight production scene/parser files and every
  production consumer now use `AuthoredScene` / `AuthoredSceneParser`; there
  are zero `TestScene` occurrences in `SkullbonezSource/` and 434 occurrences
  of the production spelling. Three test harness filenames intentionally keep
  `TestScene*`, while their source, assertions, and the standalone parser
  project use the production names. The 30/30 comment audit is recorded in
  `Agentic/Reports/2026-07-18/naming-n1-comment-audit.md`. A focused Profile
  build passed in 22.992 s. The first broad attempt stopped at six formatting
  rows; the next exposed the missed standalone parser-project paths after the
  preceding CPU lanes passed. Both were corrected without behavioral edits.
  The final `validate_full` passed in 169.262 s with 291/291 doctests,
  21,455/21,455 assertions, every coverage/CPU lane, Automation, zero-error
  DX12 captures, and byte-exact physics. Direct migration (39 files), project
  filter (738/738), and parser-test checks passed. The authored-data and
  baseline manifests remain exactly `311c995e...a847` and
  `d1de0ad4...90be`; no baseline or golden changed. N1 took about 13 minutes.
- N2 evidence (2026-07-18): `Rendering/GameModelRenderer.*` is now
  `Rendering/RenderInstanceRenderer.*`, and the type moved from the obsolete
  `GameObjects` namespace to `Rendering`. All shared source capacity, camera,
  diagnostic, UI, physics, replay, and renderer identifiers now say scene
  object rather than game model; the external `game_model_capacity` config key
  remains byte-identical. The two legacy camera key hashes remain exactly
  `0x76EECD4F` and `0x77EECEE2`. The first focused Profile build caught one
  tracked file below an ignored `Runtime/Debug` directory that the initial
  working-tree search omitted; the git-index inventory corrected it, and the
  rerun passed with zero warnings in 12.302 s. The 79/79 touched-source comment
  audit is recorded in
  `Agentic/Reports/2026-07-18/naming-n2-comment-audit.md`. Final gates passed:
  `validate_full` in 208.217 s, the plan's single replay visual-fidelity
  invocation in 430.076 s, `validate_perf` in 91.773 s, direct DX12 renderer in
  52.624 s, allocation self-test/repository scans in 0.138/8.810 s, project
  filters at 738/738 in 1.675 s, and bounded graphics stress in 61.921 s.
  Production source has zero `GameModel` occurrences; the seven excluded
  renderer interfaces and all authored-data/baseline files are unchanged. No
  baseline, golden, screenshot, or coverage floor changed. N2 took about 28
  minutes.
- N3 evidence (2026-07-18): `Runtime/RuntimeTuning.*` is now
  `Runtime/OperatorCommandApplier.*`; all 13 source consumers plus production
  project/filter metadata and project-filter tooling use the ratified name.
  Active source and build metadata have zero old-module occurrences. The
  command-seam ruling in
  `Agentic/Reports/2026-07-18/naming-n3-command-seam-rulings.md` records every
  command group: contact-audio bounds already delegate to
  `ContactAudioService`, while the remaining helpers coordinate explicit
  multi-owner borrows or keep UI packet decoding out of domain owners. No
  function move, state bag, callback bridge, compatibility alias, or ownership
  drift was introduced. The 14/14 touched-source audit is recorded in
  `Agentic/Reports/2026-07-18/naming-n3-comment-audit.md`. A focused Profile
  build passed with zero warnings in 13.146 s. `validate_full` passed in
  153.373 s; the task's single replay visual-fidelity invocation passed in
  430.624 s with all false-pass controls; and direct project-filter validation
  passed 738/738 in 1.725 s. The seven excluded renderer interfaces,
  `FRAME_COUNT = 2`, authored data, and baselines are unchanged. No baseline,
  golden, screenshot, or coverage floor changed. N3 took about 15 minutes.

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
