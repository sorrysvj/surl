<#
.SYNOPSIS
    Configures and builds SURL with the best available Windows toolchain.

.DESCRIPTION
    Locates the MSVC build environment with vswhere, imports it into this
    session, then configures CMake with Ninja and builds. Falls back to any
    compiler already on PATH when MSVC is not installed.

.PARAMETER Config
    Release (default) or Debug.

.PARAMETER BuildDir
    Build directory. Defaults to build/<Config>.

.PARAMETER Test
    Run the test suite after building.

.PARAMETER Clean
    Delete the build directory before configuring.

.EXAMPLE
    ./scripts/build.ps1 -Config Release -Test
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Config = 'Release',

    [string]$BuildDir,

    [switch]$Test,

    [switch]$Clean,

    # Treat compiler warnings as errors, the way CI does.
    [switch]$Werror
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $RepoRoot "build/$Config" }

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Add-SystemDirectoriesToPath {
    <#
    vcvars64.bat shells out to findstr, where and reg. If PATH has been
    trimmed (as happens under some CI and sandboxed shells) those lookups fail
    and vcvars reports a bogus "SDK not found". Make sure the system
    directories are present before invoking it.
    #>
    $required = @(
        (Join-Path $env:SystemRoot 'System32'),
        $env:SystemRoot,
        (Join-Path $env:SystemRoot 'System32\Wbem'),
        (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0')
    )
    $current = ($env:PATH -split ';') | Where-Object { $_ }
    $missing = $required | Where-Object { $dir = $_; -not ($current | Where-Object { $_.TrimEnd('\') -ieq $dir.TrimEnd('\') }) }
    if ($missing) {
        $env:PATH = (($missing + $current) -join ';')
        Write-Host "    added system directories to PATH for this session"
    }
}

function Import-MsvcEnvironment {
    <#
    Imports the MSVC x64 environment into the current PowerShell session so
    CMake can find cl.exe, rc.exe and mt.exe. Returns $true when MSVC was found.
    #>
    Add-SystemDirectoriesToPath

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $false }

    $installPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $installPath) { return $false }

    $vcvars = Join-Path $installPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { return $false }

    Write-Host "    MSVC: $installPath"

    # Run vcvars64 in cmd, then copy the resulting environment back into here.
    $output = & "$env:ComSpec" /s /c "`"$vcvars`" >nul 2>&1 && set"
    foreach ($line in $output) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
        }
    }

    # Sanity-check the pieces CMake actually needs. A Build Tools install that
    # cannot see the Windows SDK compiles but fails at the resource/manifest
    # step, with a confusing error, so catch it here instead.
    foreach ($tool in @('cl', 'link', 'rc', 'mt')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "The MSVC environment was imported but '$tool' is still not on PATH. " +
                  "The Visual Studio C++ workload or the Windows SDK looks incomplete."
        }
    }
    return $true
}

# Keep compiler diagnostics in English so build logs read the same everywhere
# and are searchable regardless of the machine's display language.
$env:VSLANG = '1033'

Write-Step "SURL build ($Config)"

$version = (Get-Content (Join-Path $RepoRoot 'VERSION.txt') -Raw).Trim()
Write-Host "    version: $version"

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "Cleaning $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

Write-Step 'Locating a toolchain'
$haveMsvc = Import-MsvcEnvironment
if (-not $haveMsvc) {
    Write-Warning 'MSVC not found; falling back to whatever compiler is on PATH.'
}

foreach ($tool in @('cmake', 'ninja')) {
    $found = Get-Command $tool -ErrorAction SilentlyContinue
    if (-not $found) { throw "$tool is required but was not found on PATH." }
    Write-Host "    $tool : $($found.Source)"
}

Write-Step 'Configuring'
$cmakeArgs = @(
    '-S', $RepoRoot,
    '-B', $BuildDir,
    '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$Config",
    '-DSURL_BUILD_TESTS=ON',
    "-DSURL_WERROR=$(if ($Werror) { 'ON' } else { 'OFF' })"
)
if ($haveMsvc) {
    $cmakeArgs += @('-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl')
}
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed ($LASTEXITCODE)." }

Write-Step 'Building'
& cmake --build $BuildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }

$exe = Join-Path $BuildDir 'surl.exe'
if (-not (Test-Path $exe)) { throw "Expected $exe to exist after the build." }

Write-Step 'Smoke test'
& $exe --version
if ($LASTEXITCODE -ne 0) { throw "surl --version failed ($LASTEXITCODE)." }

if ($Test) {
    Write-Step 'Running tests'
    & ctest --test-dir $BuildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)." }
}

Write-Step 'Done'
Write-Host "    $exe" -ForegroundColor Green
$size = (Get-Item $exe).Length
Write-Host ("    {0:N0} bytes" -f $size)
