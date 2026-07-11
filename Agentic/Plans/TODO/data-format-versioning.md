# Scene/Asset Data Format Versioning

Date: 2026-07-11
Status: Not started — 0%
Impact area: scene parser/writer, asset library JSON, hull baking, tests
Origin: 2026-07-11 architecture gap review. No data file carries a format
version: `TestSceneParser` reads no version field, `*.assets.json` and
`*.hull` have no schema stamp. Every past format change has been
"change the parser and hope"; there is no policy for reading old files or
rejecting future ones.

## Goal

Every authored data format (`*.scene.json`, `*.assets.json`, `*.hull`,
`engine.cfg` if practical) carries an integer `formatVersion`. Loaders have
one explicit policy: current version loads; supported older versions load
through named migration steps; newer-than-current fails as a Lane R error
naming both versions. Writers always stamp current.

## Scope decisions (binding)

- **Integer versions, one per format.** No semver strings, no per-field
  feature flags.
- **Missing version field = version 0** (everything existing today), loaded
  via the version-0 → 1 migration path. Nothing in the repo breaks on the
  day this lands.
- **Migration window policy: current and current-1** are loadable; older
  versions require running the (new) `tools\migrate_scene.py` upgrader.
  Keeping every historical reader alive forever is explicitly rejected.
- **Committed data files are upgraded** to current in the same change that
  bumps a version, so the repo's own scenes/baseline fixtures never exercise
  the deprecated window.
- Coordinate with `TODO/runtime-shell-decomposition.md` D2 (parser split /
  JSON reader decision): land this before or with D2 so the version gate is
  written once against the surviving parser shape.

## Phases

- [ ] V1. Policy + plumbing: `formatVersion` read/write in scene parser and
      writer, assets loader, and hull load/bake (`tools\bake_hulls.py`
      stamps on `--write`). Version 0 fallback; future-version Lane R
      rejection with owner/message. Gate: `validate_full` (scene/asset/hull
      rows in the validation map).
- [ ] V2. Tests: doctest cases per format — missing field loads as v0,
      current loads, future version produces a recoverable named failure
      (never fatal), and a round-trip write stamps current. Extends the
      behavioral-test-depth P3 parser suite. Gate: `validate_tests`.
- [ ] V3. Upgrader tool: `tools\migrate_scene.py` (and assets/hull as
      needed) rewrites old files to current, idempotent, with a `--check`
      mode; committed data files re-stamped. Gate: `validate_fast` (tool
      script) then `validate_full` (data files changed).
- [ ] V4. Process rule: add one paragraph to `AGENTS.md` data rules — any
      schema change bumps the format version, adds the migration step,
      upgrades committed files, and extends the V2 tests in the same
      commit. Documentation-only.
- [ ] V5. Closure: comment audit on touched parser/tool files,
      `validate_full`, MASTER-PLAN/SessionState update, delete plan.

## Acceptance

- [ ] All four formats stamp and check `formatVersion`.
- [ ] A future-versioned scene file fails recoverably with a clear message
      (unit-tested), and a version-0 file (today's files) loads unchanged.
- [ ] The repo's committed data files all carry the current version.
- [ ] Physics CSV and DX12 screenshots unchanged (versioning is metadata).

## Validation map

| Slice | Gate |
|-------|------|
| Parser/loader changes | `validate_full` (scene/asset/hull validation-map rows) |
| Test additions | `validate_tests` |
| Tool scripts | `validate_fast` + run the changed script |
