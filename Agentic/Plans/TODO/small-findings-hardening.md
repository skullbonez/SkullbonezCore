# Small Findings Hardening — Close The Round-7 Minor Red Flags

Date: 2026-07-18
Status: Active — 3/5 tasks
Branch: `nightrunner-17th-july` (owner-ratified at H0; never directly on `main`)
Impact area: `Core/LockOrderValidator.*`, `Rendering/DX12/*` (PSO cache
identity), targeted cast sites across `SkullbonezSource/`, JSON include
boundary, `UI/UITab*.cpp`
Owner: cross-cutting engine hygiene

## Problem And Evidence (measured 2026-07-18 at `nightrunner-17th-july` tip, 06a17ff31)

The 2026-07-18 hostile review (finding 7) lists surviving minor red flags.
The seven render consumer interfaces are **explicitly excluded** by owner
ruling (retained for future consumers) and are not in scope.

- `LockOrderValidator` is the last process singleton:
  `static LockOrderValidator& Instance()`
  (`Core/LockOrderValidator.cpp:114`) — mutable process-global state in an
  engine that banned the pattern everywhere else.
- `PSOKey12::rootSignature` is a `const void*` used as cache-key identity
  (`Rendering/DX12/RenderBackendDX12.h:144`). Pointer-as-identity in a
  cache key is a stale-reuse hazard if a root signature is ever destroyed
  and a new allocation lands at the same address.
- 153 `void*` / `reinterpret_cast` / `const_cast` occurrences across 43
  files in `SkullbonezSource/` have never received a consolidated
  per-site ruling (platform-API-required vs replaceable with typed
  boundaries).
- `nlohmann::json` (25,526-line header) is the engine's parser for scene,
  asset, replay, and automation data. `JSON_NOEXCEPTION` is enforced at one
  site, but nothing structurally prevents the header or JSON types from
  reaching hot/runtime translation units.
- `UI/UITabProfiler.cpp` (2,328 lines) and `UI/UITabMemory.cpp` (1,507
  lines) sit at or past the 2,000-line monolith threshold the round-6 TU
  campaign applied elsewhere; they never received rulings because the
  round-6 list was drawn from an older tip.

## Goal

Each finding gets a concrete fix or an explicit recorded owner ruling: the
singleton becomes an owned instance, PSO cache identity becomes stable and
generation-safe, the cast inventory is ruled site-by-site, the JSON
dependency is fenced to cold boundaries by enforcement rather than
convention, and the two unruled UI tab TUs receive the same cohesion
treatment as every other monolith.

## Non-Goals

- **No change to the seven render consumer interfaces** (owner ruling).
- No behavioral change; no baseline, golden, or screenshot refresh.
- No JSON library replacement — scope is boundary enforcement, not a new
  parser.
- No blanket cast rewrite; platform-required casts keep recorded reasons.

## Tasks

- [x] H0 — Rulings census. One dated table: the full 153-site cast
  inventory grouped by file with per-group proposed disposition; current
  `LockOrderValidator` call sites and its proposed owner; PSO-key identity
  design options (owner-issued stable id vs generation counter);
  JSON-include reachability map (which TUs transitively include
  `nlohmann/json.hpp`); UI tab TU measurements. Owner ratifies dispositions
  and branch. Evidence under `Agentic/Reports/`. Gate: none
  (documentation).
- [x] H1 — De-singleton `LockOrderValidator`. The validator becomes a
  startup-owned instance wired through the existing composition root;
  `Instance()` is deleted, not wrapped. Debug/Profile-only behavior and
  zero-cost Release posture are preserved and stated. Gate:
  `validate_fast` plus `validate_perf` (lock-path adjacency).
- [x] H2 — Stable PSO cache identity. Replace the `const void*`
  root-signature key field with the H0-ratified stable identity issued by
  the pipeline owner, so root-signature recreation can never alias a stale
  PSO. Gate: `validate_dx12_renderer` + `tools\run_graphics_stress.bat 1`
  per MASTER rule 10, with recorded evidence.
- [ ] H3 — Cast-site rulings applied. Execute the H0 dispositions:
  replaceable sites get typed boundaries; required sites (Win32/D3D12 ABI,
  aligned-storage internals) get a nearby `Why:` comment naming the
  constraint. The inventory ends with zero unruled sites. Gate:
  `validate_fast`; add the mapped gate for any physics/DX12 file actually
  touched (cumulative-row rule), including stress for DX12 edits.
- [ ] H4 — JSON boundary fence, UI tab rulings, and closure review. Add a
  static check (extend an existing `tools/` checker; no new ratchet
  categories) that fails when `nlohmann/json.hpp` is included outside the
  ratified cold-boundary TU list. Rule the two UI tab TUs under the
  round-6 cohesion standard: split along an owner seam or record the
  cohesion rationale. One independent review confirms every H0 disposition
  landed or carries a recorded ruling. Final gates: `validate_fast`, then
  the changed tool script per the tools mapping; `validate_full` if UI tab
  splits touched runtime wiring. Update MASTER-PLAN, SessionState, and
  delete this plan on closure.

## Dependencies And Decisions

- Runs **after** `dx12-backend-ownership-decomposition` (H2 rebases on the
  finalized pipeline-owner surface).
- H0 owner decisions: PSO identity mechanism; the ratified JSON
  cold-boundary TU list; UI tab split-vs-cohesion rulings.
- The JSON fence is a quality gate on include reachability, not a banned
  frozen-count ratchet; H0 records that governance framing.

## Acceptance

- No `Instance()`-style process singleton remains in `SkullbonezSource/`.
- PSO cache keys contain no raw pointers.
- The cast inventory has zero unruled sites; every retained cast names its
  constraint.
- The JSON include fence runs in the mapped validation path and passes;
  hot/runtime TUs cannot silently regain the include.
- Independent review clear; all mapped gates pass from final source with
  zero baseline refresh.

## Evidence

- H0: `Agentic/Reports/2026-07-18/small-findings-h0-rulings-census.md`
  reconciles all 153 sites across 43 files, the sole executable validator
  caller, 19 transitively JSON-reachable TUs, both UI TU measurements, and the
  ratified startup-owner / pipeline-identity / JSON-fence / UI-split decisions.
  Documentation only; no repository validation required.
- H1: `Agentic/Reports/2026-07-18/small-findings-h1-lock-validator-ownership.md`
  records explicit startup/test ownership, the fixed Debug graph, deletion of
  the obsolete allocation exception, and clean final fast/perf gates. Comment
  audit: `Agentic/Reports/2026-07-18/small-findings-h1-comment-audit.md`
  (10/10 checked, 0 deferred).
- H2: `Agentic/Reports/2026-07-18/small-findings-h2-stable-pso-identity.md`
  records owner-issued monotonic identity, PSO-before-signature teardown, zero
  raw pointer identity, clean renderer validation, and the bounded one-minute
  stress proof. Comment audit:
  `Agentic/Reports/2026-07-18/small-findings-h2-comment-audit.md` (3/3 checked,
  0 deferred).
