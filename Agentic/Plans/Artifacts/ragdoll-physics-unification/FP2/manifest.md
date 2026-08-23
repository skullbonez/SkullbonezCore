# FP2 Debug Runtime Bundle and Acceptance Evidence

Source revision: `25f230d861eedbc5ef7d146b5f32054675488468`

Configuration: `Debug|x64`

## Bundle inventory

| File | Bytes | SHA-256 |
|---|---:|---|
| `SKULLBONEZ_CORE-Debug.exe` | 14,052,352 | `193CB011908957B353E4A97D707C7A4E03217E8ABB93B77F38D2F01713C855E1` |
| `WinPixEventRuntime.dll` | 58,368 | `81ADCFD8253C3489BE720DA7E30F16004DC9A1F02A8B418C6C3AEF4993032E6D` |
| `dxcompiler.dll` | 14,317,000 | `A5AA1D9A95BF9EA68EFF4502EB687161464BB5A505E817782B7A770A2A312044` |
| `dxil.dll` | 1,509,800 | `95AC1BB413178C4596F49498E912C270F8F343282B63840F174CBF5154AD1557` |
| `fp1_wall200_impact.json` | 7,129 | `EA5ED2A85D68CBE297DC26E16F5C9B1734ADD2E7B8574DE3FCBDB06989C4C82B` |
| `fp2_wall200_impact.json` | 5,967 | `99F7393AA9D2B3B61AF8B38D5C33B19B3E63B957F88E1D621F05CEE228A65B8E` |

`dumpbin /DEPENDENTS` reports the two direct non-system runtime imports
`WinPixEventRuntime.dll` and `dxcompiler.dll`; `dxil.dll` is staged beside
`dxcompiler.dll` as its runtime support library. All remaining direct imports
are Windows system libraries.

## Isolated launch proof

The staged executable was launched from
`TestOutput/validation/fp2_final/isolated_cwd` with `PATH` set exactly to
`C:\WINDOWS\System32;C:\WINDOWS` and Windows critical-error dialogs disabled.

Command argument: `--physics-standalone-smoke`

Result: exit `0`, stdout `164768` bytes, stderr `0` bytes.

## Retained FP1 versus FP2 200-box witness

The authoritative current trace was produced with:

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --fixed-step --scene SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json --frames 6800 --physics-diag TestOutput/validation/fp2_final/fp2_fixed_wall200.physicsdiag.ndjson
```

The run completed all 6,800 frames and produced a 2,339,846,672-byte trace.
The retained packets contain the exact query output consumed by
`tools/check_contact_energy_scenes.py --compare-wall200-impact`.

| Fact | FP1 | FP2 |
|---|---:|---:|
| Named striker bodies | 1 | 1 |
| Named ragdoll bodies | 10 | 10 |
| Named wall bricks | 200 | 200 |
| Awake wall bricks after impact | 200 | 200 |
| Moving wall bricks after impact | 200 | 200 |
| First ragdoll contact frame | 76 | 75 |
| First wall contact frame | 173 | 173 |
| Striker-ragdoll contacts at first impact | 4 | 1 head contact |
| Striker-ragdoll contacts one frame later | 0 | 4 |
| Pre-impact striker velocity Y | -21.266657 | -20.999990 |
| One-frame post-impact striker velocity Y | +2.200317 | -2.005315 |

This is an intentional correctness transition, not byte-equivalence. FP1's
bounding-sphere approximation was already overlapping the ragdoll head's
oversized proxy at frame 75, returned a negative TOI, and missed the real
forward sphere/box collision until frame 76. FP2's exact rounded-box sweep
detects that forward contact at frame 75. The exact wall-contact frame and the
200-brick awake/moving cascade are preserved.

## Validation

- `tools\validate_build.bat Profile`: pass, zero warnings/errors.
- `tools\validate_build.bat Debug`: pass, zero warnings/errors.
- `Profile\SKULLBONEZ_TESTS.exe "--test-case=Physics motion promotion:*" --no-skip`:
  8/8 cases and 130/130 assertions pass.
- `python tools\check_contact_energy_scenes.py --self-test`: pass.
- `tools\validate_physics.bat`: build and workloads pass; comparison stops on
  the owner-authorized Physics transition. `physics_regression_varied.csv`
  changes 6,166 lines beginning at line 4,998. Candidate SHA-256:
  `19698D3C37BCF1E0199B539E99B2DE0D0EC33CEB7FC5DB2E2EEE36BCC434B038`.
- `tools\validate_physics_deep.bat`: build and workloads pass; comparison stops
  on the same transition plus `bullet_sweep_wall.csv` (one line,
  `68F78A80A740233DA3B5BE36FC83EAD8C0D0E62AB511BAA66F6F5146AA932824`)
  and `shooting_reaction_volley.csv` (320 lines,
  `3D6FD1F05EDDBA17713E611BAD0203D41FC18163523855AD8765D6021CDFBCFD`).
- `tools\validate_replay_visual_fidelity.bat`: launcher-shape proof, Automation
  build, and 18/18 typed packet/false-pass controls pass. The authoritative
  run stops on the existing `replay visual fidelity attempted a duplicate
  prediction generation` oracle; no replay or visual baseline was changed.

The Physics golden remains unchanged at
`DEBF57F744774D4E7C1EB5CC61F05BA6E41DC6DC997AD20DB6C91B02B0958C32`.
Approval is intentionally deferred to the repository owner through the
interactive baseline guard; this bundle does not bypass or refresh it.
