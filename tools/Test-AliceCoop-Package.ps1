[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Version,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$RequireCleanSource
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$artifactsRoot = Join-Path $repoRoot 'artifacts\deploy'
$installerArchive = Join-Path $artifactsRoot "AliceCoop-$Version.zip"
$dropInArchive = Join-Path $artifactsRoot "AliceCoop-$Version-drop-in.zip"
$outerChecksums = Join-Path $artifactsRoot "AliceCoop-$Version-SHA256SUMS.txt"
$builtDll = Join-Path $repoRoot "bin\$Configuration\dinput8.dll"
$builtServer = Join-Path $repoRoot "bin\$Configuration\AliceCoopServer.exe"

function Assert-Condition {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
}

function Get-RelativeFileList {
    param([string]$Root)
    return @(Get-ChildItem -LiteralPath $Root -Recurse -File |
        ForEach-Object {
            $_.FullName.Substring($Root.Length + 1).Replace('/', '\')
        } | Sort-Object)
}

function Assert-ExactFileList {
    param([string]$Root, [string[]]$Expected)
    $actual = @(Get-RelativeFileList -Root $Root)
    $expectedSorted = @($Expected | Sort-Object -Unique)
    $difference = @(Compare-Object -ReferenceObject $expectedSorted -DifferenceObject $actual)
    if ($difference.Count -ne 0) {
        $text = ($difference | ForEach-Object { "$($_.SideIndicator) $($_.InputObject)" }) -join '; '
        throw "Unexpected package file list under '$Root': $text"
    }
}

function Assert-ChecksumFile {
    param([string]$Root, [string]$ChecksumPath)
    $entries = @{}
    foreach ($line in Get-Content -LiteralPath $ChecksumPath) {
        if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') {
            throw "Malformed checksum line in '$ChecksumPath': $line"
        }
        $relative = $Matches[2].Replace('/', '\')
        if ($entries.ContainsKey($relative)) {
            throw "Duplicate checksum entry: $relative"
        }
        $entries[$relative] = $Matches[1].ToUpperInvariant()
    }
    $files = @(Get-RelativeFileList -Root $Root |
        Where-Object { $_ -ne 'SHA256SUMS.txt' })
    Assert-Condition ($entries.Count -eq $files.Count) `
        "Checksum entry count does not match package contents in '$Root'."
    foreach ($relative in $files) {
        Assert-Condition ($entries.ContainsKey($relative)) "Missing checksum entry: $relative"
        $path = Join-Path $Root $relative
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        Assert-Condition ($actual -eq $entries[$relative]) "Checksum mismatch: $relative"
    }
}

function Get-ProtocolVersion {
    param([string]$ServerPath)
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $ServerPath
    $startInfo.Arguments = '--protocol-version'
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    Assert-Condition $process.Start() 'Failed to start protocol-version query.'
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    Assert-Condition ($process.ExitCode -eq 0) `
        "Protocol-version query failed with exit code $($process.ExitCode)."
    Assert-Condition ($stderr.Length -eq 0) "Protocol-version query wrote to stderr: $stderr"
    Assert-Condition ($stdout -match '^(\d+)\r?\n$') `
        "Protocol-version query returned invalid stdout: '$stdout'"
    return [int]$Matches[1]
}

foreach ($required in @(
    $installerArchive, $dropInArchive, $outerChecksums, $builtDll, $builtServer
)) {
    Assert-Condition (Test-Path -LiteralPath $required -PathType Leaf) `
        "Required file is missing: $required"
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("AliceCoop-package-test-" + [Guid]::NewGuid().ToString('N'))
try {
    $installerExtract = Join-Path $tempRoot 'installer'
    $dropInExtract = Join-Path $tempRoot 'drop-in'
    New-Item -ItemType Directory -Force -Path $installerExtract, $dropInExtract | Out-Null
    Expand-Archive -LiteralPath $installerArchive -DestinationPath $installerExtract
    Expand-Archive -LiteralPath $dropInArchive -DestinationPath $dropInExtract

    $installerRoot = Join-Path $installerExtract "AliceCoop-$Version"
    Assert-Condition (Test-Path -LiteralPath $installerRoot -PathType Container) `
        'Installer archive does not contain the expected root directory.'

    $licenseNames = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'third_party\licenses') `
        -Recurse -File | ForEach-Object {
            $_.FullName.Substring((Join-Path $repoRoot 'third_party\licenses').Length + 1).Replace('/', '\')
        })
    $payloadFiles = @(
        'AliceCoopServer.exe', 'AliceCoop.ini', 'MadnessPatch.ini',
        'README.md', 'README_RU.md', 'LICENSE', 'NOTICE.md',
        'THIRD_PARTY_NOTICES.md', 'Uninstall-AliceCoop.ps1',
        'Uninstall-AliceCoop.bat', 'AliceCoop-LaunchConfig.bat',
        'AliceCoop-Server.bat', 'AliceCoop-Both.bat',
        'AliceCoop-Diagnostic-Both.bat', 'AliceCoop-Animation-Test.bat',
        'AliceCoop-Host.bat', 'AliceCoop-Client.bat',
        'Get-PhysicalScreenWidth.ps1', 'dinput8.dll',
        'docs\CONFIGURATION.md', 'docs\DEVELOPMENT.md', 'docs\INSTALL.md',
        'docs\INSTALL_RU.md', 'docs\KNOWN_ISSUES.md',
        'docs\TROUBLESHOOTING.md', 'images\cutsceneWatch2.png',
        'images\aliceWhait.png', 'images\aliceSoloLevel.png'
    )
    $installerExpected = @(
        'INSTALL.md', 'INSTALL_RU.md', 'LICENSE', 'NOTICE.md',
        'THIRD_PARTY_NOTICES.md', 'SOURCE_CODE.txt', 'package-manifest.json',
        'SHA256SUMS.txt', 'tools\Install-AliceCoop-Package.ps1',
        'Install-AliceCoop.bat'
    ) + @($payloadFiles | ForEach-Object { "payload\$_" }) +
        @($licenseNames | ForEach-Object { "third-party-licenses\$_" })
    Assert-ExactFileList -Root $installerRoot -Expected $installerExpected

    $dropExpected = @(
        'dinput8.dll', 'MadnessPatch.ini', 'ALICECOOP_INSTALL.md',
        'ALICECOOP_INSTALL_RU.md', 'SHA256SUMS.txt',
        'AliceCoop\package-manifest.json', 'AliceCoop\SOURCE_CODE.txt'
    ) + @($payloadFiles | Where-Object {
            $_ -notin @('dinput8.dll', 'MadnessPatch.ini',
                'Uninstall-AliceCoop.ps1', 'Uninstall-AliceCoop.bat')
        } | ForEach-Object { "AliceCoop\$_" }) +
        @($licenseNames | ForEach-Object { "AliceCoop\third-party-licenses\$_" })
    Assert-ExactFileList -Root $dropInExtract -Expected $dropExpected

    foreach ($root in @($installerRoot, $dropInExtract)) {
        $forbidden = @(Get-ChildItem -LiteralPath $root -Recurse -File |
            Where-Object {
                $_.Extension -in @('.pdb', '.obj', '.log', '.sav', '.tmp') -or
                $_.Name -eq 'AliceMadnessReturns.exe'
            })
        Assert-Condition ($forbidden.Count -eq 0) `
            "Forbidden files found in package rooted at '$root'."
    }

    Assert-ChecksumFile -Root $installerRoot `
        -ChecksumPath (Join-Path $installerRoot 'SHA256SUMS.txt')
    Assert-ChecksumFile -Root $dropInExtract `
        -ChecksumPath (Join-Path $dropInExtract 'SHA256SUMS.txt')

    $installerManifestPath = Join-Path $installerRoot 'package-manifest.json'
    $dropManifestPath = Join-Path $dropInExtract 'AliceCoop\package-manifest.json'
    $installerManifestBytes = [System.IO.File]::ReadAllBytes($installerManifestPath)
    $dropManifestBytes = [System.IO.File]::ReadAllBytes($dropManifestPath)
    Assert-Condition ([System.Linq.Enumerable]::SequenceEqual(
        [byte[]]$installerManifestBytes, [byte[]]$dropManifestBytes)) `
        'Installer and drop-in manifests differ.'
    $manifest = Get-Content -LiteralPath $installerManifestPath -Raw | ConvertFrom-Json
    Assert-Condition ($manifest.schemaVersion -eq 1) 'Unexpected manifest schema version.'
    Assert-Condition ($manifest.name -eq 'AliceCoop') 'Unexpected manifest name.'
    Assert-Condition ($manifest.version -eq $Version) 'Manifest version mismatch.'
    Assert-Condition ($manifest.architecture -eq 'Win32') 'Manifest architecture mismatch.'
    Assert-Condition ($manifest.sourceCommit -match '^[0-9a-f]{7,40}$') `
        'Manifest source commit is malformed.'
    $currentCommit = (& git -C $repoRoot rev-parse --short HEAD | Out-String).Trim()
    Assert-Condition ($LASTEXITCODE -eq 0 -and $currentCommit.Length -ne 0) `
        'Unable to resolve the current source commit.'
    Assert-Condition ($manifest.sourceCommit -eq $currentCommit) `
        'Manifest source commit does not match the current source commit.'
    if ($RequireCleanSource) {
        Assert-Condition (-not $manifest.sourceDirty) 'Package was built from a dirty source tree.'
    }

    $protocolVersion = Get-ProtocolVersion -ServerPath $builtServer
    Assert-Condition ($manifest.protocolVersion -eq $protocolVersion) `
        'Manifest protocol version does not match the built server.'
    $builtDllHash = (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash
    $builtServerHash = (Get-FileHash -LiteralPath $builtServer -Algorithm SHA256).Hash
    Assert-Condition ($manifest.dinput8Sha256 -eq $builtDllHash) `
        'Manifest DLL hash does not match the built DLL.'
    Assert-Condition ($manifest.serverSha256 -eq $builtServerHash) `
        'Manifest server hash does not match the built server.'

    $packageDlls = @(
        (Join-Path $installerRoot 'payload\dinput8.dll'),
        (Join-Path $dropInExtract 'dinput8.dll')
    )
    $packageServers = @(
        (Join-Path $installerRoot 'payload\AliceCoopServer.exe'),
        (Join-Path $dropInExtract 'AliceCoop\AliceCoopServer.exe')
    )
    foreach ($path in $packageDlls) {
        Assert-Condition ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $builtDllHash) `
            "Packaged DLL does not match the built DLL: $path"
    }
    foreach ($path in $packageServers) {
        Assert-Condition ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $builtServerHash) `
            "Packaged server does not match the built server: $path"
    }

    $installSmokeRoot = Join-Path $tempRoot 'install-smoke'
    $installSmokeWin32 = Join-Path $installSmokeRoot 'Binaries\Win32'
    New-Item -ItemType Directory -Force -Path $installSmokeWin32 | Out-Null
    New-Item -ItemType File -Force `
        -Path (Join-Path $installSmokeWin32 'AliceMadnessReturns.exe') | Out-Null
    & (Join-Path $installerRoot 'tools\Install-AliceCoop-Package.ps1') `
        -GameRoot $installSmokeRoot
    $installedCoopRoot = Join-Path $installSmokeWin32 'AliceCoop'
    Assert-Condition ((Get-FileHash `
        -LiteralPath (Join-Path $installSmokeWin32 'dinput8.dll') `
        -Algorithm SHA256).Hash -eq $builtDllHash) `
        'Installer smoke test deployed the wrong DLL.'
    Assert-Condition ((Get-FileHash `
        -LiteralPath (Join-Path $installedCoopRoot 'AliceCoopServer.exe') `
        -Algorithm SHA256).Hash -eq $builtServerHash) `
        'Installer smoke test deployed the wrong server.'
    foreach ($relative in @(
        'AliceCoop.ini', 'AliceCoop-LaunchConfig.bat',
        'AliceCoop-Diagnostic-Both.bat', 'KNOWN_ISSUES.md',
        'install-manifest.json'
    )) {
        Assert-Condition (Test-Path -LiteralPath `
            (Join-Path $installedCoopRoot $relative) -PathType Leaf) `
            "Installer smoke test omitted: $relative"
    }

    $outerEntries = @{}
    foreach ($line in Get-Content -LiteralPath $outerChecksums) {
        if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') {
            throw "Malformed outer checksum line: $line"
        }
        $outerEntries[$Matches[2]] = $Matches[1].ToUpperInvariant()
    }
    Assert-Condition ($outerEntries.Count -eq 2) 'Outer checksum file must contain two entries.'
    foreach ($archive in @($installerArchive, $dropInArchive)) {
        $name = [System.IO.Path]::GetFileName($archive)
        Assert-Condition ($outerEntries.ContainsKey($name)) "Missing outer checksum: $name"
        Assert-Condition ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash `
            -eq $outerEntries[$name]) "Outer checksum mismatch: $name"
    }

    Write-Host "AliceCoop $Version packages passed verification."
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
