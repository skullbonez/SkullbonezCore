# Contact Energy And Warm-Start Integrity — ES6

Date: 2026-08-02

Status: Historical owner checkpoint — approved in the linked closure report

Closure: `contact-energy-and-warm-start-integrity-closure.md`

## Outcome

The engineering work is complete without increasing the global solver cap above
12 iterations and without resuming the parked stack experiments. Object
restitution now follows the lifetime of the exact loaded contact feature, the
same lookup owns warm-start compatibility, and a no-contact frame ends that
lifetime. A fixed far-edge catcher is the only authorized 200-box scene change;
it retains the post-demo striker without changing the primary wall impact or the
211-body dynamic workload.

Tracked baselines remain untouched. Five exact candidates are retained under
`TestOutput/contact_energy_es6_final/candidate/` for the terminal owner ruling.

## Final Behavior Evidence

### Four-brick fixture

- Final frame: 1199.
- Dynamic bodies: 4.
- Maximum solver iterations: 12.
- Peak mechanical energy above the initial value: 0.
- Post-frame-300 upward relaunch reversals: 0.
- Final sleepers: 4/4; permanent sleep begins at frame 132.
- Invalid dynamic samples: 0.
- Authoritative retained witness:
  `TestOutput/contact_energy_es5/four_brick_final_guard.csv`, SHA-256
  `1eddec1d1cf8e987c71445dc98e5ab89ec16f49afc0398f44b2bb38b4b82118e`.

### 200-box wall fixture

- Final frame: 6799.
- Dynamic bodies: 211; final sleepers: 211/211.
- Maximum solver iterations: 12.
- Permanent complete sleep begins at frame 3286.
- Invalid dynamic samples: 0.
- Repeated full-height popcorn bodies: 0.
- Maximum permitted one-off early rise: 1.285755 times that body's nominal
  height.
- Final-300-frame relaunch count: 0.
- Maximum unexplained recovery after explicit separation-bias work:
  51.063762, below the 57.603553 tolerance.
- Peak energy relative to the impact reference: -702854.799132.
- Final striker state: position `(998.982605, 7.000844, 464.544312)`, zero
  speed, sleeping against the fixed catcher.
- Final CSV SHA-256:
  `7e8691d96add82602206ddb76e96158875ac1fd50ebdb41defa10050a8e7a63b`.
- Automatic and worker-zero witnesses are byte-identical.

The semantic checker pins the original scene payload separately from the exact
catcher geometry. Its negative controls reject masked energy injection,
transient non-finite state, repeated early relaunches, a changed striker launch,
changed restitution, and a moved catcher.

## Visible Evidence

- Four-brick settled capture:
  `TestOutput/contact_energy_es5/visual/four_brick_settled.png`, SHA-256
  `ca7bc477346b64a1d67c09fc7d4d8cc14f9b8a6ae47a4491e4529eb2c8fb9cc0`.
- 200-box settled capture:
  `TestOutput/contact_energy_es5/visual/wall200_settled.png`, SHA-256
  `6e8dfd57078a3964e722a3427e37c9112c695cb38a87cfe2345c4a54d0c650a9`.

Both waited DX12 captures were inspected. The wall is settled, the terrain is
clear of lost dynamic bodies, and the striker is visibly parked at the catcher.

## Performance And Review

The rejected broad body-pair probe increased `Frame/Physics/Step` from 0.0754
ms to 0.0944 ms and average row work from 14.2128 to 21.4761. It was removed.
The final exact-feature lookup measured 0.0728 ms for the same step and 14.5068
average rows, with whole-frame mean 0.4763 ms. `validate_perf` passes allocation,
selected-ball, DX12, and focused physics budgets with no regression.

The independent final rubber-duck review returned CLEAN after earlier findings
about initial-loss energy masking, popcorn sensitivity, non-finite sampling,
diagnostic schema versioning, scene locking, and current-body complexity
rulings were corrected. It found no remaining correctness, ownership, test
sensitivity, hot-path, scene-lock, comment, or baseline-governance blocker.

The touched-source comment audit is 7/7 with zero deferred files:

- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezSource/Physics/PhysicsDebugData.h`
- `SkullbonezSource/Physics/PhysicsDiagnosticsView.h`
- `SkullbonezSource/Physics/Diagnostics/SkullScope.cpp`
- `SkullbonezTests/TestPersistentContactSolver.cpp`
- `tools/check_contact_energy_scenes.py`
- `tools/physics_query.py`

## Validation

- Focused restitution/contact-energy tests: PASS.
- Contact-energy semantic checker and planted controls: PASS.
- `tools\validate_tests.bat`: PASS.
- `tools\validate_fast.bat`: PASS, including all seven ownership inventories;
  function complexity reports 6,336 functions, 40 triggered, 40 ruled.
- `tools\validate_physics.bat`: reaches only the expected old varied-scene
  golden mismatch: 14,534 canonical rows differ, first at line 5160/frame 139.
- `tools\validate_physics_deep.bat`: reaches the same broad mismatch; all
  remaining constituents pass directly. Bullet wall/object/terrain, shooting,
  and space three-body CSVs are byte-exact; all 10 shooting reactions pass. The
  only known-issue signature change is the explicitly deferred stacking watch.
- `tools\validate_perf.bat`: PASS on the clean retained run.
- `tools\validate_replay_visual_fidelity.bat`: exactly one engine generation was
  performed. The tracked manifest rejected only the changed scene provenance
  hash. Against the staged candidate, the 2,401-tick oracle passes with all 200
  wall bricks moved, 175 sustained toppled bricks, all 200 causal nodes, and all
  offline false-pass controls.
- `tools\validate_full.bat`: phases 0-4 pass, including 484/484 CPU cases and
  2,429,570 assertions, Automation, and DX12. Phase 5 stops only at the same
  expected 14,534-row tracked Physics mismatch.

## Exact Baseline Decision Packet

| Tracked artifact | Old SHA-256 | Candidate staging SHA-256 | Complete difference summary |
|---|---|---|---|
| `TestOutput/baselines/physics_regression_varied.csv` | `d1e0ec54de218efa4923c1505e0fdab1bd556bfa5e8f3bb595203c5ee6b8f752` | `4dce1be8ad1dde337281c7f37c25fcf3fd7b9268bcfe0b382fefb4f85dfe69aa` | Both are one canonical 44,401-line run. 14,534 lines differ, first at line 5160/frame 139; current runtime emitted two byte-identical passes and the staged candidate uses the checker's canonical single-pass projection. |
| `TestOutput/baselines/physics_known_issue_signatures.json` | `09bd3ea10a019c628a8e58241b82790c3fd58dd6f1087e92722c989e76d8566a` | `0bb75b38056dcbe02ef08972112f718b0085ec0596c95a240811a5dc5ab8f22c` | Only `stacking_stability_watch` changes: 3,075,201 to 3,082,679 bytes and SHA-256 `325d2275...` to `a95e049e...`; the line count remains 22,501. Stacking remains deferred and this row is diagnostic only. |
| `TestOutput/baselines/physics_query_varied.json` | `320dfca156c3b5ba000293420a67ce531fc6548088c3637af896723919834fdb` | `37803495168813fa47a37b5e4cbfd65d11161c01bc9938a06a86c33ea04f4164` | Query schema includes explicit `separation_bias`; deterministic solver/contact/body/energy projections change with the exact-feature restitution correction. Final sleepers improve 26 to 30 and final energy falls 608.307509 to 478.321446. Maximum iterations remain 12. |
| `TestOutput/baselines/replay_visual_fidelity_200_box.json` | `e2214ae2f8e1676c647df1d3afe308d30d12501e63ded1ce4027aa403b60f714` | `ab3d7d57a82f3ba9d0ac7c7209b447807f7b55f510d96fb57c77649427351a20` | `finalState` and all 2,401 visual ticks are identical. Only working/capture commit metadata and `sceneSha256` change for the authorized catcher. |
| `TestOutput/baselines/replay_visual_fidelity_200_box_causal.json` | `64576170593a93134a60fb449dff9ea2c7da795fdfc22c983144e0b1a5588d8d` | `50d1a973f25ec12a5d14ca1e09caff7c4b8bb3b74582251536deff12fdc5493f` | Target identity, 200-node topology, and all 2,401 causal ticks are identical. Only working/capture commit metadata and the binding to the new visual-baseline hash change. |

Candidate sizes are respectively 6,328,076; 1,761; 102,322; 4,674,420;
and 859,356 bytes. The tracked destinations were not written.

The two JSON staging hashes and sizes above describe the retained CRLF
candidates presented at this historical checkpoint. Owner approval normalized
them to the repository's required LF form; committed-byte hashes are recorded
in `contact-energy-and-warm-start-integrity-closure.md`.

## Owner Checkpoint

No engineering work remains. The sole pending decision is whether the five
exact candidates listed above may replace their tracked baseline counterparts.
If approved, the follow-through is limited to those replacements, rerunning the
baseline-sensitive/final gates, committing the closure report, and retiring the
completed TODO plan. If approval is withheld, this implementation and decision
packet remain intact without changing the tracked goldens.
