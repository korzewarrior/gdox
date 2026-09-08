param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot,

    [int]$Seconds = 5,

    [switch]$InteractiveProbe,

    [string]$ResultPath,

    [string]$RuntimeProbe
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

    [DllImport("user32.dll", CharSet = CharSet.Ansi)]
    public static extern IntPtr FindWindow(
        string className,
        string windowName
    );

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(
        IntPtr handle,
        out uint processId
    );

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr handle);
}
"@
        $PreservedFiles = @{}
        if ($RuntimeProbe) {
            $Firmware = Join-Path $env:GDOX_DATA_HOME "xemu/firmware/bios.bin"
            New-Item -ItemType Directory -Path (Split-Path $Firmware) -Force | Out-Null
            # Synthetic size-valid BIOS; no game or copyrighted firmware is booted.
            [IO.File]::WriteAllBytes($Firmware, [byte[]]::new(262144))
            $Preferences = Join-Path $env:GDOX_CONFIG_HOME "settings.conf"
            New-Item -ItemType Directory -Path $env:GDOX_CONFIG_HOME -Force | Out-Null
            Set-Content -LiteralPath $Preferences -Encoding Ascii -Value @(
                "schema=1", "auto_start=0", "internal_resolution_scale=2",
                "display_aspect=0", "display_fit=1", "fullscreen=0",
                "window_width=880", "window_height=680", "xemu_override=@included"
            )
            & $RuntimeProbe --prepare
            if ($LASTEXITCODE -ne 0) { throw "Included runtime preparation failed." }
            foreach ($Path in @(
                $Firmware,
                $Preferences,
                (Join-Path $PackageRoot "runtime/xemu/xemu.exe"),
                (Join-Path $PackageRoot "runtime/hdd/xbox_hdd.qcow2")
            )) {
                $PreservedFiles[$Path] = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
            }
        }
        $Passes = if ($RuntimeProbe) { 2 } else { 1 }
        for ($Pass = 0; $Pass -lt $Passes; $Pass++) {
            $Process = Start-Process `
                -FilePath $Executable `
                -ArgumentList "--background" `
                -WorkingDirectory $PackageRoot `
                -PassThru
            $BackgroundWindow = [IntPtr]::Zero
            for ($Attempt = 0; $Attempt -lt 40; $Attempt++) {
                $Process.Refresh()
                if ($Process.HasExited) {
                    break
                }
                $BackgroundWindow = [GdoxWindow]::FindWindow(
                    "GDOXBackgroundHost",
                    "GDOX background host"
                )
                if ($BackgroundWindow -ne [IntPtr]::Zero) {
                    break
                }
                Start-Sleep -Milliseconds 250
            }
            if ($Process.HasExited) {
                throw "GDOX exited before the background check (code $($Process.ExitCode))."
            }
            if ($BackgroundWindow -eq [IntPtr]::Zero) {
                throw "GDOX did not create its notification-area host."
            }
            [uint32]$BackgroundProcessId = 0
            [void][GdoxWindow]::GetWindowThreadProcessId(
                $BackgroundWindow,
                [ref]$BackgroundProcessId
            )
            if ($BackgroundProcessId -ne $Process.Id) {
                throw "The notification-area host belongs to another process."
            }
            if ([GdoxWindow]::IsWindowVisible($BackgroundWindow)) {
                throw "The notification-area host unexpectedly became visible."
            }
            Start-Sleep -Seconds $Seconds
            $Process.Refresh()
            if ($Process.HasExited) {
                throw "GDOX did not remain active in the notification area."
            }
            if ([GdoxWindow]::FindWindow(
                "GDOXBackgroundHost",
                "GDOX background host"
            ) -ne $BackgroundWindow) {
                throw "GDOX replaced or removed its notification-area host."
            }
            if ($Pass + 1 -lt $Passes) {
                Stop-Process -Id $Process.Id -Force
                if (-not $Process.WaitForExit(5000)) { throw "Force-close did not finish." }
                foreach ($Path in $PreservedFiles.Keys) {
                    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $PreservedFiles[$Path]) {
                        throw "Force-close changed a persistent runtime file: $Path"
                    }
                }
                & $RuntimeProbe --prepare
                if ($LASTEXITCODE -ne 0) { throw "Included runtime failed after force-close." }
            }
        }
        if (-not [GdoxWindow]::PostMessage(
            $BackgroundWindow,
            0x0011,
            [UIntPtr]::Zero,
            [IntPtr]::Zero
        )) {
            throw "Could not send WM_QUERYENDSESSION to GDOX."
        }
        if (-not [GdoxWindow]::PostMessage(
            $BackgroundWindow,
            0x0016,
            [UIntPtr]::new([uint64]1),
            [IntPtr]::Zero
        )) {
            throw "Could not send WM_ENDSESSION to GDOX."
        }
        if (-not $Process.WaitForExit(30000)) {
            throw "GDOX did not stop after a Windows session-ending request."
        }
        if ($Process.ExitCode -ne 0) {
            throw "GDOX exited with code $($Process.ExitCode)."
        }
        if ($RuntimeProbe) {
            & $RuntimeProbe --prepare
            if ($LASTEXITCODE -ne 0) { throw "Included runtime failed after normal shutdown." }
            foreach ($Path in $PreservedFiles.Keys) {
                if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $PreservedFiles[$Path]) {
                    throw "Shutdown changed a persistent runtime file: $Path"
                }
            }
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
    $(if ($RuntimeProbe) { "-RuntimeProbe `"$RuntimeProbe`"" })
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
    Write-Output "windows_background_lifecycle=passed"
    if ($RuntimeProbe) { Write-Output "windows_runtime_restart=passed" }
} finally {
    Unregister-ScheduledTask `
        -TaskName $TaskName `
        -Confirm:$false `
        -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $ResultPath -Force -ErrorAction SilentlyContinue
}
