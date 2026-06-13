# Roadmap Email Reports Plan

Status: planning draft
Created: 2026-06-12
Scope: Agentic reporting, roadmap orchestration, email delivery, HTML report design
Implementation status: plan only

## Goal

Send a polished HTML email whenever a roadmap item reaches a meaningful report
state, so the user receives a readable summary with screenshots, validation
evidence, PR links, and plain-English explanation without needing to inspect the
repo manually.

The email should feel like a product-quality status note, not a raw CI dump:

- clear subject line,
- concise executive summary,
- readable implementation explanation,
- inline screenshots with captions,
- validation result and important numbers,
- links or paths to full artifacts,
- residual risks and next queue action,
- consistent visual layout across every roadmap item.

The email provider is intentionally left as `TODO` until the user chooses a
provider. The design must support an API or SMTP backend without coupling the
repo to Gmail, Google Workspace, or any personal mailbox.

## Current Repository Context

The repo already has the main reporting contract:

- `Agentic/Orchestrator/policy.json` contains `report_channels` and an
  `email_reports` block.
- `Agentic/Orchestrator/queue.json` lists roadmap items and their screenshot /
  artifact hooks.
- `Agentic/Orchestrator/templates/report.md` defines the durable Markdown report
  shape.
- `Agentic/Runs/<yyyy-mm-dd>/<item-id>/` stores run reports, validation logs,
  screenshots, artifacts, PR notes, and worker results.

This plan adds an email delivery layer on top of those artifacts. The Markdown
report remains the durable source of truth. Email is a presentation and delivery
adapter.

## Non-Goals

- Do not choose the email provider in this plan.
- Do not store secrets, tokens, SMTP passwords, or API keys in the repository.
- Do not require Gmail, Google Workspace, or Google OAuth.
- Do not self-host an SMTP server unless the user explicitly chooses that later.
- Do not replace `report.md`; email is generated from the same run state and
  artifacts.
- Do not merge, submit PRs, force-push, rebase, or rewrite git history as part
  of this work.

## Desired User Experience

When a roadmap item is fixed, the user receives an email like:

```text
Subject: Skullbonez Roadmap Fixed: New UI Whats New
From: Skullbonez Reports <TODO-provider-sender>
To: <configured recipient>
```

The body starts with:

- roadmap item name,
- final state, for example `Fixed`, `PR Open`, `Merged`, `Blocked`, or `Failed`,
- one-sentence explanation of what changed,
- PR link when available,
- validation result, with green/red/amber treatment.

Then it shows:

- screenshots inline, sized for quick reading,
- short captions explaining what each screenshot proves,
- changed files grouped by area,
- validation details,
- conflicts or blockers,
- residual risk,
- next queue action.

Every email also links to or attaches:

- `report.md`,
- validation log when present,
- summary JSON or manifest files when useful,
- generated image attachments.

## Trigger Policy

Add explicit trigger rules under `email_reports` in
`Agentic/Orchestrator/policy.json`.

Suggested future shape:

```json
{
  "email_reports": {
    "enabled": false,
    "provider": "TODO",
    "from": "TODO",
    "reply_to": "",
    "to": [],
    "cc": [],
    "bcc": [],
    "send_on_status": [
      "pr-open",
      "merged",
      "blocked",
      "failed"
    ],
    "inline_images": true,
    "attach_markdown_report": true,
    "attach_validation_log": true,
    "dry_run_default": true
  }
}
```

Recommended behavior:

- Send for `pr-open` when the item is fixed but waiting on review or merge.
- Send for `merged` when the item is fully landed.
- Send for `blocked` or `failed` only if configured, because those are useful
  status reports but not "fixed" notifications.
- Never send while an item is still `running`.
- Never send if `email_reports.enabled` is false.
- Default to dry-run until credentials and provider are confirmed.

## Provider Abstraction

Create a small provider interface so the repo can support the chosen service
later without rewriting report generation.

Provider remains `TODO`. Candidate classes:

```text
EmailProvider
  send(message: EmailMessage) -> EmailSendResult

Providers:
  TODO API provider
  TODO SMTP provider
```

The first implementation should support either:

- HTTP API send, preferred for transactional email providers, or
- SMTP send, if the selected provider's SMTP path is simpler.

The provider adapter should be isolated in one file so the HTML report renderer,
image preparation, and orchestrator logic are provider-independent.

## Secret Handling

All secrets must live outside the repo.

Supported secret sources:

- environment variables,
- Windows Credential Manager,
- GitHub Actions secrets if this is later automated in CI,
- a local untracked `.env` file only if explicitly added to `.gitignore` and
  documented as local-only.

Suggested environment variables:

```text
SKULLBONEZ_EMAIL_PROVIDER=TODO
SKULLBONEZ_EMAIL_API_KEY=TODO
SKULLBONEZ_EMAIL_SMTP_HOST=TODO
SKULLBONEZ_EMAIL_SMTP_PORT=TODO
SKULLBONEZ_EMAIL_SMTP_USER=TODO
SKULLBONEZ_EMAIL_SMTP_PASSWORD=TODO
SKULLBONEZ_EMAIL_FROM=Skullbonez Reports <TODO>
SKULLBONEZ_EMAIL_TO=TODO
```

The script must fail closed if credentials are missing:

- print a clear error,
- write no partial send state,
- do not retry indefinitely,
- do not expose secrets in logs.

## Email Content Model

Introduce a structured intermediate model generated from `run.json`,
`report.md`, and known artifact directories.

Suggested fields:

```text
RoadmapEmailReport
  item_id
  display_title
  final_status
  status_label
  status_tone
  source_plan
  branch
  commit_sha
  pr_url
  merge_sha
  started_at
  finished_at
  elapsed
  implementation_summary
  changed_files_by_area
  validation_summary
  validation_commands
  validation_log_path
  screenshots
  artifacts
  conflicts
  residual_risk
  next_queue_action
```

Screenshot model:

```text
ScreenshotArtifact
  source_path
  email_path
  content_id
  title
  caption
  width
  height
```

Artifact model:

```text
ReportArtifact
  path
  label
  type
  attach
```

Use structured parsing where possible:

- Prefer `run.json` for machine-readable fields.
- Use `report.md` sections as fallback for older runs.
- Use `summary.json` and `manifest.json` when renderer validation artifacts
  exist.
- Avoid ad hoc parsing of large validation logs except for a bounded summary.

## HTML Design

Create a single reusable HTML template:

```text
Agentic/Orchestrator/templates/email-report.html
```

The template should be compatible with common email clients:

- table-based layout where necessary,
- inline CSS,
- no JavaScript,
- no external stylesheets,
- no remote images required,
- responsive max width around 720 px,
- dark text on light background,
- readable on mobile.

Visual direction:

- professional but not corporate-heavy,
- calm neutral background,
- high-contrast status badge,
- code and paths in compact monospace pills,
- screenshots framed with thin borders and captions,
- validation section with pass/fail/blocked treatment,
- compact changed-file section,
- clear footer with run path and generation timestamp.

Suggested layout:

```text
Header
  Skullbonez Roadmap Report
  Status badge
  Item title
  One-sentence summary

At A Glance
  Status
  PR
  Branch
  Commit
  Elapsed
  Validation

What Changed
  Human-readable implementation summary

Screenshots
  Inline images with captions

Validation
  Required gate
  Commands
  Result
  Important renderer / physics / perf numbers

Changed Files
  Grouped by area

Notes
  Conflicts
  Residual risk
  Next action

Footer
  Run path
  Attached artifacts
```

## Image Handling

The repo often produces `.bmp` screenshots. Email should use `.png`.

Add an image preparation step:

1. Discover screenshot artifacts from `run.json`, `report.md`, queue
   `screenshot_scenes`, and `Agentic/Runs/.../screenshots`.
2. Convert `.bmp` to `.png`.
3. Keep original screenshots untouched.
4. Save email-ready copies under:

```text
Agentic/Runs/<date>/<item-id>/email/images/
```

5. Resize large images only for email display, preserving full originals as
   attachments or linked artifacts.
6. Use content IDs for inline images:

```html
<img src="cid:ui_whats_new_final">
```

7. Add captions that explain why the image matters.

Caption source priority:

- explicit caption from run metadata,
- label from report line,
- filename cleaned into readable words,
- fallback `Screenshot artifact`.

## Plain Text Fallback

Every HTML email should include a plain text alternative.

The text version should include:

- item id,
- status,
- PR link,
- summary,
- validation result,
- screenshot file paths,
- artifact paths,
- residual risk,
- next action.

This helps with searchability, mobile previews, and clients that block HTML.

## CLI Design

Add:

```text
tools/send_roadmap_report.py
```

Suggested commands:

```bat
REM Render but do not send.
python tools\send_roadmap_report.py --run Agentic\Runs\2026-06-12\new-ui-whats-new --dry-run

REM Render HTML and open/save preview artifacts.
python tools\send_roadmap_report.py --run Agentic\Runs\2026-06-12\new-ui-whats-new --preview

REM Send when provider and credentials are configured.
python tools\send_roadmap_report.py --run Agentic\Runs\2026-06-12\new-ui-whats-new --send

REM Send only if policy allows the report status.
python tools\send_roadmap_report.py --run Agentic\Runs\2026-06-12\new-ui-whats-new --policy Agentic\Orchestrator\policy.json --send
```

Script responsibilities:

- load policy,
- load run/report data,
- build structured email model,
- prepare images,
- render HTML,
- render plain text,
- attach configured files,
- call selected provider,
- write send result metadata.

Generated email artifacts:

```text
Agentic/Runs/<date>/<item-id>/email/
  email.html
  email.txt
  message.json
  send-result.json
  images/
```

## Send Result Tracking

Write `send-result.json` after every attempted send.

Suggested shape:

```json
{
  "schema_version": 1,
  "attempted_at": "2026-06-12T00:00:00+10:00",
  "mode": "dry-run",
  "provider": "TODO",
  "from": "Skullbonez Reports <TODO>",
  "to": [],
  "subject": "Skullbonez Roadmap Fixed: TODO",
  "status": "rendered",
  "provider_message_id": null,
  "error": null,
  "html_path": "Agentic/Runs/.../email/email.html",
  "text_path": "Agentic/Runs/.../email/email.txt",
  "inline_images": [],
  "attachments": []
}
```

Valid statuses:

- `rendered`,
- `sent`,
- `skipped-disabled`,
- `skipped-status`,
- `failed-config`,
- `failed-provider`,
- `failed-render`.

## Orchestrator Integration

Update the manual runbook first, then automate.

Manual step after report generation:

```text
If email_reports.enabled is true, run tools\send_roadmap_report.py for the run
directory. Save generated email artifacts and send-result.json in the run folder.
```

Later, when helper scripts own the orchestrator loop:

1. Worker finishes.
2. Orchestrator validates.
3. Orchestrator writes `report.md`.
4. Orchestrator opens/updates PR if allowed.
5. Orchestrator updates queue item state.
6. Orchestrator calls `send_roadmap_report.py`.
7. Orchestrator writes send result path into `run.json`.

The email should not become the source of truth. If email fails, the roadmap
report still exists and the item state remains valid.

## Markdown Report Template Updates

The existing report template is good enough for email generation, but it would
benefit from more structured artifact metadata.

Potential additions:

```markdown
## Email Summary

{{email_summary}}

## Screenshots And Artifacts

| Label | Path | Caption | Attach | Inline |
|-------|------|---------|--------|--------|
| {{label}} | `{{path}}` | {{caption}} | yes | yes |
```

This should be added only if the parser becomes fragile. The first pass can
support current reports and write better metadata into `run.json`.

## Queue Item Updates

Extend `queue.json` screenshot entries so future emails get useful captions.

Suggested future shape:

```json
{
  "screenshot_scenes": [
    {
      "name": "ui-default-tab",
      "command": "Profile\\SKULLBONEZ_CORE.exe --renderer gl --scene Agentic\\Runs\\...\\ui_whats_new.scene",
      "output": "Agentic\\Runs\\...\\ui_whats_new_final.bmp",
      "caption": "Default WHATS NEW tab after the graphite UI refresh."
    }
  ],
  "artifact_commands": [
    {
      "name": "renderer-summary",
      "command": "type TestOutput\\validation\\renderers\\...\\summary.json",
      "output": "Agentic\\Runs\\...\\artifacts\\renderer-summary.json",
      "caption": "Renderer parity summary used by the PR gate."
    }
  ]
}
```

This keeps image explanations close to the roadmap item rather than forcing the
email script to guess.

## Error Handling

The script should distinguish:

- report missing,
- policy disabled,
- provider not configured,
- credentials missing,
- screenshot missing,
- image conversion failed,
- provider rejected send,
- network failure,
- recipient missing.

Rules:

- Missing optional images should not block email; show a warning in the email
  artifact output.
- Missing required report data should block send.
- Provider failures should write `send-result.json` with sanitized error text.
- No error output should include API keys, SMTP passwords, or full auth headers.

## Testing Strategy

Documentation-only planning requires no validation.

When implementation begins, use targeted tests while iterating:

- Run the script in `--dry-run` mode against existing run folders.
- Inspect generated `email.html` in a browser.
- Confirm `.bmp` screenshots convert to `.png`.
- Confirm missing screenshots produce warnings, not crashes.
- Confirm no secrets are written to output files.
- Confirm `send-result.json` records status correctly.
- Use a provider sandbox or test recipient once provider is chosen.

Pre-commit / PR validation mapping:

- If only docs/templates/scripts are changed: `tools\validate_fast.bat`.
- If screenshot generation, renderer validation artifacts, or baseline behavior
  change: `tools\validate_renderers.bat`.
- If only this plan is added: no validation required.

## Implementation Phases

### Phase 1: Plan And Policy Shape

- Add this plan.
- Decide whether emails should send on `pr-open`, `merged`, or both.
- Leave provider as `TODO`.
- Decide whether blocked/failed reports should also email.
- Decide sender domain/subdomain after asking around.

Deliverable:

- Planning document only.

### Phase 2: HTML Renderer Dry Run

- Add `tools/send_roadmap_report.py` without a real provider send path.
- Add `Agentic/Orchestrator/templates/email-report.html`.
- Generate `email.html`, `email.txt`, `message.json`, and image conversions.
- Support existing reports under `Agentic/Runs/2026-06-12`.
- Keep `--send` disabled until provider is configured.

Deliverable:

- Local previewable beautiful report emails.

### Phase 3: Provider Adapter

- Add chosen provider adapter.
- Read secrets from environment or approved secret store.
- Implement `--send`.
- Add provider-specific config validation.
- Add a test mode that sends to one configured recipient.

Deliverable:

- Real email delivery from the dedicated non-Google sender.

### Phase 4: Orchestrator Hook

- Update `Agentic/Orchestrator/runbook.md`.
- Update `policy.json` with final email fields.
- Call the email script after terminal report generation.
- Save `send-result.json` and include send status in future reports.

Deliverable:

- Roadmap reports are sent automatically when policy allows.

### Phase 5: Polish And Hardening

- Improve screenshot captions.
- Add renderer/physics/perf-specific summary cards.
- Add visual preview review before enabling automatic sends.
- Add retry policy if the chosen provider recommends it.
- Add attachment size guardrails.
- Add an optional "send latest run" helper.

Deliverable:

- Reliable, readable, low-maintenance email reporting.

## Suggested HTML Quality Bar

Before enabling automatic sends, approve a rendered preview against at least two
existing reports:

- `Agentic/Runs/2026-06-12/new-ui-whats-new/report.md`, because it has multiple
  screenshots.
- `Agentic/Runs/2026-06-12/asset-texture-registry/report.md`, because it has
  validation artifacts but no separate manual screenshots.

Acceptance criteria:

- Subject line is specific and understandable.
- First screen explains whether the item is fixed.
- Screenshots are visible inline when available.
- Captions explain what the image proves.
- Validation result is obvious without reading logs.
- PR link is prominent.
- Full report and validation log remain accessible.
- Email is readable on a phone.
- Missing optional images do not make the report look broken.

## Open Questions

- Which provider will send the email? `TODO`
- What sender domain or subdomain should be used? `TODO`
- Should emails send on `pr-open`, `merged`, or both?
- Should blocked and failed roadmap items email too?
- Should the full validation log be attached, linked only, or both?
- What is the maximum acceptable email size?
- Should screenshots be inline only, attached only, or both?
- Who should receive reports initially?
- Should there be a separate recipient list for failures?

## Recommended First Pass

Build the dry-run renderer before choosing the provider.

Reasoning:

- The most important product decision is what the report should look like.
- Provider choice should only affect the final delivery adapter.
- Existing run folders already provide good sample data.
- The user can approve the HTML preview before any account, domain, or secret is
  involved.

First implementation task:

```text
Create tools\send_roadmap_report.py in dry-run mode and generate a polished
Agentic\Runs\...\email\email.html preview from existing reports, with converted
PNG screenshots and a plain-text fallback.
```

