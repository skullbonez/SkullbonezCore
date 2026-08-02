# Look Lab Random Style Authoring — LL4 Input And Capture

Date: 2026-08-01
Plan: `Agentic/Plans/TODO/look-lab-random-style-authoring.md`
Phase: LL4 complete

## Outcome

F10 and F11 now enter the live Look Lab workflow as explicit
keyboard-unblocked press actions. F10 chooses a new 64-bit authoring seed,
resolves one coherent candidate, and applies its detached standalone snapshot.
F11 begins one token-owned bundle transaction for the currently applied
candidate. Existing F5 performance-histogram and F6 memory-overlay bindings are
unchanged and remain pinned by exact table tests.

Runtime Direction owns the transaction state, exact style snapshot, bundle
directory, pending receipt, and bounded status. Capture owns a separate
fixed-capacity typed post-render request queue. App only sequences those owners:
it collects timestamp and scene facts, submits one capture token, drains the
request after world/UI drawing and before Present, and returns the capture result
to the token-matched Look Lab transaction.

## Transaction And Failure Integrity

An accepted F11 action reserves one collision-safe ignored root directory, then
writes `look.style.json` and an atomic pending `look.txt` before queuing
`look.png`. No candidate means no directory. While a save is pending, another
F11 and every F10 reroll are rejected, so the screenshot cannot diverge from the
serialized candidate and one accepted action cannot duplicate a directory,
write, or capture.

Capture converts the padded bottom-up BGR readback into a top-down RGB PNG with
validated signature, IHDR, IDAT, and IEND chunks. Success atomically revises the
receipt to final status. A screenshot failure preserves the reusable style and
records partial failure; scene transition or shutdown cancels the matching
Capture owner/token and atomically records cancellation. Mismatched or stale
tokens cannot finalize another transaction.

Operator UI consumes a detached bounded status value containing the seed,
fingerprint, pending/final state, detail, and bundle path. It receives no
filesystem, Capture, Scene, or controller pointer.

## Focused Proof

Debug and Profile solution configurations, plus Automation, build with zero
warnings or errors. In both Debug and Profile:

- `*Look Lab*`: 10 cases and 4,257 assertions pass;
- `*typed post-render PNG*`: 1 case and 12 assertions pass;
- `*PNG encoder*`: 1 case and 6 assertions pass; and
- `Runtime input bindings:*`: 4 cases and 1,149 assertions pass.

The transaction matrix covers F11 before F10, pending/final/partial/cancelled
receipt publication, duplicate save and reroll suppression, token mismatch,
collision-safe naming, screenshot failure, scene/shutdown cancellation seams,
seed variation, and exact detached UI fingerprinting. The PNG test proves
orientation, channel order, row-padding handling, and invalid-input rejection.

## Governance And Comment Audit

- Dependency proof/repository scan: 27 include rules, one content rule, one
  project rule, zero findings.
- Project filters: 802 project items and 802 filter items, zero errors.
- Build configuration: 1,714 compile rows, 67 shared sources, 134 ruled
  divergences, zero dropped inheritance, zero blockers.
- Function complexity: 40/40 triggered bodies ruled.
- Authority-free aggregates: 88/88 gated rows ruled.
- Wide signatures: every operation at the 12-parameter review trigger has a
  current qualitative ruling.
- Extraction scars: the one pre-existing WorkerPool row remains ruled.
- Glossary: 586 files, 989 unique definitions, zero duplicates or drift.
- Reachability: 79 current rows, all ruled, zero blocking diagnostics. The ten
  temporary LL2/LL3 Look Lab repair-plan rulings were deleted because LL4 gives
  their operations real production roots. The unnecessary candidate accessor
  was removed instead of manufacturing a test-only ruling.
- Formatting passes for all 586 source files and every repository-relative
  `Related:` path resolves.
- `tools\\validate_fast.bat` passes in 397.5 seconds and
  `tools\\validate_full.bat` passes in 582.0 seconds. The full gate completes
  its CPU, Automation, DX12, and deterministic Physics lanes without refreshing
  a tracked baseline.

The required touched-source comment audit inspected 19/19 source-bearing files
with zero deferred or unchecked files. Each has the required learning header;
the risky save-token lifetime, pending transaction, post-render ordering, PNG
encoding, detached UI status, and scene-transition cancellation rules also have
nearby ownership/invariant/hazard comments. This is a touched-file audit, so no
subsystem checklist file is required.

## Phase Boundary

LL4 deliberately does not claim distribution breadth, idle-cost proof, or
fresh-process reuse. LL5 owns the large deterministic census, fresh-process
style reapplication, curated-style compatibility, and idle input/render cost
measurement. LL6 owns visible DX12 validation and final independent review.
