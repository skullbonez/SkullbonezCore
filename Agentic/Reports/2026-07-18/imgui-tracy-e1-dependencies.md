# ImGui + Tracy E1 Dependency Evidence

Date: 2026-07-18

Branch: `nightrunner-18th-july`

Task: E1 — reproducible development dependencies

## Immutable Pins

| Dependency | Official tag | Gitlink commit | License | License SHA-256 |
|---|---|---|---|---|
| Dear ImGui docking | `v1.92.8-docking` | `b61e56346a92cfcaf1f43a545ca37b0b32239654` | MIT, `ThirdPtySource/imgui/LICENSE.txt` | `F20418B409E53C8C9F4E90917FF395554A60320D4DFBF833DA89B339CAD8628A` |
| Tracy | `v0.13.1` | `05cceee0df3b8d7c6fa87e9638af311dbabc63cb` | BSD-3-Clause, `ThirdPtySource/tracy/LICENSE` | `482A8EEC0BF61F2DDABAFCD6441C97A3B123C08BB2BBA1423DD9BD79DBF57B7B` |

The repositories are the official upstreams at
<https://github.com/ocornut/imgui> and <https://github.com/wolfpld/tracy>.
`.gitmodules` records those URLs without a moving branch. A fresh checkout uses
`git submodule update --init --recursive`, which resolves the parent
repository's exact gitlinks. `FIRST_TIME_SETUP.md`, `ThirdPtySource/README.md`,
and `THIRD_PARTY_NOTICES.md` record bootstrap, source, license, hash, and update
instructions.

## Build Surface

`SkoreDevelopmentThirdParty.props` contributes include paths only to Debug,
Profile, and Automation. The project lists exactly these vendor translation
units:

- `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, and `imgui_widgets.cpp`;
- `backends/imgui_impl_win32.cpp` and `backends/imgui_impl_dx12.cpp`;
- `public/TracyClient.cpp`.

Warnings are disabled only on those explicit vendor items. Engine translation
units remain `/W4`. Release and Profile-WPO exclude all seven vendor sources and
do not import the property sheet. Dear ImGui demos/examples and the Tracy
viewer/server are not engine build inputs. The project-filter gate places the
seven sources under the repository's `External` ownership filter.

Adding the repository's first gitlinks exposed a staged-size checker defect:
mode-160000 entries have no parent-repository blob. The checker now treats the
gitlink itself as zero parent-blob bytes while continuing to measure the normal
`.gitmodules` blob and every ordinary staged file. Synthetic staged/HEAD mode
tests cover that distinction.

## Validation

- `python tools\check_staged_file_sizes.py --self-test` — pass:
  `SELF_TEST_PASS`; approximately 0.1 seconds.
- `python tools\check_staged_file_sizes.py --repo .` — pass: 13 staged
  candidates and 0 violations; approximately 1 second with staging.
- `tools\validate_fast.bat` — pass from final E1 source: formatting, 747/747
  project-filter items with 0 errors, staged-size guard, and zero-warning,
  zero-error Profile and Debug builds. The visible shell session took about 90
  seconds; the Debug MSBuild sub-run reported 31.56 seconds.
- `tools\validate_build.bat Release` — pass from final E1 source with 0 warnings
  and 0 errors in 46.85 seconds. The Release compile/link command contained no
  ImGui or Tracy source/object input.

No runtime, replay, visual, physics, or behavioral baseline was changed.

## Comment Audit

Touched-file audit, 1/1 substantial tool script checked and 0 deferred:
`tools/check_staged_file_sizes.py` retains the required learning header and now
defines the gitlink vocabulary and parent-blob invariant beside the affected
logic. No subsystem-wide checklist was required. The three previously committed
P1 physics files were separately formatted and audited without behavior change.
