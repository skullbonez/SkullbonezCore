# SVC-034 Blocker Remediation

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

Resolved SVC-034 from the 2026-07-07 overnight blocker ledger. Current row totals across the five authoritative CSVs are 130 done and 30 blocked.

## Change

- Documented `LockOrderValidator::Instance` as a frozen diagnostics singleton in `SkullbonezSource/Core/LockOrderValidator.h` and `.cpp`.
- Added the frozen-diagnostics singleton classification to `tools/check_runtime_boundaries.py`.
- Removed `LockOrderValidator::Instance()` from the counted global-service compatibility allowlist.
- Lowered `MAX_GLOBAL_SERVICE_ACCESS_CENSUS` from 157 to 152.
- Added checker self-tests proving the frozen singleton is not rejected and not counted.
- Marked SVC-034 done in `Agentic/Plans/Done/authoritative-plan-03-explicit-service-contexts.csv`.
- Removed SVC-034 from `Agentic/Plans/In_Progress/overnight-blockers-2026-07-07.md`.

## Validation

```text
python tools\check_runtime_boundaries.py --self-test
SELF_TEST_PASS: runtime boundary checker synthetic cases passed

python tools\check_runtime_boundaries.py
Runtime boundary summary: TestOutput\validation\runtime_boundaries\summary.json (0 errors)
PASS: Runtime boundary validation passed.

tools\validate_fast.bat
PASS: All source files correctly formatted.
Project filter summary: TestOutput\validation\project_filters\summary.json (0 errors, 551 project items, 551 filter items)
Runtime boundary summary: TestOutput\validation\runtime_boundaries\summary.json (0 errors)
Build succeeded. 0 Warning(s), 0 Error(s). [Profile]
Build succeeded. 0 Warning(s), 0 Error(s). [Debug]
VALIDATE_FAST: ALL PASSED
ELAPSED_SECONDS=37.164
```

Logs:
- `Agentic/Reports/2026-07-07/logs/svc-034-runtime-boundaries-self-test.log`
- `Agentic/Reports/2026-07-07/logs/svc-034-runtime-boundaries-scan.log`
- `Agentic/Reports/2026-07-07/logs/svc-034-validate-fast.log`

## Comment Audit

Touched source-bearing files were inspected against `Agentic/Reference/comment-style-guide.md` and `Agentic/Skills/comment-style-audit/skill.md`: `LockOrderValidator.h`, `LockOrderValidator.cpp`, and `tools/check_runtime_boundaries.py`. The slice adds local lifetime/frozen-diagnostics comments and checker comments; no files were deferred.
