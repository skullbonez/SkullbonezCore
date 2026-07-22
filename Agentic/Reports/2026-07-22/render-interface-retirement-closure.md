# Render Interface Retirement Closure

Date: 2026-07-22
Branch: `nightrunner`
Plan: `render-interface-retirement`, RH0-RH5 complete

## Outcome

The single-implementation render HAL is retired. DX12 is the terminal runtime
backend, `RenderBackendDX12` is a non-polymorphic construction/shutdown
composition root, and runtime consumers borrow concrete DX12 owners. No render
interface class, interface-shaped tracked header, render virtual destructor, or
render `virtual`/`override` declaration remains.

RH5 also narrowed the last over-broad transient paths found by independent
review. `RuntimeRenderer::FrameEntryContext` no longer carries the complete
backend view, `RuntimeRenderServices` no longer republishes seven DX12 owners,
and `UIRenderContext` no longer grants frame/graph authority. The cohesive
`RuntimeRenderer` binds its exact concrete owner borrows once at startup and
uses them to build pass-local value/context records. Compile-time architecture
assertions prevent the backend and principal concrete owners from regrowing
polymorphism.

## Final Census Against RH0

| RH0 surface | RH0 | Final | Access result |
|---|---:|---:|---|
| Render interface classes | 10 | 0 | Retired |
| Additional test interface implementations | 5 | 0 | Retired; tests use value/concrete seams |
| Interface-shaped tracked headers | 10 | 0 | Retired |
| Render virtual destructors | Present | 0 | Retired |
| Render code `virtual`/`override` declarations | Present | 0 | Retired |
| `FrameEntryContext` backend capabilities | Whole backend view | 0 | Narrowed |
| Per-frame `RuntimeRenderServices` DX12 owners | 7 | 0 | Narrowed |
| UI command authority | Union command interface | resource/texture/geometry/diagnostics owners | Narrowed |

The final per-consumer source review found no wider reachable render surface
than the RH0 used-member subsets. The remaining `RuntimeRenderBackendView` is
the RH0-selected concrete startup map: it carries no business state or queues,
`Run` retains it for allowed process wiring, and it is no longer propagated
through `FrameEntryContext` or per-frame render services. Scene/input/stress
uses remain within their former interface subsets and are eligible for the
separate owner-fanout plan rather than blocking this retirement. No production
null, mock, software, or headless renderer was introduced; text-only execution
still uses the initialized DX12 backend and skips world work at the call site.

Static review proofs returned zero interface classes, zero interface-shaped
files, zero render virtual destructors, and zero code declarations containing
`virtual` or `override`. Six textual `virtual` rows and one textual `override`
row are ordinary explanatory comments. Dependency-direction and downward
Replay include proofs all returned zero rows. No Replay growth privilege was
added or expanded.

## Comment Audit

Checklist source: RH5 in the completed render-interface-retirement plan and
this closure report. RH5 inspected 15/15 touched source-bearing files; the
campaign reconciliation from base `7520adb9f8afca5f6a7390a76b4a6ccf72324483`
inspected 115/115 extant source/tool files. Every file has the required learning
header fields and nearby ownership/lifetime/invariant comments where needed.
Checked: 115. Deferred: 0. Unchecked: none.

## Independent Review

The initial review credibly reopened RH5 because the complete backend view was
republished through `FrameEntryContext`, seven DX12 owners were copied into the
per-frame services bag, and UI carried frame/graph authority. The main agent
removed those paths and added non-polymorphism guards. Follow-up review found
one stale `Dx12TextureOwner` lifetime claim; it was corrected to document that
stable owner links intentionally persist across shutdown/re-initialization.
Final verdict: no blocking findings.

| Duck run | Reviewer | Reason | Prompt chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|---|---|---|---:|---:|---|---|---|---|
| `render-interface-retirement-duck-01` | `/root/render_interface_retirement_duck_01` | Initial closure review | 863 | 1119 | n/a | n/a | Blocked on broad transient authority | Fixed in main agent |
| `render-interface-retirement-duck-02` | same reviewer | Review after authority fixes | 730 | 1987 | n/a | n/a | Structural blockers resolved; one comment blocker | Corrected comment |
| `render-interface-retirement-duck-03` | same reviewer | Targeted final confirmation | 284 | 220 | n/a | n/a | No blocking findings | None |

## Validation

The desktop shell could not expose a separate visible console, so commands ran
through the available PowerShell/cmd host with output mirrored under
`TestOutput/`.

| Command | Time | Result |
|---|---:|---|
| Focused Profile x64 rebuild after review fixes | 8.0 s | PASS; zero warnings/errors |
| DX12 architecture unit build/test | 3.9 s + 22.2 s | PASS; all architecture tests, including non-polymorphism assertions |
| `tools\validate_all_cpu_tests.bat` | 64.7 s | PASS; tests, coverage, interaction, scene parser, DX12 architecture |
| `tools\validate_full.bat` | 179.6 s | PASS; CPU umbrella, five runtime processes, zero DX12 errors, accepted images, byte-exact physics |
| `tools\validate_dx12_renderer.bat` repeat 1 | 23.1 s | PASS; zero InfoQueue errors, accepted captures |
| `tools\validate_dx12_renderer.bat` repeat 2 | 23.1 s | PASS; zero InfoQueue errors, accepted captures |
| `tools\validate_dx12_renderer.bat` repeat 3 | 23.1 s | PASS; zero InfoQueue errors, accepted captures |
| `tools\run_graphics_stress.bat 1` | 61.0 s | PASS; PID 49524, 11,846 frames, 325 scene loads, zero upload flushes/drops, empty stderr |

The first full-gate attempt stopped after 13.1 seconds at formatting preflight
because three touched headers needed the repository inline-comment alignment
pass. Only those three headers were updated; the complete gate then passed.
No baseline, screenshot, threshold, replay artifact, physics CSV, shader, scene,
or authored configuration changed.

## Closure

RH0-RH5 are complete. The live TODO plan is deleted under MASTER inventory rule
4, the active/future denominator returns from 19 to 13, and execution advances
to `owner-fanout-reduction` OF0.
