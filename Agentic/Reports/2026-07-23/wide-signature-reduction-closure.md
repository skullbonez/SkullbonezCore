# Wide Signature Reduction Closure

Date: 2026-07-23
Owner: skullbonez
Plan: `wide-signature-reduction` W0-W4
Branch: `nightrunner-22nd-JUL-26`

## Result

The campaign is complete. The repeatable tracked-source inventory starts at
301 signatures with at least seven parameters and ends at 285. Every one of
the 16 rows ruled as a defect was removed through a named bounded domain value
or typed policy. The final 285 survivors reconcile exactly to the W1 owner
table: 159 intentional transaction boundaries and 126 accepted-with-reason
rows, with zero missing rulings and zero defect survivors.

No context bag, service bag, callback pack, compatibility alias, hidden owner
reference, or broad mutable authority was introduced. No behavioral baseline,
golden, screenshot, Replay artifact, or physics CSV was refreshed.

## Inventory And Rulings

- Scanner: `tools/inventory_wide_signatures.py`
- W0 inventory: `Agentic/Reports/2026-07-22/wide-signature-w0-inventory.md`
- W1 rulings: `Agentic/Reports/2026-07-23/wide-signature-w1-rulings.md`
- W4 final scan: 285 rows, threshold 7, scanner self-test PASS.
- W4 reconciliation: 285/285 signatures matched the W1 table; 159
  `intentional-transaction-boundary`, 126 `accepted-with-reason`, zero missing,
  zero surviving `missing-domain-value-record` or `flag-policy-value` rows.

The W4 scan uses JSON for the machine reconciliation. The W1-only Markdown
renderer classifies the original defect-family area and is not the final
survivor-report format; the authoritative row reasons remain the committed W1
table.

## Refactored Families

| Wave | Family | Rows removed | Replacement |
|---|---|---:|---|
| W2 | UI input | 2 | `InGameUIInputFrame` |
| W2 | Physics restore | 2 | `PhysicsBodyRestoreState` |
| W3 | DX12 instanced mesh | 3 | `InstancedMeshCreateDesc` plus private upload target |
| W3 | DX12 texture policy | 2 | `Texture2DUploadDesc` with typed mip/filter policy |
| W3 | Raster construction | 1 | existing `RasterStateDesc` value |
| W3 | Replay diagnostics | 6 | three bounded Replay diagnostic schemas |

The introduced values have one synchronous writer/consumer lifetime and do
not retain subsystem owners. Physics owns its restore record; Rendering owns
its upload/resource records; UI owns its input-frame value; Runtime diagnostics
owns its log schemas.

## Commit Ledger

- `cf314314` — inventory wide signatures
- `c93056c1` — rule wide signature inventory
- `c92cb781` — replace wide UI input signature
- `1b1eee9b` — replace wide physics restore signature
- `8521f116` — replace wide DX12 resource signatures
- `d8c6f969` — narrow Replay diagnostic logging schemas

## Validation

Final-source evidence:

| Command | Time | Result |
|---|---:|---|
| Inventory self-test | same-tip preflight | PASS |
| W4 final JSON scan | 27.1 s | PASS; 285 rows |
| W4 ruling reconciliation | 0.4 s | PASS; 285/285 ruled, zero defect survivors |
| `tools\validate_full.bat` (W4 closure) | 102.49 s | PASS |
| `tools\validate_replay_allocation_policy.bat` | 4.20 s | PASS |
| `tools\validate_replay_v2_artifact.bat` | 33.32 s | PASS |
| `tools\validate_replay_visual_fidelity.bat` | 426.94 s | PASS; 2,401 ticks, all controls |
| `tools\validate_replay_scrub.bat` | 426.16 s | PASS; all controls |
| `tools\run_graphics_stress.bat 1` | 60.95 s | PASS; PID-scoped crash-free run |

`validate_full` passed its CPU/coverage umbrella and five engine processes,
reported zero build warnings/errors, zero DX12 validation errors, accepted all
three committed screenshot comparisons, and reproduced the 44,401-line physics
CSV byte-exactly. Exact dependency-direction and downward-Replay-include proofs
returned no rows. Replay allocation validation found no new or expanded growth
privilege.

Comment-quality audits covered every touched source-bearing file: W0/W1 tool
1/1, W2 UI 5/5, W2 Physics 8/8, W3 DX12/resource 16/16, and W3 Replay
diagnostics 7/7; zero files were deferred.

## Independent Review

The first read-only W4 review verified the 285-row reconciliation and all W2/W3
record lifetimes, with no bag or hidden-ownership finding. It initially blocked one W0
documentation citation: the report attributed the corrected 301-row count to
the earlier 28.48-second/303-row log. The W0 verification section now cites the
actual 26.71-second `w1_inventory_correction.log` and explicitly marks the
303-row artifact as superseded. The reviewer inspected that exact correction
and returned PASS with no remaining blocker.
