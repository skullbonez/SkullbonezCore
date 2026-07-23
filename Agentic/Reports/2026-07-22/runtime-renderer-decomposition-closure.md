# RuntimeRenderer Decomposition Closure

Date: 2026-07-22
Branch: `nightrunner-22nd-JUL-26`
Result: Complete - RR0-RR5, 6/6

## Outcome

`RuntimeRenderer` is now the concrete owner of pass instances and live
frame-graph ordering. `RenderResourceLifecycle` owns backend-epoch resources;
`UiTextPass` owns UI-text resources and composition; Replay owns consequence
grade animation; and `RenderModelFramePublisher` projects scene, physics,
diagnostics, worker, and policy owners into the existing stack-only model view.

The final publisher exists outside both `RuntimeRenderer` and the logical
`Run` surface. `Run::PublishRenderModelsPhase` only sequences that typed domain
call. The renderer cannot traverse SceneWorld or PhysicsEngine to rebuild the
view, and the publisher retains no state, callback, host reference, service
bag, or allocation path.

## Final Census

| Measure | RR0 | Final | Delta |
|---|---:|---:|---:|
| `RuntimeRenderer` data members | 46 | 26 | -20 (-43%) |
| Constructor parameters | 10 | 3 | -7 (-70%) |
| Ordinary methods with at least seven parameters | 2 | 0 | -2 (-100%) |
| Direct `RuntimeRenderer.h` includers | 8 | 8 | 0 |

The 26 members remain one resource-lifecycle owner, four world/policy values,
four debug/profiling borrows, one bounded DXR transform array, eleven cohesive
passes, and five one-live-graph scratch/state members. The widest surviving
ordinary renderer methods have arity three (`BuildRenderFrameContext` and
`FinalizeFrameGraphInternal`); there is no wide-method exception. Every one of
the eight direct includers retains its RR0 construction, command, scene,
stress, or restore reason, and no new includer appeared.

## Independent Ownership Review

The required reviewer first confirmed the census, resource owner, pass order,
teardown order, and absence of downward Replay dependencies, but blocked
closure because `RuntimeRenderer::BuildModelFrameView` crossed scene, physics,
diagnostics, worker-policy, and config authority. The reviewer also found an
unused umbrella `Run.h` include in `UiTextPass.cpp`.

The first remediation removed both renderer and umbrella dependencies, but its
publisher still lived in `RunFrame.cpp`; the follow-up correctly rejected that
as a physical move inside the logical Run surface. The final remediation
created the stateless `RenderModelFramePublisher` module under
`Runtime/Render/`. The second follow-up passed with no remaining finding.

| Plan | Duck run | Reviewer/thread | Reason | Prompt chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|---|---|---|---|---:|---:|---:|---:|---|---|
| `runtime-renderer-decomposition` | `runtime-renderer-decomposition-duck-01` | `/root/rr5_rubber_duck` | Initial logical-renderer ownership review | 1,232 | 2,654 | n/a | 3m 24s | Blocking renderer publication authority; unused umbrella include | Remediated |
| `runtime-renderer-decomposition` | `runtime-renderer-decomposition-duck-02` | `/root/rr5_rubber_duck` | Follow-up after in-Run publisher move | 466 | 1,103 | n/a | 31s | Blocking logical-Run authority remained | Remediated |
| `runtime-renderer-decomposition` | `runtime-renderer-decomposition-duck-03` | `/root/rr5_rubber_duck` | Follow-up after dedicated publisher extraction | 451 | 860 | n/a | 20s | No findings | None |

## Comment Audit

Touched source-bearing files `RuntimeRenderer.h/.cpp`, `RunFrame.cpp`,
`UiTextPass.cpp`, and `RenderModelFramePublisher.h/.cpp` were checked 6/6
against the repository comment guide. The new module has a complete learning
header and local lifetime contract; the moved boundary has no stale renderer
API comment. No checklist path was required for this touched-file audit, and
zero files were deferred or unchecked. The one-line project-filter prefix edit
is metadata, not a substantial tool-script body change. No wording awaits owner
review.

## Validation

The desktop shell could not open a separate visible console, so commands ran in
the app shell.

| Command | Time | Result |
|---|---:|---|
| first focused Profile build | 15.53 s | EXPECTED FAIL; direct-include cleanup exposed 103 transitive `Run.h` dependencies, zero warnings |
| second focused Profile build | 5.39 s | EXPECTED FAIL; six remaining incomplete model-view errors, zero warnings |
| focused Profile build after direct includes | 7.85 s | PASS; zero warnings/errors |
| focused Profile build after dedicated publisher extraction | 8.17 s | PASS; zero warnings/errors |
| formatting and project-filter checks | 16.7 s | PASS; 745/745 project/filter items |
| `tools\validate_full.bat` | 145.30 s | PASS; CPU/coverage and five runtime lanes, zero DX12 errors, accepted images, physics hash `0x953D97A226665242`, byte-exact 44,401-line CSV |
| first final `tools\validate_dx12_renderer.bat` repeat | 23.23 s | PASS; zero InfoQueue errors and accepted committed images |
| second final `tools\validate_dx12_renderer.bat` repeat | 23.38 s | PASS; zero InfoQueue errors and accepted committed images |
| third final `tools\validate_dx12_renderer.bat` repeat | 23.34 s | PASS; zero InfoQueue errors and accepted committed images |
| `tools\run_graphics_stress.bat 1` | 60.91 s | PASS; PID 61432 ran crash-free until the scoped timeout |
| dependency-direction and Replay-boundary proofs | <1 s | PASS; all four exact commands returned no rows |

The three required DX12 repetitions were also preceded by an extra clean
three-repeat run (70.6 s combined) after console truncation hid two stopwatch
lines; the final bounded-output repetitions above are the authoritative timing
record. `tools\validate_full.bat` includes `validate_fast`; the changed project-
filter checker was also run directly and reported zero errors.

No behavioral baseline, golden, screenshot, replay artifact, physics CSV,
config, authored-data file, or runtime-reserve inventory changed.

## Closure

RR0-RR5 are complete. The active TODO plan is removed under MASTER inventory
rule 4; this report and commits `909ea7d8`, `9e76270e`, `720716a9`, `8547adbe`,
`8c57975d`, and the RR5 closure commit are the durable record. The binding queue
advances to `replay-deduplication-audit` RD0.
