[CmdletBinding()]
param(
    [string]$GameRoot
)

$ErrorActionPreference = 'Stop'
$packageRoot = Split-Path -Parent $PSScriptRoot
$payloadPath = Join-Path $packageRoot 'payload'

function Test-GameRoot([string]$Path) {
    if (-not $Path) {
        return $false
    }
    $candidate = [System.IO.Path]::GetFullPath($Path)
    return Test-Path -LiteralPath (Join-Path $candidate 'Binaries\Win32\AliceMadnessReturns.exe') -PathType Leaf
}

if (-not $GameRoot) {
    $candidates = @(
        $packageRoot,
        (Split-Path -Parent $packageRoot),
        (Split-Path -Parent (Split-Path -Parent $packageRoot))
    )
    foreach ($candidate in $candidates) {
        if (Test-GameRoot $candidate) {
            $GameRoot = $candidate
            break
        }
    }
}
if (-not (Test-GameRoot $GameRoot)) {
    $GameRoot = Read-Host 'Enter the Alice game root (the folder containing Binaries)'
}
if (-not (Test-GameRoot $GameRoot)) {
    throw 'AliceMadnessReturns.exe was not found under Binaries\Win32.'
}

$gameRootPath = [System.IO.Path]::GetFullPath($GameRoot)
$win32Path = Join-Path $gameRootPath 'Binaries\Win32'
$gameExe = Join-Path $win32Path 'AliceMadnessReturns.exe'
$targetDll = Join-Path $win32Path 'dinput8.dll'
$madnessPatchIni = Join-Path $win32Path 'MadnessPatch.ini'
$coopPath = Join-Path $win32Path 'AliceCoop'
$backupPath = Join-Path $coopPath 'backup'
$backupDll = Join-Path $backupPath 'dinput8.before-alicecoop.dll'
$manifestPath = Join-Path $coopPath 'install-manifest.json'
$packageManifestPath = Join-Path $packageRoot 'package-manifest.json'

if (Get-Process AliceMadnessReturns -ErrorAction SilentlyContinue) {
    throw 'Close every AliceMadnessReturns.exe process before installing.'
}
if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) {
    throw "Game executable not found: $gameExe"
}
$requiredPayload = @(
    'dinput8.dll',
    'AliceCoopServer.exe',
    'AliceCoop.ini',
    'MadnessPatch.ini',
    'README.md',
    'README_RU.md',
    'KNOWN_ISSUES.md',
    'Uninstall-AliceCoop.ps1',
    'Uninstall-AliceCoop.bat',
    'AliceCoop-LaunchConfig.bat',
    'AliceCoop-Server.bat',
    'AliceCoop-Host.bat',
    'AliceCoop-Client.bat',
    'AliceCoop-Both.bat',
    'AliceCoop-Diagnostic-Both.bat',
    'Get-PhysicalScreenWidth.ps1',
    'images\cutsceneWatch2.png',
    'images\aliceWhait.png',
    'images\aliceSoloLevel.png'
)
foreach ($relativePath in $requiredPayload) {
    $source = Join-Path $payloadPath $relativePath
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Package payload is incomplete: $relativePath"
    }
}

New-Item -ItemType Directory -Force -Path `
    $coopPath, `
    $backupPath, `
    (Join-Path $coopPath 'logs'), `
    (Join-Path $coopPath 'client-saves'), `
    (Join-Path $coopPath 'images') | Out-Null

$hadPreviousDinput8 = Test-Path -LiteralPath $targetDll -PathType Leaf
$existingHash = if ($hadPreviousDinput8) {
    (Get-FileHash -LiteralPath $targetDll -Algorithm SHA256).Hash
} else {
    $null
}
$hadPreviousMadnessPatchIni = Test-Path -LiteralPath $madnessPatchIni -PathType Leaf
$newDll = Join-Path $payloadPath 'dinput8.dll'
$newHash = (Get-FileHash -LiteralPath $newDll -Algorithm SHA256).Hash

# The first AliceCoop install owns the baseline backup. Updates must never
# replace it with an older AliceCoop combined DLL.
if ($hadPreviousDinput8 -and -not (Test-Path -LiteralPath $backupDll -PathType Leaf)) {
    Copy-Item -LiteralPath $targetDll -Destination $backupDll
}

$existingCoopIni = Join-Path $coopPath 'AliceCoop.ini'
if (Test-Path -LiteralPath $existingCoopIni -PathType Leaf) {
    Copy-Item -LiteralPath $existingCoopIni `
        -Destination (Join-Path $backupPath 'AliceCoop.before-update.ini') -Force
}

Copy-Item -LiteralPath $newDll -Destination $targetDll -Force
if (-not $hadPreviousMadnessPatchIni) {
    Copy-Item -LiteralPath (Join-Path $payloadPath 'MadnessPatch.ini') `
        -Destination $madnessPatchIni
}
foreach ($name in @(
    'AliceCoopServer.exe',
    'README.md',
    'README_RU.md',
    'KNOWN_ISSUES.md',
    'Uninstall-AliceCoop.ps1',
    'Uninstall-AliceCoop.bat',
    'AliceCoop-Server.bat',
    'AliceCoop-Host.bat',
    'AliceCoop-Client.bat',
    'AliceCoop-Both.bat',
    'AliceCoop-Diagnostic-Both.bat',
    'Get-PhysicalScreenWidth.ps1'
)) {
    Copy-Item -LiteralPath (Join-Path $payloadPath $name) `
        -Destination (Join-Path $coopPath $name) -Force
}
if (-not (Test-Path -LiteralPath $existingCoopIni -PathType Leaf)) {
    Copy-Item -LiteralPath (Join-Path $payloadPath 'AliceCoop.ini') `
        -Destination $existingCoopIni
}
if (-not (Test-Path -LiteralPath (Join-Path $coopPath 'AliceCoop-LaunchConfig.bat') -PathType Leaf)) {
    Copy-Item -LiteralPath (Join-Path $payloadPath 'AliceCoop-LaunchConfig.bat') `
        -Destination (Join-Path $coopPath 'AliceCoop-LaunchConfig.bat')
}
foreach ($name in @('cutsceneWatch2.png', 'aliceWhait.png', 'aliceSoloLevel.png')) {
    Copy-Item -LiteralPath (Join-Path $payloadPath "images\$name") `
        -Destination (Join-Path $coopPath 'images') -Force
}

$packageManifest = $null
if (Test-Path -LiteralPath $packageManifestPath -PathType Leaf) {
    $packageManifest = Get-Content -LiteralPath $packageManifestPath -Raw |
        ConvertFrom-Json
}
$manifest = [ordered]@{
    schemaVersion = 2
    installedAtUtc = [DateTime]::UtcNow.ToString('o')
    packageVersion = if ($packageManifest) { $packageManifest.version } else { 'unknown' }
    madnessPatchBase = '3.1.1'
    gameRoot = $gameRootPath
    hadPreviousDinput8 = $hadPreviousDinput8
    hadPreviousMadnessPatchIni = $hadPreviousMadnessPatchIni
    previousDinput8Sha256 = $existingHash
    installedDinput8Sha256 = $newHash
    installedMadnessPatchIniSha256 = (Get-FileHash -LiteralPath $madnessPatchIni -Algorithm SHA256).Hash
    backupPath = $backupDll
    installMode = 'Standalone MadnessPatch 3.1.1 + AliceCoop combined DLL'
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host ''
Write-Host "AliceCoop installed into: $coopPath"
Write-Host 'MadnessPatch 3.1.1 is included in the combined AliceCoop DLL.'
if ($hadPreviousMadnessPatchIni) {
    Write-Host 'The existing MadnessPatch.ini was preserved.'
}
if ($hadPreviousDinput8) {
    Write-Host "Previous dinput8.dll backup: $backupDll"
}
Write-Host ''
Write-Host 'Edit AliceCoop-LaunchConfig.bat once to set IP, port and display mode.'
Write-Host 'Host:   AliceCoop-Server.bat, then AliceCoop-Host.bat'
Write-Host 'Client: AliceCoop-Client.bat'
