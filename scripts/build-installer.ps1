<#
.SYNOPSIS
  Builds the Offnote desktop app and packages it as a Windows installer (NSIS).

.DESCRIPTION
  Wraps `npm run tauri build` so packaging a release is one command instead of
  a multi-step manual process. Also kills any running dev/installed instance
  of the app first, since a locked offnote.exe (from a previous `cargo run`
  or an installed copy) makes the release build fail with "Access is denied".

.EXAMPLE
  .\scripts\build-installer.ps1
#>
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$version = (Get-Content "$repoRoot\src-tauri\tauri.conf.json" | ConvertFrom-Json).version
Write-Host "==> Building Offnote v$version" -ForegroundColor Cyan

# A locked offnote.exe (running from a previous build/install) makes the
# release build fail with "Access is denied" when it tries to overwrite the
# binary - stop it first so re-running this script is always safe.
$running = Get-Process -Name "offnote" -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "==> Stopping running offnote.exe (PID $($running.Id -join ', '))"
    $running | Stop-Process -Force
    Start-Sleep -Seconds 1
}

npm run tauri build -- --bundles nsis
if ($LASTEXITCODE -ne 0) {
    throw "tauri build failed with exit code $LASTEXITCODE"
}

$installer = Get-ChildItem "$repoRoot\src-tauri\target\release\bundle\nsis\*.exe" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $installer) {
    throw "Build succeeded but no installer .exe was found under target\release\bundle\nsis"
}

Write-Host ""
Write-Host "==> Installer ready:" -ForegroundColor Green
Write-Host "    $($installer.FullName)"
Write-Host "    $([math]::Round($installer.Length / 1MB, 1)) MB"
