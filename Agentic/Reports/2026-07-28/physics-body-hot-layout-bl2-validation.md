# Physics Body Hot Layout BL2 Validation

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Phase: BL2 — inert control-block alignment removal

## Implemented Scope

`PhysicsBodyStore.h` no longer applies `alignas(32)` to its 20 hot-list owner
objects and no longer suppresses MSVC warning C4324 around the class.

The change does not modify:

- the retained structure-of-arrays field set or order;
- the 20 separately allocated payloads;
- `PhysicsFixedList::STORAGE_ALIGNMENT` or aligned `operator new[]`;
- hot-field views, scalar reconstruction helpers, or stage consumers; or
- body records, live-prefix rules, handles, or allocation phases.

The useful contract remains that each `PhysicsFixedList` payload allocation
starts at a boundary of at least 32 bytes.

## Mechanical Proof

The focused search below returns no rows:

```powershell
rg -n "alignas\( 32 \)|C4324|warning\( (push|pop)" `
  SkullbonezSource/Physics/PhysicsBodyStore.h
```

The candidate header in the isolated validation worktree matched the main
worktree header byte-for-byte. No other tracked file changed in that worktree.

## Determinism And Build

`tools\validate_physics.bat` passed in 110.5 seconds:

- Profile and Debug builds succeeded with zero warnings and errors; and
- the committed Physics regression comparison remained byte-exact.

No Physics baseline was refreshed.

## Performance Comparison

`tools\validate_perf.bat` passed in 91.2 seconds. Each candidate capture contains
1,140 frames. Deltas compare the BL2 candidate with the clean BL0 witness on the
same machine and settings.

| Scene | BL0 P50 | BL2 P50 | P50 delta | BL0 P99 | BL2 P99 | P99 delta |
|---|---:|---:|---:|---:|---:|---:|
| 200 | 0.1187 ms | 0.1192 ms | +0.4% | 0.2020 ms | 0.2171 ms | +7.5% |
| 520 | 0.8179 ms | 0.8446 ms | +3.3% | 1.0453 ms | 1.0794 ms | +3.3% |
| 1,000 | 1.1580 ms | 1.1654 ms | +0.6% | 1.5884 ms | 1.5705 ms | -1.1% |
| 2,000 | 2.0764 ms | 2.1037 ms | +1.3% | 3.1065 ms | 3.0926 ms | -0.4% |
| sleepy-5,000 | 1.3354 ms | 1.3681 ms | +2.4% | 26.9538 ms | 27.1327 ms | +0.7% |

The small mixed-direction deltas show no meaningful or systematic performance
degradation from removing control-block padding. The sleepy-5,000 tail remains
an observation rather than a layout attribution. No performance baseline or
accepted artifact was refreshed.

## Comment Audit

Touched-source scope: `SkullbonezSource/Physics/PhysicsBodyStore.h`.

- Checked: 1/1
- Deferred: 0
- Unchecked: none

The learning header defines the SoA term, assigns payload alignment to
`PhysicsFixedList`, and accurately states that production consumers index the
streams scalarly. All `Related:` paths resolve.
