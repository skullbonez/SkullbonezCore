---
applyTo: "Agentic/Orchestrator/**,Agentic/Plans/**"
---

# Orchestrator Guidance

Implementing work from `Agentic/Plans` defaults to the orchestrator workflow.
YAML files under `Agentic/Orchestrator` define policy, queue state, loop
behavior, and legal transitions. Markdown runbooks explain the workflow, and
JSON files are legacy migration inputs.

Do not infer runnable work from every Markdown file in `Agentic/Plans`; only
explicit queue entries are eligible. Final report-only commits must contain
only `report.md` and referenced images under the report directory.
