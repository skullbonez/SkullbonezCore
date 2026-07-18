# Physics Body-Count Scale Measurements

Date: 2026-07-18

Campaign: `physics-body-count-scale-campaign`

Branch: `nightrunner-18th-july`

Task boundary: P0 complete, P1 owner-blocked

## P0 Owner Rulings

- The campaign remains on feature branch `nightrunner-17th-july`.
- The sleeping-heavy fixture is accepted at authored capacity 6,000 with
  5,000 dynamic bodies: 4,000 fixed-seed sleepers and 1,000 continuously
  perturbed awake movers. A fixed broadphase-scale witness keeps the authored
  bounds away from knife-edge selection behavior.
- The final P7 target is ratified as at least 4,000 awake bodies within the
  approximately 1 ms physics-step budget on the reference Threadripper 3970X.
- P1's canonical pair-order transition is authorized only through the plan's
  set-equivalence-first protocol. No artifact may move until the old/new
  candidate-pair sets match tick-for-tick on all six required scenes.
- P6 is not pre-authorized. P5 must present fresh evidence and obtain a fresh
  owner decision before graph-colored solver work begins.
- P0 refreshes no baseline, golden, screenshot, authored format, or coverage
  floor.

## Fixed Before Matrix

Profile medians are milliseconds over 1,140 captured frames. The existing
inclusive solver-owner interval is
`Frame/Physics/Narrowphase/PersistentContacts`.

| Marker / counter | scale_200 | scale_520 | scale_1000 | scale_2000 | sleepy_5000 |
|---|---:|---:|---:|---:|---:|
| `Frame/Physics` | 0.1106 | 0.8486 | 1.0688 | 1.7852 | 2.1479 |
| `Frame/Physics/Broadphase` | 0.0245 | 0.1986 | 0.3377 | 0.6487 | 0.6963 |
| `Frame/Physics/Broadphase/GridInsert` | 0.0180 | 0.1764 | 0.2538 | 0.4628 | 0.4456 |
| `Frame/Physics/Broadphase/CandidatePairs` | 0.0017 | 0.0074 | 0.0222 | 0.0821 | 0.1976 |
| `Frame/Physics/Broadphase/PruneSleepPairs` | 0.0001 | 0.0002 | 0.0003 | 0.0004 | 0.0001 |
| `Frame/Physics/ApplyForces` | 0.0262 | 0.1812 | 0.1827 | 0.2051 | 0.2108 |
| `Frame/Physics/Integrate` | 0.0224 | 0.1995 | 0.2079 | 0.2455 | 0.2595 |
| `Frame/Physics/Narrowphase` | 0.0004 | 0.0030 | 0.0070 | 0.0221 | 0.0011 |
| Inclusive solver owner (`PersistentContacts`) | 0.0071 | 0.0305 | 0.0569 | 0.1353 | 0.0027 |
| Awake / total bodies | 200 / 200 | 520 / 520 | 1,000 / 1,000 | 2,000 / 2,000 | 1,000 / 5,000 |
| Bodies reinserted this step | 0 | 0 | 0 | 0 | 0 |
| Estimated hot bytes/body/step | 245.0 | 245.0 | 245.0 | 245.0 | 95.4 |

The reinsertion counter is deliberately zero until P2 owns persistent-grid
maintenance. The before matrix proves the central problem: 1,000 awake bodies
inside a 5,000-body store cost 2.1479 ms, roughly twice the all-awake 1,000-body
step, because sleeping rows still pay broadphase and bookkeeping costs.

## Hot-Byte Accounting

The diagnostic is a static logical byte model multiplied by the rows each
pass actually visits; it is not a hardware bandwidth counter. Per step it
charges 58 bytes to every body, 81 additional bytes to an awake force-stage
row, and 106 additional bytes to an awake integration row. Therefore an
all-awake body reports 245 bytes, while 5,000 total / 1,000 awake reports
`(5000*58 + 1000*(81+106))/5000 = 95.4` bytes/body/step.

The Profile-only post-wake scan is serial and outside the physics worker loops.
It avoids shared-counter races and attributes bodies released from fixed state
or awakened during the step to the integration pass that actually consumes
them.

## Capacity And Memory

The authored active capacity is 6,000; fixed runtime stores retain their
8,192-row hard maximum. The active capacity is now applied after scene load to
all runtime capacity consumers, including replay topology.

| Scene | Process private start MB | Restart MB | End MB |
|---|---:|---:|---:|
| scale_200 | 142.75 | 209.45 | 209.45 |
| scale_520 | 142.75 | 214.42 | 214.42 |
| scale_1000 | 142.65 | 222.39 | 222.38 |
| scale_2000 | 142.80 | 238.62 | 238.60 |
| sleepy_5000 | 144.59 | 288.08 | 284.95 |

Focused sleepy-scene accounting reported 94,118,476 owned bytes:
GameModel 6,187,328; PhysicsBodyStore 851,968; ColliderStore 59,506,688;
render storage 2,064,384; PhysicsWorld 25,508,108.

## Determinism Evidence

The final Debug executable produced byte-identical CSVs at worker counts 0,
1, and 4 for every required scene. The retained worker-0 artifact sizes and
SHA-256 values are:

| Scene | Bytes | SHA-256 |
|---|---:|---|
| scale_200 | 33,831,818 | `3DAD84BE52C8C7A9BCA029B11543E841E54B2F14DFE24E7FCF14D0CBE5BF522A` |
| scale_520 | 88,116,534 | `157A5B652465630BE861A558F621DE5A56A86A257D54F6BF6BEF256DC965EE21` |
| scale_1000 | 169,579,366 | `0A690DFC021AA1AD9203A399DEF47ECC45861E43EF16CB60991AE36A973449C2` |
| scale_2000 | 340,430,236 | `CE357FE9FDCE7BE18945712E33C52B516B46D55F297AE67CFE3247256DC8C1BB` |
| sleepy_5000 | 912,830,920 | `67FDBC3D7A6FBDDD6FC15E0393A9DF137AA3777193EE8E9F564090C7B7174C76` |
| regression_varied | 12,663,724 | `51877A2C7D4976343245243BC4CDA24BA7F5AAF57028D3A897F3C2904D410C93` |

The 18-process matrix took 1,461.3 seconds. Worker-1 and worker-4 duplicates
were deleted after comparison (36 files, 3,114,910,094 bytes); worker-0
artifacts remain ignored under `TestOutput/p0_determinism/`.

Provenance: Profile executable 3,278,848 bytes,
SHA-256 `413C14BC2BCCE0D1366048BBA17DB9A2D3CF8385882B3425ADEA1860DA20744D`;
sleepy scene 3,070,330 bytes,
SHA-256 `F253B3E736542770B8FF372FF993BF156DB7BB4C119156E767D93F03D8CA111A`.

## Validation And Review

- `tools\validate_perf.bat`: passed from final reviewed source; zero-warning
  Profile build, allocation guard, analyzer self-test, generator check,
  selected-ball structural probe, DX12/physics budgets, and the five-scene
  matrix all passed.
- `tools\validate_full.bat`: passed in approximately 132 seconds; 292/292
  tests and 21,458 assertions, all coverage floors, zero DX12 validation
  errors, screenshot comparisons, replay/Automation smoke, and the 44,401-line
  byte-exact varied-scene physics baseline passed.
- `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --frames 1
  --scene SkullbonezData\scenes\physics_scale_200.scene.json`: passed with
  platform markers enabled and 63 workers.
- Independent review is clear after the capacity-consumer, Profile-init,
  fixed-body awake-count, hot-byte framing, worker-counter race,
  fixed-to-dynamic release, and sleep-disabled count findings were corrected.
- Touched-source comment audit: 16/16 files compliant, zero deferred; see
  `physics-body-count-p0-comment-audit.md`.

## P1 Set-Equivalence Blocker

The Debug-only probe sorted each final candidate set before hashing and reserved
its complete vector capacity during construction. Each scene ran for 180 frames
(360 fixed ticks) on both the old and canonical emitters. Five scenes matched
byte-for-byte for every tick:

| Scene | Probe SHA-256 |
|---|---|
| `physics_scale_200` | `81F56802B755462D87349C29F223A69B4074F02EBB670B42FA429DEB8DF1B765` |
| `physics_scale_520` | `04B0CE9626A342145EE60C1B2FC9DF4A1DB91F9EAF1F687DCA1D19E6CA1A74C3` |
| `physics_scale_1000` | `E8F4570C56C45A679729897B504248508A30DEAD11F6256F0BC33A7BF25F969D` |
| `physics_scale_2000` | `3D0AC5153EED1A5636B0ADEC21D533473F244BFA5B7F72AEB86B081652169249` |
| `physics_scale_sleepy_5000` | `F29685B0A377CE3B1934CD9C3FA1C2A60CAE7FC5758DE31048460E580E13BCE9` |

`physics_bench_varied` failed at fixed ticks 152 and 332. The legacy set had ten
pairs and sorted hash `36D65BE59F63AC9F`; the canonical set had nine pairs and
hash `DF4231BC4598BEA1`. Full row capture identified the sole missing normalized
pair as `(18,20)`. A same-binary legacy/canonical diagnostic toggle reproduced
the result, excluding stale-binary provenance as the cause.

The canonical emitter, focused regression test, and temporary probe were fully
reverted under the plan's revert-on-failed-equivalence rule. No physics CSV,
replay golden, visual baseline, screenshot, scene, config, or coverage floor
changed. This is an owner blocker, not a completed P1 task: either a new design
must satisfy the existing six-scene tick-for-tick rule, or the owner must
explicitly revise the protocol before a behavior-visible transition proceeds.

### Same-Binary Causal Evidence

The exact same diagnostic Debug executable (SHA-256
`A56A4DE4F2A76FE0F1F45363B876F8DC40EEF7495D78D52BD4E3F2BD106850DC`)
ran `physics_bench_varied` twice with only its legacy/canonical ordering toggle
changed. Both 13,322-line deterministic CSVs first differ at line 3,791,
regression row/frame 102 for body 15: velocity components differ by 0.0001.
Their first final candidate-set difference remains fixed tick 152, 50 ticks
later. This establishes the causal order: solver trajectory diverges before
pair `(18,20)` leaves the final set.

| Mode | CSV bytes | SHA-256 |
|---|---:|---|
| Legacy | 1,897,378 | `E77728DD2271D617C6C37D03BE7A4AA6E25A657833E3F848D0FCF6825CBA2248` |
| Canonical | 1,897,420 | `D577C4B26A2E675E710842174E756678B854AD2224B24B6B0513C3C4B1C17914` |

The candidate span is committed in original order by narrowphase and consumed
in that order by the persistent projected Gauss-Seidel solver. Therefore an
explicitly behavior-visible order transition is expected to alter later state;
requiring independently evolved simulations to retain identical later spatial
memberships contradicts that transition's mechanism.

### Independent Protocol Review

Read-only review `p1-protocol-duck-01` found that the written rule still
unambiguously requires independently evolved old/new runs, so it cannot be
silently reinterpreted. Its technical verdict is to revise the rule to the
following exact acceptance:

> On all six scenes for 360 fixed ticks, compare legacy and canonical normalized
> raw and final candidate sets from the identical pre-broadphase state. Run the
> comparison once with legacy driving simulation and once with canonical driving
> simulation. Independently evolved trajectories need not retain identical later
> sets; canonical output must remain deterministic, artifact transitions remain
> separately owner-gated, and 0/1/4-worker byte identity remains mandatory.

Both driver distributions are required: legacy-only shadow comparison could
falsely pass while missing a canonical-trajectory state. Raw comparison occurs
immediately after `SpatialGrid::GetCandidatePairs`; final comparison occurs at
the solver-visible boundary after augmentation and pruning, so neither boundary
can mask an emitter defect in the other.

| Plan | Duck run | Reviewer | Reason | Prompt chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|---|---|---|---|---:|---:|---:|---:|---|---|
| `physics-body-count-scale-campaign` | `p1-protocol-duck-01` | `/root/p1_protocol_duck` | Repeated P1 protocol failure | 947 | 3,435 | n/a | 2m 25s | Explicit amendment required | Owner approval |

## Fresh-Agent Handoff

Do not restart P1 implementation without explicit owner approval of the exact
two-driver same-state amendment above (or a replacement design that satisfies
the original independently evolved rule). All source changes remain reverted.
P2-P7 are dependency-blocked; P6 remains separately deferred to P5.
