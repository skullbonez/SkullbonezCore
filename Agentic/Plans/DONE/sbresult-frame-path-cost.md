# SbResult Frame Path Cost

Date: 2026-07-26
Status: COMPLETE — SR0-SR2 closed on 2026-07-27. Sentinel-only success
construction preserves the complete inline failure representation; corrected
census, performance, full validation, and independent review are clear. Drafted from the
2026-07-26 from-source architecture
review of `nightrunner-26th-JUL-26` at tip `35f6de4e`. Registered in
`MASTER-PLAN.md` on 2026-07-26 as plan 12 of the Architecture Follow-Up Campaign
Round 5. 3/3 phases complete.
Impact area: `Core/SbResult.h`, `Runtime/App/Run.{h,cpp}`,
`Runtime/App/RunFrame.cpp`, every Lane R return site
Owner: core
Closure evidence: `../../Reports/2026-07-27/sbresult-frame-path-cost-closure.md`
Priority: Low-Medium — not a bug and not a measured hot spot. It is a
consistency defect: the one value type on the per-frame path that ignores the
engine's own cost discipline.

## Problem And Evidence (measured 2026-07-26)

`Core/SbResult.h:36-68` defines the Lane R carrier:

```cpp
struct SbError  { const char* owner = ""; char message[512] = {}; };
struct [[nodiscard]] SbResult { bool ok = true; SbError error; };
```

With 8-byte alignment for `owner`, `sizeof(SbResult)` is 528 bytes. Every
success return constructs and copies 528 bytes — 520 of which are a
zero-initialised message buffer — to carry `ok = true`.

Three of those return sites are on the per-frame path:

- `Run::PresentFramePhase()` — `Runtime/App/Run.h:216`, called once per frame at
  `RunFrame.cpp:864`.
- `Run::RenderOperatorUiPhase(...)` — `Run.h:208`, called once per frame at
  `RunFrame.cpp:849`.
- `Run::Execute()` — `Run.h:256`, once per process, not a concern.

`SbResult::Success()` returns `{}`, so the 512-byte buffer is zero-initialised on
every successful frame. The struct is returned by value, and both frame-path
callers immediately test `.ok` and discard the rest
(`RunFrame.cpp:850-852`, `:865-868`).

This is a real inconsistency rather than a real cost: the engine bans
`std::function`, holds zero heap allocations in steady gameplay, uses SoA hot
arrays, and pins floating-point contraction — and then pays a fixed 528-byte
copy per frame to report success. It is also the kind of detail a reader uses to
calibrate how seriously the surrounding cost discipline is meant, which is why it
is worth correcting even though no profile flags it.

The design is otherwise right and must be preserved: `[[nodiscard]]` so silent
discard is a diagnostic (`SbResult.h:18`), bounded inline message with no heap
ownership (`:16-17`), and `const char* owner` pointing at static strings.

## Goal

The success path costs what success costs. The failure path keeps its bounded
inline diagnostic message, `[[nodiscard]]` enforcement, and zero heap ownership.

## Non-Goals

- No exceptions, `std::expected`, `std::optional<std::string>`, or any heap
  ownership. The Error Handling Policy bans exceptions and the header bans heap
  ownership; both stand.
- No loss of `[[nodiscard]]`. Silent discard must remain a compiler diagnostic at
  every call site.
- No loss of message detail on the failure path. A failure must still carry owner
  plus a bounded formatted message, and the message must remain readable at the
  UI/log boundary without a lifetime hazard.
- No global `SbResult`-to-error-code conversion. Lane R stays a value-carrying
  result; this plan changes its representation, not the lane.
- No behavior change on any failure path. Every existing failure message must be
  byte-identical in the log and in automation reports.
- No change to Lane F (`SB_FATAL`) or Lane P (probe assertions).

## Phases

- [x] **SR0 — Census return sites and rule the representation.**
  Inventory every `SbResult`-returning function, classify each as per-frame,
  per-scene-load, or cold, and record which callers read `error` at all. Then rule
  the representation with evidence. Candidates to evaluate, not a predetermined
  answer:
  - **Shrink the buffer.** Measure the longest message any current
    `SbResult::Failure` call site can actually produce and size the buffer to the
    real maximum rather than 512. Cheapest change, preserves every property, and
    may be sufficient on its own.
  - **Split success from failure.** Success becomes an empty or single-byte value
    and the bounded message lives in owner-side storage the failure path fills, so
    only failures pay. Must not introduce a lifetime hazard — the reason the buffer
    is inline today is that a `const char*` into caller storage would dangle.
  - **Leave it and document it.** If SR0's measurement shows the per-frame cost is
    genuinely unobservable, the honest outcome is an explicit ruling recorded in
    the header rather than a refactor. This is a legitimate result and must not be
    treated as a failed phase.
  Acceptance: the ruling names one representation with measured evidence — the
  real maximum message length and a before-measurement from `validate_perf` — and
  states why the rejected options were rejected.
  Closed 2026-07-27. The reconciled census found 176 named definitions plus one
  trailing-return lambda: 57 frame-reachable callables, 51
  scene-load/resource-build, and 69 cold/on-demand. The
  protected real maximum is all 511 payload bytes, so the buffer cannot shrink.
  The selected representation retains the 528-byte inline carrier but initializes
  only the empty owner and leading message sentinel on success. Evidence:
  `../../Reports/2026-07-27/sbresult-frame-path-cost-sr0-census.md`.

- [x] **SR1 — Implement the ruled representation.**
  Apply SR0's ruling across every return site. If the outcome is "leave it and
  document it", SR1 is the header amendment plus the frame-path note, and the plan
  closes at 2/3 with that recorded as the deliberate result. Acceptance: every
  failure message is byte-identical to before in logs and automation reports;
  `[[nodiscard]]` holds at every site; no heap ownership introduced; no new
  aggregate created to carry the result.
  Closed 2026-07-27. `SbError` default construction writes only `owner = ""`
  and `message[0] = '\0'`; `Failure` retains the same inline `vsnprintf` path.
  Tests pin the 512-byte failure capacity and 528-byte Win64 carrier.
  `validate_tests` and `validate_perf` pass with no measured regression.
  Evidence:
  `../../Reports/2026-07-27/sbresult-frame-path-cost-sr1-closure.md`.

- [x] **SR2 — Reconcile, review, and hand off.**
  Publish the before/after `sizeof(SbResult)` and the per-frame measurement.
  Complete the comment audit for `Core/SbResult.h` — its Invariants block must
  describe the final representation, and the header should state the frame-path
  ruling so the next reader does not re-litigate it. Obtain one independent review
  asking: can any failure message now dangle, did any call site lose
  `[[nodiscard]]`, and is any message truncated relative to before. Acceptance:
  review clear; `validate_perf.bat` shows no regression and the recorded
  improvement if any; `validate_full.bat` passes with every Lane R failure message
  unchanged.
  Closed 2026-07-27. The first independent review blocked an incomplete census;
  all 15 missed named definitions and the trailing-return lambda were added.
  Repeat review passed with zero blockers. The 528-byte before/after carrier,
  final performance measurements, comment audit, and full gate are recorded in
  `../../Reports/2026-07-27/sbresult-frame-path-cost-sr2-closure.md`.

## Dependencies And Decisions

- No dependency on the other campaign plans; nothing depends on this one.
- Sequence after `runtime-frame-view-retirement` if both are run, because that
  plan changes the frame-phase signatures that return `SbResult`. Running this
  first would touch the same lines twice.
- Owner-overridable default, agent does not stop: whether a 528-byte
  per-frame copy is worth changing at all. SR0 produces the measurement; "no
  change, documented" is an acceptable and explicitly permitted outcome. Do not
  refactor a hot-path value type on aesthetics alone.
- **Ratified 2026-07-27: run after plan 5.** This is a sequencing wait, not a
  design blocker. Once the frame-view signatures settle, SR0 begins with the
  measured census; "no change, documented" remains an acceptable outcome.

## Acceptance

- `sizeof(SbResult)` and the per-frame cost are measured and recorded.
- Either the representation is improved with every property preserved, or the
  current representation is explicitly ruled and documented in the header.
- Every Lane R failure message is byte-identical; no lifetime hazard introduced.
- `[[nodiscard]]` enforcement intact everywhere.

## Validation

- `tools\validate_perf.bat` — per-frame value cost is the subject of the plan.
- `tools\validate_tests.bat` — Lane R result coverage.
- `tools\validate_full.bat` — `Core/SbResult.h` is included at 41 sites and
  `Run*`/`Runtime/*` change; required at the closure gate.
