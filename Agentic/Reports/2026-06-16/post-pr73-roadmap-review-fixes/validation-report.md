# Post-PR73 Roadmap Review Fixes Validation

Date: 2026-06-16  
Branch: `codex/post-pr73-roadmap`

This report supersedes the stale validation wording in commit `43c92573`, which said full validation would be rerun. Git history was not rewritten; this committed report is the audit-trail correction for the current branch state after the six review fixes.

## Commands

```powershell
cmd.exe /c tools\validate_full.bat 2>&1 | Tee-Object -FilePath TestOutput\post_pr73_review_validate_full.log
cmd.exe /c tools\validate_scene_parser_tests.bat 2>&1 | Tee-Object -FilePath TestOutput\post_pr73_review_scene_parser_tests.log
```

The `TestOutput\post_pr73_review_*.log` files are local validation artifacts and are intentionally not committed.

## Result

`tools\validate_full.bat` passed with exit code `0` in `134.0s`.

Key output:

```text
DX12 validation errors: 0
PASS: DX12 InfoQueue reported 0 validation errors.
water_ball_test: avg_diff=0.0000 max_diff=0 pixels_over_10=0 [PASS]
solver_smoke: avg_diff=0.0002 max_diff=123 pixels_over_10=4 [PASS]
VALIDATE_DX12_RENDERER: ALL PASSED
PASS: Build Debug|x64 succeeded.
VALIDATE_PHYSICS: ALL PASSED
VALIDATE_PERF: COMPLETE
PASS: Profile and Debug binaries are ready.
VALIDATE_FULL: ALL PHASES PASSED
VALIDATE_FULL_EXIT_CODE=0
VALIDATE_FULL_ELAPSED_SECONDS=134.0
```

`tools\validate_scene_parser_tests.bat` passed with exit code `0` in `0.6s`.

Key output:

```text
PASS: Style material authoring contract
PASS: Scene material authoring sample loads
PASS: all scene parser unit tests passed.
PASS: scene parser unit tests passed.
VALIDATE_SCENE_PARSER_TESTS_EXIT_CODE=0
VALIDATE_SCENE_PARSER_TESTS_ELAPSED_SECONDS=0.6
```

## Warnings Kept In The Audit Trail

The full gate completed successfully, but the perf phase still printed these warnings:

```text
WARNING: Machine mismatch — perf comparison is not valid across machines.
WARNING: physics_bench performance regression detected. Review output above.
WARNING: physics_bench_no_sleep performance regression detected. Review output above.
```

These warnings do not change the gate exit code, but they are material context for reviewers. The meaningful validation result is that the full script reached `VALIDATE_FULL: ALL PHASES PASSED` with exit code `0`, while the performance comparison remains machine-sensitive.
