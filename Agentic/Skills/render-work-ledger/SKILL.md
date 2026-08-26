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
   lowercase hyphenated `.html` filename. For an orchestrator completion
   artifact, use the tracked `Agentic/Ledgers/<branch-stem>.html` path instead.
5. Run the generator:

   ```powershell
   python Agentic/Skills/render-work-ledger/scripts/render_work_ledger.py `
     --ledger Agentic/Plans/WORK_LEDGER.csv `
     --output <absolute-output-path>
   ```

   Add `--standalone` when the HTML must open outside the conversation
   visualization host, including every tracked orchestrator completion artifact.

6. Read the generator's JSON summary. Check that its run id, completed-task
   count, current task, elapsed time, and portfolio progress agree with the CSV.
7. Use the conversation visualization surface to show the generated fragment.
   Prefer wide mode because the task timeline and cost panel benefit from direct
   comparison. Do not expose temporary HTML as a download link. For a standalone
   completion artifact, render the HTML in a wide browser viewport, save the
   matching PNG beside it, and visually inspect the PNG before commit.

## Output Contract

The generator owns:

- current-run selection and open-task handling;
- exact task timing, interval-graph worker lanes, proportional clock placement,
  overlap duration, and peak concurrency;
- goal and completed-task token/cost totals;
- a bounded task-cost pie that labels the nine most expensive tasks and groups
  the inexpensive tail into one honest aggregate;
- cached-input ratio;
- total completed task time with its task-work-versus-validation split;
- rubber-duck passes, findings, and fix cycles;
- commit count and portfolio progress;
- 32-bit Windows CSV field size limit (`csv.field_size_limit(2147483647)`) to safely parse large base64-encoded state payloads; and
- responsive, theme-aware HTML with accessible labels.

Fragment mode inherits the conversation host's document shell and theme.
`--standalone` preserves the same infographic inside a self-contained UTF-8
document with explicit theme tokens for browser and PNG rendering.

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
