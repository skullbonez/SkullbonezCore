# Contact Energy And Warm-Start Integrity Closure

Date: 2026-08-02
Result: ES0-ES6 complete; owner-approved five-golden transition applied

## Owner Decision

The owner accepted the loaded-contact restitution correction because the
200-brick wall falls with materially less bounce and the stack behavior is more
stable. The marginally slower ground settling is an accepted tradeoff. Terrain
and object friction remain at the repository's existing `0.8/0.8`, the global
solver cap remains 12 iterations, and no sleep-island experiment is retained.

Object restitution follows the lifetime of an exact loaded contact feature. A
no-contact frame ends that lifetime, changed contact geometry remains a fresh
impact, and elastic mutual-gravity behavior stays separate. Exact feature
identity also owns compatible warm-start reuse.

Stack-specific Bullet/Box2D experiments remain parked in
`Agentic/Plans/WNF/contact-stack-stability-techniques.md` and are not part of
this closure.

## Approved Goldens

| Artifact | Approved SHA-256 | Change |
|---|---|---|
| `TestOutput/baselines/physics_regression_varied.csv` | `4dce1be8ad1dde337281c7f37c25fcf3fd7b9268bcfe0b382fefb4f85dfe69aa` | One canonical 44,401-row run; 14,534 rows intentionally change, first at frame 139. |
| `TestOutput/baselines/physics_known_issue_signatures.json` | `dacd3cdb60ff39294a670e8c81eeada65d0251d6c418fa816a82490da2528496` | Only the deferred stacking-watch signature changes. |
| `TestOutput/baselines/physics_query_varied.json` | `f3b887a4f91498c18847cae814829d57407c92693629ae2629fd5c6b4bb2c6d5` | Solver/contact/body/energy projections follow the accepted response; final sleepers improve from 26 to 30. |
| `TestOutput/baselines/replay_visual_fidelity_200_box.json` | `7eb4e97b63b6e041a5d3ccf02a9bca58c267797d46428a1143b764d26eee9baf` | Fresh Automation provenance binds the accepted scene and current `0.8/0.8` configuration; all 2,401 approved visual ticks and final-state values remain exact. |
| `TestOutput/baselines/replay_visual_fidelity_200_box_causal.json` | `60ed162989fb3130d34eb07ac5fbdc9618b7428f1bdc57001b84709f891a0bf0` | Fresh provenance and visual-manifest binding; the 200-node topology and all 2,401 causal ticks remain exact. |

The Physics CSV matches its reviewed ES6 candidate byte-for-byte. Git's tracked
JSON policy normalized the known-issue and SkullScope candidates from CRLF to
LF before commit; their parsed values remain identical and the table records
the committed-byte hashes. The Replay pair was regenerated from the final authoritative 6,800-frame
Automation run because the staged candidates carried stale configuration
provenance. The positive oracle and every offline mutation control pass against
the regenerated pair.

## Behavior Evidence

- Four bricks record zero post-frame-300 upward relaunch reversals and sleep
  4/4 by frame 132.
- The wall retains all 211 dynamic bodies, remains finite, records no repeated
  full-height popcorn cycle, and sleeps completely by frame 3286.
- The fixed far-edge catcher retains only the post-demo striker and does not
  alter the primary wall impact.
- Automatic and worker-zero wall witnesses are byte-identical.
- The wall's attributed energy remains within the locked envelope, with no
  final-300-frame relaunch.

Detailed pre-approval evidence and complete old-versus-candidate comparisons
remain in
`Agentic/Reports/2026-08-02/contact-energy-and-warm-start-integrity-es6.md`.

## Validation

- `tools\validate_physics.bat`: PASS against the approved 44,401-row golden.
- `tools\validate_physics_deep.bat`: PASS across core/deep CSVs, known-issue
  signatures, shooting reactions, and SkullScope query output.
- Replay visual/causal oracle: PASS, 2,401 ticks, 200 moved wall bricks, 175
  sustained toppled bricks, 200 causal nodes, and all offline false-pass
  controls.
- `tools\validate_perf.bat`: PASS on the final clean sample. DX12 averages are
  0.7500 ms/frame, 0.2192 ms Physics, and 0.0241 ms persistent contacts;
  focused-bench averages are 0.4018 ms/frame, 0.0750 ms Physics, and 0.0298 ms
  persistent contacts. Two earlier samples were rejected for unrelated whole-
  frame/GPU noise and one isolated 4.0264 ms Physics maximum; neither reproduced.
- `tools\validate_format.bat`: PASS across 587 source files, 327 headers, and
  2,031 repository-relative `Related:` paths.
- `tools\validate_full.bat`: PASS in 562.6 seconds across Debug/Profile tests,
  Automation, DX12, ownership inventories, and byte-exact approved Physics.

The source-bearing implementation and its 7/7 touched-file comment audit were
already independently reviewed CLEAN before the owner checkpoint. The later
rollback/sleep-island A/B was discarded and is not part of this closure.
