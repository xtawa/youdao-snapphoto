# User-initiated manual reinstall. Never runs automatically.
param([Parameter(Mandatory = $true)][string]$AmrPath)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $AmrPath -PathType Leaf)) { throw "AMR not found" }
$resolved = (Resolve-Path -LiteralPath $AmrPath).Path
if (-not $resolved.ToLowerInvariant().EndsWith(".amr")) { throw "Expected an AMR file" }
if (-not (Get-Command adb -ErrorAction SilentlyContinue)) { throw "adb not installed" }
$devices = @(& adb devices | Where-Object { $_ -match '^\S+\s+device$' })
if ($LASTEXITCODE -ne 0 -or $devices.Count -lt 1) { throw "No authorized ADB device" }
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolved).Hash
Write-Host "Package: $resolved"
Write-Host "SHA-256: $hash"
$answer = Read-Host "Install this AMR on your dictionary pen? Type YES"
if ($answer -cne "YES") { Write-Host "Cancelled"; exit 0 }
& adb push $resolved /userdisk/Favorite/SnapPhoto-reinstall.amr
if ($LASTEXITCODE -ne 0) { throw "adb push failed" }
& adb shell "miniapp_cli install /userdisk/Favorite/SnapPhoto-reinstall.amr"
if ($LASTEXITCODE -ne 0) { throw "Installation failed" }
Write-Host "Installation requested. Verify launch and app icon on the device."
