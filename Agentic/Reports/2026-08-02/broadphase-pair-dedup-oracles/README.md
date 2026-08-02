# Broadphase Pair-Stream Oracle Artifacts

Date: 2026-08-02
Plan phase: Broadphase Pair Dedup Cost BD0
Source tip before BD0 instrumentation: `6514636cd7e02588d204d8bd7f75db4bcb581f44`
Format: `SKOREBD0` version 2

## What Is Preserved

Each `.bin` file contains every pair in every one of 360 broadphase passes, not
only a digest or aggregate. Every pass records these five ordered boundaries:

1. raw `SpatialGrid` candidates;
2. candidates after fast-small-sweep augmentation;
3. raw traversal-first sleep-pruned rows;
4. final solver candidates after fixed/joint pruning; and
5. final sleep-pruned rows after the same pruning.

The same record carries body count, reserved body capacity, dense-bitset words,
explicit `memset` bytes, committed `pairSeen` bytes, and geometry-predicate call
counts both after the grid and after augmentation. Scalars and pairs are encoded
explicitly little-endian. Every record has start/end magic, repeated ordinal,
and exact content length; the file trailer repeats total pass count and content
length.

`tools/check_broadphase_pair_stream_oracle.py` is the authoritative decoder and
byte-comparison tool. The four `*-worker-equivalence.json` receipts prove that
the complete files produced with 0, 1, and 4 workers were byte-identical.

## Permanent Artifacts

| File | Bytes | SHA-256 | Workload |
|---|---:|---|---|
| `varied.bin` | 108,984 | `02BA43A97F947CE233E3C26EC19950BCB7E96A0A927C723349A85760D3B25F48` | 37-body authored varied scene |
| `sparse4000.bin` | 41,800 | `EC8C856F8B80A96375B2781FF125A0EA147A9529E6DE99483EFEFB045D17431D` | exact 4,000-body sparse scene, sleep disabled |
| `sleepy5000.bin` | 41,800 | `5854F5D037E40D356A42DC2E8EAA002DEF700BF05DCCBAF400F0193807BCFDBA` | tracked 5,000-body sleeping-heavy scene |
| `sleep-order.bin` | 53,320 | `CEAA1741405E8997152C6C966B2ECFAFDA0F01E7FC40D6C85332F987E0A69497` | four-body traversal-order fixture |
| `sleep-order.scene.json` | 2,364 | `1286DB0EF30811B3027BFB042B93419E077B2B71DFEFF2A314758F05A673568F` | exact authored input for `sleep-order.bin` |
| `sparse4000.scene.zip` | 85,274 | `95940C0A40A0E90B80548F8F291485E5110C6DEEC41EAC39B3695717720989C7` | exact compressed input for `sparse4000.bin` |

The ZIP contains `physics_sparse_4000.scene.json`, 6,457,481 bytes, SHA-256
`322FD472FB4A4A650C7AA8543942AFA65950426C1BA76C453F9355B5D211E804`.
It is also reproducible from
`SkullbonezData/scenes/physics_scale_sleepy_5000.scene.json` by retaining the
first 4,000 objects, setting `simulation.modelCapacity` to `4000`, and changing
`logging.perfLog` to
`TestOutput/broadphase_pair_dedup_bd0/profile/physics_sparse_4000_perf_log.csv`.
Every other JSON value is unchanged.

## Input Matrix And Commands

| Workload | Input SHA-256 | Frames | Passes | Extra flag |
|---|---|---:|---:|---|
| varied | `1D73FEF58565EB71C655780220DB771B88285DB35901F6BD6F3304A8C669355C` | 180 | 360 | none |
| sparse4000 | `322FD472FB4A4A650C7AA8543942AFA65950426C1BA76C453F9355B5D211E804` | 180 | 360 | `--no-sleep` |
| sleepy5000 | `671EF4FCEB0F253D28A3B1271BD068924BE568BC144A55E3825BFAE402FFB444` | 180 | 360 | none |
| sleep-order | `1286DB0EF30811B3027BFB042B93419E077B2B71DFEFF2A314758F05A673568F` | 360 | 360 | none |

Every launch used:

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --no-contact-audio --workers <0|1|4> --frames <frames> --scene <scene>
```

`SKORE_PAIR_STREAM_ORACLE` named the output prefix. The stage appended `.0.bin`
because this live-world stream was the first stage instance; auxiliary-world
streams, if constructed, receive distinct atomic suffixes and cannot interleave.

Worker 1 and 4 were captured after the final v2 field layout at Debug executable
SHA-256 `47EA0D957C9BDC19EC0B4A8F3B419881118BF859B711C47DBA78B6A38E831BB5`.
Worker 0 was recaptured after the final thread-local/atomic/finalization safety
changes at SHA-256
`B41E612CBB53745EB4C8EE87CCD33CCC3E89D51A82CB540C85CE157B54E16AC4`.
Complete-file equality across all three worker counts proves those safety
changes did not alter a record byte.

## Verification

Validate a permanent baseline:

```powershell
python tools\check_broadphase_pair_stream_oracle.py Agentic\Reports\2026-08-02\broadphase-pair-dedup-oracles\varied.bin
```

Compare a later capture against it:

```powershell
python tools\check_broadphase_pair_stream_oracle.py Agentic\Reports\2026-08-02\broadphase-pair-dedup-oracles\varied.bin <new-varied.bin> --require-identical
```

The same command applies to the other three workload baselines. A comparison
passes only when every file byte matches after both streams independently pass
all structural and semantic checks.
