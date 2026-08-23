# SB Error Observability And Launch-Integrity Reference

Purpose: define the durable Core reporting contract that error creation,
assertion handling, process boundaries, packaging, validation, and review must
share. This reference is the implementation oracle for the error-observability
work; the current-source inventory in `tools/error_observability_rulings.json`
records how existing sites relate to it.

Owner: Core diagnostics and the process-launch boundary.

## Error Semantics

| Class | Meaning | Required behavior |
|---|---|---|
| Warning | A non-failing diagnostic. | Debug only; one bounded durable record and no stack. |
| Recoverable SB error | A failed `Core::SbResult` whose caller retains a supported way to continue, retry, fall back, reject input, or return failure to a higher owner. | Log once at creation in every non-shipping build. |
| Fatal SB error | `SB_FATAL`, an unhandled invariant, or a failed result reaching a boundary where the application cannot continue. | Flush one fatal packet, then terminate or return from the fatal process boundary. |
| Successful fallback/value state | A normal capability, predicate, reset, or explicitly successful alternative represented without a failed `SbResult`. | No SB error record. The value contract must make the state explicit. |
| Test-only deliberate failure | A bounded validation/probe outcome that deliberately manufactures or records failure evidence. | Keep it distinguishable from a production error origin. |

A warning is never an error. Optional-feature unavailability represented by a
failed `SbResult` is a recoverable error and is logged in every non-shipping
build. If unavailability is normal capability state and should not log, expose
it as a non-error value contract instead of creating an unreported failed
result.

A recoverable result is promoted to a fatal process outcome when it reaches a
boundary that cannot continue. That promotion may append a small disposition
record referencing the original diagnostic identity; it must not recapture a
propagation-site stack or pretend that the boundary created the error.

## Build-Policy Matrix

| Build | Warnings | Recoverable SB errors | Fatal SB errors | Stack policy |
|---|---|---|---|---|
| Debug | Durable | Durable | Durable | Message first, then creation-site stack for recoverable and fatal errors. |
| Profile | Off | Durable | Durable | Message first, then creation-site stack. |
| Profile-WPO | Off | Durable | Durable | Message first, then creation-site stack. |
| Automation | Off | Durable | Durable | Message first, then creation-site stack. |
| Release | Off | Off | Durable, minimal | Fatal owner/message only; no stack. |

`Release|x64` is the shipping configuration because the solution currently has
no separate Ship configuration. A future Ship configuration must copy the
Release policy explicitly; it must not inherit non-shipping diagnostics.

Release contains no warning record, recoverable record or grammar, general
event stream, stack capture, symbol initialization, or modal diagnostic dialog.
A handled recoverable result remains silent in Release. If that result reaches
a fatal process boundary, Release retains its owner/message as the one minimal
fatal record.

## Packet Grammar

Each packet is serialized by one Core-owned sink lock, written without
interleaving, and flushed before another packet can begin.

Non-shipping recoverable and fatal errors use exactly this header and stack
order:

```text
SB_ERROR severity=<recoverable|fatal> owner="<bounded owner>" message="<bounded escaped message>"
    stack_trace:
      #00 <module>!<symbol>+<offset> (<file>:<line>)
      #01 <module>!<symbol>+<offset> (<file>:<line>)
      ...
```

The header always precedes the stack. Internal reporter frames are skipped so
frame zero identifies the meaningful creation caller. If symbols or source
lines are unavailable, the frame remains present as module plus relative
virtual address. Dropping a frame or the packet is forbidden.

Debug warnings use exactly one bounded record and no stack:

```text
SB_WARNING owner="<bounded owner>" message="<bounded escaped message>"
```

Profile, Profile-WPO, Automation, and Release emit no warning record.

Release emits only:

```text
FATAL_SB_ERROR owner="<bounded owner>" message="<bounded escaped message>"
```

Control characters in owner/message text are escaped so caller data cannot
forge a second record or corrupt stack framing. First-party packet assembly is
bounded, non-throwing, and allocation-free; it copies all caller text needed by
the packet and retains no caller borrow.

## Actionable Descriptions

Every SB error and runtime assertion carries bounded, safe human text. When the
context exists, the description answers:

1. Which operation or invariant failed?
2. Which concrete owner, object, path, or input was involved?
3. Which expected constraint or valid range was violated?
4. Which OS/API result occurred, including numeric code and decoded text when
   the platform supplies both?
5. What follows: rejected input, fallback, retry, disabled feature, process
   termination, or corrupted invariant?

A bare `failed`, `unavailable`, numeric error code, source expression, or
generic constraint such as `Recoverable operation must not fail`, `This
operation is invalid now`, or `Resource must be available here` is not
actionable. Four words plus `must` is not context. The bounded classifier also
requires multiple domain-bearing words or a specific identifier such as
`setCameraPose` or `ui.stressActions`; formatted platform failures retain the
numeric/API-result exception. A stack supplements the description; it never
substitutes for one. Messages must not expose secrets or copy unbounded
external input.

The inventory uses these exact description classifications:

| Classification | Meaning |
|---|---|
| `actionable` | Bounded text names meaningful context and an expected constraint or failure condition. |
| `generic` | Only generic error vocabulary is present. |
| `code-only` | Only a numeric/API formatting token or code is present. |
| `expression-only` | A runtime assertion supplies only its source expression. |
| `context-free` | Some human text exists, but it lacks enough concrete operation/constraint context. |
| `missing` | The site supplies no discoverable description. |
| `not-applicable` | The reviewed site is a non-error value/test outcome or a binary bundle fact with no runtime message contract. |

`generic`, `code-only`, `expression-only`, `context-free`, and `missing` are
inadequate for an SB error or runtime assertion and must retain an E3/E4 repair
owner. Editing the description changes the source fingerprint and invalidates
the old ruling.

## Runtime Assertions

First-party production runtime assertions use one Core-owned contract that
stringifies the failed expression and requires an explicit owner plus a
non-empty invariant description.

- Debug, Profile, Profile-WPO, and Automation keep runtime assertions enabled.
  Failure emits owner, description, expression, file, function, and line,
  followed by the creation-site stack, then flushes and terminates.
- A Release assertion retained for memory/process safety emits only the minimal
  fatal owner/message record and terminates. An assertion omitted from Release
  needs an exact owner ruling naming the checked public boundary that makes the
  condition unreachable.
- `static_assert` remains compile-time enforcement, and every site retains a
  non-empty human diagnostic string.
- Reporting cannot depend on a CRT assertion dialog, `_DEBUG`, an attached
  debugger, or `NDEBUG` side effects.

## Durable Sink And Emergency Path

Non-shipping builds resolve the error log from the executable location, never
from the process current working directory. One Core query returns that path to
tests and agent tooling, and an attached or redirected console receives it.
Release uses one stable per-user writable product log location containing only
minimal fatal records.

The sink is established before any result-producing Runtime or Rendering owner
is published. Primary establishment failure tries one explicit fallback. If no
durable sink can be established, non-shipping startup fails visibly through a
bounded OS-level emergency write. Interactive Debug/Profile/Profile-WPO may add
one native dialog; Automation and every hidden/noninteractive launch must never
display one.

The emergency path is usable during CRT/static initialization and by the
allocation tracker before `WinMain`, console attachment, symbol initialization,
or ordinary sink establishment. In non-shipping builds it emits the fatal
message first and a bounded raw program-counter/module-RVA stack second.
Release uses it only for the minimal `FATAL_SB_ERROR` line. It cannot call
`SbDiagnosticStore::Failure`, `EngineLog`, heap allocation, symbolization, or
another recursively failing diagnostic path.

The creation thread captures its own stack synchronously in
`SbDiagnosticStore::FailureV` or `SbFatal`. DbgHelp initialization and symbol
access have one Core owner and one serialized lifetime; subsystems never call
`SymInitialize`, `StackWalk64`, or equivalent APIs directly. Recursive reporter
entry takes the emergency path, emits message then bounded raw frames without
DbgHelp or heap allocation, and terminates instead of deadlocking.

No sampling, deduplication, first-occurrence suppression, or “expected
fallback” exception may hide an SB error. The origin packet is emitted exactly
once when the failed result is created. Copies, moves, accessors, destruction,
display, and boundary inspection do not emit it again.

`SbResult` remains 16 bytes and leases only immutable owner/message bytes from
the App-composed diagnostic store. `SkullbonezSource/Core/SbResult.h` owns the
carrier-size assertion; `SkullbonezSource/Core/SbDiagnosticStore.h` owns the
current 96-byte owner capacity, 512-byte message capacity, and 256 store
entries. Later changes require an explicit owner decision; callers must never
assume unbounded text.

## Inventory And Ruling Contract

`tools/inventory_error_observability.py` scans every tracked `.cpp`, `.h`,
`.hpp`, and `.inl` file under `SkullbonezSource` using `git ls-files`. It also
parses tracked retained PE executables for known package-owned imports whose
sibling runtime is absent. Comments and string data are masked with the shared
`tools/cpp_source_scan.py` lexer before call-shape discovery. Continued
`#define` replacement bodies receive a second bounded pass: only a lexically
real macro name and its executable body are restored, while comment, ordinary
string, raw-string, and handled-result decoys remain masked.

The current site classes cover failed-result creation, central result/fatal
owners, failure/error wrappers, `SB_FATAL`, pre-entry termination, raw stderr,
event/dialog/debugger/status presentation, message templates, runtime and
static assertions, status-only returns, counter-only evidence, silent recovery
operations, ignored CRT open/write/flush/close outcomes, and retained-bundle
import mismatches. An ignored outcome row is separate from any raw-stderr row
at the same call: one records bypassed presentation, while the other records
that the persistence result itself was discarded.

Each schema-v2 ruling has this exact schema:

| Field | Contract |
|---|---|
| `path`, `line`, `column` | Canonical repository-relative location. |
| `site_class` | Bounded lexical shape that discovered the candidate. |
| `operation` | Deterministic normalized operation identity. |
| `source_fingerprint` | SHA-256 of the exact normalized source slice, or executable bytes plus import identity. |
| `disposition` | `sb-warning`, `recoverable-sb-error`, `fatal-sb-error`, `successful-fallback-value-state`, `test-only-deliberate-failure`, `runtime-assertion`, or `repair`. |
| `description` | Exact bounded current-source description evidence used during review. |
| `description_classification` | One classification from the table above. |
| `owner`, `reason` | Concrete path owner and qualitative judgement. |
| `source_context` | Exact bounded current finding excerpt. This is mechanical identity evidence, not semantic adjudication. |
| `semantic_evidence` | Owner-authored operation behavior, source-bound invariant, description basis, and consequence. The identity tag binds currentness only; it contributes neither novelty nor uniqueness. |
| `repair_phase` | Blank only when no repair remains; otherwise exactly E1-E5. |
| `adjudication` | `owner-reviewed` only after a concrete owner has reviewed this exact identity; generated rows use `unreviewed`. |

Repair ownership is phase-specific: E1 owns sink establishment and Core
pre-entry delivery; E2 owns mechanically unavoidable failed-result creation;
E3 owns fatal/assertion/build-policy/process-boundary work; E4 owns adjudication
and migration of non-central/silent sites; E5 owns runnable-bundle closure.

Strict mode fails on malformed rows, duplicate identities, an unratified file,
a missing durable reference/repair plan, a new unruled site, an edited or
deleted stale ruling, description evidence/classification drift, copied or
drifted source context, an unreviewed row, an unchanged generator suggestion
presented as adjudication, a stock field-derived semantic basis, or an
inadequate error/assertion without a repair phase. A successful fallback/value
row must name its concrete owner plus operation or invariant basis. `FAILED`
HRESULT checks and `Validate*`/`Hash*` failure returns cannot pass as value-only
queries. Every reviewed reason begins with its exact owner and names the exact
operation, site class, path, or current description evidence.

The semantic basis begins with the exact owner and fingerprint-bound
`behavior[<tag>]`, then names the readable operation, its source file/role and
participants, the description-classification basis, and the consequence of the
decision. Strict mode strips the tag before checking novelty and cross-row
conflicts. It rejects the complete non-empty scanner excerpt anywhere in the
basis, equality with `reason` or `source_context`, canned behavior or
consequence prose assembled only from emitted ruling fields, and identical
tag-stripped prose shared by different operation/description/disposition/phase
decisions. Genuinely identical semantic decisions may share prose; a hash or
line number may never manufacture uniqueness.

These checks prove identity/currentness and reject known copied or field-only
mechanical transforms. They cannot prove that an owner-authored behavior,
invariant, or consequence claim is true. Independent review remains the final
semantic authority and may reopen any otherwise mechanical pass whose claim is
wrong. Deleting the identity tag must still leave a readable explanation of
why the exact operation received its disposition and repair phase.

These are current qualitative rulings, not allowances. Row totals, class
counts, and repair counts are measurements only and must never become ceilings,
ratios, or ratchets.

Use the focused evidence commands from the repository root:

```powershell
python tools\inventory_error_observability.py --self-test
python tools\inventory_error_observability.py --strict
python tools\inventory_error_observability.py --strict --format json
```

`--write-unreviewed-template <path>` writes a deterministic candidate file and
refuses to overwrite an existing file. Its `unreviewed` status deliberately
fails strict mode. Changing only the document status or per-row adjudication
spelling still fails because unchanged suggestion fields are not owner review;
removing the suggestion prefix also fails the owner-led exact-basis rule. A
human/agent owner reviews each disposition, exact description/class, owner,
reason, source context, semantic basis, and responsibility-correct repair phase
before the document becomes `ratified`. The self-test exercises every bounded
site class, all ignored/handled `fopen`, `fprintf`, `vfprintf`, `fputs`,
`fputc`, `fwrite`, `fflush`, and `fclose` pairs plus `fopen_s`, the four Config
writer-macro shapes, normal/delay PE imports, and adversarial stock
adjudication/description cases. The adversarial adjudication cases include an
unchanged suggestion, a field-derived rewrite with every emitted field
available, a wrapped copy of the scanner excerpt, a correct-tag canned basis,
a correct-tag canned basis containing every emitted field token, and
tag-stripped prose reused by different decisions.

## Retained-Executable E5 Repair

The retained ragdoll evidence is not a runnable bundle. The current PE import
table proves that each executable imports both `dxcompiler.dll` and
`WinPixEventRuntime.dll`, while its directory contains only the executable and
`manifest.md`. The four exact mismatches are fingerprint-bound E5 repairs in
`tools/error_observability_rulings.json`; they are not artifact exceptions.

| Retained executable | Bytes | Executable SHA-256 | Missing imported runtimes |
|---|---:|---|---|
| `Agentic/Plans/Artifacts/ragdoll-physics-unification/FP0/SKULLBONEZ_CORE-Debug.exe` | 13,931,520 | `cdefc1b53c3de37c0d75fdd9a423b61aac8df368b45919f8a312cf6dc73cc053` | `dxcompiler.dll`, `WinPixEventRuntime.dll` |
| `Agentic/Plans/Artifacts/ragdoll-physics-unification/FP1/SKULLBONEZ_CORE-Debug.exe` | 13,949,440 | `612461c8dbd48eb8823468a8b06d1fb3f576b5610b6e48610b6b5a870ae7888a` | `dxcompiler.dll`, `WinPixEventRuntime.dll` |

The Windows loader runs before `WinMain`, so an in-process logger cannot
intercept a missing-import failure. A retained/distributed runtime is therefore
a bundle, not a lone executable. E5 must inventory all non-system imports,
stage exact build-matched DLLs, record SHA-256 and byte size for every staged
executable/runtime, and reject missing, wrong-architecture, or wrong-version
files before acceptance.

The isolated launch probe starts the bundle from a different working directory
with a sanitized `PATH` and Windows critical-error dialogs suppressed. It must
enter `WinMain`, emit its resolved diagnostic path, and exit through a bounded
probe mode. Passing the in-process inventory cannot waive that launch proof.
