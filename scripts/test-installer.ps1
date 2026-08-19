<#
.SYNOPSIS
    Installs SURL from the built installer, verifies it, then uninstalls it.

.DESCRIPTION
    An installer that compiles is not an installer that works. This script
    exercises the parts that actually matter, on a real machine:

      1. per-user install into a temporary directory
      2. the program files land where they should
      3. surl.exe runs and reports the expected version
      4. the PATH entry is written to the right registry hive, exactly once
      5. re-installing does not add a second PATH entry
      6. a user's downloaded website and configuration survive
      7. the uninstaller removes the program, PATH entry and shortcuts
      8. the user's data is still there afterwards

    It runs unattended, so it is safe in CI. It never touches machine-wide
    state, so it needs no administrator rights.

.PARAMETER Installer
    Path to surl-windows-x64-installer.exe. Defaults to dist/.

.EXAMPLE
    ./scripts/test-installer.ps1
#>
[CmdletBinding()]
param(
    [string]$Installer
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Installer) { $Installer = Join-Path $RepoRoot 'dist/surl-windows-x64-installer.exe' }
if (-not (Test-Path $Installer)) { throw "Installer not found: $Installer" }

$Version = (Get-Content (Join-Path $RepoRoot 'VERSION.txt') -Raw).Trim()

$script:Failures = 0
$script:Checks = 0

function Test-Case([string]$Name, [scriptblock]$Condition) {
    $script:Checks++
    $ok = $false
    try { $ok = [bool](& $Condition) } catch { $ok = $false }

    if ($ok) {
        Write-Host "  ok    $Name" -ForegroundColor Green
    } else {
        Write-Host "  FAIL  $Name" -ForegroundColor Red
        $script:Failures++
    }
}

function Get-UserPathEntries {
    # Read the raw value so %VARS% are not expanded into the comparison.
    $raw = (Get-Item 'HKCU:\Environment').GetValue('Path', '', 'DoNotExpandEnvironmentNames')
    if (-not $raw) { return @() }
    return @($raw -split ';' | Where-Object { $_ })
}

function Get-SurlPathEntries {
    return @(Get-UserPathEntries | Where-Object { $_ -like '*SURL*' })
}

function Invoke-Installer([string[]]$ExtraArgs) {
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER') + $ExtraArgs
    $process = Start-Process -FilePath $Installer -ArgumentList $arguments -Wait -PassThru
    return $process.ExitCode
}

# --- fixtures --------------------------------------------------------------

$installDir = Join-Path $env:TEMP ("surl-install-test-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$userProject = Join-Path $env:TEMP ("surl-user-project-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$configFile = Join-Path $env:APPDATA 'SURL\config.json'
$configExisted = Test-Path $configFile
$configBackup = $null
if ($configExisted) {
    $configBackup = Get-Content $configFile -Raw
}

New-Item -ItemType Directory -Force $userProject | Out-Null
Set-Content -Path (Join-Path $userProject 'index.html') -Value '<h1>my mirrored site</h1>' -Encoding utf8

$pathEntriesBefore = @(Get-SurlPathEntries).Count

Write-Host ""
Write-Host "Installer test: SURL $Version" -ForegroundColor Cyan
Write-Host "  installer   $Installer"
Write-Host "  target dir  $installDir"
Write-Host ""

try {
    # --- install -----------------------------------------------------------
    Write-Host "install (per-user, PATH + Start Menu)" -ForegroundColor Cyan

    $exitCode = Invoke-Installer @("/DIR=$installDir", '/TASKS=addtopath,startmenu')
    Test-Case 'installer exits successfully' { $exitCode -eq 0 }
    Test-Case 'surl.exe is installed'        { Test-Path (Join-Path $installDir 'surl.exe') }
    Test-Case 'LICENSE is installed'         { Test-Path (Join-Path $installDir 'LICENSE.txt') }
    Test-Case 'uninstaller is created'       { Test-Path (Join-Path $installDir 'unins000.exe') }

    $exe = Join-Path $installDir 'surl.exe'
    $reported = (& $exe --version) -join ''
    Test-Case "surl --version reports $Version" { $reported.Trim() -eq "surl $Version" }
    Test-Case 'surl --help runs'                { & $exe --help | Out-Null; $LASTEXITCODE -eq 0 }

    # --- PATH --------------------------------------------------------------
    Write-Host ""
    Write-Host "PATH integration" -ForegroundColor Cyan

    $entries = @(Get-SurlPathEntries)
    Test-Case 'exactly one SURL entry in the user PATH' { $entries.Count -eq ($pathEntriesBefore + 1) }
    Test-Case 'the entry points at the install directory' {
        $entries | Where-Object { $_.TrimEnd('\') -ieq $installDir.TrimEnd('\') }
    }
    Test-Case 'the system PATH was not touched' {
        $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
        $machine = if ($machine) { $machine } else { '' }
        @($machine -split ';' | Where-Object { $_ -like "*$installDir*" }).Count -eq 0
    }

    # A terminal started after the install resolves surl from the registry.
    $freshPath = "$([Environment]::GetEnvironmentVariable('Path','Machine'));$([Environment]::GetEnvironmentVariable('Path','User'))"
    $resolved = @(& "$env:SystemRoot\System32\where.exe" /R $installDir 'surl.exe' 2>$null)
    Test-Case 'surl.exe is discoverable in the install directory' { $resolved.Count -gt 0 }
    Test-Case 'the fresh PATH contains the install directory' {
        ($freshPath -split ';' | Where-Object { $_.TrimEnd('\') -ieq $installDir.TrimEnd('\') })
    }

    # --- re-install --------------------------------------------------------
    Write-Host ""
    Write-Host "re-install (must not duplicate PATH)" -ForegroundColor Cyan

    $exitCode = Invoke-Installer @("/DIR=$installDir", '/TASKS=addtopath,startmenu')
    Test-Case 're-install exits successfully' { $exitCode -eq 0 }
    Test-Case 'still exactly one SURL PATH entry' {
        @(Get-SurlPathEntries).Count -eq ($pathEntriesBefore + 1)
    }

    # --- user data ---------------------------------------------------------
    Write-Host ""
    Write-Host "user data" -ForegroundColor Cyan
    Test-Case 'the user project survived the install' {
        Test-Path (Join-Path $userProject 'index.html')
    }

    # --- uninstall ---------------------------------------------------------
    Write-Host ""
    Write-Host "uninstall" -ForegroundColor Cyan

    $uninstaller = Join-Path $installDir 'unins000.exe'
    $process = Start-Process -FilePath $uninstaller `
        -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait -PassThru
    Test-Case 'uninstaller exits successfully' { $process.ExitCode -eq 0 }

    # Inno's uninstaller respawns itself from the temp directory; give the
    # respawned process a moment to finish removing the install directory.
    for ($i = 0; $i -lt 30 -and (Test-Path $installDir); $i++) {
        Start-Sleep -Milliseconds 500
    }

    Test-Case 'the installation directory is gone' { -not (Test-Path $installDir) }
    Test-Case 'the PATH entry is gone' {
        @(Get-SurlPathEntries).Count -eq $pathEntriesBefore
    }
    Test-Case 'the Start Menu folder is gone' {
        -not (Test-Path (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\SURL'))
    }
    Test-Case 'the Add/Remove Programs entry is gone' {
        $found = $false
        Get-ChildItem 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' -ErrorAction SilentlyContinue |
            ForEach-Object {
                $properties = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
                if ($properties -and $properties.PSObject.Properties['DisplayName'] -and
                    $properties.DisplayName -like 'SURL*') { $found = $true }
            }
        -not $found
    }

    # The whole point: uninstalling SURL must never touch what SURL produced.
    Test-Case 'the user project still exists after uninstall' {
        Test-Path (Join-Path $userProject 'index.html')
    }
    if ($configExisted) {
        Test-Case 'existing user configuration was kept' { Test-Path $configFile }
    }
}
finally {
    Remove-Item -Recurse -Force $userProject -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $installDir -ErrorAction SilentlyContinue

    if ($configExisted -and $configBackup -and -not (Test-Path $configFile)) {
        New-Item -ItemType Directory -Force (Split-Path -Parent $configFile) | Out-Null
        Set-Content -Path $configFile -Value $configBackup -Encoding utf8
    }
}

Write-Host ""
if ($script:Failures -gt 0) {
    Write-Host "$($script:Checks - $script:Failures)/$($script:Checks) checks passed, $($script:Failures) failed" -ForegroundColor Red
    exit 1
}
Write-Host "$($script:Checks)/$($script:Checks) checks passed" -ForegroundColor Green
