# Test Suite Runner Plan

## Problem

Each render test launches a separate `SKULLBONEZ_CORE.exe` process. With N scenes × 2 GL reset passes, the pipeline makes 4+ process launches per commit. This is slow and each launch pays full startup cost (texture loading, terrain init, GL context creation).

## Approach

Add a suite file format and `--suite <path>` command line arg. `WinMain` reads the suite file, builds a list of scene paths, and loops over them using the existing `for(;;)` GL-recreation loop. Every scene still gets a fresh `SkullbonezRun::Initialise()` and the GL context is destroyed/recreated between scenes (preserving `test_gl_reset` behaviour). Zero changes to `SkullbonezRun`.

## Suite File Format

One `.scene` path per line. Lines beginning with `#` are comments. Blank lines are ignored.

```
# render_tests.suite
SkullbonezData/scenes/water_ball_test.scene
SkullbonezData/scenes/legacy_smoke.scene
```

Invocation: `SKULLBONEZ_CORE.exe --suite SkullbonezData/scenes/render_tests.suite`

Legacy `--scene` and no-arg modes are unchanged.

## Exit Timing

Exit is already immediate once a screenshot is captured:
- `return true` after pass 1 (triggers GL reset, moves to pass 2)
- `PostQuitMessage(0)` after pass 2 (or when no GL reset)

With `physics off` + `screenshot ... frame 1`, each scene renders exactly **2 frames** total (one per GL reset pass). No idle spin, no delay.

## Implementation Tasks

### th-suite-format
Create `SkullbonezData/scenes/render_tests.suite` listing both render test scenes.

### th-suite-parser
Add `ReadSuiteFile(const char* path) → std::vector<std::string>` as a free function in `SkullbonezInit.cpp`. Uses `fopen_s`, reads line-by-line with `fgets`, skips `#` comments and blank lines, strips trailing whitespace/newlines.

### th-winmain-loop
Refactor `WinMain`:
- Parse `--suite <path>` arg alongside existing `--scene <path>`
- If suite: call `ReadSuiteFile` to populate `scenes` vector
- If `--scene`: `scenes = { scenePath }`
- If no args: `scenes = { "" }` (legacy mode, empty string = nullptr passed to SkullbonezRun)
- Wrap existing `for(;;)` GL loop in `for (const auto& scene : scenes)` outer loop
- On exception in a scene: log to debug output, `break` the inner GL loop, continue to next scene

### th-suite-exit
After last scene completes (inner loop `break`s with `shouldRestart == false`), the outer scene loop ends and falls through to existing cleanup. No MessageBox shown in suite mode (same rule as `--scene` mode — check `!scenePaths.empty()` instead of `scenePath != nullptr`).

### th-skill-update
- Update `Copilot/Skills/skore-render-test/skill.md`: replace the two separate `Start-Process` blocks with a single `--suite` invocation. Update screenshot path expectations (suite run produces all 4 BMPs in one pass).
- Update `Copilot/Skills/skore-build-pipeline/skill.md` to reference the suite invocation.

### th-build-verify
Build Debug, run the full pipeline (suite render test + perf test), verify all 4 screenshots captured, pixel comparison passes, commit.

## Constraints

- Win32 only — use `fopen_s`, `fgets`, secure CRT throughout
- No new classes — suite reading is init-only, a free function is sufficient
- Must not break `--scene` or legacy no-arg mode
- Must produce zero warnings at /W4
- `SkullbonezRun` is not modified
