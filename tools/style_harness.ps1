param(
    [ValidateSet("init", "launch", "set", "shot", "setshot", "status", "show")]
    [string]$Command = "status",
    [string]$Root = "Agentic\style-harness",
    [string]$Style = "low_poly_art_style",
    [string]$Scene = "SkullbonezData\scenes\concept_12_low_poly_art_style.scene",
    [ValidateSet("gl", "dx11", "dx12")]
    [string]$Renderer = "gl",
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
$LiveStylePath = Join-Path $HarnessRoot "live.style"
$CapturePath = Join-Path $HarnessRoot "capture.txt"
$StatusPath = Join-Path $HarnessRoot "status.txt"
$ShotRoot = Join-Path $HarnessRoot "shots"

function Ensure-Harness {
    New-Item -ItemType Directory -Force -Path $HarnessRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $ShotRoot | Out-Null

    if ($Force -or -not (Test-Path $LiveStylePath)) {
        @(
            "# Live style descriptor watched by --live-style-control."
            "# Keep this style-only: cinematic_* and object_material directives."
            "style $Style"
        ) | Set-Content -Path $LiveStylePath -Encoding ASCII
    }

    if (-not (Test-Path $CapturePath)) {
        "" | Set-Content -Path $CapturePath -Encoding ASCII
    }
}

function Set-StyleDirective {
    param(
        [string]$Directive,
        [string]$DirectiveValue
    )

    if ([string]::IsNullOrWhiteSpace($Directive)) {
        throw "Set requires -Key <directive>."
    }

    Ensure-Harness
    $replacement = if ([string]::IsNullOrWhiteSpace($DirectiveValue)) { $Directive } else { "$Directive $DirectiveValue" }
    $lines = @(Get-Content -Path $LiveStylePath)
    $matched = $false
    for ($i = 0; $i -lt $lines.Count; ++$i) {
        $trimmed = $lines[$i].TrimStart()
        if ($trimmed -eq $Directive -or $trimmed.StartsWith("$Directive ")) {
            $lines[$i] = $replacement
            $matched = $true
            break
        }
    }
    if (-not $matched) {
        $lines += $replacement
    }
    $lines | Set-Content -Path $LiveStylePath -Encoding ASCII
    Write-Host "[style-harness] set $replacement"
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
        Set-StyleDirective -Directive $Key -DirectiveValue $Value
    }
    "shot" {
        Request-Shot -ShotName $Name
    }
    "setshot" {
        Set-StyleDirective -Directive $Key -DirectiveValue $Value
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
