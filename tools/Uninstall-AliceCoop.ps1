[CmdletBinding()]
param(
    [string]$GameRoot,
    [string]$Win32Path,
    [string]$StatusPath,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
trap {
    if ($StatusPath) {
        $statusDirectory = Split-Path -Parent $StatusPath
        if ($statusDirectory) {
            New-Item -ItemType Directory -Force -Path $statusDirectory | Out-Null
        }
        "Alice Co-op removal failed: $($_.Exception.Message)" |
            Set-Content -LiteralPath $StatusPath -Encoding UTF8
    }
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
if (-not $Win32Path) {
    $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    if ((Split-Path -Leaf $scriptDirectory) -eq 'Tools' -and
        (Split-Path -Leaf (Split-Path -Parent $scriptDirectory)) -eq 'Advanced') {
        $coopFromScript = [System.IO.Path]::GetFullPath(
            (Join-Path $scriptDirectory '..\..'))
        $Win32Path = Split-Path -Parent $coopFromScript
    }
    elseif ((Split-Path -Leaf $scriptDirectory) -eq 'AliceCoop') {
        $Win32Path = Split-Path -Parent $scriptDirectory
    }
    elseif ($GameRoot) {
        $Win32Path = Join-Path $GameRoot 'Binaries\Win32'
    }
    else {
        $repoRoot = Split-Path -Parent $PSScriptRoot
        $Win32Path = Join-Path (Split-Path -Parent $repoRoot) 'Binaries\Win32'
    }
}

$win32Path = [System.IO.Path]::GetFullPath($Win32Path)
$coopPath = Join-Path $win32Path 'AliceCoop'
$targetDll = Join-Path $win32Path 'dinput8.dll'
$madnessPatchIni = Join-Path $win32Path 'MadnessPatch.ini'
$backupDll = Join-Path $coopPath 'backup\dinput8.before-alicecoop.dll'
$manifestPath = Join-Path $coopPath 'install-manifest.json'

if (Get-Process AliceMadnessReturns -ErrorAction SilentlyContinue) {
    throw 'Close every AliceMadnessReturns.exe process before uninstalling.'
}
$manifest = $null
if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
}
$hadPreviousDinput8 = $true
if ($manifest -and $manifest.PSObject.Properties.Name -contains 'hadPreviousDinput8') {
    $hadPreviousDinput8 = [bool]$manifest.hadPreviousDinput8
}

if ($manifest -and (Test-Path -LiteralPath $targetDll -PathType Leaf)) {
    $currentHash = (Get-FileHash -LiteralPath $targetDll -Algorithm SHA256).Hash
    if ($manifest.installedDinput8Sha256 -and
        $currentHash -ne $manifest.installedDinput8Sha256 -and
        -not $Force) {
        throw 'The active dinput8.dll changed after AliceCoop was installed. Refusing to overwrite it. Use -Force only if restoring the saved MadnessPatch DLL is intentional.'
    }
}

if ($hadPreviousDinput8) {
    if (-not (Test-Path -LiteralPath $backupDll -PathType Leaf)) {
        throw "The recoverable dinput8.dll backup is missing: $backupDll"
    }
    Copy-Item -LiteralPath $backupDll -Destination $targetDll -Force
}
elseif (Test-Path -LiteralPath $targetDll -PathType Leaf) {
    Remove-Item -LiteralPath $targetDll -Force
}

if ($manifest -and
    $manifest.PSObject.Properties.Name -contains 'hadPreviousMadnessPatchIni' -and
    -not [bool]$manifest.hadPreviousMadnessPatchIni -and
    (Test-Path -LiteralPath $madnessPatchIni -PathType Leaf)) {
    $currentIniHash = (Get-FileHash -LiteralPath $madnessPatchIni -Algorithm SHA256).Hash
    if ($Force -or $currentIniHash -eq $manifest.installedMadnessPatchIniSha256) {
        Remove-Item -LiteralPath $madnessPatchIni -Force
    }
    else {
        Write-Warning 'MadnessPatch.ini was created by AliceCoop but later edited; it was preserved.'
    }
}
foreach ($name in @(
    'AliceCoopServer.exe',
    'AliceCoopLauncher.exe',
    'AliceCoopLauncher.exe.config',
    'AliceCoop.ini',
    'README.md',
    'install-manifest.json',
    'AliceCoop-Server.bat',
    'AliceCoop-Host.bat',
    'AliceCoop-Client.bat',
    'AliceCoop-Both.bat',
    'AliceCoop-Diagnostic-Both.bat',
    'AliceCoop-Animation-Test.bat',
    'AliceCoop-Mirror.bat',
    'AliceCoop-Trace.bat',
    'Get-PhysicalScreenWidth.ps1'
)) {
    $file = Join-Path $coopPath $name
    if (Test-Path -LiteralPath $file -PathType Leaf) {
        Remove-Item -LiteralPath $file -Force
    }
}
if (Test-Path -LiteralPath (Join-Path $coopPath 'images') -PathType Container) {
    Remove-Item -LiteralPath (Join-Path $coopPath 'images') -Recurse -Force
}
foreach ($name in @('Manual', 'Documentation', 'Licenses')) {
    $directory = Join-Path $coopPath "Advanced\$name"
    if (Test-Path -LiteralPath $directory -PathType Container) {
        Remove-Item -LiteralPath $directory -Recurse -Force
    }
}
foreach ($name in @('SOURCE_CODE.txt', 'package-manifest.json', 'SHA256SUMS.txt')) {
    $file = Join-Path $coopPath "Advanced\$name"
    if (Test-Path -LiteralPath $file -PathType Leaf) {
        Remove-Item -LiteralPath $file -Force
    }
}

if ($hadPreviousDinput8) {
    Write-Host "The previous dinput8.dll was restored."
}
else {
    Write-Host "The AliceCoop dinput8.dll was removed."
}
Write-Host "Logs, client-saves, the backup, and Advanced\Tools were preserved in: $coopPath"
if ($StatusPath) {
    "Alice Co-op was removed from $win32Path" |
        Set-Content -LiteralPath $StatusPath -Encoding UTF8
}
