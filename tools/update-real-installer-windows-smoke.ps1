$ErrorActionPreference = 'Stop'
$root = Join-Path $env:TEMP ('pv-real-update-' + [guid]::NewGuid())
$install = Join-Path $root 'Install with spaces'
$profile = Join-Path $root 'Profile with spaces'
New-Item -ItemType Directory -Path $install, $profile | Out-Null
$env:PV_UPDATE_TEST = '1'
try {
  Set-Content -LiteralPath (Join-Path $install 'PsychoVault.exe') -Value 'old executable marker'
  $installer = (Get-ChildItem -Path 'test-installer' -Filter 'PsychoVault-Setup-*.exe' | Select-Object -First 1).FullName
  if (-not $installer) { throw 'Test installer missing' }
  $hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
  $size = (Get-Item -LiteralPath $installer).Length
  $version = (Get-Content app/package.json -Raw | ConvertFrom-Json).version
  $helper = (Resolve-Path 'app/update-install.ps1').Path
  & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $helper -InstallDir $install -Installer $installer -Profile $profile -Version $version -Sha256 $hash -Size $size -ParentPid 2147483647
  if ($LASTEXITCODE -ne 0) { throw ('Update helper failed: ' + (Get-Content (Join-Path $profile 'update-error.txt') -Raw)) }
  $metadata = Get-Content (Join-Path $install 'resources/app/package.json') -Raw | ConvertFrom-Json
  $old = Get-Content (Join-Path ($install + '.pv-update-backup') 'PsychoVault.exe') -Raw
  if ($metadata.version -ne $version -or $old -notmatch 'old executable marker') { throw 'Real installer version or backup incorrect.' }
  Write-Host 'PASS: real NSIS installer ran silently through helper, target has correct version and old files remain in backup'
} finally { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
