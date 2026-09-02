[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Version,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$RequireCleanSource
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$artifactsRoot = Join-Path $repoRoot 'artifacts\deploy'
$installerArchive = Join-Path $artifactsRoot "AliceCoop-$Version-installer.zip"
$dropInArchive = Join-Path $artifactsRoot "AliceCoop-$Version-drop-in.zip"
$outerChecksums = Join-Path $artifactsRoot "AliceCoop-$Version-SHA256SUMS.txt"
$builtDll = Join-Path $repoRoot "bin\$Configuration\dinput8.dll"
$builtServer = Join-Path $repoRoot "bin\$Configuration\AliceCoopServer.exe"
$builtLauncher = Join-Path $repoRoot "bin\$Configuration\AliceCoopLauncher.exe"

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Get-RelativeFileList([string]$Root) {
    return @(Get-ChildItem -LiteralPath $Root -Recurse -File |
        ForEach-Object { $_.FullName.Substring($Root.Length + 1).Replace('/', '\') } |
        Sort-Object)
}

function Assert-ExactFileList([string]$Root, [string[]]$Expected) {
    $difference = @(Compare-Object -ReferenceObject @($Expected | Sort-Object -Unique) `
        -DifferenceObject @(Get-RelativeFileList $Root))
    if ($difference.Count -ne 0) {
        $details = ($difference | ForEach-Object {
            "$($_.SideIndicator) $($_.InputObject)"
        }) -join '; '
        throw "Unexpected package contents: $details"
    }
}

function Assert-ChecksumFile([string]$Root, [string]$ChecksumPath) {
    $entries = @{}
    foreach ($line in Get-Content -LiteralPath $ChecksumPath) {
        if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') {
            throw "Malformed checksum line: $line"
        }
        $entries[$Matches[2].Replace('/', '\')] = $Matches[1].ToUpperInvariant()
    }
    $checksumRelative = $ChecksumPath.Substring($Root.Length + 1).Replace('/', '\')
    $files = @(Get-RelativeFileList $Root | Where-Object { $_ -ne $checksumRelative })
    Assert-Condition ($entries.Count -eq $files.Count) 'Checksum entry count mismatch.'
    foreach ($relative in $files) {
        Assert-Condition $entries.ContainsKey($relative) "Missing checksum: $relative"
        $actual = (Get-FileHash -LiteralPath (Join-Path $Root $relative) `
            -Algorithm SHA256).Hash
        Assert-Condition ($actual -eq $entries[$relative]) "Checksum mismatch: $relative"
    }
}

function Get-ProtocolVersion([string]$ServerPath) {
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
    Assert-Condition ($process.ExitCode -eq 0 -and $stderr.Length -eq 0 -and
        $stdout -match '^(\d+)\r?\n$') 'Invalid protocol-version response.'
    return [int]$Matches[1]
}

$requiredBuildFiles = @($installerArchive, $dropInArchive, $outerChecksums,
    $builtDll, $builtServer, $builtLauncher)
foreach ($required in $requiredBuildFiles) {
    Assert-Condition (Test-Path -LiteralPath $required -PathType Leaf) `
        "Required file is missing: $required"
}

$documentationNames = @('README.md', 'README_RU.md', 'CONFIGURATION.md',
    'DEVELOPMENT.md', 'INSTALL.md', 'INSTALL_RU.md', 'KNOWN_ISSUES.md',
    'SMOKE_TEST.md', 'TROUBLESHOOTING.md')
$manualNames = @('AliceCoop-LaunchConfig.bat', 'AliceCoop-Server.bat',
    'AliceCoop-Host.bat', 'AliceCoop-Client.bat', 'AliceCoop-Both.bat',
    'AliceCoop-Diagnostic-Both.bat', 'AliceCoop-Animation-Test.bat',
    'Get-PhysicalScreenWidth.ps1')
$imageNames = @('cutsceneWatch2.png', 'aliceWhait.png', 'aliceSoloLevel.png')
$licenseNames = @('LICENSE', 'NOTICE.md', 'THIRD_PARTY_NOTICES.md') +
    @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'third_party\licenses') -File |
        Select-Object -ExpandProperty Name)

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ('AliceCoop-package-test-' + [Guid]::NewGuid().ToString('N'))
try {
    $installerExtract = Join-Path $tempRoot 'installer'
    $dropExtract = Join-Path $tempRoot 'drop-in'
    New-Item -ItemType Directory -Force -Path $installerExtract, $dropExtract | Out-Null
    Expand-Archive -LiteralPath $installerArchive -DestinationPath $installerExtract
    Expand-Archive -LiteralPath $dropInArchive -DestinationPath $dropExtract
    $installerRoot = Join-Path $installerExtract "AliceCoop-$Version-installer"
    Assert-Condition (Test-Path -LiteralPath $installerRoot -PathType Container) `
        'Installer archive root is missing.'

    $installerExpected = @(
        'AliceCoopLauncher.exe', 'AliceCoopServer.exe',
        'Advanced\package-manifest.json',
        'Advanced\SOURCE_CODE.txt', 'Advanced\SHA256SUMS.txt',
        'Advanced\Payload\dinput8.dll', 'Advanced\Payload\AliceCoop.ini',
        'Advanced\Payload\MadnessPatch.ini',
        'Advanced\Tools\Install-AliceCoop-Package.ps1',
        'Advanced\Tools\Uninstall-AliceCoop.ps1',
        'Advanced\Tools\Uninstall-AliceCoop.bat'
    ) + @($documentationNames | ForEach-Object { "Advanced\Documentation\$_" }) +
        @($manualNames | ForEach-Object { "Advanced\Payload\Manual\$_" }) +
        @($imageNames | ForEach-Object { "Advanced\Payload\Images\$_" }) +
        @($licenseNames | ForEach-Object { "Advanced\Licenses\$_" })
    Assert-ExactFileList $installerRoot $installerExpected

    $dropExpected = @(
        'dinput8.dll', 'MadnessPatch.ini', 'AliceCoop\AliceCoop.ini',
        'AliceCoop\AliceCoopLauncher.exe',
        'AliceCoop\AliceCoopServer.exe',
        'AliceCoop\Advanced\package-manifest.json',
        'AliceCoop\Advanced\SOURCE_CODE.txt',
        'AliceCoop\Advanced\SHA256SUMS.txt',
        'AliceCoop\Advanced\Tools\Uninstall-AliceCoop.ps1',
        'AliceCoop\Advanced\Tools\Uninstall-AliceCoop.bat'
    ) + @($documentationNames | ForEach-Object {
            "AliceCoop\Advanced\Documentation\$_"
        }) + @($manualNames | ForEach-Object {
            "AliceCoop\Advanced\Manual\$_"
        }) + @($imageNames | ForEach-Object { "AliceCoop\images\$_" }) +
        @($licenseNames | ForEach-Object { "AliceCoop\Advanced\Licenses\$_" })
    Assert-ExactFileList $dropExtract $dropExpected

    foreach ($root in @($installerRoot, $dropExtract)) {
        $forbidden = @(Get-ChildItem -LiteralPath $root -Recurse -File |
            Where-Object { $_.Extension -in @('.pdb', '.obj', '.log', '.sav', '.tmp') -or
                $_.Name -eq 'AliceMadnessReturns.exe' })
        Assert-Condition ($forbidden.Count -eq 0) "Forbidden package files: $root"
    }

    Assert-ChecksumFile $installerRoot (Join-Path $installerRoot 'Advanced\SHA256SUMS.txt')
    Assert-ChecksumFile $dropExtract `
        (Join-Path $dropExtract 'AliceCoop\Advanced\SHA256SUMS.txt')

    $installerManifestPath = Join-Path $installerRoot 'Advanced\package-manifest.json'
    $dropManifestPath = Join-Path $dropExtract 'AliceCoop\Advanced\package-manifest.json'
    $installerManifestBytes = [System.IO.File]::ReadAllBytes($installerManifestPath)
    $dropManifestBytes = [System.IO.File]::ReadAllBytes($dropManifestPath)
    Assert-Condition ([System.Linq.Enumerable]::SequenceEqual(
        [byte[]]$installerManifestBytes, [byte[]]$dropManifestBytes)) `
        'Installer and drop-in manifests differ.'
    $manifest = Get-Content -LiteralPath $installerManifestPath -Raw | ConvertFrom-Json
    Assert-Condition ($manifest.schemaVersion -eq 2) 'Unexpected manifest schema.'
    Assert-Condition ($manifest.version -eq $Version) 'Manifest version mismatch.'
    Assert-Condition ($manifest.protocolVersion -eq (Get-ProtocolVersion $builtServer)) `
        'Manifest protocol version mismatch.'
    $currentCommit = (& git -C $repoRoot rev-parse --short HEAD | Out-String).Trim()
    Assert-Condition ($manifest.sourceCommit -eq $currentCommit) 'Manifest commit mismatch.'
    if ($RequireCleanSource) {
        Assert-Condition (-not $manifest.sourceDirty) 'Package source was dirty.'
    }

    $builtDllHash = (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash
    $builtServerHash = (Get-FileHash -LiteralPath $builtServer -Algorithm SHA256).Hash
    $builtLauncherHash = (Get-FileHash -LiteralPath $builtLauncher -Algorithm SHA256).Hash
    Assert-Condition ($manifest.dinput8Sha256 -eq $builtDllHash) 'DLL manifest hash mismatch.'
    Assert-Condition ($manifest.serverSha256 -eq $builtServerHash) 'Server manifest hash mismatch.'
    Assert-Condition ($manifest.launcherSha256 -eq $builtLauncherHash) 'Launcher manifest hash mismatch.'
    foreach ($path in @(
        (Join-Path $installerRoot 'Advanced\Payload\dinput8.dll'),
        (Join-Path $dropExtract 'dinput8.dll'))) {
        Assert-Condition ((Get-FileHash $path -Algorithm SHA256).Hash -eq $builtDllHash) `
            "Packaged DLL mismatch: $path"
    }
    foreach ($path in @(
        (Join-Path $installerRoot 'AliceCoopServer.exe'),
        (Join-Path $dropExtract 'AliceCoop\AliceCoopServer.exe'))) {
        Assert-Condition ((Get-FileHash $path -Algorithm SHA256).Hash -eq $builtServerHash) `
            "Packaged server mismatch: $path"
    }
    foreach ($path in @(
        (Join-Path $installerRoot 'AliceCoopLauncher.exe'),
        (Join-Path $dropExtract 'AliceCoop\AliceCoopLauncher.exe'))) {
        Assert-Condition ((Get-FileHash $path -Algorithm SHA256).Hash -eq $builtLauncherHash) `
            "Packaged launcher mismatch: $path"
    }

    $installWin32 = Join-Path $tempRoot 'install-smoke\Binaries\Win32'
    $installStatus = Join-Path $tempRoot 'install-status.txt'
    New-Item -ItemType Directory -Force -Path $installWin32 | Out-Null
    New-Item -ItemType File -Path (Join-Path $installWin32 'AliceMadnessReturns.exe') | Out-Null
    'preexisting proxy' | Set-Content -LiteralPath (Join-Path $installWin32 'dinput8.dll')
    'custom=true' | Set-Content -LiteralPath (Join-Path $installWin32 'MadnessPatch.ini')
    $previousDllHash = (Get-FileHash -LiteralPath (Join-Path $installWin32 'dinput8.dll') `
        -Algorithm SHA256).Hash
    & (Join-Path $installerRoot 'Advanced\Tools\Install-AliceCoop-Package.ps1') `
        -Win32Path $installWin32 -StatusPath $installStatus
    Assert-Condition (Test-Path -LiteralPath $installStatus -PathType Leaf) `
        'Installer did not write its status file.'
    Assert-Condition ((Get-Content -LiteralPath $installStatus -Raw) -match
        'Alice Co-op installed into') 'Installer status did not report success.'
    $installedCoop = Join-Path $installWin32 'AliceCoop'
    foreach ($required in @('AliceCoopLauncher.exe', 'AliceCoopServer.exe',
        'AliceCoop.ini', 'install-manifest.json',
        'Advanced\Manual\AliceCoop-Host.bat',
        'Advanced\Documentation\INSTALL.md')) {
        Assert-Condition (Test-Path -LiteralPath (Join-Path $installedCoop $required) `
            -PathType Leaf) "Installer smoke omitted: $required"
    }
    Assert-Condition ((Get-FileHash (Join-Path $installWin32 'dinput8.dll') `
        -Algorithm SHA256).Hash -eq $builtDllHash) 'Installed DLL mismatch.'
    Assert-Condition ((Get-Content -LiteralPath (Join-Path $installWin32 'MadnessPatch.ini') `
        -Raw) -match 'custom=true') 'Installer overwrote an existing MadnessPatch.ini.'

    # The launcher probe must reach a real packaged relay without occupying a role.
    $udp = [System.Net.Sockets.UdpClient]::new(0)
    $probePort = ([System.Net.IPEndPoint]$udp.Client.LocalEndPoint).Port
    $udp.Dispose()
    $relay = Start-Process -FilePath (Join-Path $installerRoot 'AliceCoopServer.exe') `
        -ArgumentList "--bind 127.0.0.1 --port $probePort --log-dir `"$tempRoot\relay-logs`"" `
        -WindowStyle Hidden -PassThru
    try {
        Start-Sleep -Milliseconds 350
        $probe = Start-Process -FilePath (Join-Path $installerRoot 'AliceCoopLauncher.exe') `
            -ArgumentList "--probe 127.0.0.1 $probePort" -Wait -PassThru
        Assert-Condition ($probe.ExitCode -eq 0) 'Launcher UDP probe failed.'
    }
    finally {
        if ($relay -and -not $relay.HasExited) { Stop-Process -Id $relay.Id -Force }
    }

    $uninstallStatus = Join-Path $tempRoot 'uninstall-status.txt'
    & (Join-Path $installedCoop 'Advanced\Tools\Uninstall-AliceCoop.ps1') `
        -Win32Path $installWin32 -StatusPath $uninstallStatus
    Assert-Condition (Test-Path -LiteralPath $uninstallStatus -PathType Leaf) `
        'Uninstaller did not write its status file.'
    Assert-Condition ((Get-Content -LiteralPath $uninstallStatus -Raw) -match
        'Alice Co-op was removed from') 'Uninstaller status did not report success.'
    Assert-Condition ((Get-FileHash -LiteralPath (Join-Path $installWin32 'dinput8.dll') `
        -Algorithm SHA256).Hash -eq $previousDllHash) `
        'Uninstaller did not restore the preexisting proxy DLL.'
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $installedCoop `
        'AliceCoopLauncher.exe'))) 'Uninstaller left the launcher installed.'
    Assert-Condition (Test-Path -LiteralPath (Join-Path $installedCoop `
        'Advanced\Tools\Uninstall-AliceCoop.ps1')) `
        'Uninstaller did not preserve its recovery tools.'

    $outerEntries = @{}
    foreach ($line in Get-Content -LiteralPath $outerChecksums) {
        if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') {
            throw "Malformed outer checksum: $line"
        }
        $outerEntries[$Matches[2]] = $Matches[1].ToUpperInvariant()
    }
    Assert-Condition ($outerEntries.Count -eq 2) 'Outer checksum count mismatch.'
    foreach ($archive in @($installerArchive, $dropInArchive)) {
        $name = [System.IO.Path]::GetFileName($archive)
        Assert-Condition ($outerEntries.ContainsKey($name)) "Missing outer checksum: $name"
        Assert-Condition ((Get-FileHash $archive -Algorithm SHA256).Hash -eq
            $outerEntries[$name]) "Outer checksum mismatch: $name"
    }

    Write-Host "AliceCoop $Version launcher packages passed verification."
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
