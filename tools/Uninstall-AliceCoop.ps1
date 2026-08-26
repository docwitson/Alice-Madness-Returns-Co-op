[CmdletBinding()]
param(
    [string]$GameRoot,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
if (-not $GameRoot) {
    $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    if ((Split-Path -Leaf $scriptDirectory) -eq 'AliceCoop') {
        $GameRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDirectory '..\..\..'))
    }
    else {
        $repoRoot = Split-Path -Parent $PSScriptRoot
        $GameRoot = Split-Path -Parent $repoRoot
    }
}

$gameRootPath = [System.IO.Path]::GetFullPath($GameRoot)
$win32Path = Join-Path $gameRootPath 'Binaries\Win32'
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

if ($hadPreviousDinput8) {
    Write-Host "The previous dinput8.dll was restored."
}
else {
    Write-Host "The AliceCoop dinput8.dll was removed."
}
Write-Host "Logs, client-saves, the backup, and this uninstall script were preserved in: $coopPath"
