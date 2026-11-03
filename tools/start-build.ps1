# I delegate double-click builds to GitHub Actions. I do not compile locally.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$repo = 'Psycho-Core/PsychoVault'
$actions = "https://github.com/$repo/actions/workflows/owned-engine.yml"
$branch = (& git rev-parse --abbrev-ref HEAD).Trim()
$head = (& git rev-parse HEAD).Trim()
$remote = (& git ls-remote origin "refs/heads/$branch")
if (-not $remote -or -not $remote.StartsWith($head)) {
  Write-Host "My local commit is not pushed on $branch. I will open the build page instead of pretending GitHub built my local changes."
  Start-Process $actions
  exit 0
}
$gh = Get-Command gh -ErrorAction SilentlyContinue
if (-not $gh) {
  Write-Host 'GitHub CLI is not installed. I will open the build page; I can start the workflow there.'
  Start-Process $actions
  exit 0
}
& gh workflow run 'Build owned engine' --repo $repo --ref $branch
if ($LASTEXITCODE -ne 0) { throw 'GitHub did not accept the workflow dispatch.' }
Write-Host "I started the Windows build for $branch at $head."
Write-Host "Build page: $actions"
Start-Process $actions
