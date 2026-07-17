# Broadphase Scale Attribution

Date: 2026-07-17
Branch: `nightrunner-16th-july`
Instrumentation tip: `1712f132a`
SIMD state: explicitly OFF

## Ruling

The 1,000 -> 2,000-body cliff is a saturated linear-probing hash table, not
duplicate-chain scanning and not pair generation. At 2,000 bodies the grid
admits its maximum 4,096 active cells. A representative frame then attempts
about 8,342 exact-AABB cell visits but writes only about 4,682 entries. The
remaining 3,660 visits ask `FindOrCreate` for keys that are not present after
the table is full, so each miss probes all 4,096 slots before returning `-1`.
That is about 15.0 million failed-slot inspections per frame before counting
successful lookups in a 100%-loaded table.

The original B0 hypothesis is rejected: sampled sweeps and duplicate
rejections are both zero in the measured rows, while occupied-bucket chain
inspection grows only 2.75x. B3 therefore will decouple hash-slot capacity from
the existing 4,096-cell admission limit. It will retain the same first 4,096
cell keys, active-cell order, entries, candidate pairs, and failure behavior,
but search them in a 16,384-slot fixed table (25% maximum load). The additional
fixed storage is 12,288 `Bucket` rows, approximately 384 KiB. No runtime growth,
cell-size change, candidate change, baseline refresh, SIMD cutover, or S7 work
is authorized.

## Corrected Exclusive Timing

The Profile runs used the committed B1 marker tree. `Broadphase` is the
inclusive parent; the children below are mutually exclusive and may be summed.

| Marker (average ms) | 1,000 | 2,000 | Growth |
|---|---:|---:|---:|
| Inclusive `Broadphase` | 0.2829 | 6.6789 | 23.61x |
| `GridSetup` | 0.0042 | 0.0092 | 2.19x |
| `GridInsertScalar` | 0.2060 | 6.3734 | 30.94x |
| `CandidatePairsScalar` | 0.0279 | 0.1101 | 3.95x |
| `FastSmallSweepAugment` | 0.0430 | 0.1831 | 4.26x |
| fixed/record/sleep prune sum | 0.0008 | 0.0016 | 2.00x |
| Exclusive direct-child sum | 0.2819 | 6.6774 | 23.69x |
| Parent minus child sum | 0.0010 | 0.0015 | n/a |

`GridInsertScalar` grows from 72.8% to 95.4% of inclusive Broadphase. Candidate
generation remains small, so pair deduplication is not the first optimization.
The parent/child residual is profiler overhead and unmarked glue; no interval is
counted twice.

## Counter Attribution

The table below averages the four fully printed `broadphase --attribution`
rows for the first deterministic run in each complete 30-frame trace.

| Counter | 1,000 | 2,000 | Growth / interpretation |
|---|---:|---:|---|
| body insertions | 1,000 | 2,000 | 2.00x |
| active cells | 3,936.25 | 4,096.00 | reaches hard admission limit |
| exact AABB cell visits | 4,154.50 | 8,342.00 | 2.01x |
| entry writes | 4,154.50 | 4,682.00 | 1.13x after saturation |
| derived capacity-rejected visits | 0.00 | 3,660.00 | new dominant miss path |
| bucket-chain entries inspected | 234.75 | 646.25 | 2.75x |
| duplicate rejections | 0.00 | 0.00 | original hypothesis rejected |
| raw pair combinations | 234.75 | 646.25 | 2.75x |
| unique pairs | 147.00 | 418.75 | 2.85x |

The fact that 2.01x cell visits become a 30.94x insertion-time increase is
explained by open-addressing at 100% load. The exact-visit/write difference is
not a duplicate: duplicate rejections are counted separately and remain zero.
It is the `FindOrCreate` full-table `-1` path.

## Commands And Timings

Profile evidence (about 2.1 s at 1,000, 7.2 s at 2,000, then 0.5 s total
analysis):

```bat
Profile\SKULLBONEZ_CORE.exe --vsync off --fixed-step --shadows off --no-contact-audio --physics-simd-kernels off --scene SkullbonezData\scenes\physics_scale_1000.scene.json
Profile\SKULLBONEZ_CORE.exe --vsync off --fixed-step --shadows off --no-contact-audio --physics-simd-kernels off --scene SkullbonezData\scenes\physics_scale_2000.scene.json
python Agentic\Skills\skore-render-test\analyze_perf.py --renderer physics_scale_1000 --csv Profile\physics_scale_1000_perf_log.csv --out-dir Profile
python Agentic\Skills\skore-render-test\analyze_perf.py --renderer physics_scale_2000 --csv Profile\physics_scale_2000_perf_log.csv --out-dir Profile
```

The 1,000/2,000 Profile artifacts are 16,503/16,504 bytes with SHA-256
`E71AB6B...89A12` / `E1C6ED2...CCB9`.

Complete bounded Debug traces (about 6.5 s and 12.1 s):

```bat
Debug\SKULLBONEZ_CORE.exe --vsync off --fixed-step --shadows off --no-contact-audio --physics-simd-kernels off --frames 30 --scene SkullbonezData\scenes\physics_scale_1000.scene.json --physics-diag TestOutput\broadphase_attribution\physics_scale_1000_30f.physicsdiag.ndjson
Debug\SKULLBONEZ_CORE.exe --vsync off --fixed-step --shadows off --no-contact-audio --physics-simd-kernels off --frames 30 --scene SkullbonezData\scenes\physics_scale_2000.scene.json --physics-diag TestOutput\broadphase_attribution\physics_scale_2000_30f.physicsdiag.ndjson
```

An initial pair of 600-frame GUI launches returned control to PowerShell before
the processes exited. They were stopped by verified PIDs after about 2.5
minutes; their incomplete 1,124,267,058-byte and 981,467,136-byte traces were
deleted and were not queried or used as evidence.

## SkullScope Query Cost

Exact queries (about 2.9 s, 0.4 s, 5.4 s, and 0.4 s respectively):

```bat
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_1000_30f.physicsdiag.ndjson summary --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_1000_30f.physicsdiag.ndjson broadphase --attribution --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_2000_30f.physicsdiag.ndjson summary --limit 4
tools\physics_query.bat TestOutput\broadphase_attribution\physics_scale_2000_30f.physicsdiag.ndjson broadphase --attribution --limit 4
```

Raw on-disk artifacts are separate from model input:

| Artifact | NDJSON bytes | SQLite bytes |
|---|---:|---:|
| 1,000 bodies / 30 frames | 57,838,185 | 28,676,096 |
| 2,000 bodies / 30 frames | 115,957,299 | 57,495,552 |

| Bounded query output | GPT-read chars | UTF-8 bytes | captured file bytes |
|---|---:|---:|---:|
| 1,000 summary | 5,203 | 5,203 | 10,408 |
| 1,000 broadphase attribution | 2,344 | 2,344 | 4,690 |
| 2,000 summary | 5,255 | 5,255 | 10,512 |
| 2,000 broadphase attribution | 2,348 | 2,348 | 4,698 |
| **Total GPT-read** | **15,150** | **15,150** | **30,308** |

The capture files are UTF-16 PowerShell `Tee-Object` output, hence their larger
on-disk byte count. All four query outputs were complete and untruncated. No raw
NDJSON or SQLite contents were exposed to the model.

## B3 Acceptance

The selected candidate is kept only if seven alternating same-tip pairs at both
scales show 1,000 bodies neutral-or-faster and a repeatable 2,000-body win.
Normalized candidate bytes/order, the 44,401-line physics oracle, fixed
4,096-cell admission, zero-allocation behavior, and capacity diagnostics must
remain unchanged. A failed candidate is reverted.
