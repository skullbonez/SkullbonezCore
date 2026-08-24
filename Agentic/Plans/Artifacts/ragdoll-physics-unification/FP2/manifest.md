# FP2 Runtime Bundles and Acceptance Evidence

Source parent revision: `2806c7d70159e498c8fdd5678023e4d66a9db9d7`

Source delta: the approved executables were built from that parent plus the
Physics/lifecycle changes committed atomically with this manifest.

Approval closure: the final Debug/Profile executables and approved golden set are
committed with this manifest.

Configuration: `Debug|x64`

## Bundle inventory

| File | Bytes | SHA-256 |
|---|---:|---|
| `SKULLBONEZ_CORE-Debug.exe` | 14,052,352 | `193CB011908957B353E4A97D707C7A4E03217E8ABB93B77F38D2F01713C855E1` |
| `SKULLBONEZ_CORE-Debug-approved.exe` | 14,019,072 | `12DD17945B512E285B73E7DD6C833069AF8119BC880780F9BEC211134E0B945A` |
| `SKULLBONEZ_CORE-Profile-approved.exe` | 4,694,528 | `E78AA5799102786A3C5C6D43ECBFCCF9345D56EE1728362A47F1C61C9BD2241A` |
| `SKULLBONEZ_CORE-Automation-approved.exe` | 4,986,880 | `9D4EE221C3DA2C07FE795A7FFCCCD12E822422907BD5FA591B01F71664B9DA24` |
| `WinPixEventRuntime.dll` | 58,368 | `81ADCFD8253C3489BE720DA7E30F16004DC9A1F02A8B418C6C3AEF4993032E6D` |
| `dxcompiler.dll` | 14,317,000 | `A5AA1D9A95BF9EA68EFF4502EB687161464BB5A505E817782B7A770A2A312044` |
| `dxil.dll` | 1,509,800 | `95AC1BB413178C4596F49498E912C270F8F343282B63840F174CBF5154AD1557` |
| `fp1_wall200_impact.json` | 7,128 | `A3674BFF82332A71409BE592C8E33349657FD8E687A2FA3BB4BE14F4335A9C53` |
| `fp2_wall200_impact.json` | 5,966 | `93DDB28FAA57A5F902A4AF5B2834C0B4D9D5A2CA6D72FFA01D93B1423F40C619` |

`dumpbin /DEPENDENTS` reports the two direct non-system runtime imports
`WinPixEventRuntime.dll` and `dxcompiler.dll`; `dxil.dll` is staged beside
`dxcompiler.dll` as its runtime support library. All remaining direct imports
are Windows system libraries.

## Approved replay-visual closure

Source and capture commit:
`79f02de3b98da866610b15c8ea4a7f3f398423b1`.

The retained Automation executable above produced
`TestOutput/validation/replay_visual_fidelity/full_reveal_probe_profile.json`
and its saved replay. The first archived content transition was:

| Evidence | SHA-256 |
|---|---|
| Previous causal baseline | `CBE0F6CF5EDEB796CAACE6FE384948ACCDD3BA51BCE4F6D5A95888962749B46E` |
| First accepted causal baseline | `2B0B2FEEA7AB599A7AF882A122B8B44BEFDF5DE1DD0D52A8CDE4CD9F61D28EE1` |
| Unchanged visual baseline | `498304F1FE366E558023F37DAC5711F5BEF51084237B6BD857E42B57AAF58983` |
| Saved replay | `D0CF8D19EE029A4AA0FA9F72719E3B9D83348F50A2C1FBEB4435C485A9A7BC79` |

The exact isolated Automation smoke used the artifact directory as its working
directory, disabled critical-error dialogs, and set `PATH` to exactly
`C:\WINDOWS\System32;C:\WINDOWS` before running:

```text
SKULLBONEZ_CORE-Automation-approved.exe --physics-standalone-smoke
```

Result: exit `0`, stdout `163362` bytes, stderr `0` bytes. The exact replay
validation command after installing the archived baseline is:

```text
tools\validate_replay_visual_fidelity.bat
```

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
- `tools\validate_physics.bat`: pass; the approved 44,401-row varied-scene
  golden matches byte-exactly.
- `tools\validate_physics_deep.bat`: pass; all CSV, known-issue signature,
  shooting-reaction, SkullScope query, and contact-energy controls pass.
- `tools\validate_replay_visual_fidelity.bat`: launcher-shape proof, Automation
  build, 18/18 typed packet cases, 82/82 assertions, the authoritative
  2,401-tick run, offline projection, artifact save, final visual and causal
  comparisons, and every registered false-pass control pass.

## Archived automated golden set

The standing Physics-plan automated override accepted only the exact generated
FP2 candidates and their immutable artifact evidence. The content-bound core
transition accepted only the generated varied-scene SHA; the deep goldens were
generated from the same final Debug executable and then passed the complete
deep gate.

| Golden | SHA-256 |
|---|---|
| `physics_regression_varied.csv` | `19698D3C37BCF1E0199B539E99B2DE0D0EC33CEB7FC5DB2E2EEE36BCC434B038` |
| `bullet_sweep_wall.csv` | `68F78A80A740233DA3B5BE36FC83EAD8C0D0E62AB511BAA66F6F5146AA932824` |
| `shooting_reaction_volley.csv` | `3D6FD1F05EDDBA17713E611BAD0203D41FC18163523855AD8765D6021CDFBCFD` |
| `physics_known_issue_signatures.json` | `D60AC7DDBCB12A1D5BCECED5694F1F34AF262D53EA6D3DCD4B9AD157F33C7E36` |
| `physics_query_varied.json` | `0788BC16F441123BE52716CC69180FCC501EC9808C9CF26DDA4B2AED957E5681` |

## Final replay-visual transition

The retained Automation executable and its successful `ok=true` report were
used without another executable or DLL replacement. The official checker
payload path generated temporary visual and causal candidates first; both
candidates passed the ordinary comparator and the non-engine false-pass
controls before their exact bytes replaced the tracked baselines.

| Evidence | Previous SHA-256 | Accepted SHA-256 |
|---|---|---|
| `TestOutput/baselines/replay_visual_fidelity_200_box.json` | `498304F1FE366E558023F37DAC5711F5BEF51084237B6BD857E42B57AAF58983` | `79ACC8A12398E4F4C2A51EA2A3219A964640CF28B4F004E2B2986B50192080F4` |
| `TestOutput/baselines/replay_visual_fidelity_200_box_causal.json` | `2B0B2FEEA7AB599A7AF882A122B8B44BEFDF5DE1DD0D52A8CDE4CD9F61D28EE1` | `0E616CD3FDE12943E5F748523B80F460749F4EDE66003307841146C231D9C13A` |

The accepted causal baseline is bound to visual SHA-256
`79ACC8A12398E4F4C2A51EA2A3219A964640CF28B4F004E2B2986B50192080F4`.
The visual payload records shader-tree SHA-256
`DD36AE24BC81C9482E1A7CEF5F93C6172BB728AE4DE25C526FE9DAE51F581EBE`,
working source commit `79f02de3b98da866610b15c8ea4a7f3f398423b1`, and capture commit
`4fb0b2ebac547895c35b04aec4a6e8ad57e91873`.

| Final 200-box state | Previous | Accepted |
|---|---:|---:|
| Authored wall bricks | 200 | 200 |
| Moved wall bricks | 200 | 200 |
| Toppled wall bricks | 184 | 113 |
| Sustained-toppled wall bricks | 181 | 109 |
| Settled wall bricks | 194 | 125 |

The final-state rows remain byte-exact golden fields. The separate settled-wall
shape guard now matches the phase's majority contract: 99 is rejected and the
accepted 125 passes.

Final closure validation:

- `tools\validate_replay_visual_fidelity.bat`: pass in 497.8 seconds; Automation
  built with zero warnings/errors, 18/18 focused cases and 82/82 assertions
  passed, the single authoritative generation produced all 2,401 ticks, and
  every visual, causal, artifact, trajectory-count, and determinism false-pass
  control rejected its injected mutation.
- `tools\validate_physics.bat`: pass in 34.2 seconds; the archived accepted
  44,401-row Physics golden remained byte-exact.
- `tools\validate_physics_deep.bat`: pass in 101.9 seconds.
- `tools\validate_dependency_graph.bat`: pass; zero findings.
