param(
    [Parameter(Mandatory = $true)]
    [string]$Archive,

    [Parameter(Mandatory = $true)]
    [string]$TestExecutable,

    [Parameter(Mandatory = $true)]
    [string]$SmokeScript,

    [Parameter(Mandatory = $true)]
    [string]$WorkingRoot
)

$ErrorActionPreference = "Stop"

if (Test-Path -LiteralPath $WorkingRoot) {
    Remove-Item -LiteralPath $WorkingRoot -Recurse -Force
}
Expand-Archive -LiteralPath $Archive -DestinationPath $WorkingRoot

& $TestExecutable
if ($LASTEXITCODE -ne 0) {
    throw "GDOX tests exited with code $LASTEXITCODE."
}

$PackageRoot = Get-ChildItem -LiteralPath $WorkingRoot -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "gdox.exe") } |
    Select-Object -First 1
if (-not $PackageRoot) {
    throw "The GDOX package root was not found after extraction."
}

& $SmokeScript `
    -PackageRoot $PackageRoot.FullName `
    -Seconds 5
