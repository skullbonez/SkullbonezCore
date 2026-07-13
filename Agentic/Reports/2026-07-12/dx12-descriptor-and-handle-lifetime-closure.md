# DX12 Descriptor And Handle Lifetime Closure

Date: 2026-07-12
Plan: `dx12-descriptor-and-handle-lifetime` — 5/5 complete
Branch: `nightrunner-12th-july`

## Outcome

DX12 static SRV/UAV rows and framebuffer RTV/DSV rows now use fixed free lists.
Recreated resources return their rows through the existing frame-owner
retirement fence, so no row is reassigned while an in-flight command list may
still reference it. Texture handles use an 8-bit generation plus 24-bit slot;
stale handles resolve to the null path and emit one Debug diagnostic rather
than aliasing a replacement texture.

The texture registry is sized to the static descriptor capacity during backend
initialization. Runtime insertions only reuse tombstones; exhaustion is fatal
rather than a steady-gameplay allocation fallback. The retirement queue is a
fixed 512-entry array, and resource plus static/CPU descriptor teardown shares
one fence record where the lifetimes belong together.

## Descriptor Call-Site Audit

| Owner/call site | Lifetime classification | Reclamation |
|---|---|---|
| Typed null texture SRV | Device epoch | Heap teardown |
| Generate-mips null UAV | Device epoch | Heap teardown |
| DXR reflection SRV/UAV | Device epoch | Heap teardown after terminal drain |
| Ordinary texture SRV | Recreated/deleted | Paired resource + static row retirement |
| Framebuffer color/depth SRV | Recreated on resize | Paired resource + static row retirement |
| Framebuffer RTV/DSV | Recreated on resize | Paired resource + CPU row retirement |
| Render-graph transient SRV | Graph pool, released at shutdown | Registry unregister + static retirement |
| Render-graph transient UAV | Graph pool, released at shutdown | Explicit static retirement |
| Swap-chain RTV/main DSV | Stable device-epoch rows | Descriptor overwritten for replacement resource |

The 8/24 generation split preserves the existing `uint32_t` backend handle ABI.
Generation zero remains invalid. An 8-bit generation wraps after 255 tombstone
reuses, so it detects practical stale ownership but is not an eternal durable
identity; callers must still release backend handles normally.

## Tests And Runtime Proof

- DX12 architecture tests cover static allocate/free/reallocate reuse,
  high-water accounting, and stale-handle rejection through the real
  `Dx12TextureRegistry` remove/reuse/resolve path.
- Graphics stress reserves frames 1–170 for a deterministic proof. It records
  the pre-churn static-row baseline, requests 131 alternating native resizes,
  counts only successful backend recreation-generation publications, and
  performs 131 successful 1x1 texture create/delete turnovers.
- The launcher accepts neither a clean early exit nor a timed exit without the
  engine PASS marker.
- Final stress evidence:
  `baseline=20 current=20 requested=131 acknowledged=131 textures=131 high_water=23`.
- The first post-change stress attempt was an honest negative drill: the new
  marker requirement rejected early termination and exposed the previously
  hidden CPU DSV bump exhaustion (`used=16 capacity=16`). Adding fence-safe
  RTV/DSV reclamation made the same one-minute command pass.

## Independent Review

Three read-only rubber-duck passes were run. The initial pass found graph UAV
leakage, a codec-only test, false-passable stress acknowledgement, and retirement
vector growth. The follow-up verified those fixes and found the clean-early-exit
launcher gap. The final narrow pass reviewed CPU RTV/DSV free-list reuse,
sentinels, paired retirement, shutdown ordering, and double-free risk; it
reported no blocking findings.

## Final Validation

- `tools\validate_fast.bat` — passed final source: formatting, project/filter
  metadata, tests, zero-warning Profile and Debug builds.
- `python tools\check_allocation_policy.py --self-test` — passed.
- `python tools\check_allocation_policy.py --repo .` — passed with
  `allowlist_errors=0`.
- `tools\validate_all_cpu_tests.bat` — all four owners passed, including the
  DX12 architecture target.
- `tools\validate_dx12_renderer.bat` — zero InfoQueue errors and all three
  committed screenshot comparisons passed. Final artifact summary:
  `TestOutput/validation/dx12_renderer/20260712T053428Z/summary.json`.
- `tools\run_graphics_stress.bat 1` — passed in 61.7 seconds with the exact
  descriptor-churn proof above and crash-free PID-scoped timeout exit.

## Comment Audit

Touched-file audit completed against
`Agentic/Skills/comment-style-audit/skill.md`: 15 source/tool files checked,
0 deferred, 0 unchecked. Headers, lifetime/fence hazards, fixed capacities,
generation wrap, and stress acceptance comments are current.
