# All-Build SB Error Observability And Launch Integrity

Date: 2026-08-23
Status: Active by explicit owner direction. 0/7 phases complete; E0 ready.
Impact area: Core diagnostic storage, durable logging, creation-site stack
capture, fatal reporting, build-configuration policy, raw recoverable-error
sinks, startup/termination reporting, retained executable bundles, and
validation tooling
Owner: Core diagnostics and process launch boundary
Priority: Binding first-slot plan. E0-E6 execute in strict internal order, but
phase-local work may run beside Physics or UI when canonical subsystem,
path-owner, worktree, and mutable-resource leases are disjoint.
Commit name: `ERROR_OBSERVABILITY`

## Owner Direction

The owner activated this plan on 2026-08-23 after a retained Debug executable
displayed a Windows loader dialog for a missing `WinPixEventRuntime.dll` without
producing a Skullbonez log. The loader failure exposed a pre-entry packaging
defect, while the source audit exposed a broader in-process reporting defect.

The required build policy is binding:

| Build | Warnings | Recoverable SB errors | Fatal SB errors | Stack policy |
|---|---|---|---|---|
| Debug | Durable | Durable | Durable | Error message first, then creation-site stack for recoverable and fatal errors |
| Profile | Off | Durable | Durable | Error message first, then creation-site stack |
| Profile-WPO | Off | Durable | Durable | Error message first, then creation-site stack |
| Automation | Off | Durable | Durable | Error message first, then creation-site stack |
| Release | Off | Off | Durable, minimal | Fatal owner/message only; no stack |

`Release|x64` is the shipping configuration because the solution defines no
separate Ship configuration. Introducing a later Ship configuration must copy
the Release policy explicitly; it may not inherit non-shipping diagnostics by
accident.

For this plan, an **SB error** is either a failed `Core::SbResult` created by an
`SbDiagnosticStore` or a fatal invariant emitted through `SB_FATAL`/`SbFatal`.
A warning is not an error and is permitted only in Debug. An `SbResult` that was
recoverable at its creation site but reaches a process boundary that cannot
continue is promoted to a fatal process outcome; Release must therefore retain
its owner/message as one minimal fatal SB error.

Every non-shipping recoverable or fatal error is logged at its origin. The
record begins with severity, owner, and complete bounded message, followed
immediately by the creation-site call stack. Copying, moving, returning,
displaying, or finally handling the `SbResult` must not create a second origin
record. No sampling, first-occurrence suppression, deduplication, or
"expected fallback" exception may hide an error.

Every existing error site must have an actionable human description. An
HRESULT, Win32 code, generic `failed`, or assertion expression by itself is not
a description. Every first-party runtime assertion must state its owner and
invariant description. In Debug, Profile, Profile-WPO, and Automation an
assertion failure emits the failed expression, description, file/function/line,
and call stack before termination. A Release assertion that remains compiled is
the same minimal fatal SB error as every other Release fatal: owner/message only.

The loader cannot execute Skullbonez code before `WinMain`. Pre-entry failures
are closed through runnable-bundle construction and isolated launch validation,
not by pretending the in-process logger can intercept the Windows loader.

## Dated Baseline Evidence

This is a current measurement, not a count budget or future allowance.

- `Core::SbDiagnosticStore::FailureV` at `SkullbonezSource/Core/SbResult.cpp`
  lines 221-273 copies owner/message bytes into a leased slot and returns. It
  emits no log and captures no stack.
- The current tree has 240 diagnostic-store `Failure(...)` construction sites
  across 48 production files: Runtime 96, Rendering 75, UI 32, World 13, Scene
  10, Core 8, Assets 5, and Physics 1. Central construction-site reporting can
  cover these without relying on every caller to remember a second API.
- The current production tree has 70 runtime `assert(...)` sites across 27
  files: Physics 26, Runtime 23, Maths 9, Core 5, World 4, and Rendering 3.
  They are compiled out by `NDEBUG` in current Profile/Profile-WPO/Automation/
  Release configurations, and many contain no human invariant description.
- `Core::EngineLog` is compiled only for `_DEBUG` or
  `SKULLBONEZ_TEST_ENGINE_LOG`. Its Profile, Profile-WPO, Automation, and
  Release methods are no-ops at `SkullbonezSource/Core/Log.cpp` lines 239-262.
- The only durable event path is the current-working-directory-relative
  `Debug/runtime_events.log`. Directory creation and `fopen_s` failure are not
  reported, so the diagnostic sink can itself fail silently.
- `SB_FATAL` writes a Debug event and `stderr`, flushes, optionally breaks, and
  aborts. Outside Debug the event write is currently a no-op.
- Symbolized unhandled-exception and terminate reporting is installed only
  under `_DEBUG` in `Runtime/Startup/StartupCrashLogging.cpp`.
- The process boundary manually emits some startup/runtime failures to an
  event, `stderr`, and a dialog, but locally recovered paths vary between
  event-only, `stderr`-only, UI-only, status-only, counter-only, and silent
  fallback behavior. The current tree contains 94 `WriteEventf` call sites, 82
  `stderr` formatting sites, and 9 native/message-window sites; those numbers
  are evidence of fragmented ownership, not thresholds.
- Normal Debug/Profile/Automation output directories contain
  `WinPixEventRuntime.dll`. The retained FP0 and FP1 artifact directories contain
  only `SKULLBONEZ_CORE-Debug.exe` and `manifest.md`, even though the executable
  imports that runtime. Launching the retained executable in isolation therefore
  fails before `WinMain` and cannot produce a game-owned log.

## Initial Repair Worklist

E0 must regenerate and supersede this dated list. These source-grounded examples
prevent the next orchestrator from mistaking central logger work for complete
coverage:

- Core logging ignores directory/open, write, flush, and close outcomes, and is
  a no-op outside Debug/test (`Core/Log.cpp`).
- App scene-load paths collapse failed `SbResult` values to `.Ok()` or discard
  them in manual, stress, capture, and advance flows
  (`Runtime/App/InputFrameExecution.cpp` and `Runtime/App/RunFrame.cpp`). Some
  paths then request a zero exit code while the detailed Scene failure was only
  written to `stderr`.
- The interaction-automation report writer can report success after disk write
  or close failure, and an open failure marks the writer complete so it will not
  retry (`Runtime/Automation/InteractionAutomationReportWriter.cpp`).
- Performance CSV and memory-dump owners ignore open/write/flush/close failures
  or report them only to `stderr` (`Runtime/Diagnostics/RuntimeDiagnostics.cpp`,
  `Runtime/Diagnostics/DiagnosticsRuntime.cpp`, and `Runtime/App/Run.cpp`).
- Physics has 82 `SB_FATAL` calls plus bespoke fatal implementations in
  `PhysicsFixedList.h` and `PhysicsBodyStore.cpp` that bypass the common fatal
  event path. Several assertions execute immediately before a richer
  `SB_FATAL`, so the assertion can hide the more actionable fields.
- Physics/Maths contain assertion-only, boolean/sentinel-only, ignored-status,
  and silent conservative fallback paths. Examples include ignored body-
  registration rollback failures, ambiguous hot-stage `bool` results, replay
  restore status collapse, invalid numeric clamps, swept-grid fallback, and
  Maths normalization/orbital statuses without an owning diagnostic.
- Convex-hull loading is the only Physics/Maths failed-`SbResult` producer, but
  its reader does not distinguish `ferror` from EOF and ignores `fclose`.
- Debug Physics CSV/SkullScope writers can silently lose open/write output;
  model-record conversion can also skip or default a diagnostic row without an
  error packet.
- UI bounded draw, text, and retained-event overflow currently drops or
  truncates presentation behind flags, an opt-in environment statistic, or UI
  text only (`UI/UIDrawList.cpp`, `UI/UI.cpp`, and `UI/UITabMemory.cpp`). E0 must
  distinguish genuine Debug warnings from recoverable loss that requires a
  non-shipping SB packet.
- DXR capability and resource initialization can currently return success while
  leaving the feature unusable, and later dispatch may return silently.
  Shader reflection, cbuffer mismatch, query-timer, DRED/debug-layer, and
  InfoQueue paths also include Debug-only, event-only, debugger-only, or ignored
  failures (`Rendering/DX12` and `Runtime/Render`). E0/E4 must represent normal
  optional capability absence as typed state and route genuine initialization,
  content, or device errors through the central contract with decoded native
  detail.
- Nullable World terrain/water render resources can suppress presentation and
  retry without a World-owned result; Scene save and editor-library failures can
  lose their detailed reason before UI presentation; authored numeric values
  can be clamped silently (`World`, `Runtime/Scene`, `Runtime/Editor`, and
  `Scene/AuthoredSceneParserRuntime.cpp`). These are explicit E0 adjudication
  rows, not presumed errors or presumed harmless fallbacks.
- FP0/FP1 retained executables import PIX but omit its runtime DLL. E5 owns the
  backfill and future bundle gate.

## Goals

1. Give every SB error one mechanically unavoidable reporting path.
2. Preserve the creation site, not merely the final propagation or process-exit
   site.
3. Make each non-shipping record durable and readable as one contiguous message
   followed by its stack.
4. Keep Profile, Profile-WPO, and Automation free of Debug warning chatter while
   retaining recoverable and fatal diagnostics.
5. Keep Release logging minimal: one fatal SB owner/message record only.
6. Make log location independent of the caller's current working directory and
   make sink-establishment failures observable.
7. Make every retained or distributed executable bundle runnable with its
   non-system dynamic dependencies.
8. Give agents deterministic commands and paths for retrieving the last error
   packet after any failed validation or application run.
9. Give every existing error and runtime assertion an actionable description,
   and make every non-shipping assertion failure print its description and
   creation-site call stack.

## Non-Goals

- Do not add general telemetry, analytics, remote upload, crash reporting, or a
  user identity.
- Do not log every successful result, ordinary event, frame marker, or Physics
  diagnostic row through the SB error sink.
- Do not add Release warnings, recoverable-error chatter, symbolization, or call
  stacks.
- Do not turn a recoverable result into an exception or change the 16-byte
  `SbResult` carrier.
- Do not make Rendering, Physics, Scene, UI, or Runtime own a second global
  logger.
- Do not use a frozen site count, line count, or allowed number of silent paths
  as enforcement.
- Do not treat unit-test framework checks as production runtime assertions.
  Doctest retains its own test-failure reporting; this plan owns first-party
  production `assert(...)` and the code those tests exercise.
- Do not solve a pre-entry loader failure with a catch block or an in-process
  callback that Windows cannot reach.
- Do not refresh Physics, Replay, visual, performance, or SkullScope baselines.

## Dependencies And Scheduling

- E0 has no source-plan predecessor and is immediately ready.
- E1-E4 consume the E0 classification and format contracts in strict order.
- E5 consumes the stable sink/probe contract from E4.
- E6 consumes every earlier phase.
- `RUNTIME_BOUNDARIES` RBS2 and any E phase touching `Runtime/Startup` or
  `Runtime/App` must serialize on those path owners. Other RBS phases may run if
  their current leases are disjoint.
- Physics work may run beside E0-E5 when it does not touch Core diagnostics,
  shared project metadata, or the same build/test outputs.
- UI work may run beside E0-E5 when it does not touch the same Runtime/App
  reporting seam, shared project metadata, or the same build/test outputs.
- E3 lands and freezes the Core reporting/assertion contract before any
  subsystem assertion migration begins. E4 may then use multiple worker
  worktrees under this one plan, but each lane owns one distinct canonical
  subsystem/path owner. No two lanes edit the same subsystem, and the
  coordinator owns the final fan-in.
- Project files, dependency manifests, shared build directories, GPU launch,
  and terminal validation are short-lived exclusive resource leases rather
  than whole-plan locks.

Phase lease forecast (derive exact files from current source before dispatch;
future phases hold no lease):

| Phase | Canonical subsystem/path owners | Short-lived shared resources |
|---|---|---|
| E0 | `Validation Tooling`; new inventory/ruling/reference files only | Focused Python output root |
| E1 | `Core`; `Runtime App` only for bootstrap binding | Core-focused test build/output |
| E2 | `Core`; focused Core/test files | Per-configuration CPU build/output |
| E3 | `Core`, `Runtime App`, `path-owner:Runtime/Startup` | Per-configuration CPU build/output |
| E4 | One assigned canonical subsystem/path owner per worker lane, including explicit Core, Runtime, Physics, Maths, Rendering, World, Scene, Assets, UI, and Gameplay coverage | Lane-local focused test output; fan-in serialized |
| E5 | `Validation Tooling`, `Runtime App`, `path-owner:Runtime/Startup`, Physics artifact paths, orchestrator artifact documentation | Project/package metadata and isolated launch output |
| E6 | No new production owner unless validation finds a defect | All five build roots, CPU tests, isolated launch, then terminal gates |

E4 assignments are atomic and file-complete. A worker may not self-select a raw
site or expand into another subsystem; it pauses and returns the proposed lease
change to the coordinator.

## Target Reporting Contract

### Error classes and promotion

- `Warning`: non-failing diagnostic; Debug only.
- `Recoverable`: a failed `SbResult` whose caller retains a supported way to
  continue, retry, fall back, reject an input, or return to a higher owner.
- `Fatal`: `SB_FATAL`, an unhandled invariant, or a failed `SbResult` reaching a
  boundary at which the application cannot continue.
- Optional-feature unavailability represented as a failed `SbResult` is still a
  recoverable error and is logged in every non-shipping build. If a condition is
  normal capability state and should not log, it must become a non-error value
  contract rather than an unreported failed result.

### Actionable descriptions

Every current and future SB error message must answer, within bounded safe text:

1. Which operation or invariant failed?
2. Which concrete owner, object, path, or input was involved when that context
   exists?
3. Which expected constraint or valid range was violated?
4. Which OS/API result occurred, including both numeric code and decoded text
   when the platform supplies one?
5. What consequence follows: rejected input, fallback, retry, disabled feature,
   process termination, or corrupted invariant?

Messages must not expose secrets, dump unbounded external input, or substitute a
stack for the description. A bare "failed", "unavailable", HRESULT, source
expression, or generic "recoverable operation failed" does not pass review.

### Runtime assertions

- Replace first-party production `assert(...)` with one Core-owned runtime
  assertion contract that stringifies the failed expression and requires an
  explicit owner plus non-empty invariant description.
- Debug, Profile, Profile-WPO, and Automation keep runtime assertions enabled.
  Failure emits one fatal SB packet containing owner, assertion description,
  expression, file, function, and line, followed by the creation-site stack,
  then flushes and terminates.
- Release follows the minimal fatal policy. If an assertion is compiled into
  Release because the invariant protects memory/process safety, it emits only
  the fatal owner/message record and terminates. An assertion intentionally
  omitted from Release needs an exact owner ruling showing which checked public
  boundary makes the condition unreachable.
- `static_assert` remains compile-time enforcement, but every existing and new
  site must retain a non-empty human diagnostic string.
- Assertion reporting cannot depend on CRT assertion dialogs, `_DEBUG`, an
  attached debugger, or `NDEBUG` side effects.

### Non-shipping packet

One mutex-protected packet is emitted and flushed without interleaving:

```text
SB_ERROR severity=<recoverable|fatal> owner="<bounded owner>" message="<bounded escaped message>"
    stack_trace:
      #00 <module>!<symbol>+<offset> (<file>:<line>)
      #01 <module>!<symbol>+<offset> (<file>:<line>)
      ...
```

The header must precede the stack. When symbols or source lines are unavailable,
the frame remains as module plus relative virtual address; dropping the frame or
the entire packet is forbidden. Internal reporter frames are skipped so frame
zero identifies the meaningful caller that created the error.

### Debug warning packet

Debug warnings are bounded durable records through the same serialized sink,
but are not failures and do not carry a call stack:

```text
SB_WARNING owner="<bounded owner>" message="<bounded escaped message>"
```

The complete record is absent from Profile, Profile-WPO, Automation, and
Release. A condition requiring a stack is an SB recoverable/fatal error rather
than a warning.

### Release packet

Release emits only:

```text
FATAL_SB_ERROR owner="<bounded owner>" message="<bounded escaped message>"
```

There is no stack, warning, recoverable record, general event stream, or symbol
initialization in Release. The record is flushed before termination or return
from the fatal process boundary.

### Durable sink

- Non-shipping builds use an executable-location-derived error log, not the
  process current working directory. The exact resolved path is printed to an
  attached/redirected console and returned by one Core query for tests and
  agent tooling.
- Release uses one stable per-user writable product log location and retains
  only its minimal fatal record.
- The sink is established before any result-producing Runtime/Rendering owner
  is published. Primary-path failure tries one explicit fallback. If no durable
  sink can be established, non-shipping startup fails visibly through a bounded
  OS-level emergency write rather than continuing silently. An interactive
  Debug/Profile/Profile-WPO process may add one native dialog; Automation and
  every hidden/noninteractive launch must never display a modal dialog.
- In non-shipping builds, the emergency record still writes the fatal message
  first and a bounded raw
  program-counter/module-RVA stack second. It is usable during CRT/static
  initialization and by the allocation tracker before `WinMain`, console
  attachment, symbol initialization, or ordinary sink establishment. E0 must
  inventory every first-party pre-entry fatal; E1 either routes each through
  this writer or proves the path unreachable before initialization.
- Release uses the same pre-entry-safe writer only for its minimal
  `FATAL_SB_ERROR` owner/message line; it emits no stack or modal dialog.
- The emergency path cannot call `SbDiagnosticStore::Failure`, `EngineLog`, heap
  allocation, symbolization, or any other recursively failing diagnostic path.

### Stack ownership

- The creation thread captures its own stack synchronously at
  `SbDiagnosticStore::FailureV` or `SbFatal`.
- DbgHelp initialization and symbol access have one Core owner and one serialized
  lifetime. No subsystem calls `SymInitialize`, `StackWalk64`, or equivalent
  APIs directly.
- First-party packet assembly uses fixed bounded storage and does not allocate,
  throw, or retain caller borrows. Platform symbol APIs may use their documented
  internal storage after non-shipping startup initialization.
- Error creation from multiple threads produces complete non-interleaved packets
  in creation order at the sink lock. Recursive reporter entry takes the
  emergency path, emits message then bounded raw frames without DbgHelp or heap
  allocation, and terminates rather than deadlocking.

### Result lifetime and duplicate control

- `SbResult` remains 16 bytes and continues to lease only immutable owner/message
  bytes from the App-composed store.
- The origin packet is emitted exactly once when the failed result is created.
  Copies, moves, accessors, destruction, and process-boundary inspection do not
  repeat its stack.
- A boundary may append a small disposition or fatal-promotion line referencing
  the same diagnostic identity, but it may not pretend that propagation-site
  stack is the origin stack.

### Pre-entry launch integrity

- A retained/distributed runtime is a bundle, not a lone `.exe`.
- The bundler inventories non-system imports, stages exact build-matched DLLs,
  and records SHA-256 plus byte size for every bundled executable and runtime.
- A launch probe starts the bundle from a different working directory with a
  sanitized `PATH` and Windows critical-error dialogs suppressed for the probe.
  It must enter `WinMain`, emit its resolved diagnostic path, and exit through a
  bounded probe mode.
- Missing, wrong-architecture, or wrong-version runtime files fail validation
  before an artifact or package is accepted.

## Phase E0 - Ratify And Inventory The Complete Error Surface

Goal: freeze semantics and produce a repeatable current-source measurement
before changing reporting behavior.

Tasks:

- Add `tools/inventory_error_observability.py` with self-test fixtures and a
  repository report. It inventories failed-result construction, `SB_FATAL`, raw
  `stderr`, event-only, dialog/UI-only, assertion-only, status-only, and silent
  recovery sites, production runtime assertions, and description quality.
- Give each site an exact current-source disposition: SB warning, recoverable SB
  error, fatal SB error, successful fallback/value state, test-only deliberate
  failure, runtime assertion, or repair owned by a named E phase.
- Bind rulings to file, normalized operation/site, and source fingerprint. An
  unruled or stale site fails; the row total is never a ceiling or ratchet.
- Bind each current error/assertion description to its site fingerprint and mark
  generic, code-only, expression-only, context-free, or missing descriptions as
  E3/E4 repairs. A ruling cannot approve an inadequate message permanently.
- Add the build-policy matrix and packet grammar to a durable Core reference
  document consumed by implementation and review.
- Record the current retained-executable import/bundle mismatch as an E5 repair,
  not as an accepted artifact exception.

Acceptance:

- The inventory self-test proves all site classes and stale-ruling failure.
- The repository report covers every current SB failure/fatal construction,
  wrapper/message-template construction, raw error-like sink, dialog/UI-only,
  status-only, counter-only, silent-recovery, pre-entry fatal, and runtime-
  assertion site without an unclassified row.
- Independent review confirms that ordinary fallback values were not mislabeled
  merely to reduce work.

## Phase E1 - Establish The All-Configuration Durable Sink

Goal: make the error sink available under the exact build policy before central
error creation begins using it.

Tasks:

- Separate the SB error sink from Debug-only bulk/event diagnostics while
  retaining one Core owner and one serialization lock.
- Enable warning/error/fatal policy through explicit configuration definitions,
  not `_DEBUG` inference or an inherited property accident.
- Resolve and publish stable non-shipping and Release log locations.
- Establish the sink before result-producing owners, add the non-recursive
  emergency path, and report open/create/write/flush failures with Win32/CRT
  error values.
- Route the allocation tracker and every other first-party CRT/static-
  initialization fatal through the no-allocation emergency writer, or record
  source/build proof that the path cannot execute before ordinary sink startup.
- Escape control characters so one message cannot forge a second packet or
  corrupt stack framing.
- Preserve existing bulk diagnostic buffering independently from immediate SB
  error durability.

Acceptance:

- Focused tests prove append/open/flush, fallback-path, unwritable-path,
  re-entry, and multi-thread packet atomicity.
- Debug accepts the exact durable warning grammar; Profile/Profile-WPO/
  Automation/Release contain no warning write. Release contains no recoverable
  logging, emission, or recoverable-record grammar.
- The log path remains identical when the same executable is launched from two
  different working directories.
- Pre-`WinMain` fatal probes emit message then raw frames without heap, DbgHelp,
  console setup, or ordinary logger initialization; Automation/hidden probes
  terminate without a modal window, and Release emits only its minimal fatal
  line.

## Phase E2 - Log Every Failed Result At Its Creation Site

Goal: make recoverable-error logging mechanically unavoidable in every
non-shipping configuration.

Tasks:

- Inside `SbDiagnosticStore::FailureV`, reserve the result slot and copy the
  bounded owner/message data while holding only the diagnostic-store lock, then
  release that lock before stack capture, symbolization, sink locking, or packet
  emission. Emit one origin packet before returning the failed lease.
- Initialize and serialize the non-shipping stack symbolizer under Core.
- Preserve raw module/RVA frames whenever symbol or line resolution fails.
- Skip only the fixed reporter frames; never omit the first subsystem caller.
- Preserve `SbResult` size, slot/generation/lease behavior, no-throw behavior,
  and store lifetime rules.
- Remove manual duplicate origin logging where a later owner currently repeats
  the same failed result. Retain distinct disposition, recovery, and exit facts.

Acceptance:

- One probe per Debug/Profile/Profile-WPO/Automation creates a recoverable error,
  continues successfully, and produces exactly one message-first packet with a
  named creation caller in its stack.
- Copy/move/return/destruction probes retain exactly one origin packet.
- Concurrent creation produces complete packets with no mixed header/frames.
- A lock-order probe proves stack capture and sink emission occur after the
  diagnostic-store lock is released and cannot deadlock store reuse.
- Release creates and handles the same recoverable result without writing an SB
  error record.

## Phase E3 - Unify Fatal, Warning, And Process-Boundary Reporting

Goal: close the remaining central severity paths and the Release-only policy.

Tasks:

- Route `SB_FATAL` through the same non-shipping message-plus-stack packet, flush,
  then preserve break/abort semantics.
- Establish the Core runtime assertion contract and migrate the Core plus
  process-boundary sites owned by this phase. Stringified expressions and source
  locations supplement rather than replace explicit owner and human invariant
  descriptions. E4 owns the file-complete per-subsystem migration after this
  contract is integrated.
- Add the minimal Release fatal writer with no symbolizer, stack, warning, or
  recoverable dependencies.
- Promote non-continuable startup/runtime `SbResult` outcomes to one fatal
  process disposition. In Release this is the only record; in non-shipping it
  references the already-emitted origin rather than inventing a new origin
  stack.
- Route Debug warnings through the exact bounded `SB_WARNING` grammar and keep
  them absent from Profile/Profile-WPO/Automation/Release.
- Align terminate and unhandled-exception paths with the build matrix without
  weakening the Windows crash evidence already available in Debug.

Acceptance:

- Fatal child probes in every configuration flush before abnormal termination.
- Assertion child probes in Debug/Profile/Profile-WPO/Automation contain the
  failed expression, owner, description, source location, then stack; Release
  contains only its permitted minimal fatal line.
- Focused Core/process-boundary inventory rows contain no CRT `assert(...)`, no
  runtime assertion without a description, and no description-free
  `static_assert`; every remaining subsystem assertion is an exact E4 repair
  row rather than silently accepted debt.
- A Debug warning probe emits exactly one bounded `SB_WARNING` record; identical
  Profile, Profile-WPO, Automation, and Release probes emit none.
- Debug/Profile/Profile-WPO/Automation fatal packets contain message then stack;
  Release contains exactly one `FATAL_SB_ERROR` owner/message line and no stack.
- A recoverable result that terminates startup produces the required Release
  fatal line; one that is handled and continues produces no Release log.
- Compile/link evidence proves the Release binary does not contain the stack
  grammar, warning grammar, or DbgHelp symbolizer entry points.

## Phase E4 - Close Raw And Locally Recovered Error Paths

Goal: ensure the central APIs cover real production failures rather than only
the callers already returning `SbResult`.

Tasks:

- Adjudicate every E0 inventory row that reports through only `stderr`, a Debug
  event, a dialog/UI status, a counter, boolean/status return, assertion, or
  silent fallback.
- Replace every remaining production CRT `assert(...)` and add explicit owner
  and actionable invariant descriptions to every existing runtime assertion.
  Review description-free `static_assert` sites in the same file-complete lane.
- Review and repair every existing SB error message against the actionable
  description contract. Add operation, invariant, bounded context, expected
  constraint, decoded platform result, and consequence where applicable.
- Convert genuine recoverable/fatal conditions to the central SB path at their
  owning boundary. Preserve UI feedback and validation output as secondary
  presentation, not as the only evidence.
- Convert genuinely ordinary capability absence/defaulting to typed non-error
  values so it does not manufacture call-stack noise.
- Shard migrations by canonical subsystem/path owner only when leases are
  disjoint. Assign explicit, file-complete lanes for Core, each affected Runtime
  package, Physics, Maths, Rendering, World, Scene, Assets, UI, and Gameplay;
  combine none of those owners merely to save a slot. Each active worker uses a
  separate worktree and the coordinator performs one reviewed fan-in. The
  Physics and Maths lanes serialize against FP2 or any active RAGDOLL Physics/
  Maths lease.
- Each lane packet inventories every tracked production source file under its
  assigned owner and covers direct failures, wrappers/message templates,
  assertion sites, status/counter/UI-only evidence, and silent recovery. A lane
  cannot close from a search limited to direct `Failure(...)` calls.
- Remove or update exact E0 repair rulings as their current source changes.

Acceptance:

- The strict inventory has zero unruled or stale error-like sites.
- The strict inventory has zero missing or inadequate error/assertion
  descriptions.
- The file-complete subsystem packets account for every tracked production file
  in every affected owner and contain no unresolved assertion, wrapper,
  status-only, counter-only, UI-only, or silent-recovery row.
- Every accepted recoverable/fatal site reaches the central creation API.
- Every retained non-error fallback has a concrete owner/invariant explanation
  and no error wording or failed result.
- Focused subsystem tests prove continued behavior, UI feedback where intended,
  and one durable origin packet in non-shipping builds.

## Phase E5 - Make Every Runtime Artifact A Launchable Bundle

Goal: close failures that occur before the in-process reporter can run.

Tasks:

- Add a deterministic runtime-bundle tool that inventories imports, distinguishes
  Windows system DLLs from repository/package runtimes, resolves the recursive
  transitive PE-import closure, stages exact non-system dependencies, and writes
  the digest manifest.
- Add an isolated launch verifier with a different working directory, sanitized
  `PATH`, bounded timeout, captured streams, and suppressed Windows critical-
  error UI.
- Add an App/Startup-owned bounded diagnostic launch-probe mode that enters
  `WinMain`, establishes and prints the resolved diagnostic path, performs no
  ordinary scene/GPU work, and exits through a named success code. This seam is
  implemented and integrated under the E5 Runtime leases before bundle probing.
- Backfill FP0/FP1 retained Physics artifacts with their exact build-matched
  `WinPixEventRuntime.dll` and update their manifests without changing the
  executable bytes.
- Make FP2+ Physics artifacts and every future retained executable use the
  bundler. Update the orchestrator artifact handoff contract accordingly.
- Add the same bundle check to distributable Release staging.

Acceptance:

- Deleting the staged PIX runtime makes the negative fixture fail before launch
  with the missing import named.
- Wrong architecture and wrong digest fixtures fail closed.
- FP0 and FP1 launch from their own bundle under sanitized `PATH`, enter
  `WinMain`, and produce the bounded probe evidence without a modal loader box.
- The manifest records every executable/runtime path, size, SHA-256, source
  package/version, and isolated-launch result.

## Phase E6 - All-Build Terminal Closure

Goal: prove the policy across configurations and make it mandatory for future
work.

Tasks:

- Run the complete warning/recoverable/fatal child matrix for Debug, Profile,
  Profile-WPO, Automation, and Release.
- Run the runtime-assertion matrix in every configuration and exact negative
  fixtures for missing owner/description, CRT `assert`, expression-only text,
  and description-free `static_assert`.
- Prove error message ordering, creation-site frame identity, symbol fallback,
  flush-before-exit, concurrency, recursive failure, working-directory
  independence, and Release binary/content exclusion.
- Run isolated bundle checks for every retained/distributed runtime in scope.
- Wire the strict observability inventory, build-policy check, focused probe,
  and bundle check into fast/full/hosted gates at proportionate layers.
- Run dependency, allocation, configuration, reachability, signature,
  complexity, glossary, and comment-quality reviews for the touched source.
- Obtain an independent terminal review of the complete source, build matrix,
  negative fixtures, and actual log packets.

Acceptance:

- All five configuration builds and their exact diagnostic matrices pass.
- No non-shipping recoverable/fatal probe lacks a message-first creation stack.
- No non-shipping assertion probe lacks expression, owner, human description,
  source location, and stack.
- Release logs only fatal owner/message and links no stack-symbolization path.
- Every accepted bundle passes the isolated launch verifier.
- The strict inventory and all mapped repository gates pass with no baseline
  refresh.

## Validation Map

| Change | Required evidence |
|---|---|
| Plan/reference only | Markdown links, plan registration, `git diff --check` |
| Inventory/rulings | Self-test, positive/negative fixtures, strict repository scan |
| Sink/path/format | Focused Core tests, concurrency/re-entry tests, Debug/Profile/Profile-WPO/Automation/Release probes |
| Stack capture | Named creation frame, raw-frame fallback, message-before-stack, no interleaving |
| Assertion contract | No production CRT assert, required owner/description, expression/source/stack probes in every configuration |
| Message descriptions | Strict current-site inventory plus owner review of operation, context, platform text, and consequence |
| Build policy | All five configurations, effective-definition inventory, Release binary/content inspection |
| Raw-site migration | Owning subsystem tests plus strict observability inventory |
| Artifact bundle | Import inventory, digest manifest, missing/wrong runtime negatives, isolated launch |
| Terminal closure | `validate_fast`, all CPU tests, all configuration builds, mapped app/DX12 gates, independent review |

Heavy, GPU, stress, and terminal gates run once after source fan-in. Worker lanes
use focused CPU checks and isolated output roots while implementing.

## Mandatory Review Questions

1. Does every failed `SbResult` emit exactly one origin packet in every
   non-shipping configuration?
2. Does the first meaningful frame identify the creation caller rather than the
   process boundary or logger?
3. Does every current error explain the failed operation/invariant, useful
   bounded context, platform result, and consequence instead of merely naming a
   code or saying "failed"?
4. Does every production runtime assertion report its expression, owner, human
   invariant description, source location, and non-shipping stack?
5. Can a log-open, write, symbol, recursion, or concurrent failure make the
   original error disappear or deadlock?
6. Are warnings Debug-only and are expected capability states represented as
   values rather than hidden failures?
7. Does Release contain only the minimal fatal path, including when a failed
   result becomes process-fatal?
8. Is log location stable across working directories and visible to agents?
9. Did any subsystem gain a second logger, symbolizer, or retained diagnostic
   owner?
10. Can any retained/distributed executable still reach the Windows loader
   without every required non-system runtime beside it?
11. Are enforcement inventories current qualitative judgements rather than count
   budgets?
12. Did any test pass by accepting a dialog, missing stack, stale log, or
    ambient `PATH` dependency?

## Stop Conditions

Stop the active phase if:

- a non-shipping failed result can be created without a durable origin packet;
- a call stack records only the final handler instead of the creator;
- error packets can interleave, recurse, allocate through first-party runtime
  paths, or deadlock;
- Release logs a warning, recoverable error, stack, or general Debug event;
- Release can terminate from a failed `SbResult` without one minimal fatal line;
- the sink depends on current working directory or fails silently;
- a raw error path is hidden by rewording it as an ordinary fallback;
- an existing error remains generic, code-only, or without an actionable
  description;
- a production assertion uses the CRT dialog/path, disappears under a required
  non-shipping configuration, or lacks expression, description, source, or
  call stack;
- a retained/distributed executable depends on ambient `PATH` or an unstaged
  package runtime;
- a new checker introduces a count budget or stale-source allowance;
- validation would require an unrelated golden refresh.

## Completion Reporting

Each phase commit records:

- the exact build policies and configurations exercised;
- the focused warning/recoverable/fatal packet paths and hashes;
- the assertion inventory, description repairs, and per-configuration assertion
  packet results;
- the resolved log path and working-directory probe;
- stack symbol and raw-frame fallback results;
- strict inventory disposition changes;
- bundle contents, dependency versions, sizes, and SHA-256 values when touched;
- focused and terminal validation results;
- independent-review findings and repairs;
- confirmation that no owner-controlled baseline changed.

E6 closes only when the final report can state: every existing error has an
actionable description; every non-shipping SB recoverable/fatal error emits its
message followed by its creation stack; every non-shipping runtime assertion
emits its failed expression, description, source, and stack; Debug alone adds
warnings; Release writes fatal SB owner/message only; and every accepted runtime
artifact launches from its own verified bundle.
