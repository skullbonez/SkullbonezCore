# Source Design Validation Throughput

Date: 2026-08-29
Status: Active by owner direction on 2026-08-30. 2/5 phases complete
Impact area: validation tooling and mandatory Windows CPU CI
Owner: Validation tooling
Priority: Second active plan
Commit name: `SOURCE_DESIGN_THROUGHPUT`

## Owner Direction

Reduce the mandatory Windows CPU lane's wall time without weakening the
compiler-backed source-design rules, changing the supported toolchain, or
hiding work in a differently named job.

The owner reactivated this plan on 2026-08-30 and directed it to run after
`RESERVE_TRANSACTION` on `codex/replay-capture-bugfixes`.

## Measured Problem

GitHub Actions run `33244168782`, job `99078605186`, tested pull request 162 at
commit `26baa61e8b92173db46200c9bd306a344b31364f` on the supported
`windows-2022` runner. The complete job took about 74 minutes. Checkout and
tool installation were not the problem.

| Hosted phase | Start | End | Elapsed |
|---|---:|---:|---:|
| Runner setup, checkout, and pinned tool installation | 08:52:42 | 08:53:35 | 0m53s |
| Plain-language checks before `validate_fast` | 08:53:35 | 08:53:47 | 0m12s |
| `validate_fast --preflight-only` | 08:53:47 | 10:00:33 | 66m46s |
| Source-design and retained-policy phase inside `validate_fast` | 08:54:09 | 09:53:07 | **58m58s** |
| Profile x64 build inside `validate_fast` | 09:53:13 | 10:00:33 | 7m20s |
| Complete `validate_all_cpu_tests` umbrella | 10:00:33 | 10:07:11 | 6m38s |
| Coverage within the CPU umbrella | 10:01:05 | 10:06:03 | 4m58s |

The source-design phase consumed about 80 percent of the complete hosted job.
Coverage consumed about five minutes and is not the primary optimization
target.

For that pull request, `check_source_design.py` selected 79 changed C++ source
and header files. Its compile-context expansion produced 623 distinct
project/configuration contexts; individual headers reached as many as 14
contexts. The live scan then performed, serially, for every context:

1. one `clang-tidy` process for parameter count and function size/nesting; and
2. four separate `clang-query` processes for the four syntax-tree matchers.

That is 3,115 compiler front-end processes and 3,115 full parses of the selected
file plus its include closure. The wrapper runs the build-config and
deterministic-math checks concurrently with source design, but those small
checks cannot conceal nearly an hour of serial compiler work.

The current self-test is not the expensive part. A local run of
`python tools/check_source_design.py --repo . --self-test` completed in 3.56
seconds on 2026-08-29. The live branch-wide source/context scan owns the cost.

## Root Cause

The checker does more parser work than the policy requires:

- `clang_query_findings` launches one `clang-query` process per matcher even
  though one Clang Query session can execute several `match` commands against
  one parsed syntax tree.
- `inspect_source` walks every compile context synchronously, and `main` walks
  every changed source synchronously. No more than one compiler front end is
  active even when the hosted runner has spare processors.
- Diagnostics are assembled during traversal, which couples deterministic
  output order to serial execution rather than sorting completed result values.
- The workflow exposes the entire hosted lane as one large step, and the
  source-design checker prints no per-stage or work-count timing summary. This
  made coverage and compilation look suspicious even though the timestamps
  identify the compiler-backed scan.

The conservative header-context coverage is expensive but truthful. It is not
the first repair target. Removing configurations before proving actual consumer
coverage would trade correctness for speed.

## Goal

Keep the current changed-file selection, compile-context selection, Clang-Tidy
configuration, four Clang Query matchers, dead-code proof, exit behavior, and
diagnostic detail while making compiler work proportional to available hosted
capacity.

For the recorded 79-file/623-context workload, the target is:

- no more than 1,246 compiler front-end launches before concurrency: one
  `clang-tidy` and one batched `clang-query` per context;
- no more than 15 minutes for the source-design phase on each of two clean
  `windows-2022` hosted runs;
- no more than 30 minutes for the complete mandatory Windows CPU job on each of
  those runs; and
- exact finding/context parity with the serial reference path.

The time targets are acceptance limits, not permission to skip work when a
runner is slow. A run that exceeds them remains valid correctness evidence but
does not close the performance plan.

## Required Invariants

1. **Selection is unchanged.** The same changed files and the same distinct
   project/configuration/compiler-argument contexts are inspected.
2. **Rules are unchanged.** The parameter threshold, function size and nesting
   thresholds, four syntax-tree matchers, unpack threshold, exclusions, and
   dead-code project/link proof remain exact.
3. **Every context gets both analyses.** A successful result requires one
   completed Tidy result and one completed batched Query result for every
   selected context.
4. **Output is deterministic.** Findings are sorted by source, project,
   configuration, rule, and location after all completed work is collected.
   Scheduling order never changes bytes or first reported failure.
5. **Concurrency is bounded.** At most the configured worker count has an LLVM
   child process active. The default is derived conservatively from available
   logical processors and has a fixed upper bound suitable for hosted memory.
6. **Infrastructure failures fail closed.** Missing tools, parse failures,
   abnormal child results, incomplete work, and worker exceptions identify the
   source/context/command and return the existing infrastructure-error class.
7. **Policy failures stay distinct.** A valid syntax-tree finding remains a
   policy failure, not an infrastructure error or skipped task.
8. **Supported tooling stays supported.** Keep `windows-2022`, MSVC v143, the
   existing Visual Studio LLVM tools, and the established coverage tool. This
   plan authorizes no VS2026 or coverage-backend migration.

## Non-Goals

- Do not lower a threshold, remove a matcher, exclude a header, select fewer
  changed files, or reduce first-party project/configuration coverage merely to
  meet a time target.
- Do not add a source-coordinate exception, permission file, frozen finding
  budget, warning suppression, or timeout that converts unfinished work to a
  pass.
- Do not make hosted CI rely on stale object, syntax-tree, or finding caches in
  the first implementation. Cache identity and invalidation require a separate
  design if process reduction and concurrency are insufficient.
- Do not split the source-design scan into a separate required job as the first
  response. Parallel jobs can reduce visible PR latency while preserving the
  same waste; this plan first removes the redundant parser work.
- Do not change production C++ or its behavior.
- Do not change coverage floors, Physics evidence, or any golden baseline.
- Do not claim the problem is fixed by increasing the workflow timeout. The
  current three-hour ceiling only prevents premature cancellation.

## Design

### One syntax tree for all Query rules

Generate one Clang Query command script containing the four existing `match`
commands, or use repeated `-c` arguments if the pinned executable proves that
form equivalent. Invoke `clang-query` once per source/context and preserve a
unique binding name for each rule. Parse the combined output into the existing
labels and location lists.

The batched parser must distinguish zero matches for one rule from a failure to
execute that rule. Add explicit expected-rule accounting so partial command
execution cannot look like a clean result. Keep parameter-struct grouping by
the matched function location and retain the threshold of four unpacked fields.

The Query command file, if used, lives in a checker-owned temporary directory,
is immutable while workers run, and is removed automatically. It is not a new
tracked policy file.

### Bounded context workers

Represent each source/context pair as an immutable work item containing the
source path, project, configuration, and exact compiler arguments. A worker
runs Tidy and batched Query for one work item and returns a value containing
findings, command status, and elapsed timings. It does not print directly or
mutate shared diagnostic lists.

Use a bounded worker pool. Start with an automatic default of
`min(logical_processors, 4)` and expose `--jobs N`, with `--jobs 1` as the
serial reference and debugging path. Measure peak memory before raising the
upper bound. Do not queue an unbounded number of active subprocesses.

After all successful work completes, sort result values canonically and render
the existing human-readable report. On an infrastructure error, stop admitting
new work, allow already-started children to finish or terminate them by their
exact process handles, and report every affected work item without a broad
process-name kill.

### Measured, visible phases

Print one bounded summary for self-test and live modes:

- selected source count;
- selected context count;
- Tidy and Query process counts;
- configured and observed peak workers;
- context discovery, Tidy, Query, dead-code proof, and total elapsed time; and
- finding and infrastructure-error counts.

Add the source-design summary and elapsed duration to the hosted job summary so
the expensive phase is visible without downloading the complete log. Timing is
diagnostic only and never changes pass/fail.

## Phases

### SDT0 - Pin reference selection, findings, and timings (complete 2026-08-30)

- [x] Add focused self-test seams that enumerate immutable work-item identities
  without launching LLVM. Prove ordering and deduplication across source files,
  headers, projects, configurations, and compiler arguments.
- [x] Record the exact 79-file/623-context workload derived from pull request
  162 at `26baa61e8` as benchmark evidence generated from Git, not as a permanent
  source-coordinate allowlist.
- [x] Add timing/result value types and a bounded summary. Confirm that
  instrumentation does not change existing stdout diagnostics or exit codes
  except for the deliberate appended summary.
- [x] Run the existing serial checker twice on a clean representative diff and
  retain source count, context count, process count, findings, elapsed time, and
  peak memory as the before measurement.

### SDT1 - Batch the four Clang Query matchers (complete 2026-08-30)

- [x] Replace four Query launches per context with one session executing all
  four unchanged matchers against one syntax tree.
- [x] Add negative fixtures where each rule fails independently and where
  several rules fail in the same file. Each label and location must remain
  attributable after batching.
- [x] Add malformed-command and partial-command controls proving a missing rule
  cannot report a clean result.
- [x] Compare `--jobs 1` output, findings, contexts, and exit values against the
  pre-change serial reference. The expected process count for the recorded
  workload is 623 Tidy plus 623 Query launches.

### SDT2 - Add bounded deterministic concurrency

- [ ] Add `--jobs N` and the conservative automatic worker selection. Reject
  zero, negative, nonnumeric, and above-cap values before launching a compiler.
- [ ] Run whole source/context work items in a bounded pool; keep Tidy then Query
  sequencing within each work item unless measurements prove a different order
  is safer.
- [ ] Add planted child delays and an active-child counter proving real overlap,
  the configured maximum, and the absence of unbounded admission.
- [ ] Add controls for a policy finding, Tidy parse failure, Query parse failure,
  child crash, and worker exception. Prove deterministic diagnostics and exact
  failure classification under different completion orders.
- [ ] Run identical clean and planted-failure scans with `--jobs 1`, `--jobs 2`,
  and automatic jobs. Require byte-identical sorted findings and matching exit
  values.

### SDT3 - Integrate phase evidence without duplicating validation

- [ ] Keep `validate_fast`'s existing self-test group followed by live-scan
  group. Do not add a second source-design scan elsewhere in the CPU lane.
- [ ] Make the live scan use bounded automatic jobs while retaining an explicit
  serial command for diagnosis and parity evidence.
- [ ] Emit source/context/process/worker/timing data into the GitHub step summary
  and the existing mandatory CPU log.
- [ ] Confirm ordinary small diffs remain within the documented fast-lane intent
  and broad diffs scale with contexts rather than five serial parses per context.
- [ ] Do not split the workflow or prune header contexts if the two primary
  changes meet the hosted targets.

### SDT4 - Close with parity, resource, and hosted evidence

- [ ] Run the checker self-test and repository scan in serial and automatic
  modes. Require identical selection and findings.
- [ ] Measure process count, elapsed time, CPU use, peak LLVM child count, and
  peak committed memory on the recorded workload. If memory pressure, paging,
  or runner instability appears, lower the automatic cap and repeat rather than
  weakening inspection.
- [ ] Run `validate_fast --preflight-only` and the complete mandatory CPU lane.
  Confirm all 885 tests, coverage floors, and four focused CPU suites remain
  green; these counts must be refreshed if the live suite changes before
  activation.
- [ ] Obtain independent review of matcher parity, context coverage, bounded
  concurrency, deterministic output, and error propagation.
- [ ] Produce two clean `windows-2022` hosted measurements satisfying the
  15-minute source-design and 30-minute complete-job targets.
- [ ] At terminal plan closure, run the exactly-once plan-completion lane and
  record any unrelated pre-existing failure without changing its baseline.

## Conditional Follow-Up If Targets Are Missed

If batching and bounded concurrency preserve correctness but still exceed the
hosted target, stop and present the measurements to the owner before changing
coverage. The next investigation may derive actual transitive header consumers
from compiler-produced dependency data and deduplicate identical syntax-tree
contexts, but it requires all of the following before adoption:

- every current header/context pair is classified as an actual consumer,
  provably identical context, or non-consumer;
- planted project macros and forced includes prove a distinct consumer cannot
  disappear;
- the reduced selection is compared against the conservative scan on clean and
  failing fixtures; and
- the owner accepts the coverage rule change as a separate review decision.

CI job splitting or content-addressed syntax-tree caching may also be evaluated
then. Neither is pre-authorized by this plan.

## Validation Map For Implementation

During SDT0-SDT3, use focused checks only:

```bat
python tools\check_source_design.py --repo . --self-test
python tools\check_source_design.py --repo . --files <focused fixture or changed source paths>
python tools\check_source_design.py --repo . --jobs 1
python tools\check_source_design.py --repo .
```

Before the implementation branch is pushed:

```bat
tools\validate_fast.bat --preflight-only
```

The self-test must compile its fixtures, so this satisfies the directly affected
tool payload's compile requirement. Hosted CI owns the clean-run timing and the
complete CPU matrix. Do not run coverage separately after
`validate_all_cpu_tests`; that umbrella already owns it.

## Acceptance Criteria

- [ ] The recorded broad workload selects the same 79 files and 623 compile
  contexts as the reference checker, or a changed current tree has a documented
  Git-derived count with exact old/new selection parity.
- [ ] Exactly one Tidy and one batched Query analysis completes for every
  selected source/context pair.
- [ ] Every existing and planted source-design defect is reported with the same
  rule and source location in serial and automatic modes.
- [ ] Clean results, policy failures, and infrastructure failures retain distinct
  exit behavior and useful commands/context diagnostics.
- [ ] Output is byte-stable across completion orders after volatile timing lines
  are excluded from the comparison.
- [ ] Observed LLVM concurrency never exceeds the configured worker count, and
  peak memory does not cause hosted paging or termination.
- [ ] No header, project, configuration, matcher, threshold, dead-code proof, or
  changed-file source is removed to improve timing.
- [ ] Two clean supported hosted runs complete source design within 15 minutes
  and the complete mandatory Windows CPU job within 30 minutes.
- [ ] Coverage, tests, build warnings, Physics evidence, and golden files are
  unchanged by the validation-tool optimization.
- [ ] Independent review finds no lost finding, nondeterministic result ordering,
  unbounded child-process fan-out, or lost diagnostic.

## Reactivation Condition

Satisfied by explicit owner direction on 2026-08-30. Before source edits,
refresh the workflow timings, changed-source/context counts, LLVM version,
runner image, and current checker behavior. If the current source-design phase
already satisfies the target, close or replace this plan instead of implementing
obsolete optimization work.
