# HOME artwork host checks

Run from the repository root. Fixture generation uses the bundled installer generator, Node with its existing esbuild dependency, and Python with cryptography and Pillow. The keys are synthetic; no console keys or installed content are required. Generated data stays under build/native-artwork-reference and is excluded from release packaging.

```powershell
node tests/generate_home_artwork_fixture.cjs
python tests/decrypt_home_artwork_fixture.py
g++ -std=c++20 -O2 -Iapp/src tests/nca_artwork_test.cpp app/src/nca_artwork.cpp -lbcrypt -o build-host-tests/nca_artwork_test.exe
./build-host-tests/nca_artwork_test.exe build/native-artwork-reference
```

The NCA test uses Windows BCrypt for an independent SHA-256 implementation. It checks actual generator output, rebuilt integrity trees and metadata, preservation of Program records, and malformed input rejection. home_artwork_transaction_test.cpp injects failures into commit and rollback operations; home_artwork_toast_test.cpp checks timer boundaries. Compile each with C++20 and -Iapp/src.

These checks do not exercise Horizon IPC or HOME's texture cache. Device validation must cover an installed shortcut launched from HOME, changed and unchanged images, loss of connectivity, the 10-second notification during queue/stream, and reopening HOME to inspect the visible icon.

R2 adds home_record_policy_test.cpp for the complete record snapshot sent to NS. IPC layouts and installed-record value are matched to the bundled @nx.js/ns implementation (commands 16 and 17). R2 deliberately does not use the installer's DeleteApplicationRecord step. Tests cannot establish whether qlaunch redraws after PushApplicationRecord on a specific firmware.
