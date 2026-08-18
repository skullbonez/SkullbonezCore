---
name: render-work-ledger
description: Render SkullbonezCore's live `Agentic/Plans/WORK_LEDGER.csv` telemetry as a polished, responsive in-conversation infographic. Use when the user asks to show, visualize, summarize, graph, or turn the MASTER-PLAN work ledger, Night Runner progress, an overnight run, token/cost usage, validation time, review findings, or task timeline into a graphic.
---

# Render Work Ledger

Turn the orchestrator's exact CSV telemetry into a deterministic HTML
infographic. Keep calculation and layout in the bundled generator; do not
manually transcribe ledger values into markup.

## Workflow

1. Follow the repository startup contract and treat existing dirty files as
   user-owned.
2. Resolve the ledger beside the selected master plan. Default to
   `Agentic/Plans/WORK_LEDGER.csv`.
3. For the live default ledger, refresh its open row before reading it:

   ```bat
   Agentic\Skills\orchestrator\scripts\work_ledger.bat show
   ```

   If `show` fails, report the ledger blocker. Do not substitute estimated
   telemetry. Skip this mutation for an explicitly supplied historical copy.
   If the user explicitly requires read-only repository access, render the
   exact on-disk snapshot, preserve its visible timestamp, and disclose that
   the open row may be stale because it was not refreshed.
4. Choose a durable, task-owned output path outside the checkout. Use a
   lowercase hyphenated `.html` filename.
5. Run the generator:

   ```powershell
   python Agentic/Skills/render-work-ledger/scripts/render_work_ledger.py `
     --ledger Agentic/Plans/WORK_LEDGER.csv `
     --output <absolute-output-path>
   ```

6. Read the generator's JSON summary. Check that its run id, completed-task
   count, current task, elapsed time, and portfolio progress agree with the CSV.
7. Use the conversation visualization surface to show the generated fragment.
   Prefer wide mode because the task timeline and cost panel benefit from direct
   comparison. Do not expose the temporary HTML as a download link.

## Output Contract

The generator owns:

- current-run selection and open-task handling;
- exact task timing and proportional clock placement;
- goal and completed-task token/cost totals;
- cached-input ratio;
- validation versus other task time;
- rubber-duck passes, findings, and fix cycles;
- commit count and portfolio progress; and
- responsive, theme-aware HTML with accessible labels.

Preserve the ledger's distinctions between input, cached input, output, and
estimated API cost. Never relabel input plus output as "total tokens". Keep the
source path, run id, pricing basis, and snapshot time visible.

## Validation

For changes to this skill, run:

```powershell
python Agentic/Skills/render-work-ledger/scripts/render_work_ledger.py --self-test
python C:/Users/sesch/.codex/skills/.system/skill-creator/scripts/quick_validate.py Agentic/Skills/render-work-ledger
```

Then generate from the live ledger and inspect browser screenshots at 1,024,
736, and 360 pixels. The repository's build gates are not required for this
standalone reporting tool.
