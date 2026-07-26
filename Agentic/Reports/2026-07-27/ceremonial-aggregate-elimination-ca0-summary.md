# Ceremonial Aggregate Elimination CA0 Summary

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Source parent: `c3d4fe80`

## Outcome

CA0 is complete. The current-tip census covers every mechanically signalled
borrowed-member courier and every legacy `*Context`, `*Input(s)`, `*Params`,
`*Args`, `*Request`, `*Facts`, `*Operands`, `*Services`, `*Bindings`, and
`*Bag` review row. The `*Request` and `*Facts` families were missing from the G2
legacy context; CA0 restored them and planted a `WidgetRequest` self-test so
`SceneGeneratedPopulationRequest` and equivalent future values cannot silently
leave the census.

The complete 119-row table, including definition, member declarations, every
lexical construction and consumer site, entry-destructuring judgement, verdict,
owner reason, and removal endpoint, is
[ceremonial-aggregate-elimination-ca0-census.md](ceremonial-aggregate-elimination-ca0-census.md).
It is reproduced from current source and the ruling file by:

```text
python tools/inventory_authority_free_aggregates.py --repo . --format markdown
```

## Rulings

| Verdict | Rows | Disposition |
|---|---:|---|
| `remove` | 35 | CA1: 8 UI command contexts; CA2: 9 scene contexts/requests; CA3: 18 editor, diagnostics, input, capture, assets, render, and replay rows |
| `retain` | 62 | Current domain values, requests, facts, behavior owners, and concrete phase boundaries with a named invariant |
| `retain-prior` | 22 | PB0/GV1, render-graph, render-pass, UI boundary, and wide-signature rulings carried forward |

There are zero unruled rows and zero stale ruling rows. Each ruling pins the
current definition site and member count, so source drift fails the gate.
Every removal row records `yes`, `no`, or `mixed` for entry destructuring and a
concrete post-removal signature. The largest conservative expansion is
`ApplyRuntimePresentationUICommands` at 10 parameters; all operation endpoints
remain below the accepted 12-parameter ceiling. `ReplayTimelineCaptureResult`
returns its pointer directly and `RenderReplayOverlayView` becomes a direct
frame reference, so neither changes input arity.

## Inventory Contract

`tools/inventory_authority_free_aggregates.py` now:

- gates the full review surface rather than only the strongest ten signals;
- rejects missing, stale, malformed, site-drifted, or member-count-drifted
  rulings;
- requires every `remove` row to name its post-removal signature;
- emits JSON with the joined ruling and deterministic Markdown containing the
  complete source-site table;
- self-tests renamed/class couriers, claim-only comments, behavior owners,
  scalar values, and the legacy `*Request` review family.

The machine table reports lexical sites deliberately. It does not claim C++
overload resolution; the owner judgement is explicit in
`tools/aggregate_ownership_rulings.json`.

## Validation

- Aggregate inventory self-test: PASS.
- Current-tip inventory: 1,207 candidates; 10 signalled; 119 review rows; 119
  ruled; 0 unruled.
- Ruling JSON parse and exact-site/member contract: PASS.
- Exact Markdown regeneration: PASS, byte-identical.
- `tools\validate_fast.bat`: PASS. Formatting, 1,516 `Related:` paths,
  project filters, dependency rules, both ownership inventories, file sizes,
  Profile/Debug builds, and all 415 doctests / 2,409,555 assertions passed;
  builds reported zero warnings and zero errors.
- `git diff --check`: PASS.

CA1 is the binding next task.
