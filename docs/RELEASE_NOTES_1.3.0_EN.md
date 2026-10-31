# SwitchNOW 1.3.0 — Streaming & Experience Update

Changes from the **1.2.1 R1–R73** branch, consolidated into **1.3.0**. SwitchNOW author and maintainer: **Blade-Punisher**.

## Streaming and image quality

- Added three streaming modes: **Performance**, **Balanced**, and **Quality**. Balanced is the default; resolution, frame rate, and maximum bitrate remain independently configurable. Legacy mode settings migrate to the current profiles.
- Refined local adaptation to packet loss, delivery jitter, and decoder pressure, with bounded frame waiting, protective intervals, and gradual recovery after conditions stabilize.
- Improved video recovery through RTP reordering, NACK/RTX, keyframe requests, and reference-frame damage handling. Added output-frame checks using PTS and a verified IDR after a fault.
- Fixed handling of resolution, pixel-format, and stride changes. Retained plane validation, GPU synchronization, and safe resource recreation.
- Reduced repeated NV12 storage and frame-header allocations. Added reuse of upload descriptors and command memory.
- Refined presentation queuing to limit stale-frame buildup, cautiously reduce excess latency during stable delivery, and back off after adverse events.
- Automatic video mode retains hardware NVDEC decoding with completed frames copied into owned Deko3D textures. The experimental zero-copy path is not enabled by default.

## Input responsiveness

- Support for up to **four independent controllers**, with separate state and connection/disconnection notifications.
- Dedicated controller sampling and priority for fresh input over periodic network maintenance, reducing unnecessary waits and contention with the UI.
- Fixed lost short button presses, trigger handling, and SCTP sequence errors when opening the input channel. Added bounded retransmission and correct handling of a full delivery window.
- Network servicing continues while the on-screen keyboard, stream menu, or auto-login owns local input. Controllers are neutralized and the touch mouse is released when entering local input mode.
- Fixed Escape, Tab, Alt+Tab, and Windows commands, including actual key-down/key-up events and modifier cleanup on cancellation or reconnection.

### Current shortcuts

**Minus + Plus** opens the stream menu.

For commands, hold **Minus + R3 for 0.3 seconds**, wait for the hint, then choose an action. You may release the prefix after the hint appears; the selection window lasts up to five seconds.

| Button | Action |
|---|---|
| ZL | Escape |
| L | Tab |
| R | Alt+Tab |
| ZR | Windows |
| Y | On-screen keyboard |
| X | Available auto-login profile |
| B | Cancel command mode |

## Audio

- Fixed audio-clock resets after output restarts and handling of audio-stream changes.
- Refined underrun recovery and adaptive buffering: a short interruption does not always require full rebuffering, and an elevated reserve gradually decreases after stable playback.
- Improved gain limiting and transitions around losses to reduce crackle, abrupt volume changes, and pumping.
- Moved detailed audio logging to a bounded background writer.

## Connections, servers, and queues

- Improved official NVIDIA launch compatibility through a consistent **Browser / WebRTC** profile. Fixed signaling registration and acknowledgements, binary and fragmented WebSocket JSON handling, SCTP negotiation, and endpoint processing.
- Separate handling of session-control, signaling, and media endpoints, including explicit ports, IPv6, refreshed recovery endpoints, and fallback routes for session cleanup.
- Added bounded automatic recovery of the same cloud session and manual continuation from the Library. Retries do not launch another game, and the old transport is shut down before its replacement is created.
- Fixed false stream termination caused by an isolated system “offline” report while video is still arriving. Missing first video now has a separate timeout and clear recovery actions.
- Improved **Auto**, **Closest**, and manual region/zone selection with repeated measurements, a shared candidate pool, freshness checks, and history of actual assignments, RTT, and pauses. An unavailable external zone catalogue preserves the provider’s standard route.
- Corrected personal queue parsing and display. Unknown positions are distinct from zero and rig preparation; stale positions are not retained indefinitely. General zone queue estimates are separate from the player’s actual position.
- Improved Free-tier advertisement handling for opaque HTTP URLs, VAST, and static HLS sources. Download/playback failures are reported correctly before polling continues on the same session.
- Added user-configured **HTTP, SOCKS5, and SOCKS5h proxies** for the applicable CloudMatch requests. Removed the Zortos integration and added proxy.log. Game video, audio, and input remain direct.

## Library, Store, and interface

- Fixed incomplete libraries limited to a small game panel. Owned variants are collected through complete paginated catalogue enumeration with authenticated ownership checks.
- The Store and Library share account/provider-scoped caching, stale-response protection, and access to available games during network failures. Partial results do not overwrite a complete saved snapshot.
- Cards appear earlier from cache or progressive results. Complete membership is saved before optional descriptions/screenshots are fetched. Refresh yields to game launch and resumes on return, with a deadline and bounded retries.
- Added shared bottom navigation: **15 games per page**, numbered pages, current-page highlighting, and page-number entry through ellipses. Down from the last card row only moves focus; A or touch opens the page.
- Fixed crashes while scrolling large libraries through off-screen card culling, bounded texture retention, managed UI command-memory growth, and safe GPU resource retirement.
- Game cards can mark a selected store variant as owned or remove that mark after confirmation. NVIDIA API acknowledgement updates the Library; the action neither purchases a game nor grants a store licence.
- Redesigned operator selection with NVIDIA/Alliance cards, a current-operator indicator, background loading, error retry, and B to return.
- Improved game cards, settings, connection stages, and the stream menu. After a server-ended session, A/B returns to the menu without waiting for automatic dismissal.

## HOME shortcuts and images

- Reworked shortcut creation, result presentation, and return to the game card. Fixed crashes and UI lifetime issues during creation and dismissal.
- The installer uses a capture of the actual game page or a neutral background. Improved framebuffer readback and background preparation order; the temporary capture is removed on return.
- Shortcut artwork is checked in the background on launch without holding up the game. A successful update displays a bottom-right notification for **10 seconds**.
- Improved source-image quality and icon scaling. Artwork replacement and HOME synchronization include validation and recovery procedures.
- Faster screenshot browsing through background decoding, adjacent-image prefetching, bounded CPU/GPU caches, HTTP connection reuse, and background SD writes. The previous screenshot is no longer presented as the newly selected one while loading.

## Auto-login, descriptions, and languages

- Added separate credential-entry profiles for **Genshin Impact, Honkai: Star Rail, Wuthering Waves, and Zenless Zone Zero**, while retaining and refining the NTE flow.
- Added a shared two-stage **Epic Games** profile for selected Epic store variants, **excluding Fortnite**: email replacement, transition to the password form, and screen checks before password entry.
- Fixed email entry stopping when a domain suggestion appears. Continue-button movement is handled, and temporarily unknown frames pause input. All profiles use **Minus + R3 → X**, with profile selection when needed.
- Game descriptions are requested in the interface language. Missing localized text can use background free translation without a user API key, with caching and bounded retries.
- Expanded built-in translations for **Russian, Ukrainian, Spanish, Italian, French, Polish, and Simplified Chinese**, alongside the original English. R73 contains **946 keys and 6,622 translations**. New menus, notifications, hints, and statistics are localized, with labels refreshed after language changes.

## Updates and diagnostics

- The updater replaces the NRO at its actual launch path while retaining its filename, including files placed directly in SD:/switch. Added file and SHA-256 checks, a backup, and installation recovery procedures.
- The stream menu shows incoming/decoded/presented FPS, bitrate, RTT, loss, queues and latency, the actual NVDEC/Software decoder, and connection state.
- Added local diagnostic ZIP reports with time series, stall context, server-selection history, and connection stages, plus dedicated connection, proxy, and HOME-installer logs.
- Diagnostic writes run through a bounded background queue. Tokens and sensitive input are filtered, and auto-login credential files are excluded from reports.
- Expanded host tests and CI covering policies, protocols, libraries/caches, memory, input, and localization. Parts of large modules have been separated into testable components.

## Limitations and further testing

- Extended play sessions, input after two hours, brief network interruptions, relaunching, and session continuation, including Alliance providers.
- Auto/Closest across different routes: NVIDIA may reassign the server. Minimum ping, constant 60 FPS, and freedom from external packet loss are not guaranteed.
- Large libraries, returning from HOME shortcuts, in-place NRO updates, and every auto-login profile. Game/Epic profiles store credentials as plain text on SD; CAPTCHA and 2FA remain manual, and changed login forms may require adaptation.
- Large HOME tiles: the system icon remains 256×256, and the HOME cache may require a console restart. Switching to the separate installer may also expose a system black transition.
- Translations and long-label layout on the console. Arbitrary server text, game/brand names, and technical identifiers are retained. Free description translation depends on service availability and quotas; failures keep the original text.

