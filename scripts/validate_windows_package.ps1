param(
    [Parameter(Mandatory = $true)]
    [string]$Archive,

    [Parameter(Mandatory = $true)]
    [string]$TestExecutable,

    [Parameter(Mandatory = $true)]
    [string]$RuntimeProbe,

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

$Packages = @(Get-ChildItem -LiteralPath $WorkingRoot -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "gdox.exe") } |
    Select-Object -ExpandProperty FullName)
if ($Packages.Count -ne 1) {
    throw "The GDOX package root was not found after extraction."
}

$PackageRoot = Join-Path $WorkingRoot "Relocated GDOX package"
Move-Item -LiteralPath $Packages[0] -Destination $PackageRoot
$PackagedProbe = Join-Path $PackageRoot "gdox_runtime_probe.exe"
Copy-Item -LiteralPath $RuntimeProbe -Destination $PackagedProbe
& $SmokeScript `
    -PackageRoot $PackageRoot `
    -RuntimeProbe $PackagedProbe `
    -Seconds 5
