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
| `0003-isolated-server-instance-mutex.patch` | Makes the server's single-instance mutex prefix overridable (`WEASEL_SERVER_INSTANCE_PREFIX`). Upstream's `(WEASEL)Furandōru-Sukāretto-<user>` mutex is already held by an installed official `WeaselServer.exe`, which makes `weasel::Server::Start()` return 0 — our server could not start at all. |
| `0004-atl-free-message-loop.patch` | Drops `_Module.AddMessageLoop()` / `RemoveMessageLoop()` from `ServerImpl::Run()`. `CAppModule::AddMessageLoop` dereferences `m_pMsgLoopMap`, which is only allocated by `CAppModule::Initialize()`; without it the service crashed with `0xC0000005` on startup. This project is a Qt app and does not run ATL's module lifecycle (forcing `Initialize()` would change the whole process's COM apartment), and WTL's `CMessageLoop::Run()` is self-contained. |
| `0005-null-safe-strings-and-server-side-context.patch` | (a) Replaces the unguarded `u8tow(ptr)` calls with a NULL-safe wrapper: `u8tow` builds a `std::string` from a `const char*`, and a NULL pointer there is undefined behaviour (MSVC throws, which terminates the server thread). librime really does hand out NULL for an empty commit string or a session without a valid schema. (b) **Fills the server-side `weasel::Context`** in `_UpdateUI()` by calling the upstream `_GetContext()` — which upstream defines but never calls. Upstream's candidate list lives in the *TIP* process and is fed by the IPC response; this project's Fluent candidate window lives in the *server* process, so the server has to fetch the context itself. Without this the bridge saw an always-empty context. |
| `0006-weasel-user-dir-env-override.patch` | Lets `WEASEL_USER_DIR` override the user Rime directory. The verification suite needs an isolated Rime directory: with the official Weasel running, its server holds the LOCK file of the user dictionary, so our engine could only open it read-only and produced no candidates. The override is process-scoped and does not touch the user's registry. |
| `0007-explicit-dllmain-entry.patch` | Renames upstream's `DllMain` to `WeaselFluentDllMain`; the real entry point is provided by `tip/weasel_fluent_dllmain.cpp`. UCRT's `DllMainCRTStartup` looks the user `DllMain` up **dynamically** via `GetProcAddress(hmod, "DllMain")`, so a `DllMain` missing from the export table never runs. Upstream's `.def` exports only the four COM entry points, so upstream's `DllMain` never ran and `Globals.cpp`'s `g_hInst` stayed NULL — registration then wrote the **host process** path (`regsvr32.exe`) into `InprocServer32`, and the language bar menu/icons silently failed. |
| `0008-server-side-dpi-normalization.patch` | Converts the composition rectangle from the **host process's** coordinate space to the 96-DPI logical space the Fluent candidate window is positioned in. `ITfContextView::GetTextExt` returns **physical** pixels when the host is Per-Monitor/System DPI aware (Chromium/Electron/VS Code all are), but Qt positions windows in 96-DPI logical pixels — on a 150% display the candidate window therefore landed 1.5× too far to the right of the caret. The conversion happens in the **IPC server**, not in the TIP: the TIP DLL is loaded **once per host process**, so a conversion in the TIP would only take effect after every running application was restarted, whereas the server converts for all of them at once. The server reads the foreground host window's awareness (`GetWindowDpiAwarenessContext`) and DPI (`GetDpiForWindow`); DPI-unaware hosts are skipped, because the OS already virtualizes their coordinates into the 96-DPI space. Both APIs are resolved with `GetProcAddress` rather than called directly, so the patch does not depend on the consuming project's `_WIN32_WINNT` level and keeps working in a build that still targets Windows 8.1 (upstream Weasel's floor), where the Windows SDK does not declare them. |

### The DPI conversion lives in exactly one place

The wire protocol carries the **host's** coordinates in `WEASEL_IPC_UPDATE_INPUT_POS`,
and the IPC server normalizes them to 96-DPI logical coordinates. Neither the TIP
nor the Fluent candidate window may scale them again.

Two earlier attempts are worth recording, because both produced a *green build
with a visibly wrong window*:

* **Server-side, but broken.** The first version of this patch called
  `GetDpiForMonitor` through a typedef that was missing the `MONITOR_DPI_TYPE`
  parameter, so the call always failed, and then relied on
  `PhysicalToLogicalPointForPerMonitorDPI` — which is a no-op in a process that
  is not per-monitor DPI aware (the server is not, and the flag cannot be
  raised from there). Net effect: the server passed the host's physical pixels
  through unchanged and the Qt front end multiplied them by the device pixel
  ratio. That is exactly the "candidate window is far away from the caret"
  report.
* **TIP-side.** Converting inside the TIP is more precise (the TIP runs *inside*
  the host process and can ask `GetThreadDpiAwarenessContext()` for the host
  thread's real awareness mode), but it only takes effect after every host
  process reloads the TIP DLL.

`scripts/verify-dpi-normalization.ps1` asserts that the conversion appears once,
in the IPC server, and nowhere else.

## Local deviations (not patches)

| What | Why |
| :--- | :--- |
| `WeaselTSF/Register.cpp` is **not compiled**; [`../../tip/weasel_tsf_register.cpp`](../../tip/weasel_tsf_register.cpp) replaces it | Upstream registers the COM class under `HKEY_CLASSES_ROOT` (= HKLM) and the profile through `ITfInputProcessorProfileMgr::RegisterProfile` — both machine-level, i.e. administrator rights. This project installs per user without elevation (design §6.6), so registration is done under HKCU with the per-user `ITfInputProcessorProfiles` API. The replacement implements the same six functions declared in `WeaselTSF/Register.h`, so upstream `WeaselTSF/Server.cpp` links against it unchanged. |
| The TIP registers under the **official** Weasel CLSID `{A3F4CDED-…}` (HKCU shadow) rather than a project-specific one | Upstream `WeaselTSF::OnActivated` only takes over when `clsid == c_clsidTextService`, and that constant *is* the official CLSID. A different CLSID would load the DLL but leave the TIP inert. Shadowing the official CLSID under `HKCU\Software\Classes\CLSID` takes precedence over the machine-wide `HKLM` entry, which is exactly the "replace the official Weasel without elevation" mechanism, and makes the language-list entry that the official install already created work unchanged. |
| The language bar's icons are **ours**, not upstream's: [`../../assets/langbar/*.ico`](../../assets/langbar), generated by [`../../scripts/gen-langbar-icons.py`](../../scripts/gen-langbar-icons.py); the resource script is [`../../tip/weasel-tsf.rc`](../../tip/weasel-tsf.rc) instead of upstream's `WeaselTSF/WeaselTSF.rc` | Upstream's `.rc` does `#include "afxres.h"` — that header only exists with MFC installed, and this machine has ATL but not MFC. Ours therefore uses the resource compiler's built-in `1 VERSIONINFO` block and a numeric `LANGUAGE 0x4,0x2` instead of MFC's macros. Upstream's icons are Windows 3.x-era heavy-bordered squares (a dark-grey 「中」, a rust-red 「A」) that turn to mush at 16 px — the size the language bar actually uses — and do not match this project's Fluent visual language at all. The replacement keeps upstream's glyphs (「中」/「A」/「全」/「半」/「业」) but draws them the way this project's tray icon is drawn (`src/tray_menu.cpp`): a rounded square in a flat colour with a centred white bold glyph, accent blue for Chinese and neutral grey for English. **Without any of this the language bar is silently broken**: the TIP loads its menu and icons from the DLL's resource section at runtime (`LanguageBar.cpp`), so a missing resource yields an empty right-click menu and no icons while the build stays green. Two non-obvious traps are encoded in the scripts: the icon paths are relative to the `.rc`'s own directory, and the `ICON` resource id must be **numeric** (an undefined identifier becomes a *named* resource, which Windows ignores when picking the application icon). |
| `RimeWithWeaselHandler` is subclassed in [`../../src/weasel_engine.cpp`](../../src/weasel_engine.cpp) (`TrackingHandler`), which **guards every session-consuming call with a liveness check** | Upstream's `to_session_id(ipc_id)` is `m_session_status_map[ipc_id].session_id` — it hands the stored Rime session id straight to librime. A maintenance operation (deploy / sync user data) rebuilds librime's `SessionManager`, so every stored session id becomes stale while the TIP still holds its `ipc_id`. librime 1.17 does **not** null-check the session in `clear_composition` / `process_key` / `set_option`, so the next message carrying that `ipc_id` dereferences a dead session and the whole service process dies with `0xC0000005` inside `rime.dll` (measured 2026-11-22: one click on "Sync user data" in the language bar, then the next ordinary message, and the IME service was gone). `TrackingHandler::sessionAlive()` asks librime first (`find_session` only looks the session up in a table and is therefore safe) and drops the message when the session is gone. The session itself only comes back when the TIP re-activates (losing and regaining focus gives it a fresh `ipc_id`). |

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
