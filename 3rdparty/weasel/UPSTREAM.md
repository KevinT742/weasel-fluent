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
| `WeaselTSF/` | The full TSF text service (composition, inline pre-edit, language bar, display attributes, compartments) | The TIP — talks to our server over the Weasel IPC protocol |
| `WeaselUI/` | `UIImpl`, `WeaselPanel`, D2D layouts | **Data carrier only** — the Direct2D panel window is never created (see patch 0001) |

## Deliberately excluded

| Module | Reason |
| :--- | :--- |
| `WeaselDeployer/` | Legacy Win32 dialogs for scheme / dictionary / style / key settings. Superseded by the Fluent settings center. |
| `WeaselServer/` | Legacy Win32 tray icon and service shell. Superseded by the Fluent tray menu and the Qt service (`src/weasel_engine.cpp` takes over the server role). |
| `WeaselSetup/` | x86-only installer of the previous generation. Rewritten as `scripts/install.ps1` in Round 2. |

`WeaselUI/` and `WeaselTSF/` were listed as "excluded" in Round 1; Round 2 (T2/T5)
adopted both. `WeaselUI` is required because `WeaselTSF`'s `CCandidateList` owns a
`std::unique_ptr<weasel::UI>` — without it the TIP does not even compile.

## Local patches

Applied by `scripts/apply-weasel-patches.ps1` (idempotent; patch paths are
relative to the repository root).

| Patch | What it does |
| :--- | :--- |
| `0001-disable-d2d-candidate-panel.patch` | Removes `_ui->Create()` from `CCandidateList::_MakeUIWindow`, so the TIP never creates the Direct2D candidate panel. The Fluent candidate window stays the single visible candidate list (design rule R4). `weasel::UI` still stores Context/Status, so inline pre-edit and `ITfCandidateListUIElement` keep working. |
| `0002-isolated-ipc-pipe-and-per-user-root.patch` | (a) Makes `WEASEL_IPC_PIPE_NAME` overridable at compile time. The build force-includes `build_defines/weasel_fluent_defines.h`, which sets it to `WeaselFluentNamedPipe` so our server does not fight an installed official `WeaselServer.exe` for the same named pipe. (b) Makes the TIP look up `HKCU\Software\Rime\Weasel\WeaselRoot` before `HKLM`, so a per-user install can point the TIP at its own `start_service.bat`. |

## Local deviations (not patches)

| What | Why |
| :--- | :--- |
| `WeaselTSF/Register.cpp` is **not compiled**; [`../../tip/weasel_tsf_register.cpp`](../../tip/weasel_tsf_register.cpp) replaces it | Upstream registers the COM class under `HKEY_CLASSES_ROOT` (= HKLM) and the profile through `ITfInputProcessorProfileMgr::RegisterProfile` — both machine-level, i.e. administrator rights. This project installs per user without elevation (design §6.6), so registration is done under HKCU with the per-user `ITfInputProcessorProfiles` API. The replacement implements the same six functions declared in `WeaselTSF/Register.h`, so upstream `WeaselTSF/Server.cpp` links against it unchanged. |
| The TIP registers under the **official** Weasel CLSID `{A3F4CDED-…}` (HKCU shadow) rather than a project-specific one | Upstream `WeaselTSF::OnActivated` only takes over when `clsid == c_clsidTextService`, and that constant *is* the official CLSID. A different CLSID would load the DLL but leave the TIP inert. Shadowing the official CLSID under `HKCU\Software\Classes\CLSID` takes precedence over the machine-wide `HKLM` entry, which is exactly the "replace the official Weasel without elevation" mechanism, and makes the language-list entry that the official install already created work unchanged. |

## Build prerequisites

The adopted modules include Boost headers (`boost/interprocess`,
`boost/archive`, `boost/thread`). Build them with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_boost.ps1
```

which produces `BOOST_ROOT` (headers) plus `BOOST_ROOT/stage/lib` (static
libraries, `/MD` to match Qt 6.8.2's official binaries).

Boost 1.88's `b2` silently skips `libs/{thread,chrono,filesystem,json}`, so the
static libraries are produced by `scripts/build_boost_libs.ps1`, which invokes
`cl.exe` directly. `BOOST_THREAD_BUILD_LIB` is required, otherwise
`boost::tss_cleanup_implemented()` is missing at link time.
