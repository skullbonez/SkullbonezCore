# Dependency Proof Generation — DP0 Comparison

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Compared source tip: `19b34f9bf17e4c3fbf9f9f48bc66b04fdd719648`
Scope: Documentation-only census; no checker, rule, fixture, or source change

## Outcome

`tools/dependency_graph_rules.json` is the enforcement source of truth, but the
human proof surface is not an exact projection of it.

- The authoritative data contains 27 include rules: six broad/boundary rules,
  twenty Runtime package rows, and one exact top-level Runtime file row. It also
  contains one retired-vocabulary rule and one project-ownership rule.
- The `AGENTS.md` Runtime matrix contains the expected 21 rows: twenty packages
  plus `RuntimeFrameViews.h`. Its command block contains only 20 proofs because
  `App` has no command.
- Eighteen of the nineteen package commands match the JSON complement only
  over today's named package universe. None is closed-world: a future unlisted
  Runtime package evades every command while JSON rejects it. The `Input`
  command has an additional current-tree gap: it admits every `App/`, `Replay/`,
  and `Scene/` target while JSON admits only three exact files.
- The `RuntimeFrameViews.h` command is also incomplete over the current tree:
  it misses an include of `Runtime/RuntimeFrameViews.h` itself. Like the package
  commands, it also misses a future top-level Runtime target.
- The standalone UI prose and command cover only `UI -> Runtime`; JSON rejects
  both `UI -> Runtime` and `UI -> Rendering`.
- Human Rendering prose names more feature-domain concepts than its broad
  `rg` proof, while the machine rule intentionally checks only the two retired
  trajectory literals. These are three distinct policy layers and should not
  claim to be identical.
- The checker self-test has 46 negative include edges, two negative content
  fixtures, and one compound project fixture. It proves selected examples, not
  complete field/branch coverage of every rule row; the project negative can
  remain green if either of its two failure branches regresses.

DP1 should make the existing checker render and freshness-check one canonical
marked Markdown projection that owns both duplicated Runtime surfaces: the
21-row matrix and the 20-command block. It should also replace the seven other
mechanical broad/boundary commands, for all 27 mechanical commands total. Only
qualitative ownership prose and the explicitly labelled non-executable
Rendering feature-vocabulary search remain hand-written outside that block.

## Compared Inputs

| Surface | Current role | Classification |
|---|---|---|
| `tools/dependency_graph_rules.json` | 27 include, one content, and one project rule | Authoritative mechanical policy |
| `tools/check_dependency_graph.py` | One generic include/content/project evaluator and its synthetic fixtures | Authoritative executable mechanism |
| `tools/validate_dependency_graph.bat` | Runs self-test, then repository scan | Gate wrapper |
| `AGENTS.md:86-235` | Ownership rationale, broad direction, Runtime table, and shell review proofs | Mixed explanatory and duplicated mechanical policy |
| `tools/README.md:32,182` | Short capability/index description | Explanatory documentation |
| Archived dependency reports | Dated results from prior rule sets | Historical evidence, not a current proof surface |

No generated current-rule report or freshness-checked documentation block
exists at this tip. The checker emits only aggregate counts and findings.

## Count Reconciliation

| Kind | Authoritative rows | Human table rows | Hand-written commands | Fixture result represented by current data |
|---|---:|---:|---:|---:|
| Broad/layer include direction | 5 | 4 broad rows plus a separate UI proof | 5 | 8 selected negative edges |
| Replay downward boundary | 1 | One prose/proof row | 1 | 15 negative edges (three targets across five source prefixes) |
| Runtime package direction | 20 | 20 | 19 (`App` omitted) | 22 selected negative edges |
| Top-level `RuntimeFrameViews.h` | 1 | 1 | 1 | 1 negative edge |
| Retired Rendering vocabulary | 1 | Separate broad and exact proof commands | 2 | 2 negative content fixtures |
| UI single-project ownership | 1 | No matrix/proof row | 0 | 1 compound evaluator fixture |
| **Total** | **27 include + 1 content + 1 project** | **21 Runtime rows plus prose** | **28 commands: 27 mechanical + 1 qualitative** | **46 include + 2 content + 1 compound project** |

The plan problem statement's “approximately 21” Runtime expressions resolves to
twenty commands: nineteen package complements and one top-level file command.

## Runtime Matrix, Row By Row

“Current-set match” means the JSON allowed set and the command's complement
agree only over the twenty packages and one file named today. Every enumerated
command remains open-world and therefore differs from JSON for a future
unlisted Runtime target.

| Source row | JSON versus human table | Hand-written command versus JSON | Classification / difference |
|---|---|---|---|
| `App` | Current named targets match, including the frame-view file | No command | **Wording drift:** “Every Runtime package” sounds open-ended; JSON is closed-world and rejects a future unlisted package (`FutureGodBag` is the fixture). |
| `Automation` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `Camera` | Package set matches | Current-set match; misses any future package | **Explanatory refinement:** “App process values” is narrower than the mechanically allowed whole `Runtime/App` prefix; no machine rule enforces the value-only wording. |
| `Capture` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `DevelopmentTools` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `Diagnostics` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `Direction` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `Editor` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `Input` | Exact only when the table's three named exceptions are read as files: `App/Window.h`, `Replay/ReplayEventCommand.h`, and `Scene/SceneLifecycle.h` | **Not exact now:** admits every file under `App`, `Replay`, and `Scene`; also misses any future package | **Actual executable-proof drift:** the JSON checker is stricter than the claimed mirror in both exact-file and closed-world behavior. |
| `Interaction` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `Render` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `Replay` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift; “never Prediction or Planning” highlights two members of today's complement. |
| `Prediction` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift; “never Planning” is explanatory emphasis. |
| `Planning` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `Scene` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `Simulation` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `Startup` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `Tools` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `UI` | Exact current set | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift; separate top-level UI rule differs below. |
| `Debug` | Exact: self only | Current-set match; misses any future package | Mechanical duplicate with systemic open-world drift |
| `RuntimeFrameViews.h` | Exact: no Runtime target | **Not exact now:** misses an include of `Runtime/RuntimeFrameViews.h`; also misses any future top-level Runtime target | **Actual executable-proof drift:** an enumerated directory regex cannot express the raw `Runtime` prefix deny. |

## Broad, Content, And Project Rules

| Rule | JSON policy | Human proof | Difference / classification |
|---|---|---|---|
| `core_floor` | `Core` denies eight upper roots | Command names the same eight | Mechanical duplicate; its fixture selects only `Runtime` (1/8 target prefixes). |
| `physics_direction` | `Physics` denies six upper roots | Command names the same six | Mechanical duplicate; fixtures select `Assets`, `Scene`, `Runtime`, and relative `World` (4/6). |
| `rendering_direction` | `Rendering` denies `Gameplay`, `Runtime`, `UI` | Command names the same three | Mechanical duplicate; fixture selects only `UI` (1/3). |
| `gameplay_direction` | `Gameplay` denies five upper roots | Command names the same five | Mechanical duplicate; fixture selects only `Scene` (1/5). |
| `ui_direction` | `UI` denies `Runtime` and `Rendering` | Ownership prose and standalone command name only `Runtime` | **Actual documentation/proof drift.** The only negative fixture selects `Rendering`, so neither the prose/command nor fixture alone covers both targets. |
| `replay_downward_boundary` | Five lower source roots deny all three Replay-family Runtime packages | Command names the same source and target sets | Mechanical duplicate; fixture is complete for the named 5 x 3 matrix. |
| `rendering_retired_trajectory_vocabulary` | Exact case-sensitive deletion of `RetainedTrajectory` and `RETAINED_TRAJECTORY` | Exact command matches; separate broad command scans `trajectory`, `porkchop`, `replay`, `prediction` case-insensitively | Exact command is a mechanical duplicate. Broad command is a qualitative review aid, but it omits prose-named `planning`, `cause tree`, and `operator panel` concepts and is not equivalent to the machine rule. |
| `ui_single_project_ownership` | UI source/header paths must belong to `SKULLBONEZ_UI.vcxproj` and not Core/Tests | Mentioned only in `tools/README.md` | Special mechanical rule with concise explanatory documentation; no duplicated AGENTS set. |
| Hot physics field owner ruling | Review-only owner/consumer/stage justification | Prose only | Genuinely explanatory; intentionally absent from dependency data. |
| Runtime feature placement and Replay growth privilege | Responsibility and allocation-owner review | Prose only | Genuinely explanatory; cannot be reduced to include edges. |

## Checker Behavior

### Repository scan

1. `repository_source_files` asks Git for cached plus untracked, non-ignored
   files below `SkullbonezSource`, then keeps `.cpp`, `.h`, `.hpp`, `.inl`, and
   `.hlsl`. “Tracked physical includes” is therefore incomplete wording:
   untracked source is deliberately scanned, while ignored source is not.
2. `INCLUDE_PATTERN` accepts quoted and angle includes, leading whitespace, and
   whitespace between `#` and `include`.
3. `resolve_include` tries the including file's directory first, then a
   source-root-relative spelling. It ignores paths that resolve outside
   `SkullbonezSource`. The rooted fallback is not required to exist, so a
   syntactically repository-rooted but missing target can still be evaluated.
4. `deny` rejects a matching target prefix. `allow` constrains only targets
   within its declared scope and admits either an allowed exact file or prefix.
   Consequently Runtime rules leave lower non-Runtime engine layers to the
   standing broad rules.
   The raw distinction matters: Input's three exceptions use
   `allowed_target_files`, while six rows currently place
   `Runtime/RuntimeFrameViews.h` in `allowed_target_prefixes`. A generated proof
   must label files and prefixes separately. DP1 should migrate those six
   file-shaped entries to `allowed_target_files` rather than infer a different
   type only while rendering. Valid exact-file edges remain allowed, while the
   migration intentionally closes the current prefix-derived pseudo-descendant
   allowance such as `Runtime/RuntimeFrameViews.h/Child.h`; a negative fixture
   must pin that closure.
5. Every matching rule is evaluated. One downward include can produce more
   than one finding, for example a Physics-to-Replay edge.
6. Content rules report every occurrence of each exact literal in their bounded
   source scope.
7. Project ownership enumerates tracked files only, parses `ClCompile` and
   `ClInclude` from project XML, requires the owner project, and rejects each
   forbidden duplicate owner.

### CLI and gate composition

- Repository mode always runs `self_test(config)` before scanning.
- `validate_dependency_graph.bat` first invokes `--self-test`, then invokes
  repository mode, which runs the same self-test again. This is safe but
  redundant.
- Direct `validate_fast` reaches the dependency gate. `validate_all_cpu_tests`
  reaches it unless the owning preflight sets
  `SKULLBONEZ_DEPENDENCY_GRAPH_ALREADY_VALIDATED=1`.
- `validate_full` and hosted mandatory CPU validation run fast preflight first,
  set that flag, and avoid a third invocation in the CPU umbrella. The call
  graph therefore has no enforcement gap.
- Rule version mismatch returns 2. Fixture or repository findings return 1.
  Malformed JSON or missing fields fail through an uncaught parser/key error
  rather than a stable rule-schema diagnostic.

## Fixture Coverage

The current self-test count is deterministic from rule data:

| Fixture family | Count | What it proves | What it does not prove |
|---|---:|---|---|
| Include negative edges | 46 | Selected sources and targets reach the real parser/resolver and are rejected | Every denied prefix, every closed-world Runtime complement member, every allowed prefix, or each exact-file exception |
| Include positives | 27 rule-level targets (31 source/target evaluations because Replay has five sources) | One named allowed edge per rule | All allowed branches; notably none of Input's three exact files |
| Content negatives | 2 | Every currently listed forbidden literal is found end-to-end | Broader feature-neutral Rendering prose |
| Content positive | 1 | `RetainedGeometryStream` is not confused with retired trajectory terms | General false-positive resistance |
| Project fixture | 1 compound negative | The pure evaluator accepts required-only ownership; its negative has the required project missing while Core also owns the file | Either negative branch independently: a missing-required regression still finds forbidden Core, and a forbidden-owner regression still finds missing required. It also omits Tests, XML parsing, path discovery, and project-file failures. |

The include fixture directly compares `edge_violates` with rule fields and then
uses those same fields to compute expected end-to-end findings. It detects
parser/evaluator regressions for selected examples, but it is not an independent
oracle for a changed rule-data set. A policy row can drift from AGENTS while all
self-tests remain green.

DP2 must split the project proof into independent cases: missing required owner
with no forbidden owner present; required owner plus Core; required owner plus
Tests; and end-to-end temporary project XML plus tracked-path discovery. Each
case must assert the exact finding branch, not merely that some finding exists.

## Regex, Ordering, Escaping, And Platform Findings

### Hand-written proof blind spots

The Python checker is stronger than the shell mirrors for normal source syntax.
Runtime commands require literal `#include`, a quoted include, zero or one
`../`, forward slashes, and a package-relative spelling. They miss:

- `# include` and leading indentation;
- repository-local angle includes;
- source-rooted `"Runtime/Package/File.h"` spellings;
- paths with two or more parent traversals;
- backslash-separated includes; and
- Input's exact-file distinction described above.

Broad layer commands accept more path text through `.*`, but still require the
literal `#include` spelling. Generated proofs should report resolved edges, not
generate another regex parser.

The Python parser also has residual preprocessor/search risks:

- macro-expanded include operands such as `#include OWNER_HEADER` are invisible;
- a backslash-continued directive is invisible because the operand is not on
  the matched physical line; and
- angle and quoted includes share the same local-first resolver, while the C++
  compiler gives them different search order. The angle-bracket fixture proves
  token recognition, not faithful compiler search semantics.

DP1 should report these as residual enforcement limits. DP2 should add bounded
fixtures for macro/continuation rejection or an explicit fail-closed policy and
separate quoted/angle resolution behavior; it must not claim that regex parsing
is a compiler dependency graph.

### Determinism and path handling

- Rule and include evaluation follow JSON order, Git output order, file include
  order, then project-rule sorted path order. The final finding list has no
  explicit global sort. Git normally provides stable lexical output, but the
  checker does not state or enforce canonical report ordering.
- `normalize` unifies slash direction but does not case-fold. Prefix matching is
  case-sensitive. The current Windows `Path.resolve` canonicalizes the casing
  of an existing target before matching, so the comparison found no live
  mis-cased-include bypass. A missing rooted fallback retains its spelling, and
  case-sensitive hosts resolve differently; a focused fixture should pin the
  intended cross-platform behavior rather than rely on that incidental
  canonicalization.
- The project parser assumes the Visual Studio MSBuild XML namespace and checks
  project items, not `.filters`. That is appropriate for ownership; project
  filter layout remains a separate validator.
- UTF-8 BOM source is handled. Findings use normalized forward-slash paths.

## DP1 Recommendation

Extend `tools/check_dependency_graph.py`; do not add a second checker.

1. Add one deterministic proof model built from the already loaded config:
   ordered rule id, normalized source scope, mode, exact-file exceptions,
   allowed/denied target scopes, fixture coverage, and special content/project
   rows.
2. Add a review-friendly Markdown renderer, for example
   `--emit-proof markdown`. Canonicalize target lists explicitly rather than
   inheriting incidental JSON/Git ordering.
3. Replace both mechanically maintained Runtime surfaces—the 21-row matrix and
   20 regex commands—plus the seven broad/boundary mechanical regex commands
   with one marked generated block in `AGENTS.md`. Keep only qualitative
   ownership rationale, review-only feature-neutral language, hot-field
   rulings, placement rationale, and one explicitly labelled non-executable
   Rendering vocabulary search outside it.
4. Add `--check-proof AGENTS.md` (or equivalent) to render in memory and
   byte-compare only the marked block. A rule-data edit then fails until the
   projection is regenerated. Missing, duplicate, or reversed markers must fail
   closed. Write mode must preserve every byte outside the unique ordered
   marker pair.
5. Keep repository enforcement on the existing `evaluate_edge`,
   `evaluate_content_rule`, and `evaluate_project_rule` paths. The proof
   renderer describes those rules; it does not re-evaluate edges.
6. Migrate the six `RuntimeFrameViews.h` allow entries from raw prefix lists to
   exact-file lists, and render prefix/file categories without collapsing them.
   Preserve valid exact-file edges and add a negative descendant-path fixture
   for the intentionally closed prefix allowance.
7. DP2 fixtures should plant a rule-data edit, prove emitted Markdown changes,
   and prove the stale block fails. Add branch-complete evaluator fixtures for
   deny prefixes, closed-world Runtime complements (including a future package),
   allowed prefixes, exact-file exceptions, content literals, and project XML
   discovery without introducing edge counts or budgets. Separately prove
   missing, duplicate, and reversed markers fail closed, writes preserve all
   outside-block bytes, and Markdown escaping is deterministic.
8. Make output ordering and case policy explicit while touching the mechanism.
   A path-casing fixture should pin existing-target canonicalization and
   missing-target behavior on supported hosts. Add deterministic Markdown
   escaping fixtures for rule-controlled text so pipes, backticks, angle
   brackets, ampersands, and line breaks cannot corrupt or destabilize the
   generated block.

### Required DP1 evidence

- The generated marked block contains the complete 21-row Runtime policy with
  prefix and exact-file targets distinguishable.
- The old hand-maintained matrix and all 27 mechanical regex commands are absent
  outside that block. The sole surviving broad Rendering vocabulary search is
  explicitly labelled qualitative, non-executable review guidance.
- Two renders from unchanged rule data are byte-identical.
- `--check-proof` accepts the committed block and rejects a planted stale block.
- Missing, duplicate, and reversed block markers fail closed; a generated write
  demonstrably preserves bytes before and after the unique marker pair.
- A planted new Runtime package remains rejected by every closed-world allow
  row and changes the generated proof without Python package hardcoding.
- Input exact-file exceptions and the migrated frame-view exact-file entries
  render and evaluate distinctly from package prefixes. The exact frame-view
  edge remains allowed and a pseudo-descendant edge is rejected.
- Markdown escaping fixtures are deterministic for every delimiter/control
  character the generated table can receive from rule data.
- Broad, content, project, and residual parser limitations are represented
  without claiming that a Markdown projection is a second enforcement engine.

This design has one rule source, one evaluator, and one generated review
surface. It contains no package-specific Python branch, frozen edge count, or
allowance budget.

## Owner Input

No owner policy choice blocks DP1. The current source already establishes:

- JSON remains authoritative;
- concise explanatory prose stays in `AGENTS.md`;
- executable proof is generated by the existing checker;
- no second checker, package hardcoding, edge count, or budget is allowed.

The App row should be rendered as the explicit current closed-world set, and
Camera's “App process values” should remain qualitative prose unless a separate
semantic rule is later designed.
