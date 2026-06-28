@rem
@rem File: tools/loc_count.bat
@rem Purpose:
@rem   Reports source logical lines of code, then summarizes tracked file counts
@rem   and physical line counts by common SkullbonezCore file category.
@rem
@rem Mental model:
@rem   The Python helper owns the logical LOC report for first-party engine code
@rem   and shaders. The footer uses git-tracked files so generated, ignored, and
@rem   build output directories do not inflate the file inventory.
@rem
@rem Glossary:
@rem   LOC (Lines Of Code): Logical non-comment, non-blank source lines reported
@rem   by Agentic/Skills/loc_count.py.
@rem   Physical line: Every newline-delimited line in a tracked text file.
@rem   Code LOC: Footer-only logical LOC for files that use C/C++-style comments.
@rem
@rem Invariants:
@rem   - Tool output should be bounded and readable because agents and humans use
@rem   it for decisions.
@rem   - The summary counts tracked files only; untracked scratch files are not
@rem   part of the repository inventory.
@rem   - Physical line counts are repository inventory numbers; Code LOC is only
@rem   shown for C/C++/HLSL-style files.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - Agentic/Reference/comment-style-guide.md
@rem   - Agentic/Skills/loc_count.py
@rem
@rem
@echo off
setlocal
REM ===============================================================
REM  loc_count.bat - Count first-party source logical lines of code.
REM ===============================================================

set "REPO=%~dp0.."

call "%~dp0find_python.bat"
if errorlevel 1 exit /b %errorlevel%

"%PYTHON_EXE%" "%REPO%\Agentic\Skills\loc_count.py"
if errorlevel 1 exit /b %errorlevel%

call "%~dp0find_git.bat"
if errorlevel 1 (
    echo.
    echo WARNING: Git was not found, so the tracked file summary was skipped.
    exit /b 0
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "$bat = Get-Content -Raw -LiteralPath '%~f0'; $ps = $bat -replace '(?s)^.*?# FILE SUMMARY POWERSHELL\r?\n', ''; Invoke-Expression $ps"
exit /b %errorlevel%

# FILE SUMMARY POWERSHELL
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path -LiteralPath $env:REPO).Path
$trackedFiles = & git -C $repo ls-files

$categories = @(
    [pscustomobject]@{ Section = 'Engine';      Name = 'Engine .cpp';       CodeLoc = $true;  Match = { param($p) $p.StartsWith('SkullbonezSource/', [System.StringComparison]::OrdinalIgnoreCase) -and $p.EndsWith('.cpp', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Engine';      Name = 'Engine .h';         CodeLoc = $true;  Match = { param($p) $p.StartsWith('SkullbonezSource/', [System.StringComparison]::OrdinalIgnoreCase) -and $p.EndsWith('.h', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Engine';      Name = 'Engine .inl';       CodeLoc = $true;  Match = { param($p) $p.StartsWith('SkullbonezSource/', [System.StringComparison]::OrdinalIgnoreCase) -and $p.EndsWith('.inl', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Engine';      Name = 'Shaders .hlsl';     CodeLoc = $true;  Match = { param($p) $p.EndsWith('.hlsl', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Assets';      Name = 'Scenes';            CodeLoc = $false; Match = { param($p) $p.EndsWith('.scene.json', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Assets';      Name = 'Scene suites';      CodeLoc = $false; Match = { param($p) $p.EndsWith('.suite.json', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Assets';      Name = 'Asset recipes';     CodeLoc = $false; Match = { param($p) $p.EndsWith('.assets.json', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Assets';      Name = 'Hull assets';       CodeLoc = $false; Match = { param($p) $p.EndsWith('.hull', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Tools';       Name = 'Agent tests .cpp';  CodeLoc = $true;  Match = { param($p) $p.StartsWith('Agentic/Tests/', [System.StringComparison]::OrdinalIgnoreCase) -and $p.EndsWith('.cpp', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Third-party'; Name = 'Third-party .cpp';  CodeLoc = $true;  Match = { param($p) $p.StartsWith('ThirdPtySource/', [System.StringComparison]::OrdinalIgnoreCase) -and $p.EndsWith('.cpp', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Third-party'; Name = 'Third-party .h';    CodeLoc = $true;  Match = { param($p) $p.StartsWith('ThirdPtySource/', [System.StringComparison]::OrdinalIgnoreCase) -and $p.EndsWith('.h', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Third-party'; Name = 'Third-party .hpp';  CodeLoc = $true;  Match = { param($p) $p.StartsWith('ThirdPtySource/', [System.StringComparison]::OrdinalIgnoreCase) -and $p.EndsWith('.hpp', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Tools';       Name = 'Python .py';        CodeLoc = $false; Match = { param($p) $p.EndsWith('.py', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Tools';       Name = 'Batch .bat';        CodeLoc = $false; Match = { param($p) $p.EndsWith('.bat', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Tools';       Name = 'PowerShell .ps1';   CodeLoc = $false; Match = { param($p) $p.EndsWith('.ps1', [System.StringComparison]::OrdinalIgnoreCase) } },
    [pscustomobject]@{ Section = 'Docs';        Name = 'Markdown .md';      CodeLoc = $false; Match = { param($p) $p.EndsWith('.md', [System.StringComparison]::OrdinalIgnoreCase) } }
)

function Count-PhysicalLines {
    param([string]$Path)

    try {
        $stream = [System.IO.File]::Open(
            $Path,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite)
        $reader = [System.IO.StreamReader]::new($stream, $true)
        $count = 0
        while ($null -ne $reader.ReadLine()) {
            ++$count
        }
        return $count
    } finally {
        if ($null -ne $reader) {
            $reader.Dispose()
        } elseif ($null -ne $stream) {
            $stream.Dispose()
        }
    }
}

function Count-CodeLoc {
    param([string]$Path)

    $stream = $null
    $reader = $null
    $loc = 0
    $inBlock = $false

    try {
        $stream = [System.IO.File]::Open(
            $Path,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite)
        $reader = [System.IO.StreamReader]::new($stream, $true)

        while ($null -ne ($raw = $reader.ReadLine())) {
            $line = $raw.Trim()

            if (-not $line) {
                continue
            }

            if ($inBlock) {
                $closeIndex = $line.IndexOf('*/', [System.StringComparison]::Ordinal)
                if ($closeIndex -ge 0) {
                    $inBlock = $false
                    $remainder = $line.Substring($closeIndex + 2).Trim()
                    if ($remainder -and -not $remainder.StartsWith('//', [System.StringComparison]::Ordinal)) {
                        ++$loc
                    }
                }
                continue
            }

            $openIndex = $line.IndexOf('/*', [System.StringComparison]::Ordinal)
            if ($openIndex -ge 0) {
                $before = $line.Substring(0, $openIndex).Trim()
                $afterOpen = $line.Substring($openIndex + 2)
                $closeIndex = $afterOpen.IndexOf('*/', [System.StringComparison]::Ordinal)
                if ($closeIndex -ge 0) {
                    $outer = ($before + ' ' + $afterOpen.Substring($closeIndex + 2)).Trim()
                    if ($outer -and -not $outer.StartsWith('//', [System.StringComparison]::Ordinal)) {
                        ++$loc
                    }
                } else {
                    $inBlock = $true
                    if ($before -and -not $before.StartsWith('//', [System.StringComparison]::Ordinal)) {
                        ++$loc
                    }
                }
                continue
            }

            if ($line.StartsWith('//', [System.StringComparison]::Ordinal)) {
                continue
            }

            ++$loc
        }
    } finally {
        if ($null -ne $reader) {
            $reader.Dispose()
        } elseif ($null -ne $stream) {
            $stream.Dispose()
        }
    }

    return $loc
}

$rows = foreach ($category in $categories) {
    $files = @($trackedFiles | Where-Object { & $category.Match $_ })
    if ($files.Count -eq 0) {
        continue
    }

    $physicalLines = 0
    $codeLoc = 0

    foreach ($relativePath in $files) {
        $fullPath = Join-Path $repo $relativePath
        if (Test-Path -LiteralPath $fullPath) {
            $physicalLines += Count-PhysicalLines -Path $fullPath
            if ($category.CodeLoc) {
                $codeLoc += Count-CodeLoc -Path $fullPath
            }
        }
    }

    [pscustomobject]@{
        Section = $category.Section
        Category = $category.Name
        Files = $files.Count
        Physical = $physicalLines
        CodeLoc = if ($category.CodeLoc) { $codeLoc } else { $null }
    }
}

$totalFiles = ($rows | Measure-Object Files -Sum).Sum
$totalPhysical = ($rows | Measure-Object Physical -Sum).Sum
$totalCodeLoc = ($rows | Where-Object { $null -ne $_.CodeLoc } | Measure-Object CodeLoc -Sum).Sum

Write-Host ''
Write-Host 'Tracked File Summary'
$sectionOrder = @('Engine', 'Assets', 'Third-party', 'Tools', 'Docs')
$sectionSeparator = '=' * 25

foreach ($section in $sectionOrder) {
    $sectionRows = @($rows | Where-Object { $_.Section -eq $section })
    if ($sectionRows.Count -eq 0) {
        continue
    }

    $sectionFiles = ($sectionRows | Measure-Object Files -Sum).Sum
    $sectionPhysical = ($sectionRows | Measure-Object Physical -Sum).Sum
    $sectionCodeLoc = ($sectionRows | Where-Object { $null -ne $_.CodeLoc } | Measure-Object CodeLoc -Sum).Sum
    $sectionLocText = if ($null -ne $sectionCodeLoc -and $sectionCodeLoc -gt 0) { $sectionCodeLoc.ToString('N0') } else { '-' }

    Write-Host ''
    Write-Host $sectionSeparator
    Write-Host $section.ToUpperInvariant()
    Write-Host $sectionSeparator
    "{0,-18} {1,8} {2,12} {3,12}" -f 'Category', 'Files', 'Physical', 'Code LOC'
    "{0,-18} {1,8} {2,12} {3,12}" -f '--------', '-----', '--------', '--------'
    foreach ($row in $sectionRows) {
        $locText = if ($null -ne $row.CodeLoc) { $row.CodeLoc.ToString('N0') } else { '-' }
        "{0,-18} {1,8:N0} {2,12:N0} {3,12}" -f $row.Category, $row.Files, $row.Physical, $locText
    }
    "{0,-18} {1,8} {2,12} {3,12}" -f '--------', '-----', '--------', '--------'
    "{0,-18} {1,8:N0} {2,12:N0} {3,12}" -f 'SUBTOTAL', $sectionFiles, $sectionPhysical, $sectionLocText
}

Write-Host ''
"{0,-18} {1,8} {2,12} {3,12}" -f '--------', '-----', '--------', '--------'
"{0,-18} {1,8:N0} {2,12:N0} {3,12:N0}" -f 'TOTAL', $totalFiles, $totalPhysical, $totalCodeLoc
