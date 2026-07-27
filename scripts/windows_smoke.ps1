param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot,

    [int]$Seconds = 5,

    [switch]$InteractiveProbe,

    [string]$ResultPath
)

$ErrorActionPreference = "Stop"
$Executable = Join-Path $PackageRoot "gdox.exe"

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "GDOX package executable was not found: $Executable"
}
if ($Seconds -lt 1 -or $Seconds -gt 60) {
    throw "Seconds must be between 1 and 60."
}

if ($InteractiveProbe) {
    if (-not $ResultPath) {
        throw "The interactive probe requires a result path."
    }
    $Process = $null
    $ProfileRoot = Join-Path $env:TEMP "GDOX-Smoke-$PID-profile"
    try {
        Remove-Item `
            -LiteralPath $ProfileRoot `
            -Recurse `
            -Force `
            -ErrorAction SilentlyContinue
        $env:GDOX_CONFIG_HOME = Join-Path $ProfileRoot "config"
        $env:GDOX_DATA_HOME = Join-Path $ProfileRoot "data"
        Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class GdoxWindow {
    [DllImport("user32.dll")]
    public static extern bool PostMessage(
        IntPtr handle,
        uint message,
        UIntPtr wParam,
        IntPtr lParam
    );
}
"@
        $Process = Start-Process `
            -FilePath $Executable `
            -WorkingDirectory $PackageRoot `
            -PassThru
        Start-Sleep -Seconds $Seconds
        for ($Attempt = 0; $Attempt -lt 10; $Attempt++) {
            $Process.Refresh()
            if ($Process.HasExited -or $Process.MainWindowHandle -ne 0) {
                break
            }
            Start-Sleep -Seconds 1
        }
        if ($Process.HasExited) {
            throw "GDOX exited before the window check (code $($Process.ExitCode))."
        }
        if ($Process.MainWindowHandle -eq 0) {
            throw "GDOX did not create an interactive desktop window."
        }
        [void][GdoxWindow]::PostMessage(
            $Process.MainWindowHandle,
            0x0010,
            [UIntPtr]::Zero,
            [IntPtr]::Zero
        )
        if (-not $Process.WaitForExit(30000)) {
            throw "GDOX did not close after receiving WM_CLOSE."
        }
        if ($Process.ExitCode -ne 0) {
            throw "GDOX exited with code $($Process.ExitCode)."
        }
        Set-Content -LiteralPath $ResultPath -Value "passed" -Encoding Ascii
    } catch {
        Set-Content `
            -LiteralPath $ResultPath `
            -Value "failed: $($_.Exception.Message)" `
            -Encoding UTF8
        exit 1
    } finally {
        if ($Process -and -not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        }
        Remove-Item `
            -LiteralPath $ProfileRoot `
            -Recurse `
            -Force `
            -ErrorAction SilentlyContinue
    }
    exit 0
}

$TaskName = "GDOX-Smoke-$PID"
$ResultPath = Join-Path $env:TEMP "$TaskName.txt"
Remove-Item -LiteralPath $ResultPath -Force -ErrorAction SilentlyContinue
$Arguments = @(
    "-NoProfile"
    "-ExecutionPolicy Bypass"
    "-File `"$PSCommandPath`""
    "-PackageRoot `"$PackageRoot`""
    "-Seconds $Seconds"
    "-InteractiveProbe"
    "-ResultPath `"$ResultPath`""
) -join " "
$Action = New-ScheduledTaskAction `
    -Execute "powershell.exe" `
    -Argument $Arguments `
    -WorkingDirectory $PackageRoot
$Principal = New-ScheduledTaskPrincipal `
    -UserId $env:USERNAME `
    -LogonType Interactive `
    -RunLevel Limited

try {
    Register-ScheduledTask `
        -TaskName $TaskName `
        -Action $Action `
        -Principal $Principal `
        -Force | Out-Null
    Start-ScheduledTask -TaskName $TaskName

    $ProbeDeadlineSeconds = $Seconds + 50
    for ($Attempt = 0; $Attempt -lt $ProbeDeadlineSeconds; $Attempt++) {
        if (Test-Path -LiteralPath $ResultPath) {
            break
        }
        Start-Sleep -Seconds 1
    }
    if (-not (Test-Path -LiteralPath $ResultPath)) {
        $Info = Get-ScheduledTaskInfo -TaskName $TaskName
        throw "The interactive GDOX probe did not finish (task result $($Info.LastTaskResult))."
    }
    $Result = (Get-Content -LiteralPath $ResultPath -Raw).Trim()
    if ($Result -ne "passed") {
        throw $Result
    }
    Write-Output "windows_desktop_gui=passed"
} finally {
    Unregister-ScheduledTask `
        -TaskName $TaskName `
        -Confirm:$false `
        -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $ResultPath -Force -ErrorAction SilentlyContinue
}
