# Authored Data Format Versioning

Date: 2026-07-11
Status: Planned — 0/5 phases complete
Impact area: asset library JSON, hull baking, engine configuration, format
policy, migration tooling, tests
Origin: 2026-07-11 architecture gap review.
**Rescoped on 2026-07-11 merge reconciliation:** the completed
physics-authority C1b work shipped scene schema versioning the same day —
`TestSceneParser` now reads a required document `version`
(`m_schemaVersion`, v2 current, v1 readable through one deterministic
upgrade path). The scene half of the original problem is solved. Remaining
scope: `*.assets.json` and `*.hull` still carry no schema stamp, `engine.cfg`
is unversioned, and there is no *uniform* policy or migration tool across
formats — the scene implementation is the pattern to generalize, not
replace. Phase V1 applies to assets/hull/cfg only; scene work in this plan
is limited to aligning the policy wording with what shipped.

## Goal

Generalize the shipped scene-versioning semantics to the authored formats that
remain unversioned: `*.assets.json`, `*.hull`, and `engine.cfg` if practical.
Each format has an integer version in its native encoding; current loads,
supported older versions upgrade through deterministic named steps, newer than
current fails as a Lane R error naming both versions, and writers stamp current.

Scene behavior is binding precedent and is not implementation scope here. Scene
documents retain their required `version` field, current version 2, and the
deterministic version 1 upgrade shipped by physics-authority C1b. This plan must
not rename that field, reinterpret missing scene versions as version 0, reset
scene history, or add a competing scene migration path.

## Scope decisions (binding)

- **Scene v1→v2 is the semantic policy.** Each format owns its version history
  and native field spelling, but follows the same explicit current/previous,
  deterministic-upgrade, future-rejection, and current-writer behavior.
- **Integer versions, one per format.** No semver strings and no per-field
  feature flags. New JSON version fields should use the existing `version`
  convention unless the owning format records a concrete compatibility reason
  for another spelling.
- **Missing version means legacy version 0 only for currently unversioned
  formats.** Assets, hulls, and configuration may load through a named v0→v1
  step on adoption day. Scenes are excluded because their version is already
  required and their history is v1→v2.
- **Migration window policy: current and current-1** are loadable for each
  format; older versions require the new `tools\migrate_data_formats.py`
  upgrader. Keeping every historical reader alive forever is rejected.
- **Committed data files are upgraded** to current in the same change that
  bumps a version, so the repo's own scenes/baseline fixtures never exercise
  the deprecated window.
- The completed runtime-shell parser split is the implementation boundary;
  extend the surviving format owners rather than reopening scene composition.
- **Critical-path position.** Asset/hull investigation may run independently,
  but schedule plan delivery after editor undo/redo. The `engine.cfg` portion
  waits for `engine-config-decomposition.md` so version plumbing targets the
  surviving parser/domain structure once.

## Phases

- [ ] V1. Policy + plumbing: add native integer version read/write to the asset
      loader/writer, hull load/bake path (`tools\bake_hulls.py` stamps on
      `--write`), and `engine.cfg` if practical. For formats adopting their
      first version, missing input is legacy v0 and upgrades deterministically
      to v1; future versions fail through Lane R with owner/message. Do not edit
      scene version semantics. Gate: `validate_full` for asset/hull/config rows.
- [ ] V2. Tests: add cases for each newly versioned format proving missing
      legacy v0 input upgrades, current input loads, future input produces a
      recoverable named failure, and round-trip writes stamp current. Retain the
      existing scene v1→v2 coverage as precedent evidence rather than replacing
      it with a v0 test. Gate: `validate_tests`.
- [ ] V3. Upgrader tool: `tools\migrate_data_formats.py` rewrites supported
      asset/hull/config inputs to current, is idempotent, and provides `--check`.
      It may dispatch to format-specific helpers but must not duplicate the
      scene parser's shipped v1→v2 logic. Restamp committed files. Gate:
      `validate_fast` for the tool, then `validate_full` for changed data.
- [ ] V4. Process rule: add one paragraph to `AGENTS.md` data rules — any
      schema change bumps the format version, adds the migration step,
      upgrades committed files, and extends the V2 tests in the same
      commit. Documentation-only.
- [ ] V5. Closure: comment audit on touched parser/tool files,
      `validate_full`, MASTER-PLAN/SessionState update, delete plan.

## Acceptance

- [ ] Assets, hulls, and configuration (unless V1 records why configuration is
      impractical) stamp and check an owned integer version.
- [ ] Scene documents still use required `version` v2, retain deterministic v1
      upgrade coverage, and have no new v0 or competing migration behavior.
- [ ] Every newly versioned format rejects future versions recoverably and
      upgrades its legacy unversioned input through a tested v0→v1 step.
- [ ] The repository's committed assets/hulls/config carry current versions;
      committed scene files remain on their already-current scene version.
- [ ] Physics CSV and DX12 screenshots unchanged (versioning is metadata).

## Validation map

| Slice | Gate |
|-------|------|
| Asset/hull/config loader changes | `validate_full` |
| Test additions | `validate_tests` |
| Tool scripts | `validate_fast` + run the changed script |
