# Determinism Contract Hardening Closure

Date: 2026-07-12
Plan: `determinism-contract-hardening` — 4/4 complete
Branch: `nightrunner-12th-july`

## Outcome

All x64 Debug, Profile, Release, and Profile-WPO configurations in the core,
maths, physics, and test projects now explicitly select MSVC's precise
floating-point model. The 16 project-file entries encode the existing
`/fp:precise` behavior rather than relying on the compiler default.

The complete worker-pool call-site audit found no per-chunk numeric reduction
whose result changes with worker count. Parallel work either writes independent
per-item slots, appends outputs in a stable serial order, or computes local
rendering data whose ownership and merge order do not cross chunk boundaries.
No validation worker-count pin is therefore required. The detailed inventory
is in
`Agentic/Reports/2026-07-12/determinism-chunk-accumulation-audit.md`.

## Contract

`Agentic/Reference/physics-overview.md` now defines the supported determinism
envelope as Windows x64 with MSVC v143, explicit `/fp:precise`, fixed-step
ordering, and the audited thread-order result. A change to any input requires
baseline regeneration and the matching validation evidence in the same commit.

## Isolation And Validation

The work ran in an isolated commit window with no concurrent physics, config,
or baseline edits. No baseline was regenerated and no runtime behavior changed.

- `tools\validate_tests.bat` — passed in 2.19 seconds: 173 test cases and
  3,964 assertions.
- `tools\validate_physics.bat` — passed in 42.69 seconds: standalone smoke and
  the 44,401-line varied-scene CSV byte-exact.
- `tools\validate_physics_deep.bat` — passed in 110.74 seconds: varied scene,
  bullet sweeps, shooting reaction, three-body, known-issue, target, and
  SkullScope query baselines all exact.

Both Debug and Profile compiler output showed `/fp:precise`. No non-physics
divergence was observed, so the plan's conditional broad validation gate was
not triggered.

## Independent Review

One read-only rubber-duck review checked all 16 configuration pins, the complete
parallel call-site inventory, the documented compiler/thread contract, and the
absence of behavioral edits. It requested explicit rows for two renderer chunk
range invocations and an explicit `MSVC v143` scope statement; both documentation
gaps were corrected. The final verdict reported no blocking findings.
