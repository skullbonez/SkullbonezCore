<#
File: tools/style_harness.ps1
Purpose:
  Creates and controls a live style-authoring harness for renderer look tests.

Mental model:
  The harness writes small control files under Agentic/style-harness, launches
  the DX12 runtime against them, and captures requested screenshots/status.

Glossary:
  Live style control: Runtime mode that watches a style descriptor and applies
  visual look changes without editing committed scene files.
  Harness root: Scratch directory that holds control files, status text, and
  screenshots for one authoring session.

Invariants:
  - Harness paths resolve inside the repository unless the caller gives an
  explicit absolute path.
  - Generated files are scratch artifacts, not source baselines.

Related:
  - Agentic/Reference/runtime-reference.md
#>
param(
    [ValidateSet("init", "launch", "set", "shot", "setshot", "status", "show")]
    [string]$Command = "status",
    [string]$Root = "Agentic\style-harness",
    [string]$Style = "low_poly_art_style",
    [string]$Scene = "SkullbonezData\scenes\concept_12_low_poly_art_style.scene.json",
    [ValidateSet("dx12")]
    [string]$Renderer = "dx12",
    [string]$Key = "",
    [string]$Value = "",
    [string]$Name = "",
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Resolve-RepoPath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

$HarnessRoot = Resolve-RepoPath $Root
$LiveStylePath = Join-Path $HarnessRoot "live.style.json"
$CapturePath = Join-Path $HarnessRoot "capture.txt"
$StatusPath = Join-Path $HarnessRoot "status.txt"
$ShotRoot = Join-Path $HarnessRoot "shots"

function Ensure-Harness {
    New-Item -ItemType Directory -Force -Path $HarnessRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $ShotRoot | Out-Null

    if ($Force -or -not (Test-Path $LiveStylePath)) {
        $root = [ordered]@{
            format = "skullbonez.style.json"
            version = 1
            includes = @($Style)
            cinematic = [ordered]@{}
            objectMaterials = @()
        }
        $root | ConvertTo-Json -Depth 8 | Set-Content -Path $LiveStylePath -Encoding ASCII
    }

    if (-not (Test-Path $CapturePath)) {
        "" | Set-Content -Path $CapturePath -Encoding ASCII
    }
}

function Get-JsonMember {
    param(
        [object]$Object,
        [string]$Name
    )
    return $Object.PSObject.Properties[$Name]
}

function Set-JsonMember {
    param(
        [object]$Object,
        [string]$Name,
        [object]$Value
    )
    $member = Get-JsonMember -Object $Object -Name $Name
    if ($null -eq $member) {
        $Object | Add-Member -MemberType NoteProperty -Name $Name -Value $Value
    } else {
        $member.Value = $Value
    }
}

function Ensure-JsonObjectMember {
    param(
        [object]$Object,
        [string]$Name
    )
    $member = Get-JsonMember -Object $Object -Name $Name
    if ($null -eq $member -or $null -eq $member.Value) {
        $child = [pscustomobject]@{}
        Set-JsonMember -Object $Object -Name $Name -Value $child
        return $child
    }
    return $member.Value
}

function Convert-ScalarStyleValue {
    param([string]$RawValue)

    $trimmed = $RawValue.Trim()
    if ($trimmed -match '^(on|open|all|true|yes)$') {
        return $true
    }
    if ($trimmed -match '^(off|closed|none|false|no)$') {
        return $false
    }
    if ($trimmed -match '^-?\d+$') {
        return [int]$trimmed
    }
    if ($trimmed -match '^-?(?:\d+\.\d*|\d*\.\d+)(?:[eE][+-]?\d+)?$' -or $trimmed -match '^-?\d+[eE][+-]?\d+$') {
        return [double]::Parse($trimmed, [System.Globalization.CultureInfo]::InvariantCulture)
    }
    return $trimmed
}

function Convert-StyleValue {
    param([string]$RawValue)

    if ([string]::IsNullOrWhiteSpace($RawValue)) {
        return $true
    }

    $trimmed = $RawValue.Trim()
    if ($trimmed.StartsWith("[") -or $trimmed.StartsWith("{") -or $trimmed.StartsWith('"')) {
        try {
            return $trimmed | ConvertFrom-Json
        } catch {
            throw "Value is not valid JSON: $RawValue"
        }
    }

    $parts = @($trimmed -split '[,\s]+' | Where-Object { $_ -ne "" })
    if ($parts.Count -gt 1) {
        $values = @($parts | ForEach-Object { Convert-ScalarStyleValue $_ })
        return ,$values
    }

    return Convert-ScalarStyleValue $trimmed
}

function Convert-SnakeToCamel {
    param([string]$Name)

    $parts = @($Name -split '_' | Where-Object { $_ -ne "" })
    if ($parts.Count -eq 0) {
        return $Name
    }
    $camel = $parts[0].ToLowerInvariant()
    for ($i = 1; $i -lt $parts.Count; ++$i) {
        $part = $parts[$i].ToLowerInvariant()
        $camel += $part.Substring(0, 1).ToUpperInvariant() + $part.Substring(1)
    }
    return $camel
}

function Read-LiveStyleJson {
    Ensure-Harness
    $root = Get-Content -Raw -Path $LiveStylePath | ConvertFrom-Json
    Set-JsonMember -Object $root -Name "format" -Value "skullbonez.style.json"
    Set-JsonMember -Object $root -Name "version" -Value 1
    [void](Ensure-JsonObjectMember -Object $root -Name "cinematic")
    return $root
}

function Write-LiveStyleJson {
    param([object]$Root)
    $Root | ConvertTo-Json -Depth 8 | Set-Content -Path $LiveStylePath -Encoding ASCII
}

function Set-StyleJsonValue {
    param(
        [string]$StyleKey,
        [string]$StyleValue
    )

    if ([string]::IsNullOrWhiteSpace($StyleKey)) {
        throw "Set requires -Key <json-path>."
    }

    $root = Read-LiveStyleJson
    $value = Convert-StyleValue $StyleValue
    $key = $StyleKey.Trim()

    if ($key -eq "style" -or $key -eq "include") {
        Set-JsonMember -Object $root -Name "includes" -Value @([string]$value)
    } elseif ($key.StartsWith("cinematic.")) {
        $field = $key.Substring("cinematic.".Length)
        Set-JsonMember -Object (Ensure-JsonObjectMember -Object $root -Name "cinematic") -Name $field -Value $value
    } elseif ($key.StartsWith("cinematic_")) {
        $field = Convert-SnakeToCamel $key.Substring("cinematic_".Length)
        Set-JsonMember -Object (Ensure-JsonObjectMember -Object $root -Name "cinematic") -Name $field -Value $value
    } else {
        Set-JsonMember -Object $root -Name $key -Value $value
    }

    Write-LiveStyleJson -Root $root
    Write-Host "[style-harness] set $key"
}

function New-ShotPath {
    param([string]$ShotName)

    if ([string]::IsNullOrWhiteSpace($ShotName)) {
        $ShotName = Get-Date -Format "yyyyMMdd_HHmmss_fff"
    }

    if ([System.IO.Path]::IsPathRooted($ShotName)) {
        if ([System.IO.Path]::GetExtension($ShotName) -eq "") {
            return "$ShotName.bmp"
        }
        return [System.IO.Path]::GetFullPath($ShotName)
    }

    foreach ($bad in [System.IO.Path]::GetInvalidFileNameChars()) {
        $ShotName = $ShotName.Replace([string]$bad, "_")
    }
    if ([System.IO.Path]::GetExtension($ShotName) -eq "") {
        $ShotName = "$ShotName.bmp"
    }
    return Join-Path $ShotRoot $ShotName
}

function Request-Shot {
    param([string]$ShotName)

    Ensure-Harness
    $shotPath = New-ShotPath $ShotName
    New-Item -ItemType Directory -Force -Path ([System.IO.Path]::GetDirectoryName($shotPath)) | Out-Null
    "capture `"$shotPath`"" | Set-Content -Path $CapturePath -Encoding ASCII
    Write-Host "[style-harness] requested $shotPath"
}

switch ($Command) {
    "init" {
        Ensure-Harness
        Write-Host "[style-harness] root $HarnessRoot"
        Write-Host "[style-harness] live $LiveStylePath"
    }
    "launch" {
        Ensure-Harness
        $exe = Resolve-RepoPath "Profile\SKULLBONEZ_CORE.exe"
        if (-not (Test-Path $exe)) {
            throw "Profile executable not found. Run tools\validate_build.bat Profile or tools\validate_full.bat first."
        }
        $scenePath = Resolve-RepoPath $Scene
        $args = @(
            "--renderer", $Renderer,
            "--scene", $scenePath,
            "--cinematic", "on",
            "--interactive",
            "--live-style-control", $HarnessRoot,
            "--vsync", "off"
        )
        $process = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $RepoRoot -PassThru
        Write-Host "[style-harness] launched pid $($process.Id)"
    }
    "set" {
        Set-StyleJsonValue -StyleKey $Key -StyleValue $Value
    }
    "shot" {
        Request-Shot -ShotName $Name
    }
    "setshot" {
        Set-StyleJsonValue -StyleKey $Key -StyleValue $Value
        Start-Sleep -Milliseconds 120
        Request-Shot -ShotName $Name
    }
    "status" {
        Ensure-Harness
        if (Test-Path $StatusPath) {
            Get-Content -Path $StatusPath
        } else {
            Write-Host "[style-harness] no status yet; launch the app with --live-style-control."
        }
    }
    "show" {
        Ensure-Harness
        Get-Content -Path $LiveStylePath
    }
}
