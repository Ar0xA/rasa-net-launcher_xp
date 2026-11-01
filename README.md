# Infinite Rasa Launcher for Windows XP

A native Win32 port of the [Infinite Rasa launcher](https://github.com/InfiniteRasa/rasa-net-launcher) for playing Tabula Rasa on [Infinite Rasa](https://infiniterasa.org). It runs on Windows XP (SP2 or SP3, 32-bit or x64) and on every later Windows.

The Tauri launcher cannot run on XP: WebView2, Rust's standard library and current TLS all need Windows 7 or later. This version is a single 32-bit `.exe` with no installer and no runtime to install.

It does what the Tauri launcher does on Windows:

- finds your Tabula Rasa 1.16.5.0 install (or lets you pick the folder),
- checks the game files against the same known-good manifest and lists anything missing or modified,
- shows community news and patch notes from the Infinite Rasa site,
- lets you pick an auth server (Infinite Rasa by default, or add your own, e.g. `localhost:2106`),
- can remember your username, and optionally your password, per server,
- can skip the intro movie and close itself when the game starts.

It reads and writes the same `settings.json` (`%APPDATA%\org.infiniterasa.launcher`) and the same Credential Manager entries, so it can be used next to the Tauri launcher.

Not included: self-update (download new releases from GitHub) and the Linux/Wine support.

The launcher does **not** download, patch or repair game files. If files are reported missing or modified, reinstall a clean Tabula Rasa 1.16.5.0 client.

## How it works on XP

- **HTTPS.** XP's own TLS stops at TLS 1.0 without modern ciphers or SNI, which the news site (behind Cloudflare) refuses. The launcher brings its own TLS 1.2 stack, [BearSSL](https://bearssl.org) (`third_party/bearssl`), and validates certificates against the Mozilla root store compiled into the exe (`third_party/cacert.pem` → `src/roots.inc`).
- **C runtime.** Current Visual Studio's static C runtime imports five kernel32 functions XP lacks (`FlsAlloc`, `FlsGetValue`, `FlsSetValue`, `FlsFree`, `InitializeCriticalSectionEx`), and XP refuses to start an exe that imports a missing function. `src/xp_imports.asm` redirects those imports to `src/xp_shims.cpp`, which uses the real function where Windows has it and the XP equivalent otherwise.
- **Checked at build time.** `tools/check-xp-imports.ps1` runs after every build and fails it if the exe targets anything newer than Windows 5.01 or imports a known post-XP function.
- **Passwords.** Saved in Windows Credential Manager. XP Home cannot keep generic credentials between sessions, so there they are encrypted with DPAPI for your Windows account and kept in `%APPDATA%\org.infiniterasa.launcher\passwords`. A saved password is passed to the game on its command line at launch, which other programs on the same PC can read while the game runs.
- **Hashing.** SHA-256 is built in (`src/sha256.cpp`); XP's CryptoAPI only has it from SP3.
- **Icon.** XP cannot read PNG-compressed icons, so `res/icon.ico` holds plain 32-bit images (`tools/make-xp-icon.ps1`).

## Building

Prerequisites: Visual Studio or the Build Tools with "Desktop development with C++" (2022 or later). Nothing else; the XP-era SDKs and toolsets are not needed.

```bat
build.cmd              :: build\InfiniteRasaLauncherXP.exe
build.cmd test         :: also builds and runs build\tests.exe (fetches the live news feed)
build.cmd test --offline
build.cmd clean        :: rebuild everything, including BearSSL
```

GitHub Actions (`.github/workflows/build.yml`) builds and tests every push. Pushing a `v*` tag publishes a GitHub Release with the `.exe` and `SHA256SUMS.txt`, with the version taken from the tag:

```bat
git tag v1.0.0
git push origin v1.0.0
```

## Configuration

- **Default auth server**: `Settings::Defaults` in [`src/settings.cpp`](src/settings.cpp).
- **News feed**: `kFeedHost` / `kFeedPath` in [`src/news.cpp`](src/news.cpp).
- **Version**: set from the release tag in CI; for local builds, `APP_VERSION_STR` in [`src/resource.h`](src/resource.h).
- **File manifest**: [`res/manifest.json`](res/manifest.json), the same file as the Tauri launcher's `src-tauri/manifest.json`.
- **Root certificates**: replace `third_party/cacert.pem` with a newer [curl CA bundle](https://curl.se/docs/caextract.html) and run `tools\gen-roots.ps1`.

## License

The launcher source code is released under the [MIT License](LICENSE). BearSSL is under its own MIT-style license (`third_party/bearssl/LICENSE.txt`); the CA bundle is from Mozilla via curl (MPL 2.0).

The Infinite Rasa name and logo are not covered by that license. No game files are included.

## Legal notice

Richard Garriott’s Tabula Rasa and its derivatives are Copyright of NC Interactive, LLC. Tabula Rasa and all associated logos or designs are trademarks or registered trademarks of NC Corporation. All rights reserved.

Infinite Rasa is a non-commercial, community-run fan project. It is not affiliated with, endorsed by, or operated under license from NC Corporation, NC Interactive, LLC or Destination Games.
