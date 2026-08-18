# surl Installation Script for Windows
# Run as Administrator for system-wide installation

$ErrorActionPreference = "Stop"

$installDir = "$env:LOCALAPPDATA\surl"
$binPath = "$installDir\surl.exe"
$sourcePath = Join-Path $PSScriptRoot "bin\surl.exe"

# Check if source exists
if (-not (Test-Path $sourcePath)) {
    Write-Host "Error: surl.exe not found. Run 'npm run build:exe' first." -ForegroundColor Red
    exit 1
}

# Create installation directory
if (-not (Test-Path $installDir)) {
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
    Write-Host "Created directory: $installDir" -ForegroundColor Green
}

# Copy executable
Copy-Item -Path $sourcePath -Destination $binPath -Force
Write-Host "Copied surl.exe to $binPath" -ForegroundColor Green

# Add to user PATH
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$installDir*") {
    $newPath = "$userPath;$installDir"
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "Added $installDir to user PATH" -ForegroundColor Green
    Write-Host ""
    Write-Host "Please restart your terminal for PATH changes to take effect." -ForegroundColor Yellow
} else {
    Write-Host "$installDir is already in PATH" -ForegroundColor Cyan
}

Write-Host ""
Write-Host "Installation complete! Run 'surl --help' to get started." -ForegroundColor Green
