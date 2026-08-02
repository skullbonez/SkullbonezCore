# Look Lab Random Style Authoring - LL2 Serialization

Date: 2026-08-01
Branch: `nightrunner-1st-AUG-26`
Plan: `Agentic/Plans/TODO/look-lab-random-style-authoring.md`
Phase: LL2 - complete

## Decision

Scene now owns `StandaloneStyleSnapshot` and `StandaloneStyleWriter`, the exact
schema-v1 standalone-style value and serialization boundary. The writer accepts
detached resolved cinematic values plus ordered material rules and includes no
Runtime owner. It always emits `format`, `version`, the complete LL0 eighty-atom
`cinematic` object, and complete `objectMaterials` payloads. It emits no include,
seed, recipe, timestamp, or generator metadata; those transaction facts remain
in the bundle name and human-readable receipt.

The output vocabulary remains the current parser vocabulary. In particular,
the renamed in-memory sun angles serialize as `sunScreenX`/`sunScreenY`, style
vectors keep their existing array spellings, shadow values use the existing
eight schema fields, and material kinds use canonical text such as `textured`,
`metal`, and `pine`. No schema version or parser compatibility surface changed.

## Exact Serialization Contract

`nlohmann::ordered_json` owns stable insertion order and shortest
round-trippable float text. The writer validates all finite cinematic values,
every schema-v1 scalar range, shadow map/radius bounds, material text
termination, typed kind, unit fields, nonnegative emission, flags, and legacy
compatibility mode before publication. The serialized text ends with one
newline.

Seed `0x0123456789abcdef` pins standalone JSON FNV-1a fingerprint
`0xc31502d6333e241b` in both Debug and Profile. The focused test writes the
candidate, loads it through `AuthoredScene::TryLoadStyleFromFile`, verifies the
complete override mask `0..62` excluding reserved bit 55, reconstructs a
snapshot only from production parser outputs, and obtains byte-identical JSON.
This proves all cinematic groups and all three complete material rules survive
serialize/parse, including stable key order and float formatting.

LL2 also corrected the LL1 textured-material compatibility payload. Canonical
`"textured"` reparses to the established `-1` legacy sentinel, so the generator
now stores `RenderMaterialKindLegacyMode(kind)` for every material. The fixed
candidate fingerprint moved from `0xa4f9bcb64731071d` to
`0xc3b6fad6b7b4defa`; no recipe, draw, range, or random stream changed.

## Atomic Artifact And Bundle Contract

Core now provides one cold `WriteTextFileAtomic` primitive. It creates missing
parents, reserves a unique same-directory temporary file with `CREATE_NEW`,
writes and flushes all bytes, then uses `MoveFileEx` with replacement and
write-through flags. A failed write or rename deletes only its temporary
sibling; the old destination remains intact. Recoverable path failures return
bounded `SbDiagnosticStore` messages.

Runtime Direction owns `LookLabBundleWriter`, not JSON construction. It:

- exclusively creates
  `<YYYY-MM-DD_HH-mm-ss>_seed_<16-lowercase-hex>` beneath the supplied Look Lab
  root and rejects an existing directory as a collision;
- resolves the fixed `look.style.json`, `look.txt`, and `look.png` siblings;
- combines timestamp/UTC offset, seed/version/recipe, source scene identity,
  fixed output names, independent style/screenshot status plus diagnostics,
  and Scene's derived complete flattened listing into receipt version 1; and
- atomically publishes pending or final receipt revisions without treating the
  receipt as a reload parser.

The repository root now ignores `/LookLab/`. Tests cover nested root creation,
exact lowercase seed names, collisions, style replacement, temporary cleanup,
invalid values, directory-as-file failure, invalid timestamps, newline-bearing
metadata, unterminated bounded text, and pending-to-failed receipt revision.

## Ownership Review

`LookLabReceiptFacts` is a cohesive immutable receipt-revision value, not a
service/context bag. Timestamp and offset, generator identity, source-scene
identity, and each artifact's status/diagnostic pair must be consumed together;
splitting them would permit a receipt to combine a candidate with another
transaction's status. The exact current retain ruling is in
`tools/aggregate_ownership_rulings.json`, and
`TestLookLabSerialization.cpp` proves the invariant.

Nine generator, serialization, and bundle operations remain intentionally
test-only or own-TU-and-test-only until LL3/LL4 wire the live controller and F11
transaction. Exact `repair-plan` rows in `tools/reachability_rulings.json` name
the active Look Lab plan. LL3/LL4 own removing those rows as each real production
call path arrives; the rows are current evidence, not an allowance.

## Evidence

- Profile x64 focused Look Lab tests: 7 cases, 4,165 assertions, all pass.
- Debug x64 focused Look Lab tests: 7 cases, 4,165 assertions, all pass with the
  same candidate and JSON fingerprints.
- Project-filter ownership: 800/800 production items and 127/127 focused test
  items, zero errors; the Scene serializer is a ratified cold JSON boundary.
- Dependency proof and repository scan: 27 include rules, one content rule, one
  project rule, zero findings.
- Build-configuration consistency: self-test and direct scan pass; 1,701
  compile rows, 132 current reviewed divergence pairs, zero dropped inheritance,
  and zero blocking diagnostics.
- Strict reachability after synchronized Automation, Debug, and Profile builds:
  88 current rows, all ruled, zero diagnostics.
- Aggregate ownership: 86 gated current aggregates, 86 ruled, zero unruled.
- Strict glossary inventory: 578 files, 969 unique definitions, zero duplicate,
  drifted, ruled, or unruled multi-file terms.
- `tools\validate_fast.bat`: pass in 379 seconds.
- `tools\validate_full.bat`: pass in 591.1 seconds from final source.
- Touched-source comment audit: 10/10 inspected, zero deferred
  (`AtomicTextFileWriter.h/.cpp`, `StandaloneStyleWriter.h/.cpp`,
  `LookLabBundleWriter.h/.cpp`, `LookLabGenerator.cpp`,
  `TestLookLabGenerator.cpp`, `TestLookLabSerialization.cpp`, and
  `validate_project_filters.py`). This was a touched-file pass, so no subsystem
  checklist plan was required.
- No style, scene, configuration, screenshot, Physics, Replay, DX12, or other
  tracked baseline changed.
