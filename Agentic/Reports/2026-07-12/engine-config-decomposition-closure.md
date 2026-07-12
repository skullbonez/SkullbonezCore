# EngineConfig Decomposition Closure Draft

Date: 2026-07-12
Plan: `engine-config-decomposition`
Baseline: `cb1d7db4`
E4 reviewed head: `748c4bc2`
Result: E5 review fixes implemented; independent follow-up and formal gates pending

## Closure boundary

`EngineConfig` is composition only: its class body contains 23 named domain
value members and zero direct `bool`, `int`, `float`, `double`, or
`std::string` fields. A targeted scan derived the 48 pre-E3 direct field names
from the baseline header and found zero remaining direct config-access hits in
current source or tests.

Every composed member has an owner statement. The moved domains document their
policy or concrete consumer beside the struct, and the E1 inventory records the
consumer/validation owner per key. The pre-existing composed domains are owned
as follows:

| Domain | Concrete owner |
|---|---|
| `WindowConfig` | Window/Init startup display creation |
| `RuntimeRenderFlags` | `RuntimeRenderer` live presentation switches |
| `ContactAudioConfig` | `ContactAudioService` voice/gain presentation policy |
| `SceneLightConfig` | `RuntimeRenderer` scene-light presentation |
| `OrdinaryRenderConfig` | `RuntimeRenderer` ordinary pass/shader profile |
| `CinematicRenderConfig` | `RuntimeRenderer` cinematic pass profile |

## Registry proof

- There are 23 one-per-domain static binding tables containing exactly 218
  rows and 218 unique public keys.
- The global compatibility order contains 27 slices because runtime-render,
  world-force, body-simulation, and physics-material rows were historically
  interleaved.
- Each slice now records a typed domain plus first/count coordinates. The
  runtime resolves the table from that domain, so a slice cannot silently point
  at a different table.
- `ConfigSettingOrderCoversEveryDomainRowExactlyOnce()` is a constexpr nested
  proof over every table row. It rejects any missing, duplicate, or
  out-of-bounds slice at compile time. Full-domain additions expand locally;
  additions to an interleaved table cause an obvious static-assert failure
  until their compatibility position is added.
- Both `FindConfigSetting()` and `EngineConfig::Dump()` call
  `VisitConfigSettingsInOrder()`. Early lookup termination occurs only after
  the compile-time registry proof has succeeded.
- Review of all added source lines found no heap/growth/throw token and no new
  migration-named owner type. `ConfigSettingRange` is fixed metadata only, not
  a service/context/authority bag.

## Independent review findings and fixes

The first plan-end review credibly reopened four closure gaps.

1. Count-only/debug traversal checks did not prove sliced exact coverage.
   Replaced by the compile-time domain/row proof above.
2. Parser compatibility had no durable public regression test. Added three
   main-doctest cases through only `EngineConfig::Load()` and `Dump()`.
3. Existing composed structs did not all literally name a concrete owner.
   Added owner comments for window, runtime-render, contact-audio, scene-light,
   ordinary-render, and cinematic-render values after checking adjacent domains.
4. Acceptance still named `physics_regression_solver.csv` as the per-commit
   canonical artifact. The current AGENTS/tool contract uses
   `validate_physics` and `physics_regression_varied.csv`; the retained solver
   signature is now an explicit supplemental `validate_physics_deep` closure
   gate rather than silently weakened or falsely claimed.

No final review conclusion is claimed here. Independent follow-up remains
pending after these fixes.

## Public parser regression coverage

`SkullbonezTests/TestConfig.cpp` is part of the existing
`SKULLBONEZ_TESTS` target and covers:

- canonical integer, float, boolean, and string input through `Load()`;
- a 219-line dump with 218 unique setting rows;
- a stable FNV-1a fingerprint over all 218 key names in dump order, preventing
  omissions, duplicates, renames, or reorders from false-passing;
- unknown-key skip-and-continue, with valid rows on both sides applied;
- malformed numeric and out-of-range rejection, defaults retained, and a later
  valid row applied to prove parsing continued; and
- cleanup of both cold temporary fixture files.

The expected warning lines for the unknown/malformed fixtures are observable in
test output. The assertions verify destination state as well, so warning-only
or unread-file false passes cannot satisfy the cases.

## Comment-style audit

The inventory intersects `git diff --name-only cb1d7db4^..748c4bc2` with
tracked source-bearing files from `git ls-files`, then includes the new E5 test
source. All 28 files have complete learning headers and were inspected during
their E2, E3, E4, or E5 touched-source audit at the changed sections for local
owner, units, determinism, ordering, and hazard explanations. The independent
follow-up review caught and corrected the originally exclusive E2 range.

Checklist source: the completed plan's `E5 full-plan comment audit checklist`,
deleted by this closure commit and retained in git history.

- Checked: 28/28
- Deferred: 0
- Unchecked: 0
- Human wording decisions pending: none

## Validation evidence

Historical plan gates already recorded:

| Slice | Command | Result |
|---|---|---|
| E2 focused | `tools\validate_build.bat Profile` | Passed in 17.75s, 0 warnings/errors |
| E2 grouped | `tools\validate_full.bat` | Passed in 134.940s; CPU/DX12/physics clean |
| E3 focused | `tools\validate_build.bat Profile` | Passed in 17.28s, 0 warnings/errors |
| E3 performance | `tools\validate_perf.bat` | Passed in 65.847s |
| E3 grouped | `tools\validate_full.bat` | Passed in 104.114s; varied baseline byte-exact |
| E4 focused | `tools\validate_build.bat Profile` | Passed in 6.71s, 0 warnings/errors |
| E4 dump smoke | `--dump-config --scene-load-only` | Exit 0 in 2.025s; 219 lines, empty stderr |
| E4 grouped | `tools\validate_full.bat` | Passed in 102.856s; varied baseline byte-exact |

Current E5 focused evidence:

| Command | Result |
|---|---|
| `tools\validate_build.bat Profile` | Full E5 addition passed in 17.31s; final incremental registry refinement passed in 7.08s; both 0 warnings/errors |
| `Profile\SKULLBONEZ_TESTS.exe` | Final source passed in 2.7s; 139/139 cases and 3,311/3,311 assertions |
| `Profile\SKULLBONEZ_TESTS.exe --test-case="EngineConfig:*"` | Passed in 0.3s; 3/3 cases and 458/458 assertions; expected rejection warnings observed |

The E2/E3/E4 dump comparisons each retained the 219-line block and SHA-256
`3bcc5f6247d6266b8f01dba7a7ebcf1963998e998713c80b14dc0deb3a78ab74`.
E3 and E4 formal gates each matched all 44,401 lines of the current canonical
`physics_regression_varied.csv` byte-exactly.

## Closure gates

- [x] Independent follow-up review resolved every technical finding and the
      corrected 28-file comment-audit inventory leaves no remaining blocker.
- [x] `tools\validate_physics_deep.bat` passed in 122.369s and matched the
      varied, bullet sweep, shooting, three-body, known-issue/solver-signature,
      and SkullScope query baselines.
- [x] Exact final runtime dump comparison retained 219 lines, 0 diff rows, and
      SHA-256
      `3bcc5f6247d6266b8f01dba7a7ebcf1963998e998713c80b14dc0deb3a78ab74`.
- [x] The first `tools\validate_full.bat` closure attempt stopped in mandatory
      preflight after 10.478s because `Config.h` needed the repository header
      format pipeline. Targeted clang-format plus inline-comment alignment
      changed only the intended owner comments; the header pipeline then passed.
- [x] The final `tools\validate_full.bat` retry passed in 134.932s: 139/139
      doctest cases and 3,311/3,311 assertions plus every standalone CPU lane,
      0-warning/0-error Profile and Debug builds, 0 DX12 InfoQueue errors with
      all three screenshot comparisons passing, and the 44,401-line varied
      physics baseline byte-exactly.
- [x] MASTER-PLAN and SessionState advance to shader modernization; the
      completed TODO plan is deleted in the closure commit.
