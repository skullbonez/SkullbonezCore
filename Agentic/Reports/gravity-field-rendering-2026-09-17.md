# Space gravity field

Tools > Options exposes the default-on space gravity grid, a vertical height
offset (-1000 to 1000 world units), opacity (0 to 100%), and Blue, Orange or Grey.
Saving the level writes height, opacity and colour to `debug.gravityField`.
Existing levels default to height 0, opacity 1 and blue. Visibility is a session
toggle retained across scene changes.

Runtime/Render builds a 49 by 49 illustrative potential surface from positive
body masses and the displayed render-instance poses. App updates it after
historical/predicted pose substitution. Stable scene identities join render
instances to masses, without writing presentation poses back to Physics. The
surface bounds stay fixed for the scene; well depth uses smooth compression.
Height moves the surface vertically without changing the sampled potential.

CPU storage is fixed at about 386 KiB, with 4,704 line segments. No per-frame
heap allocation or prediction-horizon-sized storage is added. Rendering owns
only generic depth-tested coloured lines and alpha; no replay-facing type or
allocation-growth privilege was introduced.

## Validation

- All 1,092 CPU tests pass: 3,804,367 assertions, one existing skip.
  `TestOutput/gravity-field-tests-final.log`.
- Final Automation build passes with zero warnings/errors (12 seconds).
  `TestOutput/gravity-field-automation-release.log`.
- Native Skarness acceptance passes for 300 balls and the existing 200-ball
  scene: default-on, toggle, moving wells, four viewports, height, opacity,
  all colours, saved JSON, reset, ordinary-scene hiding, and scene switching.
  Repeated historical and future scrubs reproduce identical first-source
  identity, presented position and minimum height. Future source identity is
  tied to the published prediction target, not merely non-zero geometry.
  `TestOutput/skarness/gravity-field-release/` and
  `TestOutput/gravity-field-release.log`.
- Full compiler-backed design scan initially found one oversized telemetry
  function in two configurations. Extracting its field projection repaired it;
  the focused final check passes in all three contexts (8 seconds).
  `TestOutput/gravity-field-fast.log` (full scan about 7 minutes),
  `TestOutput/gravity-field-design-recheck.log`.
- Formatting, project/filter ownership, dependency graph, plain language,
  golden guard, commit policy and validation-runner self-tests passed in that
  preflight. Separate final build-config, math and allocation checks pass;
  allocation allowlist errors are zero. No cap or growth privilege changed.
  `TestOutput/gravity-field-{build-policy,math-policy,allocation}-final.log`.
- Broad UI validation passed CPU, causal playback, causal viewports, velocity,
  editor and scrubber checks. It stops at header_autohide because the shipped
  Solver Lab comparison archive rejects current asset identities. This is
  not reported as a passing full UI gate. No archived inputs were refreshed.
  `TestOutput/gravity-field-ui-final.log`.

The mandatory staged Physics check also passes: all 44,401 lines match the
accepted golden byte-for-byte across workers 0/1/4 and a repeated process.
Worker runs take 22-26 seconds. Staged fingerprint: `f726c53a39f4`.
Evidence: `TestOutput/validation/parallel/physics-workers/20260917T033236Z-10288/summary.json`.

## Visual references and review

The space_three_body DX12 reference intentionally gains the default-on grid.
The reviewed side-by-side keeps the same three bodies and adds the blue field.
Water is pixel-exact; solver_smoke differs by at most one channel value. Only
`TestOutput/baselines/baseline_dx12_space_three_body.png` is updated. Physics
baselines and Solver Lab archives are unchanged. Final graphics evidence is in
`TestOutput/gravity-field-dx12-accepted.log` (PASS, zero DX12 errors).
Profile and Debug are ready; Debug took 41 seconds. The unchanged second
Profile build took 1.22 seconds with every compile/link output up to date
(`TestOutput/gravity-field-profile-noop.log`). Existing Options/Keys controls
also pass all 27 native checks (`TestOutput/gravity-field-options-regression.log`).
Compact Tools passes all eleven tabs at three small sizes, including scrolling,
footer controls and focus loss (`TestOutput/gravity-field-compact-regression.log`).

Native screenshots inspected include the colour controls and all four panes.
Review checked displayed-pose ordering, stable identity joins, borrowed-buffer
lifetime, fixed capacities, depth format matching, alpha propagation, and all
level save paths. The remaining archive mismatch is recorded above.

This feature is a separate review PR stacked on `nightrunner-16th-SEP-26`
(hull PR #173). Hull CH7 owner acceptance stays pending; no merge is performed.
