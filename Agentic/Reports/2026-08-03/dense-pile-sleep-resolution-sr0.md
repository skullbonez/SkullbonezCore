# Dense Pile Sleep Resolution — SR0 Three-Point Baseline

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Status: SR0 complete; SR1 mechanism diagnosis is binding next

## Outcome

The dense-pile regression is real, but the measured history is more precise
than the initial statement. The pre-gate implementation and current exact-
feature implementation both fail to reach permanent all-sleep by frame 20,000.
The intermediate pair-prefix implementation reaches permanent all-sleep at
frame 8,513. Current exact-feature behavior retains the decisive four-brick and
wall improvements, but loses that pile continuity benefit.

This evidence does not yet prove feature churn is the mechanism. SR1 must now
explain why pair-prefix lifetime continuity closes the pile while exact-feature
continuity does not, and distinguish that from solver-cap and support-
classification alternatives.

## Exact Historical Points

| Label | Commit | Meaning |
|---|---|---|
| pre-gate | `9b12badbfdbba1e134d8d958ea44e6606d0f21c2` | Parent of `63d7e92f`; no persistent-object restitution suppression |
| pair-prefix | `12dbb3eb67e3c789c0fa59f02365a118edcf6941` | Loaded body-pair prefix supplies restitution lifetime |
| exact-feature | `194cbf8219c8a3c166fba6f36cf7e933959514c7` | Exact loaded feature supplies lifetime and warm start; retained wall catcher |

Each point was checked out detached in a clean worktree, its pinned ImGui/Tracy
submodules were initialized, and `tools\validate_build.bat Debug` passed. All
runs use DX12, vsync/shadows/top text off, a hidden automation window, fixed
step `0.008333`, and the same tracked scene at that commit.

The common full-diagnostic command shape is:

```powershell
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step `
  --shadows off --hide-top-text --automation-hidden-window --frames 6800 `
  --scene SkullbonezData/scenes/box_pile_throw_300.scene.json `
  --physics-diag TestOutput/dense_pile_sr0/pile_<point>_6800.physicsdiag.ndjson `
  --physics-regression-log TestOutput/dense_pile_sr0/pile_<point>_6800.csv
```

A CSV-only 20,000-frame run at each point supplies the long-horizon sleep-bit
censor. Full diagnostics stop at the common 6,800-frame horizon so energy and
per-body rows remain exactly comparable without tripling an already 2.4-2.5 GB
trace beyond the decision need.

## Dense-Pile Sleep Result

There are 330 dynamic bodies after authored ragdoll expansion.

| Metric | pre-gate | pair-prefix | exact-feature |
|---|---:|---:|---:|
| First sleep frame | 223 | 223 | 223 |
| Permanent all-sleep frame | >20,000 | **8,513** | >20,000 |
| Last sleep transition through 20,000 | 19,994 | **8,513** | 19,991 |
| Final sleeping at frame 20,000 | 328/330 | **330/330** | 327/330 |
| Bodies never sleeping through 20,000 | 1 | **0** | 1 |
| Wake oscillations through 20,000 | 5,195 | **4,227** | 5,168 |
| Bodies with a wake oscillation | 209 | 198 | **190** |
| Maximum wakes for one body | 575 | **182** | 475 |
| Final sleeping at frame 6,800 | 308/330 | 309/330 | **320/330** |
| Wake oscillations through 6,800 | 3,832 | **3,687** | 4,287 |
| Tail kinetic mean, frames 6,500-6,799 | **4.794123** | 21.912017 | 19.713287 |
| Tail kinetic peak, frames 6,500-6,799 | **19.150856** | 77.229217 | 65.474819 |

The result is not a monotonic ranking. Exact-feature has ten awake bodies at
frame 6,799 versus 22 pre-gate, yet its late kinetic energy and wake count are
higher. At 20,000, pre-gate has two awake bodies and exact-feature has three;
both are still changing state in the final ten frames. Pair-prefix alone makes
the complete pile permanently sleep.

### Dense-Pile Total Kinetic Energy Over Time

| Frame | pre-gate | pair-prefix | exact-feature |
|---:|---:|---:|---:|
| 0 | 1,674,642.167801 | 1,674,642.167801 | 1,674,642.167801 |
| 300 | 722,229.894477 | 717,876.949217 | 688,379.593937 |
| 600 | **3,479.834860** | 5,398.620810 | 5,623.295492 |
| 1,200 | **101.865430** | 403.527968 | 210.237994 |
| 2,400 | **15.714145** | 92.156422 | 38.004864 |
| 3,600 | 15.014524 | **7.301156** | 17.495663 |
| 4,800 | **4.244249** | 7.870327 | 19.109083 |
| 6,799 | **1.992529** | 4.636185 | 24.216974 |

Peak kinetic energy is 9,889,136.215137 pre-gate, 9,907,872.469261 pair-
prefix, and 9,905,513.754833 exact-feature. Peak launch energy is similar;
the distinguishing defect is late sleep resolution and repeated wake state,
not an unexplained initial-energy-scale difference.

## Four-Brick Retained Benefit

The pre-gate trace was regenerated at exact commit `9b12badb`; the earlier ES0
trace was deliberately rejected for this table because its measured tip already
descended from `63d7e92f`.

| Metric | pre-gate | pair-prefix | exact-feature |
|---|---:|---:|---:|
| First/permanent all-sleep frame | never / never | 294 / 294 | **132 / 132** |
| Final sleeping | 0/4 | 4/4 | 4/4 |
| Bodies never sleeping | 4 | 0 | 0 |
| Wake oscillations | 0 | 0 | 0 |
| Tail kinetic mean, frames 900-1,199 | 35.165664 | **0** | **0** |
| Tail kinetic peak | 391.070104 | **0** | **0** |

| Frame | pre-gate | pair-prefix | exact-feature |
|---:|---:|---:|---:|
| 0 | 11.391669 | 11.391669 | 11.391669 |
| 300 | 27.834713 | **0** | **0** |
| 600 | 5.249997 | **0** | **0** |
| 1,199 | 14.837286 | **0** | **0** |

The lifetime gate is therefore retained: pair-prefix removes the old perpetual
four-brick motion, and exact-feature improves the permanent sleep frame by 162
frames without reintroducing a wake oscillation.

## 200-Box Wall Retained Benefit

The pre-gate and pair-prefix scenes lack the later fixed catcher. Their one
never-sleeping body is the escaped striker; exact-feature includes the owner-
accepted catcher at `194cbf82` and retains that comparison intentionally.

| Metric | pre-gate | pair-prefix | exact-feature |
|---|---:|---:|---:|
| Permanent all-sleep frame | never | never | **3,286** |
| Final sleeping | 210/211 | 210/211 | **211/211** |
| Bodies never sleeping | 1 | 1 | **0** |
| Last sleep transition | 1,823 | 2,964 | 3,286 |
| Wake oscillations | **426** | 595 | 512 |
| Tail kinetic mean, frames 6,500-6,799 | 142,809,169.201995 | 145,724,197.371998 | **0** |
| Tail kinetic peak | 151,549,008.829324 | 154,551,781.142326 | **0** |

| Frame | pre-gate | pair-prefix | exact-feature |
|---:|---:|---:|---:|
| 0 | 2,424,807.366663 | 2,424,807.366663 | 2,424,807.366663 |
| 300 | 379,687.152782 | 388,932.985798 | 379,847.962597 |
| 600 | 136,740.434905 | 133,789.967392 | **111,962.842991** |
| 1,200 | 99,559.062558 | 104,314.560871 | **31,189.312603** |
| 2,400 | 2,807,665.241238 | 3,228,914.171904 | **9,796.689320** |
| 3,600 | 20,857,819.403384 | 21,983,046.814881 | **0** |
| 4,800 | 55,803,457.944464 | 57,632,663.871750 | **0** |
| 6,799 | 151,549,008.829324 | 154,551,781.142326 | **0** |

The exact-feature/catcher state fully retains the accepted wall outcome: all
211 bodies remain and sleep, with zero final-tail kinetic energy. SR1/SR2 may
not recover pile continuity by reverting this behavior.

## Repeatable Measurement Tool

`tools/measure_dense_pile_sleep.py` reuses `physics_query.ensure_db()` and
prints a compact packet while writing full per-body rows to ignored JSON. Its
planted self-test covers first sleep, permanent all-sleep, wake transitions,
final sleeping count, and energy checkpoints. The optional `--horizon-csv`
path extends sleep-bit censoring using only the dynamic body IDs proven by the
full trace, so fixed scene rows cannot manufacture a never-sleeping result.

The ignored metric artifacts are under `TestOutput/dense_pile_sr0/`. Trace and
long-horizon witness hashes are:

| Point | 6,800-frame trace SHA-256 | 20,000-frame CSV SHA-256 |
|---|---|---|
| pre-gate | `617CDBE60626F6B95E4047D978545F399DC6637C8A947CAA8F214D0BAD46AC44` | `6A2536380BC6735725167A1809F4FBABEE9D03AECE5CEA467095F813E509DDAB` |
| pair-prefix | `70317DDDAF2783650439409D6894F91E101D25A4676E4A986FCBCEF6EE991D93` | `63E2D93C9DAAFFD64585ACC8FE8DC6360E39A8B8F257F9886937FC28A22A3089` |
| exact-feature | `FE7A938601C435761A5BA853F0D347C218CA990F239DC14B4C9643B34116579D` | `062D5C25279622FDEF7DF3A496AFA2B5EC9985D7EF2942D5D843A61F3AAFCB9B` |

## SR1 Handoff

SR1 must compare exact-feature against pair-prefix, not treat pre-gate as a
known-good sleep oracle. The key measured deltas are loss of permanent all-
sleep (8,513 to censored beyond 20,000), 941 additional wake oscillations by
frame 20,000, and one never-sleeping body. The four-brick and wall constraints
remain absolute: 4/4 permanent sleep with zero tail energy, and 211/211 wall
sleep with zero tail energy and the catcher retained.
