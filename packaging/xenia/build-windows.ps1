param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("72ce13097", "7d8be7f17")]
    [string]$Revision,

    [Parameter(Mandatory = $true)]
    [string]$WorkRoot,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true)]
    [string]$VulkanInstaller
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-CanonicalPath {
    param([string]$Path, [string]$Label)

    if ([String]::IsNullOrWhiteSpace($Path)) {
        throw "$Label must not be empty."
    }
    try {
        return [IO.Path]::GetFullPath($Path)
    } catch {
        throw "$Label is not a valid path: $Path"
    }
}

function Assert-NotFilesystemRoot {
    param([string]$Path, [string]$Label)

    $VolumeRoot = [IO.Path]::GetPathRoot($Path).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    )
    $Normalized = $Path.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    )
    if (-not $Normalized -or
        [String]::Equals(
            $Normalized,
            $VolumeRoot,
            [StringComparison]::OrdinalIgnoreCase
        )) {
        throw "$Label must not be a filesystem root: $Path"
    }
}

function Assert-NoReparsePoints {
    param([string]$Path, [string]$Label)

    $VolumeRoot = [IO.Path]::GetPathRoot($Path)
    $Relative = $Path.Substring($VolumeRoot.Length)
    $Current = $VolumeRoot
    $Separators = [char[]]@(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    )
    foreach ($Part in $Relative.Split(
            $Separators,
            [StringSplitOptions]::RemoveEmptyEntries
        )) {
        $Current = Join-Path $Current $Part
        if (-not (Test-Path -LiteralPath $Current)) {
            break
        }
        $Item = Get-Item -Force -LiteralPath $Current
        if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label must not traverse a reparse point: $Current"
        }
    }
}

function Test-PathsOverlap {
    param([string]$First, [string]$Second)

    $FirstRoot = $First.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    )
    $SecondRoot = $Second.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    )
    if ([String]::Equals(
            $FirstRoot,
            $SecondRoot,
            [StringComparison]::OrdinalIgnoreCase
        )) {
        return $true
    }
    $FirstPrefix = $FirstRoot + [IO.Path]::DirectorySeparatorChar
    $SecondPrefix = $SecondRoot + [IO.Path]::DirectorySeparatorChar
    return $FirstRoot.StartsWith(
        $SecondPrefix,
        [StringComparison]::OrdinalIgnoreCase
    ) -or $SecondRoot.StartsWith(
        $FirstPrefix,
        [StringComparison]::OrdinalIgnoreCase
    )
}

function Require-ExactOutput {
    param([string[]]$Command, [string]$Expected, [string]$Label)

    $Lines = @(& $Command[0] $Command[1..($Command.Count - 1)] 2>&1)
    $ExitCode = $LASTEXITCODE
    $Actual = ($Lines | Select-Object -First 1).ToString().Trim()
    if ($ExitCode -ne 0 -or $Actual -ne $Expected) {
        throw "$Label must report '$Expected'; received '$Actual'."
    }
}

$RepositoryRoot = Get-CanonicalPath (Join-Path $PSScriptRoot "../..") `
    "Repository root"
$WorkRoot = Get-CanonicalPath $WorkRoot "WorkRoot"
$OutputDirectory = Get-CanonicalPath $OutputDirectory "OutputDirectory"
$VulkanInstaller = Get-CanonicalPath $VulkanInstaller "VulkanInstaller"
foreach ($Root in @(
        [pscustomobject]@{
            Path = $RepositoryRoot
            Label = "Repository root"
        },
        [pscustomobject]@{
            Path = $WorkRoot
            Label = "WorkRoot"
        },
        [pscustomobject]@{
            Path = $OutputDirectory
            Label = "OutputDirectory"
        }
    )) {
    Assert-NotFilesystemRoot $Root.Path $Root.Label
    Assert-NoReparsePoints $Root.Path $Root.Label
}
if ((Test-PathsOverlap $RepositoryRoot $WorkRoot) -or
    (Test-PathsOverlap $RepositoryRoot $OutputDirectory) -or
    (Test-PathsOverlap $WorkRoot $OutputDirectory)) {
    throw "RepositoryRoot, WorkRoot, and OutputDirectory must be separate trees."
}
Assert-NoReparsePoints $VulkanInstaller "VulkanInstaller"
if (-not (Test-Path -LiteralPath $VulkanInstaller -PathType Leaf)) {
    throw "The Vulkan SDK installer does not exist: $VulkanInstaller"
}

$ManifestPath = Join-Path $RepositoryRoot "packaging/runtime-manifest.json"
$Manifest = Get-Content -Raw -LiteralPath $ManifestPath | ConvertFrom-Json
$Integration = $Manifest.xenia.integration
$Definition = $Manifest.xenia.revisions.$Revision
if ($null -eq $Definition) {
    throw "The runtime manifest does not define Xenia $Revision."
}
$Asset = $Definition.targets.'x86_64-pc-windows-msvc'
$IsCandidate = $Asset.release_state -eq "candidate-only" -and
    $null -eq $Asset.url
$IsPublished = $Asset.release_state -eq "published" -and
    $Asset.url -is [string] -and $Asset.url.StartsWith("https://")
if ($Asset.origin -ne "gdox-patched" -or
    (-not $IsCandidate -and -not $IsPublished)) {
    throw "Xenia $Revision is not a reviewed GDOX-patched runtime."
}

foreach ($Directory in @($WorkRoot, $OutputDirectory)) {
    if (Test-Path -LiteralPath $Directory) {
        if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
            throw "Required directory path is not a directory: $Directory"
        }
        if ((Get-ChildItem -Force -LiteralPath $Directory |
                Measure-Object).Count -ne 0) {
            throw "Directory must not exist or must be empty: $Directory"
        }
    } else {
        New-Item -ItemType Directory -Path $Directory | Out-Null
    }
    Assert-NoReparsePoints $Directory "Build directory"
}

foreach ($Name in @("git", "python", "cmake", "ninja", "cl")) {
    if ($null -eq (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name is required in PATH."
    }
}
Require-ExactOutput @("git", "--version") $Integration.windows_build.git "Git"
Require-ExactOutput @("python", "--version") $Integration.windows_build.python "Python"
Require-ExactOutput @("cmake", "--version") $Integration.windows_build.cmake "CMake"
Require-ExactOutput @("ninja", "--version") $Integration.windows_build.ninja "Ninja"
$SavedErrorPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$CompilerOutput = (& cl 2>&1 | Out-String)
$ErrorActionPreference = $SavedErrorPreference
if (-not $CompilerOutput.Contains($Integration.windows_build.msvc_compiler)) {
    throw "cl does not match the reviewed MSVC compiler."
}
if ($env:VCToolsVersion.TrimEnd("\") -ne $Integration.windows_build.vc_tools) {
    throw "VCToolsVersion must be $($Integration.windows_build.vc_tools)."
}
if ($env:WindowsSDKVersion.TrimEnd("\") -ne
    $Integration.windows_build.windows_sdk) {
    throw "WindowsSDKVersion must be $($Integration.windows_build.windows_sdk)."
}

$InstallerAsset = $Integration.windows_build.vulkan_sdk
$InstallerItem = Get-Item -LiteralPath $VulkanInstaller
$InstallerAttributes = $InstallerItem.Attributes
if (($InstallerAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "The Vulkan SDK installer must not be a reparse point."
}
$InstallerHash = (Get-FileHash -Algorithm SHA256 `
    -LiteralPath $VulkanInstaller).Hash.ToLowerInvariant()
if ($InstallerItem.Length -ne $InstallerAsset.size -or
    $InstallerHash -ne $InstallerAsset.sha256) {
    throw "The Vulkan SDK installer failed manifest verification."
}
$env:VULKAN_SDK = Join-Path $WorkRoot "vulkan-sdk"
& $VulkanInstaller --root $env:VULKAN_SDK --accept-licenses `
    --default-answer --confirm-command install copy_only=1
if ($LASTEXITCODE -ne 0) {
    throw "The isolated Vulkan SDK installation failed."
}
$VulkanHeader = Join-Path $env:VULKAN_SDK "Include/vulkan/vulkan_core.h"
if (-not (Test-Path -LiteralPath $VulkanHeader -PathType Leaf)) {
    throw "The isolated Vulkan SDK is incomplete."
}
$HeaderText = Get-Content -Raw -LiteralPath $VulkanHeader
$VulkanVersion = $InstallerAsset.version.Split('.')
if (-not $HeaderText.Contains(
        "#define VK_HEADER_VERSION $($VulkanVersion[2])")) {
    throw "The isolated Vulkan SDK version is incorrect."
}
$env:PATH = (Join-Path $env:VULKAN_SDK "Bin") + ";" + $env:PATH

$SourceRoot = Join-Path $WorkRoot "xenia-canary"
& git init --quiet $SourceRoot
if ($LASTEXITCODE -ne 0) { throw "Could not initialize the Xenia source tree." }
& git -C $SourceRoot config core.autocrlf false
if ($LASTEXITCODE -ne 0) { throw "Could not disable Git line-ending conversion." }
& git -C $SourceRoot config core.eol lf
if ($LASTEXITCODE -ne 0) { throw "Could not pin Git line endings to LF." }
& git -C $SourceRoot config core.safecrlf true
if ($LASTEXITCODE -ne 0) { throw "Could not enable Git line-ending verification." }
& git -C $SourceRoot remote add origin $Integration.upstream_repository
& git -C $SourceRoot fetch --quiet --depth=1 origin $Definition.commit
if ($LASTEXITCODE -ne 0) { throw "Could not fetch the reviewed Xenia commit." }
& git -C $SourceRoot checkout --quiet --detach FETCH_HEAD
if ($LASTEXITCODE -ne 0) { throw "Could not check out the reviewed Xenia commit." }
if ((& git -C $SourceRoot rev-parse HEAD).Trim() -ne $Definition.commit) {
    throw "The checked-out Xenia commit is not the reviewed commit."
}

$LicensePath = Join-Path $SourceRoot "LICENSE"
$LicenseHash = (Get-FileHash -Algorithm SHA256 `
    -LiteralPath $LicensePath).Hash.ToLowerInvariant()
if ((Get-Item -LiteralPath $LicensePath).Length -ne $Definition.license.size -or
    $LicenseHash -ne $Definition.license.sha256) {
    throw "The checked-out Xenia license failed manifest verification."
}
foreach ($Patch in $Integration.patches) {
    $PatchPath = Join-Path $RepositoryRoot $Patch.path
    $Hash = (Get-FileHash -Algorithm SHA256 `
        -LiteralPath $PatchPath).Hash.ToLowerInvariant()
    if ((Get-Item -LiteralPath $PatchPath).Length -ne $Patch.size -or
        $Hash -ne $Patch.sha256) {
        throw "Xenia integration patch failed verification: $($Patch.path)"
    }
    & git -C $SourceRoot apply --3way --whitespace=error-all $PatchPath
    if ($LASTEXITCODE -ne 0) { throw "Could not apply $($Patch.path)." }
}
& git -C $SourceRoot diff --check
if ($LASTEXITCODE -ne 0) {
    throw "The patched Xenia tree failed git diff --check."
}

$CommitEpoch = (& git -C $SourceRoot show -s --format=%ct HEAD).Trim()
$env:SOURCE_DATE_EPOCH = $CommitEpoch
$MappedWorkRoot = "work"
$MappedSourceRoot = "xenia-canary"
$MappedBuildRoot = "build"
$CommonCompileFlags = (
    "/Brepro /experimental:deterministic " +
    "/pathmap:$SourceRoot\build=$MappedBuildRoot " +
    "/pathmap:$SourceRoot=$MappedSourceRoot " +
    "/pathmap:$($env:VULKAN_SDK)=vulkan " +
    "/pathmap:$WorkRoot=$MappedWorkRoot " +
    "/pathmap:C:\dvds-tools=tools " +
    "/pathmap:C:\BuildTools=msvc"
)
$CCompileFlags = "/DWIN32 /D_WINDOWS $CommonCompileFlags"
$CxxCompileFlags = "/DWIN32 /D_WINDOWS /EHsc $CommonCompileFlags"
Remove-Item Env:CL -ErrorAction SilentlyContinue
$env:LINK = "/PDBALTPATH:%_PDB%"

Push-Location $SourceRoot
try {
    & .\xb.bat setup
    if ($LASTEXITCODE -ne 0) { throw "Xenia setup failed." }

    $ConfigureArguments = @(
        "-S", ".",
        "-B", "build",
        "-G", "Ninja Multi-Config",
        "-DCMAKE_C_FLAGS=$CCompileFlags",
        "-DCMAKE_CXX_FLAGS=$CxxCompileFlags",
        "-DCMAKE_EXE_LINKER_FLAGS=/machine:x64 /Brepro",
        "-DCMAKE_SHARED_LINKER_FLAGS=/machine:x64 /Brepro",
        "-DCMAKE_STATIC_LINKER_FLAGS=/Brepro"
    )
    & cmake @ConfigureArguments
    if ($LASTEXITCODE -ne 0) { throw "Xenia configure failed." }

    $CommitDate = [DateTimeOffset]::FromUnixTimeSeconds(
        [Int64]$CommitEpoch
    ).UtcDateTime.ToString(
        "yyyy-MM-dd",
        [Globalization.CultureInfo]::InvariantCulture
    )
    $CommitShort = $Definition.commit.Substring(0, 7)
    $VersionHeaderText = @(
        "// Deterministic Xenia build identity.",
        "#ifndef GENERATED_VERSION_H_",
        "#define GENERATED_VERSION_H_",
        "#define XE_BUILD_BRANCH `"detached`"",
        "#define XE_BUILD_COMMIT `"$($Definition.commit)`"",
        "#define XE_BUILD_COMMIT_SHORT `"$CommitShort`"",
        "#define XE_BUILD_DATE `"$CommitDate`"",
        "#endif  // GENERATED_VERSION_H_",
        ""
    ) -join "`n"
    $Utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    $VersionHeader = Join-Path $SourceRoot "build/version.h"
    [IO.File]::WriteAllText(
        $VersionHeader,
        $VersionHeaderText,
        $Utf8WithoutBom
    )
    $WrittenVersionHeader = [IO.File]::ReadAllText(
        $VersionHeader,
        $Utf8WithoutBom
    )
    if ($WrittenVersionHeader -cne $VersionHeaderText -or
        $WrittenVersionHeader.Contains("__DATE__")) {
        throw "The deterministic Xenia version header failed verification."
    }

    & cmake --build build --config Release --target xenia-app
    if ($LASTEXITCODE -ne 0) { throw "Xenia build failed." }
} finally {
    Pop-Location
}

$Executable = Join-Path $SourceRoot "build/bin/Windows/Release/xenia_canary.exe"
$Pdb = Join-Path $SourceRoot "build/bin/Windows/Release/xenia_canary.pdb"
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "The Xenia executable was not produced."
}
if (-not (Test-Path -LiteralPath $Pdb -PathType Leaf)) {
    throw "The Xenia link PDB was not produced."
}

$Packager = Join-Path $RepositoryRoot `
    "scripts/package_xenia_windows_runtime.py"
$Archive = Join-Path $OutputDirectory $Asset.archive_name
& python $Packager `
    --revision $Revision `
    --executable $Executable `
    --license $LicensePath `
    --output $Archive
if ($LASTEXITCODE -ne 0) {
    throw "Xenia archive verification failed."
}

[ordered]@{
    revision = $Revision
    commit = $Definition.commit
    source_date_epoch = $CommitEpoch
    source_date = $CommitDate
    executable_size = (Get-Item -LiteralPath $Executable).Length
    executable_sha256 = (Get-FileHash -Algorithm SHA256 `
        -LiteralPath $Executable).Hash.ToLowerInvariant()
    archive_size = (Get-Item -LiteralPath $Archive).Length
    archive_sha256 = (Get-FileHash -Algorithm SHA256 `
        -LiteralPath $Archive).Hash.ToLowerInvariant()
    full_pdb_temporary_only = $true
    candidate_contains_pdb = $false
} | ConvertTo-Json
