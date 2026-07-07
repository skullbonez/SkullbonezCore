# Fable 01 E3 Scene Parser Tests

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

E3 is complete. `SKULLBONEZ_TESTS` now covers the focused authored-scene parser
success path and the current malformed JSON throwing contract.

## Changes

- Added `SkullbonezTests/TestSceneParserUnit.cpp`.
- Added `SkullbonezTests/TestSceneParserLinkStubs.cpp`.
- Added `SkullbonezSource/Scene/TestScene.cpp` and
  `SkullbonezSource/Scene/TestSceneParser.cpp` to `SKULLBONEZ_TESTS.vcxproj`.
- Added matching Tests and Scene filters to
  `SKULLBONEZ_TESTS.vcxproj.filters`.

## Evidence

- CodeGraph mapped `TestScene::LoadFromFile` through
  `LoadTestSceneFromFileImpl` into `TestSceneParser::LoadScene`.
- Discovery command `rg -n "Cfg\(|Gfx\(|::Instance"
  SkullbonezSource/Scene/TestScene.cpp
  SkullbonezSource/Scene/TestSceneParser.cpp
  SkullbonezSource/Scene/TestScene.h` returned no hits.
- `Get-ChildItem SkullbonezData\scenes\*.scene.json | Sort-Object Length`
  identified `SkullbonezData/scenes/terrain_compare.scene.json` as the
  smallest committed scene at 525 bytes.
- Tests assert that the scene parses one `main` camera, disables physics/text,
  hides water, preserves the screenshot frame/path, and has no body records.
- Tests create a temporary malformed scene file and assert the current
  `std::runtime_error` contract contains `Invalid JSON`, the path, and
  `TestScene::LoadFromFile`.
- The test file is named `TestSceneParserUnit.cpp` rather than
  `TestSceneParser.cpp` because MSVC emits object files by basename and the
  production parser translation unit must also be compiled into the test
  project.
- `TestSceneParserLinkStubs.cpp` supplies a loud test-only stub for the
  uncalled runtime asset-library lookup and throws if a focused path-based
  parser test crosses into `AssetSystem` resolution.

## Validation

- First `tools\validate_tests.bat` attempt failed in 5.662s:
  `TestSceneParser.cpp` basename collision with the production parser TU, plus
  one unresolved uncalled `AssetSystem::FindAssetLibrarySourceAsset` reference.
- Final `tools\validate_tests.bat`: exit 0 in 4.120s, 40 doctest cases and
  417 assertions passed with 0 warnings/errors.
- Logs:
  - `Agentic/Reports/2026-07-07/logs/fable-01-e3-validate-tests-attempt1.log`
  - `Agentic/Reports/2026-07-07/logs/fable-01-e3-validate-tests-attempt2.log`

## Comment Audit

Touched source-bearing test files inspected against the comment-style guide:
`SkullbonezTests/TestSceneParserUnit.cpp` and
`SkullbonezTests/TestSceneParserLinkStubs.cpp` both have learning headers with
glossary, invariants, and related links. The unit test has a local `Why:`
comment for the plan-05 error-contract transition, and the link stub has a
local `Hazard:` comment for accidental runtime asset lookup.
