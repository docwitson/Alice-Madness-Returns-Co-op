[CmdletBinding()]
param(
    [string]$GameRoot,
    [string]$Win32Path,
    [string]$StatusPath
)

$ErrorActionPreference = 'Stop'

trap {
    if ($StatusPath) {
        $statusDirectory = Split-Path -Parent $StatusPath
        if ($statusDirectory) {
            New-Item -ItemType Directory -Force -Path $statusDirectory |
                Out-Null
        }
        $_.Exception.Message | Set-Content -LiteralPath $StatusPath -Encoding UTF8
    }
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}

$packageRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$advancedSource = Join-Path $packageRoot 'Advanced'
$payloadPath = Join-Path $advancedSource 'Payload'

function Test-Win32Directory([string]$Path) {
    if (-not $Path) { return $false }
    return Test-Path -LiteralPath (Join-Path ([System.IO.Path]::GetFullPath($Path)) `
        'AliceMadnessReturns.exe') -PathType Leaf
}

if (-not (Test-Win32Directory $Win32Path) -and $GameRoot) {
    foreach ($relative in @('Binaries\Win32', 'Game\Alice2\Binaries\Win32')) {
        $candidate = Join-Path $GameRoot $relative
        if (Test-Win32Directory $candidate) {
            $Win32Path = $candidate
            break
        }
    }
}
if (-not (Test-Win32Directory $Win32Path)) {
    $Win32Path = Read-Host `
        'Enter the folder containing AliceMadnessReturns.exe (Binaries\Win32)'
}
if (-not (Test-Win32Directory $Win32Path)) {
    throw 'AliceMadnessReturns.exe was not found in the selected directory.'
}

$win32Path = [System.IO.Path]::GetFullPath($Win32Path)
$targetDll = Join-Path $win32Path 'dinput8.dll'
$madnessPatchIni = Join-Path $win32Path 'MadnessPatch.ini'
$coopPath = Join-Path $win32Path 'AliceCoop'
$backupPath = Join-Path $coopPath 'backup'
$backupDll = Join-Path $backupPath 'dinput8.before-alicecoop.dll'
$manifestPath = Join-Path $coopPath 'install-manifest.json'
$packageManifestPath = Join-Path $advancedSource 'package-manifest.json'

if (Get-Process AliceMadnessReturns -ErrorAction SilentlyContinue) {
    throw 'Close every AliceMadnessReturns.exe process before installing.'
}

$requiredFiles = @(
    (Join-Path $packageRoot 'AliceCoopLauncher.exe'),
    (Join-Path $packageRoot 'AliceCoopServer.exe'),
    (Join-Path $payloadPath 'dinput8.dll'),
    (Join-Path $payloadPath 'AliceCoop.ini'),
    (Join-Path $payloadPath 'MadnessPatch.ini'),
    (Join-Path $payloadPath 'Manual\AliceCoop-LaunchConfig.bat'),
    (Join-Path $advancedSource 'Documentation\INSTALL.md'),
    (Join-Path $advancedSource 'Licenses\LICENSE'),
    (Join-Path $PSScriptRoot 'Uninstall-AliceCoop.ps1'),
    $packageManifestPath
)
foreach ($required in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Package is incomplete: $required"
    }
}

New-Item -ItemType Directory -Force -Path $coopPath, $backupPath,
    (Join-Path $coopPath 'logs'), (Join-Path $coopPath 'client-saves'),
    (Join-Path $coopPath 'images') | Out-Null

$previousManifest = if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
    Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
} else { $null }
$hadPreviousDinput8 = if ($previousManifest -and
    $previousManifest.PSObject.Properties.Name -contains 'hadPreviousDinput8') {
    [bool]$previousManifest.hadPreviousDinput8
} else {
    Test-Path -LiteralPath $targetDll -PathType Leaf
}
$existingHash = if ($previousManifest -and
    $previousManifest.PSObject.Properties.Name -contains 'previousDinput8Sha256') {
    $previousManifest.previousDinput8Sha256
} elseif ($hadPreviousDinput8) {
    (Get-FileHash -LiteralPath $targetDll -Algorithm SHA256).Hash
} else { $null }
$hadPreviousMadnessPatchIni = if ($previousManifest -and
    $previousManifest.PSObject.Properties.Name -contains 'hadPreviousMadnessPatchIni') {
    [bool]$previousManifest.hadPreviousMadnessPatchIni
} else {
    Test-Path -LiteralPath $madnessPatchIni -PathType Leaf
}
$newDll = Join-Path $payloadPath 'dinput8.dll'
$newHash = (Get-FileHash -LiteralPath $newDll -Algorithm SHA256).Hash

if ($hadPreviousDinput8 -and -not (Test-Path -LiteralPath $backupDll -PathType Leaf)) {
    if ($previousManifest) {
        throw "The previous dinput8.dll backup is missing: $backupDll"
    }
    Copy-Item -LiteralPath $targetDll -Destination $backupDll
}
$existingCoopIni = Join-Path $coopPath 'AliceCoop.ini'
if (Test-Path -LiteralPath $existingCoopIni -PathType Leaf) {
    Copy-Item -LiteralPath $existingCoopIni `
        -Destination (Join-Path $backupPath 'AliceCoop.before-update.ini') -Force
}

# Preserve a customized legacy manual-launch configuration before cleaning up
# files installed by launcher prototypes.
$legacyLaunchConfig = Join-Path $coopPath 'AliceCoop-LaunchConfig.bat'
if (Test-Path -LiteralPath $legacyLaunchConfig -PathType Leaf) {
    Copy-Item -LiteralPath $legacyLaunchConfig `
        -Destination (Join-Path $backupPath 'AliceCoop-LaunchConfig.before-cleanup.bat') `
        -Force
}

Copy-Item -LiteralPath $newDll -Destination $targetDll -Force
if (-not $hadPreviousMadnessPatchIni) {
    Copy-Item -LiteralPath (Join-Path $payloadPath 'MadnessPatch.ini') `
        -Destination $madnessPatchIni
}
if (-not (Test-Path -LiteralPath $existingCoopIni -PathType Leaf)) {
    Copy-Item -LiteralPath (Join-Path $payloadPath 'AliceCoop.ini') `
        -Destination $existingCoopIni
}

Get-ChildItem -LiteralPath (Join-Path $payloadPath 'Images') -File |
    Copy-Item -Destination (Join-Path $coopPath 'images') -Force

# Remove only known files installed by older launcher prototypes.
foreach ($name in @(
    'AliceCoop-LaunchConfig.bat', 'AliceCoop-Server.bat',
    'AliceCoop-Host.bat', 'AliceCoop-Client.bat', 'AliceCoop-Both.bat',
    'AliceCoop-Diagnostic-Both.bat', 'AliceCoop-Animation-Test.bat',
    'Get-PhysicalScreenWidth.ps1', 'README.md', 'README_RU.md',
    'KNOWN_ISSUES.md', 'Uninstall-AliceCoop.ps1', 'Uninstall-AliceCoop.bat',
    'AliceCoopLauncher.exe', 'AliceCoopLauncher.exe.config',
    'AliceCoopServer.exe'
)) {
    $legacyFile = Join-Path $coopPath $name
    if (Test-Path -LiteralPath $legacyFile -PathType Leaf) {
        Remove-Item -LiteralPath $legacyFile -Force
    }
}

$packageManifest = Get-Content -LiteralPath $packageManifestPath -Raw |
    ConvertFrom-Json
$manifest = [ordered]@{
    schemaVersion = 4
    installedAtUtc = [DateTime]::UtcNow.ToString('o')
    packageVersion = $packageManifest.version
    protocolVersion = $packageManifest.protocolVersion
    gameWin32Directory = $win32Path
    hadPreviousDinput8 = $hadPreviousDinput8
    hadPreviousMadnessPatchIni = $hadPreviousMadnessPatchIni
    previousDinput8Sha256 = $existingHash
    installedDinput8Sha256 = $newHash
    installedMadnessPatchIniSha256 = (Get-FileHash -LiteralPath $madnessPatchIni -Algorithm SHA256).Hash
    backupPath = $backupDll
    installMode = 'AliceCoop runtime payload only'
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8

if ($StatusPath) {
    "Alice Co-op installed into $coopPath" |
        Set-Content -LiteralPath $StatusPath -Encoding UTF8
}

Write-Host "AliceCoop installed into: $coopPath"
Write-Host 'Keep this installer folder and use its AliceCoopLauncher.exe to play.'
