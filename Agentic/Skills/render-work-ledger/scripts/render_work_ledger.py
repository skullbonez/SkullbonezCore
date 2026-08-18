#!/usr/bin/env python3
"""
File: render_work_ledger.py
Purpose:
  Converts the orchestrator's live CSV work ledger into a responsive HTML
  infographic fragment for Codex's in-conversation visualization surface.

Summary:
  The ledger remains the telemetry authority. This script selects its newest
  run, derives display-only aggregates without estimating missing counters, and
  emits one theme-aware fragment whose timeline, effort, cost, review, and
  portfolio views all come from the same rows.

Invariants:
  - Input tokens include cached input; the script reports them separately and
    never adds cached input to the input total.
  - Goal totals come from the goal row; task totals aggregate completed tasks.
  - An unfinished task ends at the ledger snapshot and is labelled in progress.
  - User-provided ledger text is HTML-escaped before it reaches the fragment.

Related:
  - Agentic/Skills/orchestrator/scripts/work_ledger.ps1 owns the CSV schema.
  - Agentic/Skills/render-work-ledger/SKILL.md owns the presentation workflow.
"""

from __future__ import annotations

import argparse
import csv
import html
import json
import tempfile
from dataclasses import dataclass
from datetime import datetime, timedelta
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class Task:
    task_id: str
    title: str
    status: str
    started: datetime
    finished: datetime | None
    elapsed_seconds: int
    validation_seconds: int
    input_tokens: int
    output_tokens: int
    cached_input_tokens: int
    cost_usd: float
    duck_passes: int
    fix_cycles: int
    findings: int
    commit_hash: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Render a WORK_LEDGER.csv infographic fragment.")
    parser.add_argument("--ledger", type=Path, help="CSV ledger to render")
    parser.add_argument("--output", type=Path, help="HTML fragment path")
    parser.add_argument("--self-test", action="store_true", help="Run a deterministic smoke test")
    return parser.parse_args()


def as_int(value: str | None) -> int:
    if not value:
        return 0
    return int(float(value))


def as_float(value: str | None) -> float:
    if not value:
        return 0.0
    return float(value)


def as_datetime(value: str | None) -> datetime | None:
    if not value:
        return None
    return datetime.fromisoformat(value)


def format_duration(seconds: int, *, compact: bool = False) -> str:
    seconds = max(0, seconds)
    hours, remainder = divmod(seconds, 3600)
    minutes, _ = divmod(remainder, 60)
    if compact:
        return f"{hours}h {minutes:02d}m"
    return f"{hours:02d}:{minutes:02d}"


def compact_number(value: int) -> str:
    if value >= 1_000_000_000:
        return f"{value / 1_000_000_000:.2f}B"
    if value >= 1_000_000:
        return f"{value / 1_000_000:.2f}M"
    if value >= 1_000:
        return f"{value / 1_000:.1f}K"
    return str(value)


def task_code(task_id: str) -> str:
    prefix, separator, ordinal = task_id.rpartition("-T")
    if not separator:
        return task_id
    initials = "".join(part[0] for part in prefix.split("_") if part)
    return f"{initials}{ordinal}"


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def load_run(ledger: Path) -> tuple[dict[str, str], list[Task]]:
    with ledger.open("r", encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    goals = [row for row in rows if row.get("record_type") == "goal"]
    if not goals:
        raise ValueError("Ledger has no goal row.")
    goal = max(goals, key=lambda row: row.get("updated_at") or row.get("started_at") or "")
    run_id = goal.get("run_id", "")
    tasks: list[Task] = []
    for row in rows:
        if row.get("record_type") != "task" or row.get("run_id") != run_id:
            continue
        started = as_datetime(row.get("started_at"))
        if started is None:
            raise ValueError(f"Task {row.get('task_id') or '<unknown>'} has no start timestamp.")
        tasks.append(
            Task(
                task_id=row.get("task_id", ""),
                title=row.get("task_title", ""),
                status=row.get("status", ""),
                started=started,
                finished=as_datetime(row.get("finished_at")),
                elapsed_seconds=as_int(row.get("elapsed_seconds")),
                validation_seconds=as_int(row.get("validation_seconds")),
                input_tokens=as_int(row.get("combined_input_tokens")),
                output_tokens=as_int(row.get("combined_output_tokens")),
                cached_input_tokens=as_int(row.get("combined_cached_input_tokens")),
                cost_usd=as_float(row.get("combined_api_cost_usd")),
                duck_passes=as_int(row.get("duck_passes")),
                fix_cycles=as_int(row.get("fix_cycles")),
                findings=as_int(row.get("findings")),
                commit_hash=row.get("commit_hash", ""),
            )
        )
    tasks.sort(key=lambda item: item.started)
    if not tasks:
        raise ValueError(f"Ledger run {run_id or '<blank>'} has no task rows.")
    return goal, tasks


def timeline_bounds(goal: dict[str, str], tasks: Iterable[Task]) -> tuple[datetime, datetime]:
    task_list = list(tasks)
    start = as_datetime(goal.get("started_at")) or min(task.started for task in task_list)
    snapshot = as_datetime(goal.get("updated_at"))
    ends = [task.finished or snapshot or task.started for task in task_list]
    end = max(ends)
    if end <= start:
        end = start + timedelta(seconds=1)
    return start, end


def build_css() -> str:
    return r"""
#work-ledger-infographic{color:var(--foreground);font-family:inherit;width:100%;padding:4px 0 10px}
#work-ledger-infographic *{box-sizing:border-box}
#work-ledger-infographic h1,#work-ledger-infographic h2,#work-ledger-infographic p{margin-top:0}
#work-ledger-infographic .masthead{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:24px;align-items:end;padding-bottom:18px;border-bottom:1px solid var(--border)}
#work-ledger-infographic .eyebrow{color:var(--viz-series-1);letter-spacing:.12em;text-transform:uppercase;margin-bottom:6px}
#work-ledger-infographic h1{margin-bottom:6px}
#work-ledger-infographic .subtitle,#work-ledger-infographic .muted{color:var(--muted-foreground)}
#work-ledger-infographic .subtitle{margin-bottom:0}
#work-ledger-infographic .status{display:flex;align-items:center;gap:10px;white-space:nowrap}
#work-ledger-infographic .dot{width:12px;height:12px;border-radius:50%;background:var(--viz-series-1);box-shadow:0 0 0 6px color-mix(in srgb,var(--viz-series-1) 16%,transparent)}
#work-ledger-infographic .status strong{display:block;font-weight:500}
#work-ledger-infographic .stats{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin:18px 0 26px}
#work-ledger-infographic .viz-stat-value,#work-ledger-infographic .numeric{font-variant-numeric:tabular-nums}
#work-ledger-infographic .section-head{display:flex;align-items:baseline;justify-content:space-between;gap:14px;margin-bottom:14px}
#work-ledger-infographic .section-head h2{margin-bottom:0}
#work-ledger-infographic .axis{display:grid;grid-template-columns:repeat(8,1fr);margin:0 76px 8px 170px;color:var(--muted-foreground);font-variant-numeric:tabular-nums}
#work-ledger-infographic .axis span:last-child{text-align:right}
#work-ledger-infographic .timeline{display:grid;gap:7px;margin-bottom:28px}
#work-ledger-infographic .task-row{display:grid;grid-template-columns:158px minmax(0,1fr) 72px;gap:12px;align-items:center;min-height:34px}
#work-ledger-infographic .task-label{min-width:0}
#work-ledger-infographic .task-label strong{display:block;font-weight:500}
#work-ledger-infographic .task-label span{display:block;color:var(--muted-foreground);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#work-ledger-infographic .track{position:relative;height:28px;background:repeating-linear-gradient(90deg,transparent 0,transparent calc(12.5% - 1px),var(--border) calc(12.5% - 1px),var(--border) 12.5%);border-left:1px solid var(--border);border-right:1px solid var(--border)}
#work-ledger-infographic .task-bar{position:absolute;left:var(--start);width:max(var(--width),7px);top:3px;bottom:3px;border-radius:4px;background:var(--viz-series-2)}
#work-ledger-infographic .task-row.current .task-bar{background:color-mix(in srgb,var(--viz-series-1) 22%,transparent);border:1px solid var(--viz-series-1)}
#work-ledger-infographic .duration{text-align:right;color:var(--muted-foreground);font-variant-numeric:tabular-nums}
#work-ledger-infographic .lower{display:grid;grid-template-columns:minmax(0,1.2fr) minmax(280px,.8fr);gap:28px;margin-bottom:22px}
#work-ledger-infographic .allocation{display:flex;height:26px;margin:10px 0 8px;border-radius:6px;overflow:hidden;background:var(--muted)}
#work-ledger-infographic .allocation .validation{width:var(--validation);background:var(--viz-series-3)}
#work-ledger-infographic .allocation .other{flex:1;background:var(--viz-series-2)}
#work-ledger-infographic .legend{display:flex;flex-wrap:wrap;gap:8px 18px;color:var(--muted-foreground)}
#work-ledger-infographic .legend span{display:inline-flex;align-items:center;gap:7px}
#work-ledger-infographic .swatch{width:10px;height:10px;border-radius:2px;background:var(--viz-series-3)}
#work-ledger-infographic .swatch.other{background:var(--viz-series-2)}
#work-ledger-infographic .review{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-top:14px}
#work-ledger-infographic .review div{border-top:2px solid var(--viz-series-1);padding-top:8px}
#work-ledger-infographic .review strong{display:block;font-weight:500;font-variant-numeric:tabular-nums}
#work-ledger-infographic .cost-list{display:grid;gap:9px;margin-top:12px}
#work-ledger-infographic .cost-row{display:grid;grid-template-columns:54px minmax(0,1fr) 70px;gap:9px;align-items:center}
#work-ledger-infographic .cost-track{height:8px;background:var(--muted);border-radius:999px;overflow:hidden}
#work-ledger-infographic .cost-track span{display:block;height:100%;width:var(--cost);background:var(--viz-series-4)}
#work-ledger-infographic .money{text-align:right;color:var(--muted-foreground);font-variant-numeric:tabular-nums}
#work-ledger-infographic footer{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:16px;align-items:center;padding-top:14px;border-top:1px solid var(--border);color:var(--muted-foreground)}
#work-ledger-infographic .portfolio{display:flex;align-items:center;gap:12px}
#work-ledger-infographic .portfolio-bar{flex:1;height:8px;min-width:120px;background:var(--muted);border-radius:999px;overflow:hidden}
#work-ledger-infographic .portfolio-bar span{display:block;width:var(--portfolio);height:100%;background:var(--viz-series-1)}
#work-ledger-infographic .source{white-space:nowrap}
@media(max-width:760px){#work-ledger-infographic .stats{grid-template-columns:repeat(2,minmax(0,1fr))}#work-ledger-infographic .lower{grid-template-columns:1fr}#work-ledger-infographic .task-row{grid-template-columns:128px minmax(0,1fr) 62px;gap:8px}#work-ledger-infographic .axis{margin-left:136px;margin-right:66px}#work-ledger-infographic .axis span:nth-child(even){visibility:hidden}}
@media(max-width:480px){#work-ledger-infographic .masthead{grid-template-columns:1fr;align-items:start}#work-ledger-infographic .stats{grid-template-columns:1fr}#work-ledger-infographic .section-head{display:block}#work-ledger-infographic .section-head span{display:block;margin-top:4px}#work-ledger-infographic .axis{display:none}#work-ledger-infographic .task-row{grid-template-columns:minmax(0,1fr);padding-bottom:5px}#work-ledger-infographic .track{grid-row:2}#work-ledger-infographic .duration{display:none}#work-ledger-infographic .cost-row{grid-template-columns:46px minmax(0,1fr) 58px;gap:6px}#work-ledger-infographic footer{grid-template-columns:1fr}#work-ledger-infographic .portfolio{display:grid;grid-template-columns:auto minmax(0,1fr)}#work-ledger-infographic .portfolio>span:last-child{grid-column:1/-1}#work-ledger-infographic .source{white-space:normal;overflow-wrap:anywhere}}
"""


def build_fragment(ledger: Path, goal: dict[str, str], tasks: list[Task]) -> tuple[str, dict[str, object]]:
    completed = [task for task in tasks if task.status == "complete"]
    current = next((task for task in reversed(tasks) if task.status != "complete"), None)
    start, snapshot = timeline_bounds(goal, tasks)
    span_seconds = max(1.0, (snapshot - start).total_seconds())

    # Invariant: cumulative traffic and cost belong to the goal row. Summing
    # task rows would omit live goal overhead and can double-count future CSV
    # schemas that retain more than one task projection.
    goal_elapsed = as_int(goal.get("elapsed_seconds")) or int(span_seconds)
    completed_elapsed = sum(task.elapsed_seconds for task in completed)
    validation_seconds = sum(task.validation_seconds for task in completed)
    other_seconds = max(0, completed_elapsed - validation_seconds)
    validation_percent = 100.0 * validation_seconds / completed_elapsed if completed_elapsed else 0.0
    goal_input = as_int(goal.get("combined_input_tokens"))
    goal_output = as_int(goal.get("combined_output_tokens"))
    goal_cached = as_int(goal.get("combined_cached_input_tokens"))
    cache_percent = 100.0 * goal_cached / goal_input if goal_input else 0.0
    goal_cost = as_float(goal.get("combined_api_cost_usd"))
    task_cost = sum(task.cost_usd for task in completed)
    max_cost = max((task.cost_usd for task in completed), default=1.0)
    duck_passes = sum(task.duck_passes for task in completed)
    findings = sum(task.findings for task in completed)
    fix_cycles = sum(task.fix_cycles for task in completed)
    commits = sum(bool(task.commit_hash) for task in completed)
    portfolio_done = as_int(goal.get("portfolio_completed_tasks"))
    portfolio_total = as_int(goal.get("portfolio_total_tasks"))
    portfolio_percent = as_float(goal.get("portfolio_percent_complete"))
    run_id = goal.get("run_id", "")
    overnight = start.date() != snapshot.date() and goal_elapsed <= 24 * 3600
    opening = "One night." if overnight else "One run."
    task_noun = "task" if len(completed) == 1 else "tasks"
    current_label = task_code(current.task_id) if current else "Run complete"
    current_title = current.title if current else (goal.get("outcome") or "All recorded tasks closed")

    axis_ticks = [start + timedelta(seconds=span_seconds * index / 7) for index in range(8)]
    axis_html = "".join(f"<span>{tick:%H:%M}</span>" for tick in axis_ticks)
    task_rows: list[str] = []
    for task in tasks:
        task_end = task.finished or snapshot

        # Concept: every task uses the goal's wall-clock axis. Gaps remain
        # visible, and an unfinished task ends at the refreshed snapshot.
        left = 100.0 * (task.started - start).total_seconds() / span_seconds
        width = 100.0 * max(0.0, (task_end - task.started).total_seconds()) / span_seconds
        state_class = " current" if task.status != "complete" else ""
        duration = "started" if task.status != "complete" and task.elapsed_seconds == 0 else format_duration(task.elapsed_seconds, compact=True)
        task_rows.append(
            f'<div class="task-row{state_class}">'
            f'<div class="task-label"><strong>{esc(task_code(task.task_id))}</strong><span>{esc(task.title)}</span></div>'
            f'<div class="track"><span class="task-bar" style="--start:{left:.3f}%;--width:{width:.3f}%"></span></div>'
            f'<div class="duration">{esc(duration)}</div></div>'
        )

    cost_rows = "".join(
        f'<div class="cost-row"><strong>{esc(task_code(task.task_id))}</strong>'
        f'<div class="cost-track"><span style="--cost:{100.0 * task.cost_usd / max_cost:.2f}%"></span></div>'
        f'<div class="money">${task.cost_usd:,.2f}</div></div>'
        for task in completed
    )
    pricing = " · ".join(
        item for item in (goal.get("pricing_tier", ""), goal.get("pricing_context", "")) if item
    )
    ledger_label = ledger.as_posix()
    source_label = ledger_label.split("Agentic/", 1)[-1]
    if source_label != ledger_label:
        source_label = f"Agentic/{source_label}"

    fragment = f"""<div id="work-ledger-infographic">
<style>{build_css()}</style>
<header class="masthead">
  <div><p class="eyebrow text-small">Work ledger · run {esc(run_id)}</p><h1>{opening} {len(completed)} {task_noun} landed.</h1>
  <p class="subtitle">{start:%H:%M %a %d %b} → {snapshot:%H:%M %a %d %b} · {format_duration(goal_elapsed, compact=True)} recorded</p></div>
  <div class="status" aria-label="Current task: {esc(current_label)} {esc(current_title)}"><span class="dot" aria-hidden="true"></span>
  <div><strong>{esc(current_label)}{' in progress' if current else ''}</strong><span class="muted">{esc(current_title)}</span></div></div>
</header>
<section class="stats" aria-label="Run totals">
  <div class="card viz-stat"><div class="text-muted">Tasks closed</div><div class="viz-stat-value">{len(completed)}</div><div class="text-small text-muted">{commits} commits pushed</div></div>
  <div class="card viz-stat"><div class="text-muted">Validation</div><div class="viz-stat-value">{format_duration(validation_seconds, compact=True)}</div><div class="text-small text-muted">{validation_percent:.1f}% of completed task time</div></div>
  <div class="card viz-stat"><div class="text-muted">Model traffic</div><div class="viz-stat-value">{compact_number(goal_input)} in</div><div class="text-small text-muted">{compact_number(goal_output)} out · {cache_percent:.1f}% cached</div></div>
  <div class="card viz-stat"><div class="text-muted">Recorded API cost</div><div class="viz-stat-value">${goal_cost:,.2f}</div><div class="text-small text-muted">{esc(pricing or 'ledger pricing basis')}</div></div>
</section>
<section aria-labelledby="ledger-timeline-title"><div class="section-head"><h2 id="ledger-timeline-title">The run, task by task</h2><span class="muted">bar position = clock time</span></div>
  <div class="axis text-small" aria-hidden="true">{axis_html}</div>
  <div class="timeline" role="img" aria-label="Task timeline from {start:%H:%M} to {snapshot:%H:%M}">{''.join(task_rows)}</div>
</section>
<section class="lower" aria-label="Effort cost and review details">
  <div><div class="section-head"><h2>Where the run went</h2><span class="muted">completed task time · {format_duration(completed_elapsed, compact=True)}</span></div>
    <div class="allocation" style="--validation:{validation_percent:.3f}%" role="img" aria-label="{format_duration(validation_seconds, compact=True)} validation and {format_duration(other_seconds, compact=True)} other task work"><span class="validation"></span><span class="other"></span></div>
    <div class="legend text-small"><span><i class="swatch"></i>Validation · {format_duration(validation_seconds, compact=True)}</span><span><i class="swatch other"></i>Implementation, review &amp; commits · {format_duration(other_seconds, compact=True)}</span></div>
    <div class="section-head" style="margin-top:24px"><h2>Review pressure</h2><span class="muted">recorded critique and repair</span></div>
    <div class="review"><div><strong>{duck_passes}</strong><span class="muted">duck passes</span></div><div><strong>{findings}</strong><span class="muted">findings</span></div><div><strong>{fix_cycles}</strong><span class="muted">fix cycles</span></div></div>
  </div>
  <div><div class="section-head"><h2>Cost by task</h2><span class="muted">${task_cost:,.2f} completed-task total</span></div><div class="cost-list" role="img" aria-label="Recorded API cost by completed task">{cost_rows}</div></div>
</section>
<footer><div class="portfolio"><strong>Portfolio</strong><div class="portfolio-bar" style="--portfolio:{portfolio_percent:.3f}%" aria-label="{portfolio_done} of {portfolio_total} tasks complete"><span></span></div><span class="numeric">{portfolio_done} / {portfolio_total} · {portfolio_percent:.2f}%</span></div>
<div class="source"><code>{esc(source_label)}</code> · snapshot {snapshot:%H:%M:%S %z}</div></footer>
</div>"""
    summary: dict[str, object] = {
        "run_id": run_id,
        "completed_tasks": len(completed),
        "current_task": current.task_id if current else None,
        "elapsed_seconds": goal_elapsed,
        "portfolio_completed_tasks": portfolio_done,
        "portfolio_total_tasks": portfolio_total,
        "portfolio_percent_complete": portfolio_percent,
        "combined_input_tokens": goal_input,
        "combined_output_tokens": goal_output,
        "combined_cached_input_tokens": goal_cached,
        "combined_api_cost_usd": round(goal_cost, 6),
        "validation_seconds": validation_seconds,
        "output": None,
    }
    return fragment, summary


def render(ledger: Path, output: Path) -> dict[str, object]:
    goal, tasks = load_run(ledger)
    fragment, summary = build_fragment(ledger, goal, tasks)

    # Invariant: write a fragment, not a standalone document. The conversation
    # visualization host supplies the document shell and theme variables.
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(fragment, encoding="utf-8", newline="\n")
    summary["output"] = str(output.resolve())
    return summary


def self_test() -> None:
    headers = [
        "record_type", "run_id", "goal", "task_id", "task_title", "status", "started_at", "finished_at",
        "elapsed_seconds", "validation_seconds", "combined_input_tokens", "combined_output_tokens",
        "combined_cached_input_tokens", "combined_api_cost_usd", "duck_passes", "fix_cycles", "findings",
        "commit_hash", "portfolio_completed_tasks", "portfolio_total_tasks", "portfolio_percent_complete",
        "pricing_tier", "pricing_context", "updated_at", "outcome",
    ]
    rows = [
        {
            "record_type": "goal", "run_id": "test-run", "goal": "MASTER-PLAN", "status": "in_progress",
            "started_at": "2026-08-18T22:00:00+10:00", "elapsed_seconds": "14400",
            "combined_input_tokens": "1000000", "combined_output_tokens": "2000",
            "combined_cached_input_tokens": "900000", "combined_api_cost_usd": "12.5",
            "portfolio_completed_tasks": "2", "portfolio_total_tasks": "4", "portfolio_percent_complete": "50.0",
            "pricing_tier": "standard", "pricing_context": "short", "updated_at": "2026-08-19T02:00:00+10:00",
        },
        {
            "record_type": "task", "run_id": "test-run", "task_id": "DEMO_PLAN-T0", "task_title": "Close <unsafe> task",
            "status": "complete", "started_at": "2026-08-18T22:00:00+10:00", "finished_at": "2026-08-19T00:00:00+10:00",
            "elapsed_seconds": "7200", "validation_seconds": "1800", "combined_input_tokens": "500000",
            "combined_output_tokens": "1000", "combined_cached_input_tokens": "450000", "combined_api_cost_usd": "5.0",
            "duck_passes": "1", "fix_cycles": "1", "findings": "2", "commit_hash": "abc123",
        },
        {
            "record_type": "task", "run_id": "test-run", "task_id": "DEMO_PLAN-T1", "task_title": "Continue safely",
            "status": "in_progress", "started_at": "2026-08-19T00:00:00+10:00", "elapsed_seconds": "7200",
            "combined_input_tokens": "500000", "combined_output_tokens": "1000", "combined_cached_input_tokens": "450000",
            "combined_api_cost_usd": "7.5",
        },
    ]
    with tempfile.TemporaryDirectory(prefix="work-ledger-infographic-") as temp_dir:
        ledger = Path(temp_dir) / "WORK_LEDGER.csv"
        output = Path(temp_dir) / "ledger.html"
        with ledger.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=headers, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(rows)
        summary = render(ledger, output)
        rendered = output.read_text(encoding="utf-8")
        checks = [
            summary["completed_tasks"] == 1,
            summary["current_task"] == "DEMO_PLAN-T1",
            summary["portfolio_percent_complete"] == 50.0,
            "One night. 1 task landed." in rendered,
            "Close &lt;unsafe&gt; task" in rendered,
            "Close <unsafe> task" not in rendered,
            "$12.50" in rendered,
            "90.0% cached" in rendered,
        ]
        if not all(checks):
            raise AssertionError("Self-test output did not preserve the expected ledger contract.")
    print("PASS: work-ledger CSV parsing, aggregation, escaping, timeline, and infographic rendering.")


def main() -> int:
    args = parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.ledger is None or args.output is None:
        raise SystemExit("--ledger and --output are required unless --self-test is used.")
    summary = render(args.ledger.resolve(), args.output.resolve())
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
