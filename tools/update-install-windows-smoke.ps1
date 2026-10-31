$ErrorActionPreference = 'Stop'
$root = Join-Path $env:TEMP ('pv-update-test-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $root | Out-Null
$env:PV_UPDATE_TEST = '1'
try {
  $source = @'
using System;
using System.IO;
class Setup {
  static int Main(string[] args) {
    string command = Environment.CommandLine;
    int i = command.IndexOf("/D=", StringComparison.OrdinalIgnoreCase);
    if (i < 0) return 8;
    string dir = command.Substring(i + 3).Trim().Trim('"');
    Directory.CreateDirectory(dir);
    File.WriteAllText(Path.Combine(dir,"PsychoVault.exe"), "new program");
    return Environment.GetEnvironmentVariable("PV_FAKE_FAIL") == "1" ? 3 : 0;
  }
}
'@
  $installer = Join-Path $root 'Setup fake.exe'
  Add-Type -TypeDefinition $source -OutputAssembly $installer -OutputType ConsoleApplication
  $helper = (Resolve-Path 'app/update-install.ps1').Path
  $hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
  $size = (Get-Item -LiteralPath $installer).Length
  foreach ($fail in @('1', '0')) {
    $install = Join-Path $root ('app with spaces ' + $fail)
    $profile = Join-Path $root ('profile ' + $fail)
    New-Item -ItemType Directory $install, $profile | Out-Null
    Set-Content -LiteralPath (Join-Path $install 'PsychoVault.exe') -Value 'old program'
    $env:PV_FAKE_FAIL = $fail
    & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $helper -InstallDir $install -Installer $installer -Profile $profile -Version 1.2.3 -Sha256 $hash -Size $size -ParentPid 2147483647
    $result = $LASTEXITCODE
    $text = Get-Content -LiteralPath (Join-Path $install 'PsychoVault.exe') -Raw
    if ($fail -eq '1') {
      if ($result -eq 0 -or $text -notmatch 'old program' -or (Test-Path ($install + '.pv-update-backup'))) { throw 'Failed installer did not restore the old program.' }
    } else {
      if ($result -ne 0 -or $text -notmatch 'new program' -or -not (Test-Path ($install + '.pv-update-backup')) -or -not (Test-Path (Join-Path $profile 'update-backup.json'))) { throw 'Successful install did not retain its old program and marker.' }
    }
  }
  Write-Host 'PASS: silent update helper installs to a spaced path and restores old files after failure'
} finally { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
