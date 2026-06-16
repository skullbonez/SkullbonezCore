---
applyTo: "Agentic/Orchestrator/**,Agentic/Plans/**"
---

# Orchestrator Guidance

Implementing work from `Agentic/Plans` defaults to the orchestrator workflow.
JSON files under `Agentic/Orchestrator` define executable policy, queue state,
and legal transitions. YAML files are human-readable design mirrors until parser
support is intentionally added.

Do not infer runnable work from every Markdown file in `Agentic/Plans`; only
explicit queue entries are eligible. Final report-only commits must contain
only `report.md` and referenced images under the report directory.

Use `tools\orchestrator.bat run-loop [item-id]` for the formal Codex worker /
rubber-duck verifier loop, and `tools\orchestrator.bat finalize <item-id>` for
terminal queue state, source-plan archival, report drafting, and the optional
final report-only commit.
