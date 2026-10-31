# Public release contents

The public export excludes Git history, developer build directories, runtime
logs/reports, real accounts, saved passwords/tokens, console key dumps and
private diagnostic documents. Developer source-directory paths are replaced
with relative paths in the native binary. Examples in tests are synthetic.

## Public homebrew self-signing material

The unchanged third-party hacBrewPack WASM contains a deliberately published
RSA self-signing key for homebrew packages. It is part of the public tool,
not a Switch console key, Nintendo signing key, account secret or maintainer's
authentication key. It is retained to preserve the existing HOME generator.
Removing it blindly could break shortcut creation.

Its PEM bytes were compared with the public upstream source:
https://github.com/rlaphoenix/hacBrewPack/blob/master/rsa_keys.h

SHA-256 of the matched PEM block:
`c62a7599df0fe31876e38c10c27b82e22fd62411234cf8dd4e7b259887cffd95`

The audit accepts only that exact fingerprint in the three expected generator
containers. An unknown PEM key, account token or runtime key file remains a
blocking finding. Crypto format strings alone are not key material. Public
protocol identifiers, trust anchors and upstream copyright contacts are also
retained; the release is not a claim that all cryptographic constants were removed.

The audit checks known patterns and an explicit input selection; it is not a
proof against every possible encoding of unknown data. No production account
credentials or console keys were needed for this release build.
