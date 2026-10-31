# Runs outside the browser process. The installer is already verified before this helper starts.
param(
  [Parameter(Mandatory=$true)][string]$InstallDir,
  [Parameter(Mandatory=$true)][string]$Installer,
  [Parameter(Mandatory=$true)][string]$Profile,
  [Parameter(Mandatory=$true)][string]$Version,
  [Parameter(Mandatory=$true)][string]$Sha256,
  [Parameter(Mandatory=$true)][long]$Size,
  [Parameter(Mandatory=$true)][int]$ParentPid
)
$ErrorActionPreference = 'Stop'
$old = $InstallDir + '.pv-update-backup'
$marker = Join-Path $Profile 'update-backup.json'
$log = Join-Path $Profile 'update-error.txt'
try {
  if ($Sha256 -notmatch '^[a-f0-9]{64}$' -or $Size -le 0) { throw 'Invalid installer verification data.' }
  if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid update version.' }
  if (-not [IO.Path]::IsPathRooted($InstallDir) -or -not [IO.Path]::IsPathRooted($Installer)) { throw 'Invalid update paths.' }
  if (Test-Path -LiteralPath $old) { throw 'An earlier update backup already exists. It has not been removed.' }
  if (-not (Test-Path -LiteralPath (Join-Path $InstallDir 'PsychoVault.exe'))) { throw 'The current installation was not found.' }
  Wait-Process -Id $ParentPid -ErrorAction SilentlyContinue
  if ((Get-Item -LiteralPath $Installer).Length -ne $Size -or (Get-FileHash -Algorithm SHA256 -LiteralPath $Installer).Hash.ToLowerInvariant() -ne $Sha256) { throw 'The downloaded installer has changed since verification.' }
  # Retry while Chromium releases file handles. Never kill another browser process.
  $moved = $false
  for ($i = 0; $i -lt 40; $i++) {
    try { [IO.Directory]::Move($InstallDir, $old); $moved = $true; break } catch { Start-Sleep -Milliseconds 500 }
  }
  if (-not $moved) { throw 'Could not move the current installation. Close all PsychoVault processes and retry.' }
  try {
    # /D must be the last NSIS argument. Start-Process needs an explicitly quoted path here.
    $process = Start-Process -FilePath $Installer -ArgumentList @('/S', ('/D=' + $InstallDir)) -Wait -PassThru
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $InstallDir 'PsychoVault.exe'))) { throw ('Setup failed with exit code ' + $process.ExitCode) }
    $data = @{ version = $Version; install = $InstallDir; backup = $old } | ConvertTo-Json -Compress
    [IO.File]::WriteAllText($marker, $data, [Text.Encoding]::UTF8)
    if ($env:PV_UPDATE_TEST -ne '1') { Start-Process -FilePath (Join-Path $InstallDir 'PsychoVault.exe') | Out-Null }
  } catch {
    $failure = $_.Exception.Message
    # Keep the backup intact if removing a partial install or restoring it fails.
    if (Test-Path -LiteralPath $InstallDir) { Remove-Item -LiteralPath $InstallDir -Recurse -Force }
    [IO.Directory]::Move($old, $InstallDir)
    throw ('The new version was not installed; the previous installation was restored. ' + $failure)
  }
} catch {
  $message = 'PsychoVault update failed. ' + $_.Exception.Message + "`r`nYour profile data was not removed. Backup, if present: " + $old
  try { [IO.File]::WriteAllText($log, $message, [Text.Encoding]::UTF8) } catch {}
  if ($env:PV_UPDATE_TEST -ne '1') { try { Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.MessageBox]::Show($message, 'PsychoVault update') | Out-Null } catch {} }
  if ($env:PV_UPDATE_TEST -ne '1' -and (Test-Path -LiteralPath (Join-Path $InstallDir 'PsychoVault.exe'))) { try { Start-Process -FilePath (Join-Path $InstallDir 'PsychoVault.exe') | Out-Null } catch {} }
  exit 1
}
