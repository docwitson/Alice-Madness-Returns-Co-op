[CmdletBinding()]
param(
    [string]$Version = '0.1.0-alpha.1',
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
    if (-not $process.Start()) {
        throw 'Failed to start AliceCoopServer protocol-version query.'
    }
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "AliceCoopServer --protocol-version failed with exit code $($process.ExitCode)."
    }
    if ($stderr.Length -ne 0) {
        throw "AliceCoopServer --protocol-version wrote to stderr: $stderr"
    }
    if ($stdout -notmatch '^(\d+)\r?\n$') {
        throw "AliceCoopServer --protocol-version returned invalid stdout: '$stdout'"
    }
    return [int]$Matches[1]
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceStatus = (& git -C $repoRoot status --porcelain | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to query the source worktree status.'
}
if ($RequireCleanSource -and $sourceStatus.Length -ne 0) {
    throw 'Packaging requires a clean source worktree.'
}
$artifactsRoot = Join-Path $repoRoot 'artifacts\deploy'
$packageName = "AliceCoop-$Version"
$stagingPath = Join-Path $artifactsRoot $packageName
$archivePath = Join-Path $artifactsRoot "$packageName.zip"
$dropInName = "$packageName-drop-in"
$dropInStagingPath = Join-Path $artifactsRoot $dropInName
$dropInArchivePath = Join-Path $artifactsRoot "$dropInName.zip"
$archiveChecksumsPath = Join-Path $artifactsRoot "AliceCoop-$Version-SHA256SUMS.txt"
$payloadPath = Join-Path $stagingPath 'payload'
$payloadDocsPath = Join-Path $payloadPath 'docs'

$resolvedArtifactsRoot = [System.IO.Path]::GetFullPath($artifactsRoot)
$resolvedStagingPath = [System.IO.Path]::GetFullPath($stagingPath)
if (-not $resolvedStagingPath.StartsWith(
    $resolvedArtifactsRoot + [System.IO.Path]::DirectorySeparatorChar,
    [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe staging path: $resolvedStagingPath"
}
$resolvedDropInStagingPath = [System.IO.Path]::GetFullPath($dropInStagingPath)
if (-not $resolvedDropInStagingPath.StartsWith(
    $resolvedArtifactsRoot + [System.IO.Path]::DirectorySeparatorChar,
    [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe drop-in staging path: $resolvedDropInStagingPath"
}

if (-not $SkipBuild) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'Visual Studio Installer (vswhere.exe) was not found.'
    }
    $msbuild = & $vswhere -latest -products * `
        -requires Microsoft.Component.MSBuild `
        -find 'MSBuild\**\Bin\MSBuild.exe' |
        Select-Object -First 1
    if (-not $msbuild) {
        throw 'MSBuild was not found.'
    }
    & $msbuild (Join-Path $repoRoot 'AliceCoop.sln') /m /t:Build `
        "/p:Configuration=$Configuration" /p:Platform=x86
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE."
    }
}

$buildPath = Join-Path $repoRoot "bin\$Configuration"
$builtDll = Join-Path $buildPath 'dinput8.dll'
$builtServer = Join-Path $buildPath 'AliceCoopServer.exe'
foreach ($requiredFile in @($builtDll, $builtServer)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required build output is missing: $requiredFile"
    }
}

& $builtServer --self-test
if ($LASTEXITCODE -ne 0) {
    throw "AliceCoopServer self-test failed with exit code $LASTEXITCODE."
}
$protocolVersion = Get-AliceCoopProtocolVersion -ServerPath $builtServer

New-Item -ItemType Directory -Force -Path $artifactsRoot | Out-Null
if (Test-Path -LiteralPath $stagingPath) {
    Remove-Item -LiteralPath $stagingPath -Recurse -Force
}
if (Test-Path -LiteralPath $archivePath -PathType Leaf) {
    Remove-Item -LiteralPath $archivePath -Force
}
if (Test-Path -LiteralPath $dropInStagingPath) {
    Remove-Item -LiteralPath $dropInStagingPath -Recurse -Force
}
if (Test-Path -LiteralPath $dropInArchivePath -PathType Leaf) {
    Remove-Item -LiteralPath $dropInArchivePath -Force
}
New-Item -ItemType Directory -Force -Path `
    $payloadPath, $payloadDocsPath, (Join-Path $payloadPath 'images'), `
    (Join-Path $stagingPath 'tools') | Out-Null

Copy-Item -LiteralPath $builtDll -Destination (Join-Path $payloadPath 'dinput8.dll')
Copy-Item -LiteralPath $builtServer -Destination (Join-Path $payloadPath 'AliceCoopServer.exe')
Copy-Item -LiteralPath (Join-Path $repoRoot 'AliceCoop.ini') -Destination $payloadPath
Copy-Item -LiteralPath (Join-Path $repoRoot 'client\MadnessPatch.ini') -Destination $payloadPath
Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') `
    -Destination (Join-Path $payloadPath 'README.md')
Copy-Item -LiteralPath (Join-Path $repoRoot 'README_RU.md') `
    -Destination (Join-Path $payloadPath 'README_RU.md')
foreach ($name in @(
    'CONFIGURATION.md',
    'DEVELOPMENT.md',
    'INSTALL.md',
    'INSTALL_RU.md',
    'KNOWN_ISSUES.md',
    'SMOKE_TEST.md',
    'TROUBLESHOOTING.md'
)) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "docs\$name") `
        -Destination $payloadDocsPath
}
foreach ($name in @('LICENSE', 'NOTICE.md', 'THIRD_PARTY_NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $payloadPath
}

# Keep release packages compact while preserving the README artwork links.
# Promotional media remains in the tagged source tree instead of being copied
# into the game installation archive.
$taggedMediaRoot = "https://raw.githubusercontent.com/docwitson/" +
    "Alice-Madness-Returns-Co-op/v$Version/docs/media/"
foreach ($name in @('README.md', 'README_RU.md')) {
    $readmePath = Join-Path $payloadPath $name
    $readmeText = [System.IO.File]::ReadAllText($readmePath)
    $readmeText = $readmeText.Replace('(docs/media/', "($taggedMediaRoot")
    [System.IO.File]::WriteAllText(
        $readmePath,
        $readmeText,
        [System.Text.UTF8Encoding]::new($false))
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'tools\Uninstall-AliceCoop.ps1') `
    -Destination $payloadPath
Copy-Item -LiteralPath (Join-Path $repoRoot 'tools\Uninstall-AliceCoop.bat') `
    -Destination $payloadPath
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $stagingPath
Copy-Item -LiteralPath (Join-Path $repoRoot 'NOTICE.md') -Destination $stagingPath
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') -Destination $stagingPath
Copy-Item -LiteralPath (Join-Path $repoRoot 'third_party\licenses') `
    -Destination (Join-Path $stagingPath 'third-party-licenses') -Recurse
$sourceNotice = @"
Alice Co-op $Version corresponding source code

Repository: https://github.com/docwitson/Alice-Madness-Returns-Co-op
Release tag: https://github.com/docwitson/Alice-Madness-Returns-Co-op/tree/v$Version

Alice Co-op is distributed under GNU GPL version 2.0. The tagged repository
contains the source and build scripts corresponding to the release binaries.
"@
$sourceNotice | Set-Content -LiteralPath `
    (Join-Path $stagingPath 'SOURCE_CODE.txt') -Encoding UTF8

foreach ($name in @(
    'AliceCoop-LaunchConfig.bat',
    'AliceCoop-Server.bat',
    'AliceCoop-Both.bat',
    'AliceCoop-Diagnostic-Both.bat',
    'AliceCoop-Animation-Test.bat',
    'Get-PhysicalScreenWidth.ps1'
)) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "manual-launch\$name") `
        -Destination $payloadPath
}
foreach ($name in @('AliceCoop-Host.bat', 'AliceCoop-Client.bat')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "manual-launch\$name") `
        -Destination $payloadPath
}
foreach ($name in @('cutsceneWatch2.png', 'aliceWhait.png', 'aliceSoloLevel.png')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "images\$name") `
        -Destination (Join-Path $payloadPath 'images')
}

Copy-Item -LiteralPath (Join-Path $repoRoot 'tools\Install-AliceCoop-Package.ps1') `
    -Destination (Join-Path $stagingPath 'tools')
Copy-Item -LiteralPath (Join-Path $repoRoot 'tools\Install-AliceCoop-Package.bat') `
    -Destination (Join-Path $stagingPath 'Install-AliceCoop.bat')
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\INSTALL.md') `
    -Destination (Join-Path $stagingPath 'INSTALL.md')
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\INSTALL_RU.md') `
    -Destination (Join-Path $stagingPath 'INSTALL_RU.md')

$commit = & git -C $repoRoot rev-parse --short HEAD
$dirty = $sourceStatus.Length -gt 0
$manifest = [ordered]@{
    schemaVersion = 1
    name = 'AliceCoop'
    version = $Version
    architecture = 'Win32'
    protocolVersion = $protocolVersion
    buildUtc = [DateTime]::UtcNow.ToString('o')
    sourceCommit = $commit
    sourceDirty = $dirty
    madnessPatchBase = '3.1.1'
    installMode = 'Standalone MadnessPatch 3.1.1 + AliceCoop combined DLL'
    license = 'GPL-2.0-only'
    sourceRepository = 'https://github.com/docwitson/Alice-Madness-Returns-Co-op'
    dinput8Sha256 = (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash
    serverSha256 = (Get-FileHash -LiteralPath $builtServer -Algorithm SHA256).Hash
}
$manifest | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $stagingPath 'package-manifest.json') -Encoding UTF8

$hashLines = Get-ChildItem -LiteralPath $stagingPath -Recurse -File |
    Where-Object Name -ne 'SHA256SUMS.txt' |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($stagingPath.Length + 1)
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        "$hash  $relative"
    }
$hashLines | Set-Content -LiteralPath (Join-Path $stagingPath 'SHA256SUMS.txt') -Encoding ASCII

Compress-Archive -LiteralPath $stagingPath -DestinationPath $archivePath `
    -CompressionLevel Optimal

# Direct-copy package. Its archive root is the game's Binaries\Win32 directory:
# dinput8.dll and MadnessPatch.ini sit next to AliceMadnessReturns.exe, while
# co-op launchers and data live in the AliceCoop subdirectory.
$dropInCoopPath = Join-Path $dropInStagingPath 'AliceCoop'
New-Item -ItemType Directory -Force -Path $dropInCoopPath | Out-Null
Copy-Item -LiteralPath (Join-Path $payloadPath 'dinput8.dll') -Destination $dropInStagingPath
Copy-Item -LiteralPath (Join-Path $payloadPath 'MadnessPatch.ini') -Destination $dropInStagingPath
Get-ChildItem -LiteralPath $payloadPath -Force |
    Where-Object Name -notin @(
        'dinput8.dll',
        'MadnessPatch.ini',
        'Uninstall-AliceCoop.ps1',
        'Uninstall-AliceCoop.bat'
    ) |
    Copy-Item -Destination $dropInCoopPath -Recurse
Copy-Item -LiteralPath (Join-Path $stagingPath 'package-manifest.json') -Destination $dropInCoopPath
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\INSTALL.md') `
    -Destination (Join-Path $dropInStagingPath 'ALICECOOP_INSTALL.md')
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\INSTALL_RU.md') `
    -Destination (Join-Path $dropInStagingPath 'ALICECOOP_INSTALL_RU.md')
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $dropInCoopPath
Copy-Item -LiteralPath (Join-Path $repoRoot 'NOTICE.md') -Destination $dropInCoopPath
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') -Destination $dropInCoopPath
Copy-Item -LiteralPath (Join-Path $stagingPath 'third-party-licenses') `
    -Destination (Join-Path $dropInCoopPath 'third-party-licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $stagingPath 'SOURCE_CODE.txt') -Destination $dropInCoopPath

$dropInHashes = Get-ChildItem -LiteralPath $dropInStagingPath -Recurse -File |
    Where-Object Name -ne 'SHA256SUMS.txt' |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($dropInStagingPath.Length + 1)
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        "$hash  $relative"
    }
$dropInHashes | Set-Content -LiteralPath (Join-Path $dropInStagingPath 'SHA256SUMS.txt') -Encoding ASCII
Compress-Archive -Path (Join-Path $dropInStagingPath '*') -DestinationPath $dropInArchivePath `
    -CompressionLevel Optimal

$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
$dropInArchiveHash = (Get-FileHash -LiteralPath $dropInArchivePath -Algorithm SHA256).Hash
@(
    "$archiveHash  $([System.IO.Path]::GetFileName($archivePath))"
    "$dropInArchiveHash  $([System.IO.Path]::GetFileName($dropInArchivePath))"
) | Set-Content -LiteralPath $archiveChecksumsPath -Encoding ASCII
Write-Host ''
Write-Host "Installer package: $archivePath"
Write-Host "SHA256:           $archiveHash"
Write-Host "Drop-in package:  $dropInArchivePath"
Write-Host "SHA256:           $dropInArchiveHash"
Write-Host "Archive checksums: $archiveChecksumsPath"
