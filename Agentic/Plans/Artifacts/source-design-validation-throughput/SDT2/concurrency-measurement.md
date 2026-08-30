# SDT2 Bounded Concurrency Measurement

Date: 2026-08-30

SDT2 schedules immutable source/context work items through a bounded pool. Each
worker runs Tidy and then the single batched Query session, returns a value, and
does not print or mutate coordinator-owned diagnostic state. Automatic mode is
`min(logical_processors, 4)`; this machine selected four workers.

## Scheduler Controls

- `--jobs 0`, `--jobs=-1`, `--jobs nonnumeric`, and `--jobs 5` all returned
  command-line exit 2 before tool discovery or compiler output.
- Planted delays and an independent active-child counter observed exactly two
  concurrent children for `--jobs 2`; peak in-flight work was also exactly two.
- A fast planted infrastructure failure admitted only the initial two work
  items, allowed the other started item to finish, and named all four unadmitted
  contexts as incomplete.
- Tidy, Query, child-crash, and unexpected worker-exception controls retained
  distinct infrastructure labels and exit 2 under forward and reverse
  completion orders.
- Clean controls retained exit 0 and planted policy controls retained exit 1
  with identical sorted diagnostics at serial, two-worker, and automatic job
  counts.

## Real Clean Scan

The same one-source/five-context allocator scan passed in every mode with five
Tidy and five Query launches and zero findings/errors:

| Jobs | Peak workers | Elapsed seconds |
|---:|---:|---:|
| 1 | 1 | 7.404 |
| 2 | 2 | 4.566 |
| automatic (4) | 4 | 3.232 |

After excluding only the volatile summary line, all three outputs were
byte-identical.

## Real Planted Policy Scan

The allocator plus a temporary all-four-rules source selected two files and 19
contexts. Every mode returned policy exit 1, 19 Tidy plus 19 Query launches, 70
findings, and zero infrastructure errors:

| Jobs | Peak workers | Elapsed seconds |
|---:|---:|---:|
| 1 | 1 | 8.981 |
| 2 | 2 | 5.102 |
| automatic (4) | 4 | 3.669 |

Sorted output excluding the volatile summary was byte-identical at SHA-256
`bb315541039b1b82bf44a785e29269626dd5d6bd2cacacc10279bf706dd4e917`.
The temporary source was deleted unchanged after measurement.

## Branch-Wide Observation

Automatic mode completed the current 19-source/138-context working-tree scan
with 138 Tidy plus 138 Query launches, four observed workers, no infrastructure
error, and 94.331 seconds elapsed. It reported 22 source findings: ten belong to
the preserved user-owned `PersistentContactSolver.cpp` experiment and twelve
are pre-existing findings in the branch's Replay source. SDT2 did not edit any
of those sources.

No source selection, context, compiler argument, matcher, threshold, test,
coverage, Physics evidence, or golden baseline changed.
