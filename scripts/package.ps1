<#
.SYNOPSIS
    Packages SURL: portable ZIP, Windows installer and checksums.

.DESCRIPTION
    Takes an already-built surl.exe and produces everything a GitHub release
    needs, into dist/:

        surl-windows-x64.zip            program files only (used by the
                                        installer's download path)
        surl-windows-x64-portable.zip   the same plus README and LICENSE
        surl-windows-x64-installer.exe  Inno Setup installer
        checksums.txt                   SHA-256 of every asset

    The version comes from VERSION.txt so nothing is duplicated.

.PARAMETER BuildDir
    Directory containing surl.exe. Defaults to build/Release.

.PARAMETER OutputDir
    Where to write the assets. Defaults to dist.

.PARAMETER SkipInstaller
    Produce the archives and checksums but not the installer (useful when
    Inno Setup is not installed).

.EXAMPLE
    ./scripts/package.ps1
#>
[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$OutputDir,
    [switch]$SkipInstaller
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $RepoRoot 'build/Release' }
if (-not $OutputDir) { $OutputDir = Join-Path $RepoRoot 'dist' }

$Version = (Get-Content (Join-Path $RepoRoot 'VERSION.txt') -Raw).Trim()

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Find-InnoSetup {
    <#
    Locates ISCC.exe: PATH first, then the standard install locations, then
    the registry entry Inno Setup writes.
    #>
    $onPath = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 5\ISCC.exe')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }

    $regPath = 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1'
    if (Test-Path $regPath) {
        $location = (Get-ItemProperty $regPath -ErrorAction SilentlyContinue).InstallLocation
        if ($location) {
            $candidate = Join-Path $location 'ISCC.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }
    return $null
}

Write-Step "Packaging SURL $Version"

$exe = Join-Path $BuildDir 'surl.exe'
if (-not (Test-Path $exe)) {
    throw "surl.exe not found at $exe. Build it first: ./scripts/build.ps1"
}
Write-Host "    payload: $exe ($((Get-Item $exe).Length) bytes)"

# A freshly built binary must actually run before it is shipped.
& $exe --version | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'The built surl.exe does not run.' }

if (Test-Path $OutputDir) { Remove-Item -Recurse -Force $OutputDir }
New-Item -ItemType Directory -Force $OutputDir | Out-Null

# --- staging ---------------------------------------------------------------
Write-Step 'Staging archive contents'

$stageBare = Join-Path $OutputDir '_stage-bare'
$stagePortable = Join-Path $OutputDir '_stage-portable'
New-Item -ItemType Directory -Force $stageBare | Out-Null
New-Item -ItemType Directory -Force $stagePortable | Out-Null

# The bare archive is what the installer downloads: program files only, no
# documentation, no sources.
Copy-Item $exe $stageBare

Copy-Item $exe $stagePortable
Copy-Item (Join-Path $RepoRoot 'LICENSE') (Join-Path $stagePortable 'LICENSE.txt')
Copy-Item (Join-Path $RepoRoot 'README.md') $stagePortable
if (Test-Path (Join-Path $RepoRoot 'CHANGELOG.md')) {
    Copy-Item (Join-Path $RepoRoot 'CHANGELOG.md') $stagePortable
}

# A note so someone unzipping the portable build knows it changes nothing.
$portableNote = @"
SURL $Version - portable build
==============================

This is the portable edition of SURL. Extract it anywhere and run:

    surl.exe --help

It does not install anything: it does not modify PATH, does not write
registry entries, and does not create an uninstaller. Deleting this folder
removes it completely.

To use `surl` from any terminal, either:

  * add this folder to your PATH yourself, or
  * run `surl.exe install`, which adds it to your user PATH, or
  * download the Windows installer from the releases page instead.

Configuration and cache, if you create any, live outside this folder:

    %APPDATA%\SURL          configuration
    %LOCALAPPDATA%\SURL     cache and logs

Documentation: https://github.com/sorrysvj/surl
"@
Set-Content -Path (Join-Path $stagePortable 'PORTABLE.txt') -Value $portableNote -Encoding utf8

# --- archives --------------------------------------------------------------
Write-Step 'Creating archives'

$bareZip = Join-Path $OutputDir 'surl-windows-x64.zip'
$portableZip = Join-Path $OutputDir 'surl-windows-x64-portable.zip'

Compress-Archive -Path (Join-Path $stageBare '*') -DestinationPath $bareZip -CompressionLevel Optimal
Compress-Archive -Path (Join-Path $stagePortable '*') -DestinationPath $portableZip -CompressionLevel Optimal

Write-Host "    $(Split-Path -Leaf $bareZip)      $((Get-Item $bareZip).Length) bytes"
Write-Host "    $(Split-Path -Leaf $portableZip)  $((Get-Item $portableZip).Length) bytes"

Remove-Item -Recurse -Force $stageBare, $stagePortable

# --- installer -------------------------------------------------------------
if (-not $SkipInstaller) {
    Write-Step 'Building the Windows installer'

    $iscc = Find-InnoSetup
    if (-not $iscc) {
        throw @'
Inno Setup 6 was not found. Install it from https://jrsoftware.org/isdl.php
(or the official GitHub mirror at https://github.com/jrsoftware/issrc/releases),
or re-run with -SkipInstaller.
'@
    }
    Write-Host "    ISCC: $iscc"

    $iss = Join-Path $RepoRoot 'installer/windows/surl.iss'
    & $iscc "/DAppVersion=$Version" "/DPayloadDir=$BuildDir" "/O$OutputDir" $iss
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed ($LASTEXITCODE)." }

    $installer = Join-Path $OutputDir 'surl-windows-x64-installer.exe'
    if (-not (Test-Path $installer)) { throw "Expected $installer to exist." }
    Write-Host "    $(Split-Path -Leaf $installer)  $((Get-Item $installer).Length) bytes"
}

# --- checksums -------------------------------------------------------------
Write-Step 'Computing checksums'
& (Join-Path $PSScriptRoot 'checksum.ps1') -Directory $OutputDir
if ($LASTEXITCODE -ne 0) { throw 'Checksum generation failed.' }

Write-Step 'Release assets'
Get-ChildItem $OutputDir -File | ForEach-Object {
    Write-Host ("    {0,12:N0}  {1}" -f $_.Length, $_.Name) -ForegroundColor Green
}
