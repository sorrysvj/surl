<#
.SYNOPSIS
    Writes checksums.txt for every release asset in a directory.

.DESCRIPTION
    Produces the standard "<sha256>  <filename>" format that sha256sum -c and
    the Windows installer both understand. Entries are sorted by name so the
    file is byte-identical for identical inputs.

.PARAMETER Directory
    Directory holding the release assets. Defaults to dist.

.PARAMETER Verify
    Instead of writing checksums.txt, check the existing one and fail if any
    file is missing or does not match.

.EXAMPLE
    ./scripts/checksum.ps1
    ./scripts/checksum.ps1 -Verify
#>
[CmdletBinding()]
param(
    [string]$Directory,
    [switch]$Verify
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Directory) { $Directory = Join-Path $RepoRoot 'dist' }

if (-not (Test-Path $Directory)) {
    throw "No such directory: $Directory"
}

$checksumFile = Join-Path $Directory 'checksums.txt'

if ($Verify) {
    if (-not (Test-Path $checksumFile)) { throw "No checksums.txt in $Directory" }

    $failures = 0
    foreach ($line in Get-Content $checksumFile) {
        $trimmed = $line.Trim()
        if (-not $trimmed) { continue }

        $parts = $trimmed -split '\s+', 2
        if ($parts.Count -ne 2) { continue }

        $expected = $parts[0].ToUpperInvariant()
        $name = $parts[1].TrimStart('*')
        $path = Join-Path $Directory $name

        if (-not (Test-Path $path)) {
            Write-Host "MISSING  $name" -ForegroundColor Red
            $failures++
            continue
        }

        $actual = (Get-FileHash $path -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($actual -ne $expected) {
            Write-Host "MISMATCH $name" -ForegroundColor Red
            $failures++
        } else {
            Write-Host "ok       $name" -ForegroundColor Green
        }
    }

    if ($failures -gt 0) { throw "$failures checksum failure(s)." }
    Write-Host "All checksums verified." -ForegroundColor Green
    exit 0
}

# checksums.txt never lists itself.
$assets = Get-ChildItem $Directory -File |
    Where-Object { $_.Name -ne 'checksums.txt' } |
    Sort-Object Name

if (-not $assets) { throw "No release assets found in $Directory" }

$lines = foreach ($asset in $assets) {
    $hash = (Get-FileHash $asset.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    # Two spaces is what sha256sum emits and what the installer parses.
    "$hash  $($asset.Name)"
}

# ASCII with LF keeps the file identical whatever produced it.
$content = ($lines -join "`n") + "`n"
[System.IO.File]::WriteAllText($checksumFile, $content, [System.Text.UTF8Encoding]::new($false))

Write-Host "Wrote $checksumFile" -ForegroundColor Green
Get-Content $checksumFile | ForEach-Object { Write-Host "    $_" }
