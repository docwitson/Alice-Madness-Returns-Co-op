[CmdletBinding()]
param(
    [string]$Version = '0.1.0-alpha.2',
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$SkipBuild,
    [switch]$RequireCleanSource
)

$ErrorActionPreference = 'Stop'

function Get-AliceCoopProtocolVersion {
    param([Parameter(Mandatory)][string]$ServerPath)
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $ServerPath
    $startInfo.Arguments = '--protocol-version'
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw 'Failed to query the protocol version.' }
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0 -or $stderr.Length -ne 0 -or
        $stdout -notmatch '^(\d+)\r?\n$') {
        throw "Invalid protocol-version response: '$stdout' '$stderr'"
    }
    return [int]$Matches[1]
}

function Reset-StagingPath {
    param([Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$AllowedRoot)
    $resolved = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetFullPath($AllowedRoot)
    if (-not $resolved.StartsWith(
        $root + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe staging path: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}

function Write-Checksums {
    param([Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$Destination)
    Get-ChildItem -LiteralPath $Root -Recurse -File |
        Where-Object FullName -ne $Destination |
        Sort-Object FullName |
        ForEach-Object {
            $relative = $_.FullName.Substring($Root.Length + 1)
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            "$hash  $relative"
        } | Set-Content -LiteralPath $Destination -Encoding ASCII
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceStatus = (& git -C $repoRoot status --porcelain | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Unable to query the source worktree status.' }
if ($RequireCleanSource -and $sourceStatus.Length -ne 0) {
    throw 'Packaging requires a clean source worktree.'
}

if (-not $SkipBuild) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
        -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if (-not $msbuild) { throw 'MSBuild was not found.' }
    & $msbuild (Join-Path $repoRoot 'AliceCoop.sln') /m /t:Build `
        "/p:Configuration=$Configuration" /p:Platform=x86
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed: $LASTEXITCODE" }
}

$buildPath = Join-Path $repoRoot "bin\$Configuration"
$builtDll = Join-Path $buildPath 'dinput8.dll'
$builtServer = Join-Path $buildPath 'AliceCoopServer.exe'
$builtLauncher = Join-Path $buildPath 'AliceCoopLauncher.exe'
foreach ($required in @($builtDll, $builtServer, $builtLauncher)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required build output is missing: $required"
    }
}
& $builtServer --self-test
if ($LASTEXITCODE -ne 0) { throw 'AliceCoopServer self-test failed.' }
$launcherTest = Start-Process -FilePath $builtLauncher -ArgumentList '--self-test' `
    -Wait -PassThru
if ($launcherTest.ExitCode -ne 0) { throw 'AliceCoopLauncher self-test failed.' }
$protocolVersion = Get-AliceCoopProtocolVersion -ServerPath $builtServer

$artifactsRoot = Join-Path $repoRoot 'artifacts\deploy'
$installerName = "AliceCoop-$Version-installer"
$installerRoot = Join-Path $artifactsRoot $installerName
$installerArchive = Join-Path $artifactsRoot "$installerName.zip"
$dropInName = "AliceCoop-$Version-drop-in"
$dropInRoot = Join-Path $artifactsRoot $dropInName
$dropInArchive = Join-Path $artifactsRoot "$dropInName.zip"
$archiveChecksums = Join-Path $artifactsRoot "AliceCoop-$Version-SHA256SUMS.txt"
New-Item -ItemType Directory -Force -Path $artifactsRoot | Out-Null
Reset-StagingPath -Path $installerRoot -AllowedRoot $artifactsRoot
Reset-StagingPath -Path $dropInRoot -AllowedRoot $artifactsRoot
foreach ($archive in @($installerArchive, $dropInArchive)) {
    if (Test-Path -LiteralPath $archive -PathType Leaf) {
        Remove-Item -LiteralPath $archive -Force
    }
}

$advanced = Join-Path $installerRoot 'Advanced'
$payload = Join-Path $advanced 'Payload'
$payloadImages = Join-Path $payload 'Images'
$payloadManual = Join-Path $payload 'Manual'
$documentation = Join-Path $advanced 'Documentation'
$tools = Join-Path $advanced 'Tools'
$licenses = Join-Path $advanced 'Licenses'
New-Item -ItemType Directory -Force -Path $installerRoot, $advanced,
    $payload, $payloadImages, $payloadManual, $documentation, $tools,
    $licenses | Out-Null

# The archive root contains only the user-facing executables and Advanced.
Copy-Item -LiteralPath $builtLauncher -Destination $installerRoot
Copy-Item -LiteralPath $builtServer -Destination $installerRoot
Copy-Item -LiteralPath $builtDll -Destination $payload
Copy-Item -LiteralPath (Join-Path $repoRoot 'AliceCoop.ini') -Destination $payload
Copy-Item -LiteralPath (Join-Path $repoRoot 'client\MadnessPatch.ini') -Destination $payload
$Version | Set-Content -LiteralPath (Join-Path $payload 'AliceCoop.version') `
    -Encoding ASCII

foreach ($name in @('cutsceneWatch2.png', 'aliceWhait.png', 'aliceSoloLevel.png')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "images\$name") -Destination $payloadImages
}
foreach ($name in @(
    'AliceCoop-LaunchConfig.bat', 'AliceCoop-Server.bat',
    'AliceCoop-Host.bat', 'AliceCoop-Client.bat', 'AliceCoop-Both.bat',
    'AliceCoop-Diagnostic-Both.bat', 'AliceCoop-Animation-Test.bat',
    'Get-PhysicalScreenWidth.ps1'
)) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "manual-launch\$name") `
        -Destination $payloadManual
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination $documentation
Copy-Item -LiteralPath (Join-Path $repoRoot 'README_RU.md') -Destination $documentation
foreach ($name in @('CONFIGURATION.md', 'DEVELOPMENT.md', 'INSTALL.md',
    'INSTALL_RU.md', 'MANUAL_INSTALL.md', 'MANUAL_INSTALL_RU.md',
    'KNOWN_ISSUES.md', 'SMOKE_TEST.md', 'TROUBLESHOOTING.md')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "docs\$name") -Destination $documentation
}
foreach ($name in @('LICENSE', 'NOTICE.md', 'THIRD_PARTY_NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $licenses
}
Get-ChildItem -LiteralPath (Join-Path $repoRoot 'third_party\licenses') -Force |
    Copy-Item -Destination $licenses -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'tools\Install-AliceCoop-Package.ps1') `
    -Destination $tools
Copy-Item -LiteralPath (Join-Path $repoRoot 'tools\Uninstall-AliceCoop.ps1') `
    -Destination $tools
Copy-Item -LiteralPath (Join-Path $repoRoot 'tools\Uninstall-AliceCoop.bat') `
    -Destination $tools

$commit = (& git -C $repoRoot rev-parse --short HEAD | Out-String).Trim()
$manifest = [ordered]@{
    schemaVersion = 2
    name = 'AliceCoop'
    version = $Version
    architecture = 'Win32'
    launcherFramework = '.NET Framework 4.8'
    protocolVersion = $protocolVersion
    buildUtc = [DateTime]::UtcNow.ToString('o')
    sourceCommit = $commit
    sourceDirty = $sourceStatus.Length -gt 0
    madnessPatchBase = '3.1.1'
    installMode = 'External launcher + AliceCoop runtime payload'
    license = 'GPL-2.0-only'
    sourceRepository = 'https://github.com/docwitson/Alice-Madness-Returns-Co-op'
    dinput8Sha256 = (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash
    serverSha256 = (Get-FileHash -LiteralPath $builtServer -Algorithm SHA256).Hash
    launcherSha256 = (Get-FileHash -LiteralPath $builtLauncher -Algorithm SHA256).Hash
}
$manifestPath = Join-Path $advanced 'package-manifest.json'
$manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8
$sourceNotice = @"
Alice Co-op $Version corresponding source code

Repository: https://github.com/docwitson/Alice-Madness-Returns-Co-op
Release tag: https://github.com/docwitson/Alice-Madness-Returns-Co-op/tree/v$Version

Alice Co-op is distributed under GNU GPL version 2.0. The tagged repository
contains the source and build scripts corresponding to the release binaries.
"@
$sourceNotice | Set-Content -LiteralPath (Join-Path $advanced 'SOURCE_CODE.txt') `
    -Encoding UTF8
$installerChecksums = Join-Path $advanced 'SHA256SUMS.txt'
Write-Checksums -Root $installerRoot -Destination $installerChecksums
Compress-Archive -LiteralPath $installerRoot -DestinationPath $installerArchive `
    -CompressionLevel Optimal

# Drop-in root is exactly Binaries\Win32: two proxy files and AliceCoop.
$dropCoop = Join-Path $dropInRoot 'AliceCoop'
$dropAdvanced = Join-Path $dropCoop 'Advanced'
New-Item -ItemType Directory -Force -Path $dropCoop, $dropAdvanced | Out-Null
Copy-Item -LiteralPath $builtDll -Destination $dropInRoot
Copy-Item -LiteralPath (Join-Path $repoRoot 'client\MadnessPatch.ini') -Destination $dropInRoot
Copy-Item -LiteralPath $builtLauncher -Destination $dropCoop
Copy-Item -LiteralPath $builtServer -Destination $dropCoop
Copy-Item -LiteralPath (Join-Path $payload 'AliceCoop.ini') -Destination $dropCoop
Copy-Item -LiteralPath (Join-Path $payload 'AliceCoop.version') -Destination $dropCoop
Copy-Item -LiteralPath $payloadImages -Destination (Join-Path $dropCoop 'images') -Recurse
Copy-Item -LiteralPath $payloadManual -Destination (Join-Path $dropAdvanced 'Manual') -Recurse
Copy-Item -LiteralPath $documentation -Destination (Join-Path $dropAdvanced 'Documentation') -Recurse
Copy-Item -LiteralPath $licenses -Destination (Join-Path $dropAdvanced 'Licenses') -Recurse
New-Item -ItemType Directory -Force -Path (Join-Path $dropAdvanced 'Tools') | Out-Null
Copy-Item -LiteralPath (Join-Path $tools 'Uninstall-AliceCoop.ps1') `
    -Destination (Join-Path $dropAdvanced 'Tools')
Copy-Item -LiteralPath (Join-Path $tools 'Uninstall-AliceCoop.bat') `
    -Destination (Join-Path $dropAdvanced 'Tools')
Copy-Item -LiteralPath $manifestPath -Destination $dropAdvanced
Copy-Item -LiteralPath (Join-Path $advanced 'SOURCE_CODE.txt') -Destination $dropAdvanced
$dropChecksums = Join-Path $dropAdvanced 'SHA256SUMS.txt'
Write-Checksums -Root $dropInRoot -Destination $dropChecksums
Compress-Archive -Path (Join-Path $dropInRoot '*') -DestinationPath $dropInArchive `
    -CompressionLevel Optimal

$installerHash = (Get-FileHash -LiteralPath $installerArchive -Algorithm SHA256).Hash
$dropHash = (Get-FileHash -LiteralPath $dropInArchive -Algorithm SHA256).Hash
@(
    "$installerHash  $([System.IO.Path]::GetFileName($installerArchive))"
    "$dropHash  $([System.IO.Path]::GetFileName($dropInArchive))"
) | Set-Content -LiteralPath $archiveChecksums -Encoding ASCII

Write-Host ''
Write-Host "Installer package: $installerArchive"
Write-Host "SHA256:           $installerHash"
Write-Host "Drop-in package:  $dropInArchive"
Write-Host "SHA256:           $dropHash"
Write-Host "Archive checksums: $archiveChecksums"
