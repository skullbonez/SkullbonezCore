# FP Envelope Hardening — Diagnose The Inline Flip, Pin Contraction, Re-Scope The Contract

Date: 2026-07-15
Status: Active — 0/4 tasks complete
Impact area: build flags/vcxproj for all four projects, persistent-contact
manifold evidence, determinism contract documentation, `AGENTS.md` danger-zone
wording
Owner: physics / build

## Problem And Evidence

The 2026-07-15 round-4 review found that `vector3-inline-hot-math` empirically
falsified its own "no float behavior change" premise:

1. The `TestPersistentContactSolver` edge-toppling fixture had to be moved off
   a float boundary in the same commit that inlined `Vector3`
   (`ff6e780e`, `SkullbonezTests/TestPersistentContactSolver.cpp:307-318`).
   The new fixture comment states that "tiny code-generation shifts select a
   four-point face" — i.e. inlining changed observable floating-point
   outcomes in at least one reachable configuration under `/fp:precise`.
2. The mechanism was never diagnosed. "Code-generation shifts" is an
   observation, not a root cause; without the root cause the right guard
   cannot be chosen, and the deferred padded-SIMD/SoA work will hit the same
   class of drift much harder.
3. The FP envelope has a standing gap: `/fp:precise` is pinned
   (`determinism-contract-hardening`, 2026-07-12), but there is no
   FP-contraction control anywhere — no `/fp:contract-`, no
   `#pragma fp_contract`, no `/arch` policy statement. On today's x64 SSE2
   default there is no FMA to fuse, but the first `/arch:AVX2` flip (or the
   planned SIMD work) would let MSVC contract `a*b+c` under `/fp:precise`,
   changing bits wholesale.
4. The determinism documentation implies unconditional byte-exactness. The
   demonstrated truth is narrower: bit-exactness is certified for identical
   binaries running gated content; codegen-affecting changes may flip
   knife-edge branches and are detected by the gates, not prevented.

Owner ruling 2026-07-15: execute layers 1-3 (diagnose, pin, re-scope docs)
only. Layer 4 — adding hysteresis/tolerance to manifold feature selection so
codegen noise cannot flip contacts — is explicitly deferred because it changes
physics and forces a full baseline regeneration; revisit when SIMD work forces
the question.

## Goal

The inline flip has a recorded root-cause diagnosis; FP contraction is pinned
off across every project so the envelope cannot drift when `/arch` or inlining
changes; and the determinism contract documentation states exactly what is
guaranteed, for which binaries, under which toolchain envelope.

## Non-Goals

- No solver/manifold behavior change and no hysteresis (deferred by owner
  ruling; record the deferral where the diagnosis lands).
- No `/fp:strict` (exception semantics are not needed and cost perf).
- No `/arch` upgrade and no SIMD work.
- No baseline refresh of any kind.

## Tasks

- [ ] T1 — Root-cause diagnosis. In a scratch/standalone test (not committed
      as a passing gate), restore the pre-ff6e780e fixture values
      (`edgeRotation 0.70f`, `edgeContactHeight 1.5f`), reproduce the branch
      flip, and identify the exact build configuration(s) where it occurs.
      Diff the disassembly of the manifold clip/feature-selection path before
      and after Vector3 inlining and classify the mechanism: FP contraction,
      intermediate-precision or vectorization difference, or a test-only
      build/config difference. Record the finding, config matrix, and
      disassembly evidence in a dated report under `Agentic/Reports/2026-07-15/`.
- [ ] T2 — Pin contraction. Apply `/fp:contract-` (v143) or an equivalent
      forced-include `#pragma fp_contract(off)` to all four projects
      (`SKULLBONEZ_CORE`, `SKULLBONEZ_MATHS`, `SKULLBONEZ_PHYSICS`,
      `SKULLBONEZ_TESTS`) in every configuration, with a comment naming this
      plan and the AVX2/FMA hazard. On SSE2 this must be a codegen no-op: the
      physics gate proves it byte-exact.
- [ ] T3 — Re-scope the contract documentation. Update the determinism
      envelope statement (the `determinism-contract-hardening` closure's
      living reference in `Agentic/Reference/physics-overview.md`) and the
      `AGENTS.md` "Physics determinism" danger-zone row wording to state:
      byte-exactness is certified per binary + toolchain envelope + gated
      content; inlining/compiler/flag/SIMD changes may flip knife-edge
      branches and are caught by gates; knife-edge fixtures must be
      constructed away from boundaries, and any fixture moved off a boundary
      must record the flip that motivated it. Fold the ff6e780e fixture edit
      into this framing retroactively.
- [ ] T4 — Final gates. Flag/pragma changes touch every TU:
      `tools\validate_full.bat` with the unchanged 44,401-line byte-exact
      physics baseline (this is the no-op proof for T2) plus
      `tools\validate_perf.bat` to confirm no perf movement. No baseline
      refresh authorized.

## Dependencies And Decisions

- Owner ruling 2026-07-15: layers 1-3 only; manifold hysteresis deferred, not
  rejected — the deferral and its trigger (SIMD lane) are recorded here.
- Independent of the other round-5 plans.

## Acceptance

- A committed diagnosis report names the mechanism and config matrix for the
  ff6e780e flip, or records a bounded negative result (cannot reproduce) with
  the exact attempts.
- Every project/configuration compiles with contraction pinned off; grep-proof
  plus a build-log line or vcxproj diff in the closure evidence.
- Documentation states the certified envelope; the unconditional wording is
  gone.
- `validate_full` passes with zero baseline changes.

## Validation

- `tools\validate_full.bat`, then `tools\validate_perf.bat`, output pasted at
  closure with an explicit no-baseline-change statement.
