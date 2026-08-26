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
import math
import sys
import tempfile
from dataclasses import dataclass, replace
from datetime import datetime, timedelta
from pathlib import Path
from typing import Iterable

# Increase CSV field size limit for embedded base64 state payloads (Windows C long max)
csv.field_size_limit(2147483647)


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


@dataclass(frozen=True)
class ParallelTimeline:
    placements: tuple[tuple[Task, int], ...]
    lane_count: int
    active_seconds: int
    parallel_seconds: int
    worker_seconds: int
    peak_concurrency: int
    overlap_segments: tuple[tuple[datetime, datetime, int], ...]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Render a WORK_LEDGER.csv infographic.")
    parser.add_argument("--ledger", type=Path, help="CSV ledger to render")
    parser.add_argument("--output", type=Path, help="HTML output path")
    parser.add_argument(
        "--standalone",
        action="store_true",
        help="Wrap the infographic in a self-contained UTF-8 HTML document",
    )
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
    # The ledger exporter refreshes every historical goal row with one snapshot
    # timestamp. The newest run is therefore the goal with the latest start,
    # not the first row sharing the latest update timestamp.
    goal = max(goals, key=lambda row: row.get("started_at") or "")
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


def task_stream(task_id: str) -> str:
    if task_id.startswith("RUNTIME_BOUNDARIES"):
        return "Runtime boundaries"
    if task_id.startswith("RAGDOLL_PHYSICS"):
        return "Physics"
    if task_id.startswith("GAME_UI_COMPONENTS"):
        return "Game UI"
    if "ERROR_OBSERVABILITY" in task_id:
        return "Error handling"
    if task_id.startswith("ORCHESTRATOR"):
        return "Orchestration"
    return "Bug fixes"


def group_task_costs(tasks: Iterable[Task], *, individual_limit: int = 9) -> list[tuple[str, float, int]]:
    """Keep the expensive tasks legible and combine the long cheap tail."""
    paid = sorted((task for task in tasks if task.cost_usd > 0.0), key=lambda task: (-task.cost_usd, task.task_id))
    groups = [(task_code(task.task_id), task.cost_usd, 1) for task in paid[:individual_limit]]
    remainder = paid[individual_limit:]
    if remainder:
        groups.append(("Other tasks", sum(task.cost_usd for task in remainder), len(remainder)))
    return groups


def pie_slice_path(start_angle: float, end_angle: float, *, center: float = 150.0, radius: float = 126.0) -> str:
    """Return one clockwise SVG pie wedge, with zero degrees at twelve o'clock."""
    start_x = center + radius * math.sin(start_angle)
    start_y = center - radius * math.cos(start_angle)
    end_x = center + radius * math.sin(end_angle)
    end_y = center - radius * math.cos(end_angle)
    large_arc = 1 if end_angle - start_angle > math.pi else 0
    return (
        f"M {center:.3f} {center:.3f} L {start_x:.3f} {start_y:.3f} "
        f"A {radius:.3f} {radius:.3f} 0 {large_arc} 1 {end_x:.3f} {end_y:.3f} Z"
    )


def build_parallel_timeline(tasks: Iterable[Task], start: datetime, snapshot: datetime) -> ParallelTimeline:
    """Assign interval-graph lanes and measure real wall-clock overlap."""
    ordered = sorted(tasks, key=lambda item: (item.started, item.task_id))
    lane_ends: list[datetime] = []
    placements: list[tuple[Task, int]] = []
    events: dict[datetime, int] = {}
    worker_seconds = 0

    for task in ordered:
        task_end = task.finished or snapshot
        task_end = max(task.started + timedelta(seconds=1), task_end)
        lane = next((index for index, end in enumerate(lane_ends) if end <= task.started), len(lane_ends))
        if lane == len(lane_ends):
            lane_ends.append(task_end)
        else:
            lane_ends[lane] = task_end
        placements.append((task, lane))

        clipped_start = max(start, task.started)
        clipped_end = min(snapshot, task_end)
        if clipped_end <= clipped_start:
            continue
        events[clipped_start] = events.get(clipped_start, 0) + 1
        events[clipped_end] = events.get(clipped_end, 0) - 1
        worker_seconds += int((clipped_end - clipped_start).total_seconds())

    active = 0
    active_seconds = 0
    parallel_seconds = 0
    peak = 0
    previous = start
    overlap_segments: list[tuple[datetime, datetime, int]] = []
    for moment in sorted(events):
        if moment > previous:
            seconds = int((moment - previous).total_seconds())
            if active > 0:
                active_seconds += seconds
            if active > 1:
                parallel_seconds += seconds
                overlap_segments.append((previous, moment, active))
        active += events[moment]
        peak = max(peak, active)
        previous = moment

    return ParallelTimeline(
        placements=tuple(placements),
        lane_count=len(lane_ends),
        active_seconds=active_seconds,
        parallel_seconds=parallel_seconds,
        worker_seconds=worker_seconds,
        peak_concurrency=peak,
        overlap_segments=tuple(overlap_segments),
    )


def build_css() -> str:
    return r"""
#work-ledger-infographic{color:var(--foreground);font-family:inherit;width:100%;padding:4px 0 10px}
#work-ledger-infographic *{box-sizing:border-box}
#work-ledger-infographic h1,#work-ledger-infographic h2,#work-ledger-infographic p{margin-top:0}
#work-ledger-infographic .masthead{display:grid;grid-template-columns:minmax(320px,.85fr) minmax(0,1.15fr);gap:32px;align-items:end;padding-bottom:18px;border-bottom:1px solid var(--border)}
#work-ledger-infographic .eyebrow{color:var(--viz-series-1);letter-spacing:.12em;text-transform:uppercase;margin-bottom:6px}
#work-ledger-infographic h1{margin-bottom:6px}
#work-ledger-infographic .subtitle,#work-ledger-infographic .muted{color:var(--muted-foreground)}
#work-ledger-infographic .subtitle{margin-bottom:0}
#work-ledger-infographic .status{display:flex;align-items:flex-start;gap:10px;min-width:0}
#work-ledger-infographic .dot{width:12px;height:12px;border-radius:50%;background:var(--viz-series-1);box-shadow:0 0 0 6px color-mix(in srgb,var(--viz-series-1) 16%,transparent)}
#work-ledger-infographic .status strong{display:block;font-weight:500}
#work-ledger-infographic .stats{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin:18px 0 26px}
#work-ledger-infographic .viz-stat-value,#work-ledger-infographic .numeric{font-variant-numeric:tabular-nums}
#work-ledger-infographic .section-head{display:flex;align-items:baseline;justify-content:space-between;gap:14px;margin-bottom:14px}
#work-ledger-infographic .section-head h2{margin-bottom:0}
#work-ledger-infographic .axis{display:grid;grid-template-columns:repeat(8,1fr);margin:0 76px 8px 170px;color:var(--muted-foreground);font-variant-numeric:tabular-nums}
#work-ledger-infographic .axis span:last-child{text-align:right}
#work-ledger-infographic .timeline{display:grid;gap:7px;margin-bottom:12px}
#work-ledger-infographic .lane-row,#work-ledger-infographic .overlap-row{display:grid;grid-template-columns:158px minmax(0,1fr) 72px;gap:12px;align-items:center;min-height:36px}
#work-ledger-infographic .lane-label{min-width:0}
#work-ledger-infographic .lane-label strong{display:block;font-weight:500}
#work-ledger-infographic .lane-label span{display:block;color:var(--muted-foreground);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#work-ledger-infographic .track{position:relative;height:30px;background:repeating-linear-gradient(90deg,transparent 0,transparent calc(12.5% - 1px),var(--border) calc(12.5% - 1px),var(--border) 12.5%);border-left:1px solid var(--border);border-right:1px solid var(--border)}
#work-ledger-infographic .task-bar{position:absolute;left:var(--start);width:max(var(--width),7px);top:3px;bottom:3px;border-radius:4px;background:color-mix(in srgb,var(--bar-color) 54%,var(--background));border:1px solid var(--bar-color);color:var(--foreground);display:flex;align-items:center;overflow:hidden;padding:0 6px;white-space:nowrap}
#work-ledger-infographic .task-bar span{overflow:hidden;text-overflow:ellipsis;font-variant-numeric:tabular-nums}
#work-ledger-infographic .task-bar.current{background:color-mix(in srgb,var(--viz-series-1) 28%,var(--background));border-color:var(--viz-series-1)}
#work-ledger-infographic .overlap-row{margin-bottom:4px}
#work-ledger-infographic .overlap-track{height:14px;background:var(--muted)}
#work-ledger-infographic .overlap-segment{position:absolute;left:var(--start);width:max(var(--width),2px);top:0;bottom:0;background:var(--viz-series-1);opacity:var(--overlap-opacity)}
#work-ledger-infographic .duration{text-align:right;color:var(--muted-foreground);font-variant-numeric:tabular-nums}
#work-ledger-infographic .stream-legend{display:flex;flex-wrap:wrap;gap:7px 16px;margin:0 76px 26px 170px;color:var(--muted-foreground)}
#work-ledger-infographic .stream-legend span{display:inline-flex;align-items:center;gap:6px}
#work-ledger-infographic .stream-swatch{width:10px;height:10px;border-radius:2px;background:var(--stream-color)}
#work-ledger-infographic .cost-section{margin-bottom:30px}
#work-ledger-infographic .cost-visual{display:grid;grid-template-columns:minmax(300px,.9fr) minmax(340px,1.1fr);gap:34px;align-items:center}
#work-ledger-infographic .cost-pie{display:block;width:min(100%,390px);margin:0 auto;overflow:visible}
#work-ledger-infographic .cost-pie path,#work-ledger-infographic .cost-pie circle{stroke:var(--background);stroke-width:2}
#work-ledger-infographic .cost-pie text{fill:var(--foreground);font-family:inherit;text-anchor:middle}
#work-ledger-infographic .cost-pie .pie-total{font-size:20px;font-weight:500}
#work-ledger-infographic .cost-pie .pie-caption{font-size:12px;fill:var(--muted-foreground)}
#work-ledger-infographic .cost-legend{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px 24px}
#work-ledger-infographic .cost-item{display:grid;grid-template-columns:12px minmax(0,1fr) auto;gap:8px;align-items:start;border-bottom:1px solid var(--border);padding:8px 0}
#work-ledger-infographic .cost-item .stream-swatch{margin-top:4px}
#work-ledger-infographic .cost-item strong{display:block;font-weight:500;overflow-wrap:anywhere}
#work-ledger-infographic .cost-item .money{text-align:right;color:var(--foreground);font-variant-numeric:tabular-nums;white-space:nowrap}
#work-ledger-infographic .effort-section{margin-bottom:22px}
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
#work-ledger-infographic footer{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:16px;align-items:center;padding-top:14px;border-top:1px solid var(--border);color:var(--muted-foreground)}
#work-ledger-infographic .portfolio{display:flex;align-items:center;gap:12px}
#work-ledger-infographic .portfolio-bar{flex:1;height:8px;min-width:120px;background:var(--muted);border-radius:999px;overflow:hidden}
#work-ledger-infographic .portfolio-bar span{display:block;width:var(--portfolio);height:100%;background:var(--viz-series-1)}
#work-ledger-infographic .source{white-space:nowrap}
@media(max-width:760px){#work-ledger-infographic .masthead{grid-template-columns:1fr;align-items:start}#work-ledger-infographic .stats{grid-template-columns:repeat(2,minmax(0,1fr))}#work-ledger-infographic .cost-visual{grid-template-columns:1fr}#work-ledger-infographic .lane-row,#work-ledger-infographic .overlap-row{grid-template-columns:128px minmax(0,1fr) 62px;gap:8px}#work-ledger-infographic .axis{margin-left:136px;margin-right:66px}#work-ledger-infographic .stream-legend{margin-left:136px;margin-right:66px}#work-ledger-infographic .axis span:nth-child(even){visibility:hidden}}
@media(max-width:480px){#work-ledger-infographic .masthead{grid-template-columns:1fr;align-items:start}#work-ledger-infographic .stats{grid-template-columns:1fr}#work-ledger-infographic .section-head{display:block}#work-ledger-infographic .section-head span{display:block;margin-top:4px}#work-ledger-infographic .axis{display:none}#work-ledger-infographic .lane-row,#work-ledger-infographic .overlap-row{grid-template-columns:minmax(0,1fr);padding-bottom:5px}#work-ledger-infographic .track{grid-row:2}#work-ledger-infographic .duration{display:none}#work-ledger-infographic .stream-legend{margin:0 0 22px}#work-ledger-infographic .cost-visual{gap:16px}#work-ledger-infographic .cost-legend{grid-template-columns:1fr}#work-ledger-infographic footer{grid-template-columns:1fr}#work-ledger-infographic .portfolio{display:grid;grid-template-columns:auto minmax(0,1fr)}#work-ledger-infographic .portfolio>span:last-child{grid-column:1/-1}#work-ledger-infographic .source{white-space:normal;overflow-wrap:anywhere}}
"""


def build_fragment(ledger: Path, goal: dict[str, str], tasks: list[Task]) -> tuple[str, dict[str, object]]:
    completed = [task for task in tasks if task.status == "complete"]
    current = next((task for task in reversed(tasks) if task.status != "complete"), None)
    start, snapshot = timeline_bounds(goal, tasks)
    span_seconds = max(1.0, (snapshot - start).total_seconds())
    parallel = build_parallel_timeline(tasks, start, snapshot)

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
    stream_colors = {
        "Runtime boundaries": "var(--viz-series-1)",
        "Physics": "var(--viz-series-2)",
        "Game UI": "var(--viz-series-3)",
        "Error handling": "var(--viz-series-4)",
        "Orchestration": "var(--viz-series-5)",
        "Bug fixes": "var(--viz-series-6)",
    }
    lane_tasks: list[list[Task]] = [[] for _ in range(parallel.lane_count)]
    for task, lane in parallel.placements:
        lane_tasks[lane].append(task)

    lane_rows: list[str] = []
    for lane, assigned in enumerate(lane_tasks):
        bars: list[str] = []
        for task in assigned:
            task_end = max(task.started + timedelta(seconds=1), task.finished or snapshot)
            left = 100.0 * (task.started - start).total_seconds() / span_seconds
            width = 100.0 * max(0.0, (task_end - task.started).total_seconds()) / span_seconds
            stream = task_stream(task.task_id)
            current_class = " current" if task.status != "complete" else ""
            tooltip = f"{task_code(task.task_id)} · {task.title} · {task.started:%H:%M}–{task_end:%H:%M}"
            bars.append(
                f'<span class="task-bar{current_class}" style="--start:{left:.3f}%;--width:{width:.3f}%;--bar-color:{stream_colors[stream]}" '
                f'data-tooltip="{esc(tooltip)}" aria-label="{esc(tooltip)}"><span>{esc(task_code(task.task_id))}</span></span>'
            )
        lane_rows.append(
            f'<div class="lane-row"><div class="lane-label"><strong>Worker lane {lane + 1}</strong>'
            f'<span>{len(assigned)} task{"s" if len(assigned) != 1 else ""}</span></div>'
            f'<div class="track">{"".join(bars)}</div><div class="duration">{len(assigned)} tasks</div></div>'
        )

    overlap_spans = "".join(
        f'<span class="overlap-segment" style="--start:{100.0 * (segment_start - start).total_seconds() / span_seconds:.3f}%;'
        f'--width:{100.0 * (segment_end - segment_start).total_seconds() / span_seconds:.3f}%;'
        f'--overlap-opacity:{min(0.9, 0.24 + 0.16 * count):.2f}" '
        f'data-tooltip="{count} workers active · {segment_start:%H:%M}–{segment_end:%H:%M}"></span>'
        for segment_start, segment_end, count in parallel.overlap_segments
    )
    stream_legend = "".join(
        f'<span><i class="stream-swatch" style="--stream-color:{color}"></i>{esc(stream)}</span>'
        for stream, color in stream_colors.items()
        if any(task_stream(task.task_id) == stream for task in tasks)
    )

    cost_groups = group_task_costs(completed)
    cost_colors = [f"var(--viz-series-{index})" for index in range(1, 7)] + ["var(--muted-foreground)"]
    cost_total = sum(cost for _, cost, _ in cost_groups)
    pie_shapes: list[str] = []
    cost_items: list[str] = []
    cursor = 0.0
    for index, (label, cost, grouped_count) in enumerate(cost_groups):
        fraction = cost / cost_total if cost_total else 0.0
        end = cursor + math.tau * fraction
        color = cost_colors[index % len(cost_colors)]
        aria = f"{label}: ${cost:,.2f}, {100.0 * fraction:.1f}%"
        if fraction >= 0.999999:
            shape = f'<circle cx="150" cy="150" r="126" fill="{color}" data-tooltip="{esc(aria)}"></circle>'
        else:
            shape = f'<path d="{pie_slice_path(cursor, end)}" fill="{color}" data-tooltip="{esc(aria)}"></path>'
        pie_shapes.append(shape)
        grouped_note = f"{grouped_count} inexpensive tasks" if grouped_count > 1 else f"{100.0 * fraction:.1f}% of task cost"
        cost_items.append(
            f'<div class="cost-item"><i class="stream-swatch" style="--stream-color:{color}"></i>'
            f'<div><strong>{esc(label)}</strong><span class="text-small text-muted">{esc(grouped_note)}</span></div>'
            f'<div class="money">${cost:,.2f}</div></div>'
        )
        cursor = end
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
  <div class="card viz-stat"><div class="text-muted">Tasks closed</div><div class="viz-stat-value">{len(completed)}</div><div class="text-small text-muted">{commits} commits · peak {parallel.peak_concurrency} parallel</div></div>
  <div class="card viz-stat"><div class="text-muted">Total time</div><div class="viz-stat-value">{format_duration(completed_elapsed, compact=True)}</div><div class="text-small text-muted">{format_duration(other_seconds, compact=True)} tasks · {format_duration(validation_seconds, compact=True)} validation</div></div>
  <div class="card viz-stat"><div class="text-muted">Model traffic</div><div class="viz-stat-value">{compact_number(goal_input)} in</div><div class="text-small text-muted">{compact_number(goal_output)} out · {cache_percent:.1f}% cached</div></div>
  <div class="card viz-stat"><div class="text-muted">Recorded API cost</div><div class="viz-stat-value">${goal_cost:,.2f}</div><div class="text-small text-muted">{esc(pricing or 'ledger pricing basis')}</div></div>
</section>
<section aria-labelledby="ledger-timeline-title"><div class="section-head"><h2 id="ledger-timeline-title">The run, in parallel</h2><span class="muted">{format_duration(parallel.parallel_seconds, compact=True)} with 2+ workers · peak {parallel.peak_concurrency}</span></div>
  <div class="axis text-small" aria-hidden="true">{axis_html}</div>
  <div class="timeline" role="img" aria-label="Parallel worker timeline from {start:%H:%M} to {snapshot:%H:%M}; peak concurrency {parallel.peak_concurrency}">
    <div class="overlap-row"><div class="lane-label"><strong>Parallel overlap</strong><span>2 or more workers</span></div><div class="track overlap-track">{overlap_spans}</div><div class="duration">peak {parallel.peak_concurrency}</div></div>
    {''.join(lane_rows)}
  </div>
  <div class="stream-legend text-small">{stream_legend}</div>
</section>
<section class="cost-section" aria-labelledby="ledger-cost-title"><div class="section-head"><h2 id="ledger-cost-title">Cost by task</h2><span class="muted">nine most expensive shown individually · inexpensive tail grouped</span></div>
  <div class="cost-visual">
    <svg class="cost-pie" viewBox="0 0 300 300" role="img" aria-label="Recorded API cost distribution by task">{''.join(pie_shapes)}
      <circle cx="150" cy="150" r="64" fill="var(--background)"></circle>
      <text class="pie-total" x="150" y="146">${task_cost:,.0f}</text><text class="pie-caption" x="150" y="166">completed-task cost</text>
    </svg>
    <div class="cost-legend">{''.join(cost_items)}</div>
  </div>
</section>
<section class="effort-section" aria-label="Effort and review details">
  <div><div class="section-head"><h2>Where the run went</h2><span class="muted">completed task time · {format_duration(completed_elapsed, compact=True)}</span></div>
    <div class="allocation" style="--validation:{validation_percent:.3f}%" role="img" aria-label="{format_duration(other_seconds, compact=True)} task work and {format_duration(validation_seconds, compact=True)} validation"><span class="validation"></span><span class="other"></span></div>
    <div class="legend text-small"><span><i class="swatch"></i>Validation · {format_duration(validation_seconds, compact=True)}</span><span><i class="swatch other"></i>Implementation, review &amp; commits · {format_duration(other_seconds, compact=True)}</span></div>
    <div class="section-head" style="margin-top:24px"><h2>Review pressure</h2><span class="muted">recorded critique and repair</span></div>
    <div class="review"><div><strong>{duck_passes}</strong><span class="muted">duck passes</span></div><div><strong>{findings}</strong><span class="muted">findings</span></div><div><strong>{fix_cycles}</strong><span class="muted">fix cycles</span></div></div>
  </div>
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
        "parallel_lane_count": parallel.lane_count,
        "peak_concurrency": parallel.peak_concurrency,
        "parallel_seconds": parallel.parallel_seconds,
        "active_seconds": parallel.active_seconds,
        "worker_seconds": parallel.worker_seconds,
        "cost_slice_count": len(cost_groups),
        "grouped_cost_tasks": sum(count for _, _, count in cost_groups if count > 1),
        "output": None,
    }
    return fragment, summary


def build_standalone_document(fragment: str, run_id: str) -> str:
    """Supply the host theme that an exported ledger needs outside Codex."""
    shell_css = r"""
:root {
  color-scheme: dark;
  --background:#08111f;
  --foreground:#eef5ff;
  --muted:#152238;
  --muted-foreground:#95a7c2;
  --border:#263853;
  --viz-series-1:#55d6be;
  --viz-series-2:#8bd450;
  --viz-series-3:#ffca5c;
  --viz-series-4:#ff7aa2;
  --viz-series-5:#b69cff;
  --viz-series-6:#66a8ff;
}
html { background:var(--background); }
body {
  margin:0;
  padding:40px;
  background:
    radial-gradient(circle at 12% 0%,color-mix(in srgb,var(--viz-series-6) 12%,transparent),transparent 32rem),
    var(--background);
  color:var(--foreground);
  font:15px/1.45 "Segoe UI",Inter,Arial,sans-serif;
}
main { max-width:1440px; margin:0 auto; }
h1 { font-size:clamp(2rem,3vw,3.25rem); line-height:1.05; letter-spacing:-.035em; }
h2 { font-size:1.45rem; letter-spacing:-.015em; }
.text-small { font-size:.82rem; }
.text-muted { color:var(--muted-foreground); }
.card {
  min-height:112px;
  padding:18px;
  border:1px solid var(--border);
  border-radius:12px;
  background:color-mix(in srgb,var(--muted) 72%,transparent);
}
.viz-stat-value { margin:5px 0 3px; font-size:1.8rem; font-weight:650; letter-spacing:-.025em; }
@media(max-width:760px) { body { padding:22px; } }
"""
    return (
        "<!doctype html>\n<html lang=\"en\">\n<head>\n"
        "<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
        f"<title>Work Ledger · Run {esc(run_id)}</title>\n"
        f"<style>{shell_css}</style>\n</head>\n<body>\n<main>\n{fragment}\n</main>\n</body>\n</html>\n"
    )


def render(ledger: Path, output: Path, *, standalone: bool = False) -> dict[str, object]:
    goal, tasks = load_run(ledger)
    fragment, summary = build_fragment(ledger, goal, tasks)

    # Invariant: fragment mode inherits the conversation host's theme. Durable
    # repository artifacts carry the same visual through a self-contained shell.
    rendered = build_standalone_document(fragment, str(summary["run_id"])) if standalone else fragment
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(rendered, encoding="utf-8", newline="\n")
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
            "record_type": "goal", "run_id": "old-run", "goal": "MASTER-PLAN", "status": "complete",
            "started_at": "2026-08-17T22:00:00+10:00", "elapsed_seconds": "3600",
            "portfolio_completed_tasks": "1", "portfolio_total_tasks": "4", "portfolio_percent_complete": "25.0",
            "updated_at": "2026-08-19T02:00:00+10:00",
        },
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
            "status": "in_progress", "started_at": "2026-08-18T23:00:00+10:00", "elapsed_seconds": "10800",
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
        standalone_output = Path(temp_dir) / "ledger-standalone.html"
        render(ledger, standalone_output, standalone=True)
        standalone_rendered = standalone_output.read_text(encoding="utf-8")
        _, parsed_tasks = load_run(ledger)
        cost_fixture = [replace(parsed_tasks[0], task_id=f"COST_PLAN-T{index}", cost_usd=float(index + 1)) for index in range(12)]
        grouped_costs = group_task_costs(cost_fixture)
        checks = [
            summary["run_id"] == "test-run",
            summary["completed_tasks"] == 1,
            summary["current_task"] == "DEMO_PLAN-T1",
            summary["portfolio_percent_complete"] == 50.0,
            summary["parallel_lane_count"] == 2,
            summary["peak_concurrency"] == 2,
            summary["parallel_seconds"] == 3600,
            "One night. 1 task landed." in rendered,
            "The run, in parallel" in rendered,
            "Worker lane 2" in rendered,
            "cost-pie" in rendered,
            "Where the run went" in rendered,
            "cost-list" not in rendered,
            "Close &lt;unsafe&gt; task" in rendered,
            "Close <unsafe> task" not in rendered,
            "$12.50" in rendered,
            "90.0% cached" in rendered,
            "Total time" in rendered,
            "2h 00m" in rendered,
            "1h 30m tasks · 0h 30m validation" in rendered,
            standalone_rendered.startswith("<!doctype html>"),
            '<meta charset="utf-8">' in standalone_rendered,
            "--viz-series-6:#66a8ff" in standalone_rendered,
            "Close &lt;unsafe&gt; task" in standalone_rendered,
            len(grouped_costs) == 10,
            grouped_costs[-1][0] == "Other tasks",
            grouped_costs[-1][2] == 3,
            abs(sum(cost for _, cost, _ in grouped_costs) - 78.0) < 0.000001,
        ]
        if not all(checks):
            raise AssertionError("Self-test output did not preserve the expected ledger contract.")
    print("PASS: work-ledger CSV parsing, aggregation, escaping, parallel lanes, and infographic rendering.")


def main() -> int:
    args = parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.ledger is None or args.output is None:
        raise SystemExit("--ledger and --output are required unless --self-test is used.")
    summary = render(args.ledger.resolve(), args.output.resolve(), standalone=args.standalone)
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
