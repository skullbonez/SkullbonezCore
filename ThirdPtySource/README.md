# Third-Party Source

Vendor source kept here is checked in only when the dependency is small,
single-file or otherwise build-system-light enough that NuGet/vcpkg plumbing
would add more moving parts than the dependency itself.

## doctest

- Version: 2.4.12
- Source: https://github.com/doctest/doctest/releases/tag/v2.4.12
- Header: https://raw.githubusercontent.com/doctest/doctest/v2.4.12/doctest/doctest.h
- License: MIT

## Development Tool Dependencies

Dear ImGui and Tracy are pinned Git submodules used only by the
development-editor campaign. Initialize them after cloning:

```powershell
git submodule update --init --recursive
```

| Dependency | Upstream tag | Pinned commit | License | Compiled sources |
|---|---|---|---|---|
| Dear ImGui docking | `v1.92.8-docking` | `b61e56346a92cfcaf1f43a545ca37b0b32239654` | MIT; `imgui/LICENSE.txt` | `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `backends/imgui_impl_win32.cpp`, `backends/imgui_impl_dx12.cpp` |
| Tracy | `v0.13.1` | `05cceee0df3b8d7c6fa87e9638af311dbabc63cb` | BSD-3-Clause; `tracy/LICENSE` | `public/TracyClient.cpp` |

The parent repository records immutable gitlinks, so bootstrap never resolves
an unbounded `latest` branch. Dear ImGui demo/examples and Tracy viewer/server
sources are not part of the engine build. The Tracy viewer remains an external
release artifact from the matching upstream version.

`SkoreDevelopmentThirdParty.props` contributes include paths only to Debug,
Profile, and Automation. Each third-party translation unit disables warnings
locally; engine translation units remain `/W4`. Release and Profile-WPO exclude
all seven sources and do not import the property sheet.

### Updating A Pin

1. Review the official upstream release notes and licenses.
2. Check out an exact tag/commit inside the submodule; never record a moving
   branch head.
3. Update this table and `THIRD_PARTY_NOTICES.md` with the tag, full commit,
   license hash, and source-list changes.
4. Stage the submodule gitlink plus documentation and project changes.
5. Run the campaign task's mapped validation, including a Release symbol/source
   exclusion check when the development capability is active.

Current license SHA-256 values:

- Dear ImGui `LICENSE.txt`:
  `F20418B409E53C8C9F4E90917FF395554A60320D4DFBF833DA89B339CAD8628A`
- Tracy `LICENSE`:
  `482A8EEC0BF61F2DDABAFCD6441C97A3B123C08BB2BBA1423DD9BD79DBF57B7B`
