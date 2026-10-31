# Building SwitchNOW 1.3.0

The release is based on 1.2.1 R73 test. Runtime streaming policies are retained.

## Native NRO

Install devkitPro/devkitA64, Switch portlibs (curl, Jansson, deko3d and the other linked dependencies), CMake, Ninja, Python/Jinja2/jsonschema, and MSYS2 on Windows. The checked-in `extern/` directory contains the patched dependency snapshot. FFmpeg headers/static archives are required build inputs, not user data. The forwarder NRO and generator WASM/ExeFS are also intentional build inputs.

Windows: `./build-switch.ps1`

MSYS2/Linux with devkitPro configured: `bash scripts/build-switch-msys2.sh`

Output: `build/switch/SwitchNOW.nro`. Default version: 1.3.0. Always-on connection debugging defaults OFF; diagnostics remain available in the app.

## Forwarder installer

The release includes the rebuilt installer. To rebuild it after source changes:

```sh
cd tools/switchnow-forwarder-installer
corepack pnpm install --frozen-lockfile
corepack pnpm run nro
```

No account credentials or console keys are required to build the client. HOME installation uses the console's own services/runtime key material; no personal key dump is shipped.

## Tests

On Linux install g++, libjansson-dev, libcurl development headers, Python and Node, then run `bash scripts/run-host-tests.sh`. POSIX socket tests run on Linux; Windows hosts skip those platform-specific tests. The Windows artwork fixture uses generated synthetic test values.

## Packaging

`./scripts/package-release.ps1 -Version 1.3.0` builds the distributable directory/ZIP from explicit public inputs after the NRO build. `python scripts/export-public-source.py` exports a fresh source snapshot under dist without Git history, local builds, caches, runtime state, reports or private documents.

The source export preserves original upstream license/attribution files. Its manifest records the included files and SHA-256 hashes. Never publish the working directory wholesale; publish the reviewed export. No tag or GitHub release is created by these scripts.
