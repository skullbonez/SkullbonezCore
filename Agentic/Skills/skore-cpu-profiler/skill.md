---
name: skore-cpu-profiler
description: Add targeted CPU profiler markers, run perf scenes, analyze marker timings, and optionally remove temporary markers.
---

# skore-cpu-profiler

Use when the user asks why a subsystem is slow or asks to profile CPU cost. Marker placement requires reading the code; do not try to automate insertion blindly.

## Workflow

1. Identify the renderer: `gl`, `dx11`, or `dx12`.
2. Find the parent profiler marker with `rg "PROFILE_(BEGIN|SCOPED)" SkullbonezSource`.
3. Read the code under that parent marker and its immediate callees.
4. Insert 4-8 meaningful temporary sub-markers using sentinel comments.
5. Run a before perf sample.
6. Analyze with `analyze_markers.py`.
7. Apply the fix if authorized.
8. Run an after perf sample and compare.
9. Ask whether to remove temporary markers.

## Sentinel Format

```cpp
// [SKORE-PROFILER-BEGIN:pm001]
PROFILE_SCOPED( "Frame/Render/Balls/DrawCalls" );
// [SKORE-PROFILER-END:pm001]
```

Use unique IDs (`pm001`, `pm002`, etc.). Record them in:

```text
Agentic/Skills/skore-cpu-profiler/session_markers.json
```

Minimal session shape:

```json
{
  "concern": "balls render slowly",
  "renderer": "dx12",
  "area_path": "Frame/Render/Balls",
  "added_markers": [
    {
      "id": "pm001",
      "file": "SkullbonezSource/SkullbonezRun.cpp",
      "marker": "Frame/Render/Balls/DrawCalls",
      "macro": "PROFILE_SCOPED"
    }
  ]
}
```

## Run Perf Sample

```powershell
$renderer = "dx12"
$args = if ($renderer -eq "gl") {
    "--vsync off --fixed-step --scene SkullbonezData/scenes/perf_test.scene"
} else {
    "--renderer $renderer --vsync off --fixed-step --scene SkullbonezData/scenes/perf_test.scene"
}
$proc = Start-Process "Profile\SKULLBONEZ_CORE.exe" -ArgumentList $args -WorkingDirectory . -PassThru
$proc.WaitForExit(120000) | Out-Null
```

Move `Profile\perf_log.csv` to a before/after name before running the next sample.

## Analyze

```bat
py Agentic\Skills\skore-cpu-profiler\analyze_markers.py --csv Profile\perf_before_dx12.csv --area Frame/Render/Balls --session Agentic\Skills\skore-cpu-profiler\session_markers.json --slot before
py Agentic\Skills\skore-cpu-profiler\analyze_markers.py --csv Profile\perf_after_dx12.csv  --area Frame/Render/Balls --session Agentic\Skills\skore-cpu-profiler\session_markers.json --slot after
```

Report the hottest sub-marker, its source location, average time, p99, and share of the parent marker.

## Cleanup

Only remove markers that were recorded in the session file:

```bat
py Agentic\Skills\skore-cpu-profiler\cleanup_markers.py --session Agentic\Skills\skore-cpu-profiler\session_markers.json --delete-session
tools\validate_build.bat Profile
```

If markers are intentionally kept, delete `session_markers.json` and state that the markers are now permanent profiler coverage.
