# Agentic Instructions

Follow the root `../AGENTS.md` contract first. This folder is documentation,
handoff state, process metadata, reports, skills, audits, and reference
material.

## Local Rules

- Keep `SessionState.md` short and current. Move durable history into plans,
  reports, audits, or reference docs.
- Load skills, plans, audits, reports, and references only when the current task
  calls for them.
- Completed plans belong in `Plans/Done`; failed or rejected plans belong in
  their matching archive folders.
- Implementing work from `Plans/` should default to
  `Skills/orchestrator/SKILL.md`. Ordinary plan drafting or plan maintenance
  can stay as documentation work.
- `Agentic/Runs` contains run-state and raw evidence. Do not treat it as the
  final user-facing report unless a specific workflow says so.
- Documentation-only edits under this folder require no repository validation
  script.
- If editing executable helper scripts under `../tools`, use the root
  `AGENTS.md` tools validation mapping.
