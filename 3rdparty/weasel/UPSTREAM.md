# Vendored upstream: rime/weasel

- **Repository**: https://github.com/rime/weasel
- **Commit**: `f96ed24a8a535961b6ffc7160d98851a69cee4f8`
- **Vendored on**: 2026-11-16
- **License**: GPLv3 — full text in [`LICENSE.txt`](LICENSE.txt)

This directory holds the parts of the official Weasel project that constitute a
battle-tested Windows TSF input-method implementation. Weasel ships the same six
capabilities this project needs (scheme selection + deployment, user dictionary
management, skin/style settings, key bindings, installer, tray), but with legacy
Win32 menus. The goal of this fork is to keep Weasel's engine and replace its
menus with a modern Qt 6 / Fluent-Qt interface.

## Adopted modules

| Module | Contents | Used for |
| :--- | :--- | :--- |
| `include/` | `WeaselIPC.h`, `WeaselIPCData.h`, `WeaselStyleColor.h`, `WeaselStyleKeys.h`, `PipeChannel.h`, `ResponseParser.h`, `WeaselConstants.h`, `WeaselUtility.h`, `include/wtl/` | Shared protocol types, style tokens, WTL headers |
| `WeaselIPC/` | `Configurator`, `Styler`, `ActionLoader`, `Committer`, `ContextUpdater`, `ResponseParser`, `PipeChannel`, `WeaselClientImpl` | Client side of the Weasel IPC protocol — scheme config, style, key actions |
| `WeaselIPCServer/` | `WeaselServerImpl`, `SecurityAttribute` | Server side of the protocol |
| `RimeWithWeasel/` | `RimeWithWeasel`, `WeaselUtility` | librime wrapper used by the Weasel server |

## Deliberately excluded

| Module | Reason |
| :--- | :--- |
| `WeaselUI/` | Legacy Direct2D candidate panel. Superseded by the Fluent Qt candidate window; keeping both would render two candidate lists at once. |
| `WeaselTSF/` | The full TSF TIP. Arrives in Round 2 behind a Qt bridge (`CCandidateList` → Fluent candidate window). |
| `WeaselDeployer/` | Legacy Win32 dialogs for scheme / dictionary / style / key settings. Superseded by the Fluent settings center. |
| `WeaselServer/` | Legacy Win32 tray icon and service shell. Superseded by the Fluent tray menu and the Qt service. |
| `WeaselSetup/` | x86-only installer of the previous generation. Rewritten as `scripts/install.ps1` in Round 2. |

## Build prerequisites

The adopted modules include Boost headers (`boost/interprocess`,
`boost/archive`, `boost/thread`). Build them with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_boost.ps1
```

which produces `BOOST_ROOT` (headers) plus `BOOST_ROOT/stage/lib` (static
libraries, `/MD` to match Qt 6.8.2's official binaries).

Linking the adopted modules into the application is deferred to Round 2; until
then they are vendored and compile-verified by
`scripts/verify_weasel_vendor.ps1`.
