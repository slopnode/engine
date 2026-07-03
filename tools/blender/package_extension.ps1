$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $PSScriptRoot "daggerlike_exporter"
$output = Join-Path $PSScriptRoot "daggerlike_exporter.zip"

if (-not (Test-Path $source)) {
    throw "Missing extension source folder: $source"
}

if (Test-Path $output) {
    Remove-Item $output
}

Compress-Archive -Path (Join-Path $source "*") -DestinationPath $output -Force

Write-Host "Created $output"
Write-Host "In Blender: Edit > Preferences > Get Extensions > menu (v) > Install from Disk..."
