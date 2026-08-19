<#
.SYNOPSIS
    Installs a pinned Inno Setup from its official GitHub releases.

.DESCRIPTION
    Inno Setup's downloads moved to github.com/jrsoftware/issrc in March 2026.
    The version is pinned and the download is verified against a known SHA-256
    so a release build is reproducible and cannot silently pick up a different
    compiler.

    If a matching Inno Setup is already present, nothing is downloaded.

.PARAMETER Version
    Inno Setup version to install. Defaults to the pinned version.

.PARAMETER Sha256
    Expected SHA-256 of the installer. Pass an empty string to skip the check
    when deliberately trying a different version.

.EXAMPLE
    ./scripts/install-innosetup.ps1
#>
[CmdletBinding()]
param(
    [string]$Version = '6.7.3',
    [string]$Sha256 = '9C73C3BAE7ED48D44112A0F48E66742C00090BDB5BEF71D9D3C056C66E97B732'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$iscc = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
if (Test-Path $iscc) {
    Write-Host "Inno Setup already installed: $iscc" -ForegroundColor Green
    exit 0
}

# Tag format on the issrc repository is "is-6_7_3".
$tag = 'is-' + ($Version -replace '\.', '_')
$url = "https://github.com/jrsoftware/issrc/releases/download/$tag/innosetup-$Version.exe"
$target = Join-Path $env:TEMP "innosetup-$Version.exe"

Write-Host "Downloading Inno Setup $Version"
Write-Host "  $url"

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $url -OutFile $target -UseBasicParsing -Headers @{ 'User-Agent' = 'surl-build' }

$actual = (Get-FileHash $target -Algorithm SHA256).Hash.ToUpperInvariant()
Write-Host "  SHA-256: $actual"

if ($Sha256) {
    if ($actual -ne $Sha256.ToUpperInvariant()) {
        Remove-Item $target -Force -ErrorAction SilentlyContinue
        throw "Inno Setup download failed its integrity check. Expected $Sha256, got $actual."
    }
    Write-Host "  checksum verified" -ForegroundColor Green
} else {
    Write-Warning 'Checksum verification skipped.'
}

Write-Host 'Installing silently'
$process = Start-Process -FilePath $target `
    -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/ALLUSERS' `
    -Wait -PassThru
if ($process.ExitCode -ne 0) {
    throw "The Inno Setup installer exited with $($process.ExitCode)."
}

Remove-Item $target -Force -ErrorAction SilentlyContinue

if (-not (Test-Path $iscc)) {
    throw "Inno Setup reported success but $iscc does not exist."
}
Write-Host "Installed: $iscc" -ForegroundColor Green
