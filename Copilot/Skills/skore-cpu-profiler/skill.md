---
name: skore-cpu-profiler
description: >
  Add fine-grained CPU profiling markers around a suspected bottleneck, run the perf scene,
  and pinpoint the hottest line. Report timings to the user and offer to fix. After fix
  is verified present before/after timings. Optionally clean up added markers.
  Invoke when the user describes a performance concern (e.g. "why are the balls so slow
  on DX12"). Pre-authorize fixing by including "fix it" or "and fix it" in the invocation.
---

# skore-cpu-profiler

## Overview

This skill instruments a suspected performance bottleneck with fine-grained CPU profiler
markers, runs the perf scene, and identifies the exact hotspot. It then reports findings,
optionally fixes the problem, and presents a before/after timing comparison. Cleanup of
investigation markers is optional and driven by the user.

The skill maintains a **session state file** at:
```
Copilot/Skills/skore-cpu-profiler/session_markers.json
```
This file records every marker inserted during the investigation so cleanup removes **only
those markers** and never touches pre-existing ones.

---

## Phase 0 — Parse Invocation

Extract three things from the user's message:

1. **Concern** — the natural-language description (e.g. "balls render slowly")
2. **Renderer** — one of `gl`, `dx11`, `dx12`
3. **Pre-authorization** — whether the user said "fix it", "and fix", "auto-fix", or similar

**If the renderer is not mentioned, you MUST use `ask_user` before doing anything else:**

```
Use ask_user tool:
  question: "Which renderer should I profile?"
  choices: ["gl (OpenGL)", "dx11 (DirectX 11)", "dx12 (DirectX 12)"]
```

Map the choice to the short key: `gl`, `dx11`, or `dx12`.

---

## Phase 1 — Check for Stale Session

Check whether `Copilot/Skills/skore-cpu-profiler/session_markers.json` already exists.

If it exists, read it and present what was in progress, then use `ask_user`:

```
Use ask_user tool:
  question: "A previous profiling session exists for '<concern>' on <renderer>.
             What would you like to do?"
  choices: [
    "Clean up leftover markers from last session and start fresh",
    "Resume from where the last session left off",
    "Discard previous session and start fresh (leave old markers in source)"
  ]
```

- **Clean up + start fresh**: jump to Phase 12 (cleanup), then continue from Phase 2.
- **Resume**: skip to whichever phase makes sense given `session_markers.json` state.
- **Discard + start fresh**: delete `session_markers.json` and proceed from Phase 2.

---

## Phase 2 — Map Concern to Code Area

### 2a. Find the parent profiler marker

Grep the source to find existing markers relevant to the concern:

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
Select-String -Path "$REPO\SkullbonezSource\*.cpp" -Pattern 'PROFILE_(BEGIN|SCOPED)\s*\(' |
    Select-Object Filename, LineNumber, Line
```

Use the following table as a **starting point only**. The table gives the likely parent marker
and source files to read. Always verify by reading the actual source — the code is the ground truth.

| Concern keywords              | Parent marker path              | Source files to read first                                                         |
|-------------------------------|---------------------------------|------------------------------------------------------------------------------------|
| ball, sphere, model, render   | `Frame/Render/Balls`            | `SkullbonezRun.cpp` (around the BEGIN), `SkullbonezGameModelCollection.cpp`, `SkullbonezHelper.cpp` |
| reflection, mirror            | `Frame/Render/Reflection`       | `SkullbonezRun.cpp`                                                                |
| terrain, ground               | `Frame/Render/Terrain`          | `SkullbonezRun.cpp`, `SkullbonezTerrain.cpp`                                       |
| shadow                        | `Frame/Render/Shadows`          | `SkullbonezRun.cpp`, `SkullbonezGameModelCollection.cpp`                           |
| water, fluid                  | `Frame/Render/Water`            | `SkullbonezRun.cpp`, `SkullbonezWorldEnvironment.cpp`                              |
| skybox, sky                   | `Frame/Render/Skybox`           | `SkullbonezRun.cpp`, `SkullbonezSkyBox.cpp`                                        |
| text, fps, overlay            | `Frame/Text`                    | `SkullbonezRun.cpp`, `SkullbonezText.cpp`                                          |
| physics, forces, integrate    | `Frame/Physics`                 | `SkullbonezRun.cpp`, `SkullbonezGameModelCollection.cpp`                           |
| broadphase, spatial, grid     | `Frame/Physics/Broadphase`      | `SkullbonezGameModelCollection.cpp`, `SkullbonezSpatialGrid.cpp`                   |
| narrowphase, collision        | `Frame/Physics/Narrowphase`     | `SkullbonezGameModelCollection.cpp`                                                |
| integrate, velocity           | `Frame/Physics/Integrate`       | `SkullbonezGameModelCollection.cpp`, `SkullbonezRigidBody.cpp`                     |

### 2b. Read the source files

Read each relevant source file. Locate the parent marker's `PROFILE_BEGIN`/`PROFILE_SCOPED`
call and read the code that runs between it and the matching `PROFILE_END`. Trace the immediate
callees — read those functions too (they are the sub-areas worth instrumenting).

### 2c. Identify sub-marker insertion sites

Based on the actual code structure, identify 4–8 meaningful sub-markers. Good candidates:
- Each significant function call within the profiled block
- Any loop whose body cost scales with object count
- State-change calls that might be expensive (texture bind, shader bind, buffer upload)

**Sub-marker naming rule:** always extend the parent path with a `/LeafName`, e.g.:
```
Frame/Render/Balls/TextureBind
Frame/Render/Balls/UniformUpload
Frame/Render/Balls/DrawCalls
```

All sub-markers use `PROFILE_SCOPED` or `PROFILE_BEGIN`/`PROFILE_END` — CPU wall-clock timing
via `QueryPerformanceCounter`.

### 2d. Check marker budget

Count the current number of registered markers, then check headroom:

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$count = (Select-String -Path "$REPO\SkullbonezSource\*.cpp" `
    -Pattern 'PROFILE_(BEGIN|SCOPED)\s*\(' |
    Where-Object { $_.Line -notmatch '^\s*//' }).Count
Write-Host "Current unique PROFILE call sites (approx): $count"
Write-Host "MAX_MARKERS = 64. Headroom: $( 64 - $count )"
```

If adding your planned sub-markers would push the total above 58 (leaving a safety margin),
reduce the number of sub-markers or increase `MAX_MARKERS` in `SkullbonezProfiler.h` first.

---

## Phase 3 — Add Sub-Markers

Insert each sub-marker wrapped in **sentinel comment tags** using this exact format:

```cpp
// [SKORE-PROFILER-BEGIN:XXXX]
PROFILE_SCOPED( "Frame/Render/Balls/DrawCalls" );
// [SKORE-PROFILER-END:XXXX]
```

Where `XXXX` is a short unique ID for this marker (e.g. `pm001`, `pm002`, …).

**Rules for insertion:**
- Only insert inside existing braced blocks `{ }`. Never insert inside a single-statement
  `if`/`for`/`while` body that has no braces — add braces first.
- For a section wrapping multiple lines, use `PROFILE_BEGIN`/`PROFILE_END` pair (also wrapped
  in sentinel comments at each call site with the same ID).
- Ensure every `PROFILE_BEGIN` has a matching `PROFILE_END` — mismatches abort the engine
  at runtime.
- `PROFILE_SCOPED` markers must be inside a block scope so their destructor fires correctly.
- Follow existing code formatting (clang-format style, Allman braces).

**Example — wrapping a single call:**
```cpp
m_cTextures->SelectTexture( TEXTURE_BOUNDING_SPHERE );
```
becomes:
```cpp
{
    // [SKORE-PROFILER-BEGIN:pm001]
    PROFILE_SCOPED( "Frame/Render/Balls/TextureBind" );
    // [SKORE-PROFILER-END:pm001]
    m_cTextures->SelectTexture( TEXTURE_BOUNDING_SPHERE );
}
```

**Example — wrapping a function call:**
```cpp
m_cGameModelCollection.RenderModels( baseView, proj, lightPosition );
```
becomes:
```cpp
{
    // [SKORE-PROFILER-BEGIN:pm002]
    PROFILE_SCOPED( "Frame/Render/Balls/DrawCalls" );
    // [SKORE-PROFILER-END:pm002]
    m_cGameModelCollection.RenderModels( baseView, proj, lightPosition );
}
```

---

## Phase 4 — Write session_markers.json

After adding all sub-markers, write the session state file. This file is the **authoritative
record** of what was added — cleanup reads nothing else.

```pwsh
$REPO   = (git rev-parse --show-toplevel).Trim()
$commit = (git rev-parse --short HEAD).Trim()
$ts     = (Get-Date -Format "yyyyMMdd-HHmmss")
$outPath = "$REPO\Copilot\Skills\skore-cpu-profiler\session_markers.json"

# Build the JSON manually or use ConvertTo-Json with a hashtable.
# IMPORTANT: populate added_markers with one entry per sentinel ID, including:
#   id, file (relative path from repo root), marker (full path string),
#   macro (PROFILE_SCOPED / PROFILE_BEGIN+END),
#   inserted_lines (exact text inserted between the sentinels, for reference).

$session = @{
    session_id      = "profiler-$ts-$commit"
    concern         = "<user concern>"
    renderer        = "<gl|dx11|dx12>"
    area_path       = "<parent marker path>"
    commit_at_start = $commit
    added_markers   = @(
        @{
            id             = "pm001"
            file           = "SkullbonezSource/SkullbonezRun.cpp"
            marker         = "Frame/Render/Balls/TextureBind"
            macro          = "PROFILE_SCOPED"
            inserted_lines = 'PROFILE_SCOPED( "Frame/Render/Balls/TextureBind" );'
        }
        # ... one entry per sentinel ID
    )
    profiling_csv   = "Profile/perf_profiling_${renderer}_$ts.csv"
    after_fix_csv   = $null
    before_stats    = $null
    after_stats     = $null
    fixed           = $false
    pre_authorized  = $false
}

$session | ConvertTo-Json -Depth 10 | Set-Content $outPath -Encoding UTF8
Write-Host "Written: $outPath"
```

---

## Phase 5 — Build Profile Configuration

```pwsh
$REPO    = (git rev-parse --show-toplevel).Trim()
$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -requires Microsoft.Component.MSBuild `
    -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1

& $msbuild "$REPO\SKULLBONEZ_CORE.sln" /p:Configuration=Profile /p:Platform=x64 /nologo /v:minimal
```

**If build fails with LNK1168** (exe locked): kill the running process first:
```pwsh
Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue | Stop-Process
```

Build must produce 0 errors and 0 warnings before proceeding.

---

## Phase 6 — Run "Before" (Profiling) Run

This run collects the **before** baseline. The new markers are present but no fix has been
applied yet.

```pwsh
$REPO     = (git rev-parse --show-toplevel).Trim()
$renderer = "<gl|dx11|dx12>"
$ts       = "<same timestamp from Phase 4>"

Remove-Item "$REPO\Profile\perf_log.csv" -ErrorAction SilentlyContinue

$rendererArgs = if ($renderer -eq "gl") {
    "--scene SkullbonezData/scenes/perf_test.scene"
} else {
    "--renderer $renderer --scene SkullbonezData/scenes/perf_test.scene"
}

$proc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
    -ArgumentList $rendererArgs `
    -WorkingDirectory $REPO -PassThru `
    -RedirectStandardOutput "$REPO\Profile\profiler_stdout.txt" `
    -RedirectStandardError  "$REPO\Profile\profiler_stderr.txt"

if (-not $proc.WaitForExit(120000)) {
    $proc.Kill()
    Write-Host "FAIL: Perf scene timed out"
    exit 1
}
if ($proc.ExitCode -ne 0) {
    Write-Host "FAIL: Exit code $($proc.ExitCode)"
    Get-Content "$REPO\Profile\profiler_stderr.txt"
    exit 1
}

# Rename to session-specific path so it isn't overwritten by subsequent runs
$beforeCsv = "$REPO\Profile\perf_profiling_${renderer}_${ts}.csv"
Copy-Item "$REPO\Profile\perf_log.csv" $beforeCsv
Write-Host "Before CSV: $beforeCsv"
```

### 6a. Validate new markers appear in CSV

```pwsh
$csv     = Get-Content $beforeCsv
$header  = ($csv | Where-Object { $_ -match '^pass,frame,' })[0]
$columns = $header -split ','

# Check each expected marker name — populate from session_markers.json
$expectedMarkers = @( "Frame/Render/Balls/TextureBind", "Frame/Render/Balls/DrawCalls" )
$missing = $expectedMarkers | Where-Object { $_ -notin $columns }

if ($missing) {
    Write-Host "FAIL: These markers never executed and are absent from CSV:"
    $missing | ForEach-Object { Write-Host "  $_" }
    Write-Host "Check that the instrumented code path is actually reached by perf_test.scene."
    exit 1
}
Write-Host "PASS: All $($expectedMarkers.Count) new markers present in CSV"
```

### 6b. Extract before-stats and update session_markers.json

```pwsh
$REPO    = (git rev-parse --show-toplevel).Trim()
$sjPath  = "$REPO\Copilot\Skills\skore-cpu-profiler\session_markers.json"

$env:SKORE_CSV   = $beforeCsv
$env:SKORE_AREA  = "<area_path>"   # e.g. Frame/Render/Balls
$env:SKORE_SJ    = $sjPath

py -c "
import os, json
from pathlib import Path

csv_path  = os.environ['SKORE_CSV']
area_path = os.environ['SKORE_AREA']
sj_path   = Path(os.environ['SKORE_SJ'])

rows = []
fieldnames = []
with open(csv_path, newline='') as f:
    for line in f:
        line = line.strip()
        if line.startswith('pass,frame,'):
            fieldnames = line.split(',')
        elif line and not line.startswith('#') and fieldnames:
            parts = line.split(',')
            row = {}
            for i, name in enumerate(fieldnames):
                row[name] = float(parts[i]) if i >= 2 and i < len(parts) else parts[i]
            rows.append(row)

# Only use second-pass rows (after warmup)
rows = [r for r in rows if int(r.get('pass', 0)) == 2]

def pct(s, p):
    k = (len(s)-1)*(p/100.0); f=int(k); c=min(f+1,len(s)-1)
    return s[f]+(k-f)*(s[c]-s[f])

# Compute stats for every column under area_path
stats = {}
for col in fieldnames[2:]:
    if col == area_path or col.startswith(area_path + '/'):
        vals = sorted([r[col] for r in rows if col in r])
        if vals:
            stats[col] = {
                'avg':   round(sum(vals)/len(vals), 4),
                'p50':   round(pct(vals, 50), 4),
                'p99':   round(pct(vals, 99), 4),
                'count': len(vals)
            }

# Print ranked table
parent_avg = stats.get(area_path, {}).get('avg', 0.0)
children   = {k:v for k,v in stats.items() if k.startswith(area_path+'/') and k.count('/')==area_path.count('/')+1}
ranked     = sorted(children.items(), key=lambda x: x[1]['avg'], reverse=True)

print(f'Parent: {area_path}  avg={parent_avg:.4f}ms')
print()
print(f'  {\"Sub-marker\":<50} {\"avg ms\":>8}  {\"p99 ms\":>8}  {\"% of parent\":>12}')
print(f'  {\"-\"*50} {\"-\"*8}  {\"-\"*8}  {\"-\"*12}')
for name, s in ranked:
    share = (s[\"avg\"]/parent_avg*100) if parent_avg > 0 else 0
    print(f'  {name:<50} {s[\"avg\"]:>8.4f}  {s[\"p99\"]:>8.4f}  {share:>11.1f}%')

if ranked:
    hottest = ranked[0]
    share   = (hottest[1][\"avg\"]/parent_avg*100) if parent_avg > 0 else 0
    print()
    print(f'HOTSPOT: {hottest[0]}')
    print(f'         avg={hottest[1][\"avg\"]:.4f}ms  p99={hottest[1][\"p99\"]:.4f}ms  ({share:.1f}% of parent)')

# Update session JSON
if sj_path.exists():
    sj = json.loads(sj_path.read_text())
    sj['before_stats']  = stats
    sj['profiling_csv'] = csv_path
    sj_path.write_text(json.dumps(sj, indent=2))
    print()
    print('Updated session_markers.json with before_stats')
"
```

---

## Phase 7 — Report Finding

After the analysis, report to the user in plain language:

```
Profiling complete (CPU wall-clock). Sub-marker timings inside <area_path> on <RENDERER>:

  Sub-marker                                  avg ms   p99 ms   % of parent
  ─────────────────────────────────────────────────────────────────────────
  Frame/Render/Balls/DrawCalls                 1.2031   2.1140      68.2%
  Frame/Render/Balls/UniformUpload             0.4107   0.9283      23.3%
  Frame/Render/Balls/TextureBind               0.1522   0.3010       8.6%
  ─────────────────────────────────────────────────────────────────────────
  Frame/Render/Balls (parent)                  1.7642   3.0920     100.0%

Hotspot: Frame/Render/Balls/DrawCalls — 68.2% of ball render time (avg 1.20ms)
```

Then cite the specific source location (file + line number range) that corresponds to the
hottest marker.

---

## Phase 8 — Fix

### If pre-authorized (user said "fix it" on invocation):

Proceed directly to implement the fix. Do not ask.

### If not pre-authorized:

```
Use ask_user tool:
  question: "The hotspot is <marker> (<X>ms avg, <Y>% of parent). Would you like me to investigate and apply a fix?"
  choices: [
    "Yes, fix it",
    "No, just report — I'll fix it myself"
  ]
```

If "No": skip to Phase 9 (which runs without a fix) then Phase 10 (shows flat before/after).
If "Yes": implement the fix.

### Implementing the fix

Read the code at the hotspot carefully. Common patterns and their fixes in this engine:

| Pattern | Fix |
|---------|-----|
| Per-ball texture rebind to the same texture every iteration | Hoist `SelectTexture` before the loop |
| Per-ball uniform upload for static data | Hoist to init-time or upload once per frame before the loop |
| Redundant per-frame buffer uploads of never-changing data | Hoist to shader init-time (already done for constant uniforms — check for missed cases) |
| Unnecessary state toggle inside tight loop | Cache state, only set on change |
| Per-ball individual draw calls (no batching) | Convert to instanced draw or geometry batching |
| O(n²) loop body | Reduce algorithmic complexity |

If the fix is non-obvious (novel algorithmic issue), describe what you found and reason about
the best approach before implementing.

---

## Phase 9 — Build + Run "After" Profiling Run

After the fix is implemented (or skipped):

**9a. Build:**
```pwsh
$REPO    = (git rev-parse --show-toplevel).Trim()
$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -requires Microsoft.Component.MSBuild `
    -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild "$REPO\SKULLBONEZ_CORE.sln" /p:Configuration=Profile /p:Platform=x64 /nologo /v:minimal
```

**9b. Run perf scene again** (same renderer, same markers still in place for apples-to-apples):
```pwsh
$REPO     = (git rev-parse --show-toplevel).Trim()
$ts_after = (Get-Date -Format "yyyyMMdd-HHmmss")

Remove-Item "$REPO\Profile\perf_log.csv" -ErrorAction SilentlyContinue

$rendererArgs = if ($renderer -eq "gl") {
    "--scene SkullbonezData/scenes/perf_test.scene"
} else {
    "--renderer $renderer --scene SkullbonezData/scenes/perf_test.scene"
}

$proc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
    -ArgumentList $rendererArgs `
    -WorkingDirectory $REPO -PassThru `
    -RedirectStandardOutput "$REPO\Profile\profiler_after_stdout.txt" `
    -RedirectStandardError  "$REPO\Profile\profiler_after_stderr.txt"

if (-not $proc.WaitForExit(120000)) { $proc.Kill(); Write-Host "FAIL: timed out"; exit 1 }
if ($proc.ExitCode -ne 0)           { Write-Host "FAIL: exit $($proc.ExitCode)";  exit 1 }

$afterCsv = "$REPO\Profile\perf_profiling_after_${renderer}_${ts_after}.csv"
Copy-Item "$REPO\Profile\perf_log.csv" $afterCsv
Write-Host "After CSV: $afterCsv"
```

**9c. Extract after-stats** using the same Python snippet as Phase 6b, but reading `$afterCsv`
and writing results to `after_stats` in `session_markers.json`. Also set `"fixed": true` and
`"after_fix_csv": "<afterCsv path>"` in the JSON.

---

## Phase 10 — Present Before/After Comparison

Read `before_stats` and `after_stats` from `session_markers.json` and print the full comparison:

```pwsh
$REPO    = (git rev-parse --show-toplevel).Trim()
$env:SKORE_SJ = "$REPO\Copilot\Skills\skore-cpu-profiler\session_markers.json"

py -c "
import os, json
from pathlib import Path

sj     = json.loads(Path(os.environ['SKORE_SJ']).read_text())
before = sj.get('before_stats', {})
after  = sj.get('after_stats',  {})
area   = sj['area_path']

all_markers  = sorted(set(list(before.keys()) + list(after.keys())))
par_b_avg    = before.get(area, {}).get('avg', 0.0)
par_a_avg    = after.get(area,  {}).get('avg', 0.0)

def delta(b, a):
    if b == 0: return '   n/a'
    pct = (a - b) / b * 100
    sign = '+' if pct > 0 else ''
    return f'{sign}{pct:.1f}%'

def share(avg, parent):
    if parent == 0: return '  n/a'
    return f'{avg/parent*100:.1f}%'

W = 52
print()
print(f'CPU Timing Before/After: {area}')
print(f'Renderer: {sj[\"renderer\"].upper()}')
print()
hdr = f'  {\"Marker\":<{W}}  {\"B avg\":>7}  {\"A avg\":>7}  {\"Δavg\":>7}  {\"B p99\":>7}  {\"A p99\":>7}  {\"Δp99\":>7}  {\"B%par\":>6}  {\"A%par\":>6}'
print(hdr)
print(f'  {\"-\"*W}  {\"-\"*7}  {\"-\"*7}  {\"-\"*7}  {\"-\"*7}  {\"-\"*7}  {\"-\"*7}  {\"-\"*6}  {\"-\"*6}')

for m in all_markers:
    b_avg = before.get(m, {}).get('avg', 0.0)
    a_avg = after.get(m,  {}).get('avg', 0.0)
    b_p99 = before.get(m, {}).get('p99', 0.0)
    a_p99 = after.get(m,  {}).get('p99', 0.0)
    d_avg = delta(b_avg, a_avg)
    d_p99 = delta(b_p99, a_p99)
    pp_b  = share(b_avg, par_b_avg) if m != area else '100.0%'
    pp_a  = share(a_avg, par_a_avg) if m != area else '100.0%'
    label = m if len(m) <= W else '…' + m[-(W-1):]
    print(f'  {label:<{W}}  {b_avg:>7.4f}  {a_avg:>7.4f}  {d_avg:>7}  {b_p99:>7.4f}  {a_p99:>7.4f}  {d_p99:>7}  {pp_b:>6}  {pp_a:>6}')

print()
if par_b_avg > 0:
    d = (par_a_avg - par_b_avg) / par_b_avg * 100
    sign = '+' if d > 0 else ''
    print(f'Overall {area}: {par_b_avg:.4f}ms -> {par_a_avg:.4f}ms  ({sign}{d:.1f}%)')
"
```

Columns: **B avg** = before avg ms, **A avg** = after avg ms, **Δavg** = % change in avg,
**B p99** / **A p99** = tail latency before/after, **Δp99** = % change in p99,
**B%par** / **A%par** = share of parent marker time before/after.

---

## Phase 11 — Ask About Cleanup

After presenting the before/after table, ask the user:

```
Use ask_user tool:
  question: "Would you like me to clean up the profiling markers I added?
             (They add a small overhead and are not needed for production.)"
  choices: [
    "Yes, remove the profiling markers",
    "No, keep them — they're useful to monitor"
  ]
```

If **"No, keep them"**: leave the markers in place, delete `session_markers.json`, and note
that the markers are now permanent (tracked by the profiler overlay going forward).

If **"Yes"**: proceed to Phase 12.

---

## Phase 12 — Cleanup (Remove Added Markers)

Read `session_markers.json` to get the list of markers to remove. For each unique source file
mentioned, remove **only** the sentinel-wrapped blocks by ID.

```pwsh
$REPO    = (git rev-parse --show-toplevel).Trim()
$sjPath  = "$REPO\Copilot\Skills\skore-cpu-profiler\session_markers.json"
$session = Get-Content $sjPath | ConvertFrom-Json

# Collect distinct files
$files = $session.added_markers | Select-Object -ExpandProperty file -Unique

foreach ($relFile in $files) {
    $fullPath = "$REPO\$($relFile -replace '/', '\')"
    if (-not (Test-Path $fullPath)) {
        Write-Host "WARNING: $relFile not found — skipping"
        continue
    }

    $ids     = ($session.added_markers | Where-Object { $_.file -eq $relFile }).id
    $content = Get-Content $fullPath -Raw

    foreach ($id in $ids) {
        # Remove the sentinel block including surrounding blank lines introduced during insertion.
        # The pattern matches from the BEGIN sentinel line through the END sentinel line (inclusive).
        $pattern = "[ \t]*// \[SKORE-PROFILER-BEGIN:$id\][^\r\n]*[\r\n]+(?:.*?[\r\n]+)*?[ \t]*// \[SKORE-PROFILER-END:$id\][^\r\n]*[\r\n]?"
        $before  = $content
        $content = [regex]::Replace(
            $content, $pattern, '',
            [System.Text.RegularExpressions.RegexOptions]::Singleline
        )
        if ($content -eq $before) {
            Write-Host "WARNING: Sentinel $id not found in $relFile (already removed?)"
        } else {
            Write-Host "  Removed sentinel $id from $relFile"
        }
    }

    Set-Content $fullPath $content -NoNewline -Encoding UTF8
    Write-Host "  Updated: $relFile"
}
```

### 12a. Verify cleanup compiles clean

```pwsh
$REPO    = (git rev-parse --show-toplevel).Trim()
$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -requires Microsoft.Component.MSBuild `
    -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild "$REPO\SKULLBONEZ_CORE.sln" /p:Configuration=Profile /p:Platform=x64 /nologo /v:minimal
```

If the build fails: the sentinel removal corrupted something. Inspect the file, fix manually,
then rebuild.

### 12b. Delete session state file

```pwsh
$sjPath = "$REPO\Copilot\Skills\skore-cpu-profiler\session_markers.json"
Remove-Item $sjPath -Force
Write-Host "Session state cleared: session_markers.json deleted"
```

### 12c. Reformat edited files

After editing source files, always reformat to keep the codebase clean:

```pwsh
$REPO     = (git rev-parse --show-toplevel).Trim()
$clangfmt = "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe"
$files    = @(Get-ChildItem "$REPO\SkullbonezSource\*.cpp") + @(Get-ChildItem "$REPO\SkullbonezSource\*.h")
foreach ($f in $files) { & $clangfmt -i $f.FullName }
Write-Host "Reformatted $($files.Count) files"
```

---

## Phase 13 — Handoff

After cleanup (or if the user chose to keep markers), remind the user:

- If a fix was applied and they want it committed: invoke `skore-build-pipeline` to run the
  full pipeline (build, visual regression, perf regression, commit).
- If no fix was applied: the source is back to its original state (with or without markers).
- The before/after profiling CSVs are left in `Profile/` as reference and are gitignored.
