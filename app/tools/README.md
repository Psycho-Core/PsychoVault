# tools/

This folder is where PsychoVault looks for **wireproxy**, the userspace WireGuard client
that backs Settings -> VPN. The Windows installer ships
`resources\app\tools\wireproxy.exe` inside `PsychoVault-Setup-1.0.0.exe`; the source
archive does not carry the binary, so if you run PsychoVault from source and want the
tunnel, drop it in here yourself:

```sh
# Linux (binary name: wireproxy)
curl -LO https://github.com/windtf/wireproxy/releases/download/v1.1.3/wireproxy_linux_amd64.tar.gz
tar xzf wireproxy_linux_amd64.tar.gz && install -m 755 wireproxy <this folder>/wireproxy

# Windows (binary name: wireproxy.exe)  - in PowerShell
Invoke-WebRequest https://github.com/windtf/wireproxy/releases/download/v1.1.3/wireproxy_windows_amd64.tar.gz -outfile wp.tgz
tar -xzf wp.tgz; Copy-Item wireproxy.exe <this folder>\wireproxy.exe
```

Check it against upstream before you run anything that gets to see your network:

```
wireproxy_linux_amd64.tar.gz    e88c1d090740373fc606c1bafd81d9a5eadc642cce5667616e20e9d7a444f51c
wireproxy_windows_amd64.tar.gz  bce041ea9fe0f8a3351301dcbe29cdf6a523bb25cf9c62f17ebb5699a8051d0f
```

The exact expected digests are in `THIRD-PARTY-NOTICES.md` at the top level, and the
upstream release page publishes `checksums.txt` next to the tarballs. PsychoVault only
ever runs the binary with `-n -c <config>` (a config test) and `-c <config>`; it never
passes a shell, and the config it writes holds your private key, so it is written 0600
inside your profile.

If you would rather not have the binary at all: Settings -> VPN, leave "binary path"
empty, and the VPN button in the rail will tell you it is missing instead of pretending.

## If this file is missing

It is 10.6 MB and the largest thing in the source tree, so the workspace snapshot has
dropped it more than once. `tools/build-installer.sh` now restores it from
`PsychoVault-1.0.0-full.zip` before building and checks the upstream digest
`b176b561fd8bf15d...`. Nothing else needs it at rest - the app looks for the binary at
runtime, not at build time.
