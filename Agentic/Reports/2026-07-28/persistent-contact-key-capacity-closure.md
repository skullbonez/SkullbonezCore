# Persistent Contact Key Capacity Closure

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26-takeover`
Ledger effect: none; this was the protected final local action outside the
0/0 plan ledger.

## Decision

Accept and commit the warm-start key-capacity guard.

`PERSISTENT_CONTACT_BODY_MASK` now owns the existing `0x7fff` key-schema value
shared by solver packing and narrowphase cache-prefix lookup. The runtime
arithmetic and bit layout are unchanged:

- feature id: bits 0-31;
- second body index: bits 32-46;
- first body index: bits 47-61;
- terrain discriminator: bit 62.

`MAX_SCENE_OBJECTS` is 8,192, so every valid index from 0 through 8,191 fits
the 15-bit body fields. A compile-time assertion now fails if scene capacity
outgrows that key contract. Physics may depend on Core, so the
`Core/SceneCapacity.h` include preserves dependency direction.

## Acceptance Evidence

| Dimension | Evidence | Result |
|---|---|---|
| Contact stability | `tools\validate_physics.bat`; standalone lifecycle/runtime-handle smoke; `physics_regression_varied.csv` 44,401-line byte-exact match | Pass in 112.4 s |
| Physics-frame time | `tools\validate_perf.bat`; DX12 `Frame/Physics` 0.2204 ms average and focused Physics bench 0.0731 ms average | Absolute budgets pass; both comparisons report `No regressions` |
| Inclusive solver time | DX12 `Frame/Physics/Narrowphase/PersistentContacts` 0.0253 ms average; focused Physics bench 0.0271 ms average | Performance gate accepts both runs in 90.8 s |

The current-vs-committed-reference averages were deliberately retained as
evidence rather than normalized away:

| Scenario | Marker | Baseline avg (ms) | Current avg (ms) | Delta |
|---|---|---:|---:|---:|
| DX12 | `Frame/Physics` | 0.2042 | 0.2204 | +7.9% |
| DX12 | `PersistentContacts` | 0.0218 | 0.0253 | +16.1% |
| Physics bench | `Frame/Physics` | 0.0652 | 0.0731 | +12.1% |
| Physics bench | `PersistentContacts` | 0.0244 | 0.0271 | +11.1% |

The comparator's ramped threshold is `max(10%, 10/sqrt(ms))` per marker.
Both artifacts remain far below their absolute budgets and the formal gate
classified neither scenario as a regression. No Physics, performance, Replay,
DX12, golden, config, schema, or other baseline was refreshed.

Validation logs:

- `TestOutput/validation/persistent_contact_key_capacity_validate_physics.log`
- `TestOutput/validation/persistent_contact_key_capacity_validate_perf.log`

The first Physics shell wrapper returned after five seconds while its child
MSBuild process finished; no result was claimed from that interrupted wrapper.
The exact gate was then rerun to completion and produced the passing result
above.

Additional checks:

- `git diff --check`: pass.
- `tools\validate_format.bat`: 571 source files and 317 headers pass.
- `tools\validate_dependency_graph.bat`: 27 include rules, one content rule,
  one project rule, zero findings.
- `python tools/check_related_paths.py`: 571 files, 1,519 repository paths,
  zero findings.
- Authority-free aggregates: 85 gated, 85 ruled, zero unruled.
- Extraction scars: one ruled, zero unruled.
- Wide signatures: every signature at or above 12 parameters has a current
  ruling; the unchanged solver `Solve` operation retains its stage-owner ruling.

## Independent Review

Run `persistent-contact-key-capacity-duck-01` returned **CLEAR** with zero
blockers. It confirmed unchanged deterministic behavior, correct bit layout,
permitted Physics-to-Core direction, honest key-schema ownership, sufficient
compile-time plus byte-exact proof, and no aggregate, capability-slice,
extraction-scar, rename-evasion, or false-comment issue.

| Reviewer | Start | End | Elapsed | Prompt chars | Response chars | Tokens |
|---|---|---|---:|---:|---:|---:|
| Copernicus (`dependency_proof_duck_01`) | 2026-07-28 23:27:05 +10:00 | 2026-07-28 23:30:10 +10:00 | 3m 5s | 1,206 | 1,592 | n/a |

The non-blocking suggestion to add a second assertion for bit 62 was not
adopted: the exact `0x7fff` mask, local bit-layout invariant, and capacity
assertion already pin the intended contract without adding a duplicate
condition.

## Comment Audit

All 3 touched source-bearing files were inspected against
`Agentic/Skills/comment-style-audit/skill.md`: 3 checked, 0 deferred.
Learning headers remain valid, the shared header states the capacity and
bit-layout invariants, the solver explains warm-start key packing, and the
narrowphase helper documents why it mirrors the object/object prefix.

## Closure

The protected working-tree experiment is accepted. The plan ledger remains
empty at 0/0, and no further local action remains for the Principal Engineer
Feedback Campaign.
