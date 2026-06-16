# Roadmap Item Report: {{item_id}}

<!--
Embed visual evidence inline throughout the report wherever it helps explain the
work: screenshots, focused crops, heat maps, image diffs, artifact previews, and
before/after architectural diagrams. Do not collect visuals in a standalone
image section. Err on the side of more useful images and diagrams rather than
fewer. Every committed image must live under images/ beside this Markdown file
and be referenced with a relative Markdown link.
-->

## What Changed, In Plain English

{{layman_summary}}

## At A Glance

- Source plan: `{{plan_path}}`
- Archived plan: `{{archived_plan_path}}`
- Branch: `{{branch}}`
- Implementation commit: `{{commit_sha}}`
- Report commit: `{{report_commit_sha}}`
- Report web URL: {{report_web_url}}
- PR: `{{pr_link}}`
- Merge SHA: `{{merge_sha}}`
- Final status: `{{final_status}}`
- Queue status: `{{queue_status}}`
- Queue/status commit: `{{queue_status_commit_sha}}`
- Started: `{{started_at}}`
- Finished: `{{finished_at}}`
- Elapsed: `{{elapsed}}`

## Progress Timeline

{{progress_timeline}}

## Timings

{{timings}}

## Implementation

{{implementation_summary}}

## Changed Files

{{changed_files}}

## Validation

- Required gate: `{{validation_gate}}`
- Commands run:

```text
{{validation_commands}}
```

- Result:

```text
{{validation_result}}
```

## Verification Loop

{{verification_loop}}

## Screenshots And Artifacts

{{artifacts}}

## Interesting Code Snippets

{{code_snippets}}

## PR Status

{{pr_status}}

## Merge Status

{{merge_status}}

## Conflicts

{{conflicts}}

## Residual Risk

{{residual_risk}}

## Sub-Agent Result Summary

{{sub_agent_summary}}

## Verifier Result Summary

{{verifier_summary}}

## Next Queue Action

{{next_queue_action}}
