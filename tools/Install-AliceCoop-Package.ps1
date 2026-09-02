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
$gameExe = Join-Path $win32Path 'AliceMadnessReturns.exe'
$targetDll = Join-Path $win32Path 'dinput8.dll'
$madnessPatchIni = Join-Path $win32Path 'MadnessPatch.ini'
$coopPath = Join-Path $win32Path 'AliceCoop'
$targetServer = Join-Path $coopPath 'AliceCoopServer.exe'
$targetLauncher = Join-Path $coopPath 'AliceCoopLauncher.exe'
$backupPath = Join-Path $coopPath 'backup'
$backupDll = Join-Path $backupPath 'dinput8.before-alicecoop.dll'
$manifestPath = Join-Path $coopPath 'install-manifest.json'
$packageManifestPath = Join-Path $advancedSource 'package-manifest.json'
$targetAdvanced = Join-Path $coopPath 'Advanced'

if (Get-Process AliceMadnessReturns -ErrorAction SilentlyContinue) {
    throw 'Close every AliceMadnessReturns.exe process before installing.'
}
$runningTargetRelay = @(Get-Process AliceCoopServer -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path -eq $targetServer })
if ($runningTargetRelay.Count -ne 0) {
    throw 'Close the installed AliceCoopServer.exe before installing.'
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
    (Join-Path $coopPath 'images'), $targetAdvanced,
    (Join-Path $targetAdvanced 'Manual'),
    (Join-Path $targetAdvanced 'Documentation'),
    (Join-Path $targetAdvanced 'Licenses'),
    (Join-Path $targetAdvanced 'Tools') | Out-Null

$hadPreviousDinput8 = Test-Path -LiteralPath $targetDll -PathType Leaf
$existingHash = if ($hadPreviousDinput8) {
    (Get-FileHash -LiteralPath $targetDll -Algorithm SHA256).Hash
} else { $null }
$hadPreviousMadnessPatchIni = Test-Path -LiteralPath $madnessPatchIni -PathType Leaf
$newDll = Join-Path $payloadPath 'dinput8.dll'
$newHash = (Get-FileHash -LiteralPath $newDll -Algorithm SHA256).Hash

if ($hadPreviousDinput8 -and -not (Test-Path -LiteralPath $backupDll -PathType Leaf)) {
    Copy-Item -LiteralPath $targetDll -Destination $backupDll
}
$existingCoopIni = Join-Path $coopPath 'AliceCoop.ini'
if (Test-Path -LiteralPath $existingCoopIni -PathType Leaf) {
    Copy-Item -LiteralPath $existingCoopIni `
        -Destination (Join-Path $backupPath 'AliceCoop.before-update.ini') -Force
}

# Preserve a customized legacy manual-launch configuration while moving it
# out of the normal user-facing directory.
$legacyLaunchConfig = Join-Path $coopPath 'AliceCoop-LaunchConfig.bat'
$advancedLaunchConfig = Join-Path $targetAdvanced 'Manual\AliceCoop-LaunchConfig.bat'
if (Test-Path -LiteralPath $legacyLaunchConfig -PathType Leaf) {
    Copy-Item -LiteralPath $legacyLaunchConfig -Destination $advancedLaunchConfig -Force
}

Copy-Item -LiteralPath $newDll -Destination $targetDll -Force
if (-not $hadPreviousMadnessPatchIni) {
    Copy-Item -LiteralPath (Join-Path $payloadPath 'MadnessPatch.ini') `
        -Destination $madnessPatchIni
}
Copy-Item -LiteralPath (Join-Path $packageRoot 'AliceCoopLauncher.exe') `
    -Destination $targetLauncher -Force
Copy-Item -LiteralPath (Join-Path $packageRoot 'AliceCoopServer.exe') `
    -Destination $targetServer -Force
if (-not (Test-Path -LiteralPath $existingCoopIni -PathType Leaf)) {
    Copy-Item -LiteralPath (Join-Path $payloadPath 'AliceCoop.ini') `
        -Destination $existingCoopIni
}

Get-ChildItem -LiteralPath (Join-Path $payloadPath 'Images') -File |
    Copy-Item -Destination (Join-Path $coopPath 'images') -Force
Get-ChildItem -LiteralPath (Join-Path $payloadPath 'Manual') -File |
    Copy-Item -Destination (Join-Path $targetAdvanced 'Manual') -Force
if (Test-Path -LiteralPath $legacyLaunchConfig -PathType Leaf) {
    Copy-Item -LiteralPath $legacyLaunchConfig -Destination $advancedLaunchConfig -Force
}
Get-ChildItem -LiteralPath (Join-Path $advancedSource 'Documentation') -File |
    Copy-Item -Destination (Join-Path $targetAdvanced 'Documentation') -Force
Get-ChildItem -LiteralPath (Join-Path $advancedSource 'Licenses') -Force |
    Copy-Item -Destination (Join-Path $targetAdvanced 'Licenses') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Uninstall-AliceCoop.ps1') `
    -Destination (Join-Path $targetAdvanced 'Tools') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Uninstall-AliceCoop.bat') `
    -Destination (Join-Path $targetAdvanced 'Tools') -Force
Copy-Item -LiteralPath $packageManifestPath -Destination $targetAdvanced -Force
Copy-Item -LiteralPath (Join-Path $advancedSource 'SOURCE_CODE.txt') `
    -Destination $targetAdvanced -Force

# Remove only known legacy AliceCoop package files after their replacements
# have been installed under Advanced.
foreach ($name in @(
    'AliceCoop-LaunchConfig.bat', 'AliceCoop-Server.bat',
    'AliceCoop-Host.bat', 'AliceCoop-Client.bat', 'AliceCoop-Both.bat',
    'AliceCoop-Diagnostic-Both.bat', 'AliceCoop-Animation-Test.bat',
    'Get-PhysicalScreenWidth.ps1', 'README.md', 'README_RU.md',
    'KNOWN_ISSUES.md', 'Uninstall-AliceCoop.ps1', 'Uninstall-AliceCoop.bat'
)) {
    $legacyFile = Join-Path $coopPath $name
    if (Test-Path -LiteralPath $legacyFile -PathType Leaf) {
        Remove-Item -LiteralPath $legacyFile -Force
    }
}

$packageManifest = Get-Content -LiteralPath $packageManifestPath -Raw |
    ConvertFrom-Json
$manifest = [ordered]@{
    schemaVersion = 3
    installedAtUtc = [DateTime]::UtcNow.ToString('o')
    packageVersion = $packageManifest.version
    protocolVersion = $packageManifest.protocolVersion
    gameWin32Directory = $win32Path
    hadPreviousDinput8 = $hadPreviousDinput8
    hadPreviousMadnessPatchIni = $hadPreviousMadnessPatchIni
    previousDinput8Sha256 = $existingHash
    installedDinput8Sha256 = $newHash
    installedServerSha256 = (Get-FileHash -LiteralPath $targetServer -Algorithm SHA256).Hash
    installedLauncherSha256 = (Get-FileHash -LiteralPath $targetLauncher -Algorithm SHA256).Hash
    installedMadnessPatchIniSha256 = (Get-FileHash -LiteralPath $madnessPatchIni -Algorithm SHA256).Hash
    backupPath = $backupDll
    installMode = 'AliceCoop launcher + combined MadnessPatch client DLL'
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8

if ($StatusPath) {
    "Alice Co-op installed into $coopPath" |
        Set-Content -LiteralPath $StatusPath -Encoding UTF8
}

Write-Host "AliceCoop installed into: $coopPath"
Write-Host 'Start AliceCoopLauncher.exe and choose Host Game or Join Game.'
