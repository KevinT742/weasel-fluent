#ifndef WEASEL_PROTOCOL_H
#define WEASEL_PROTOCOL_H

/// 官方 Weasel IPC / TSF 注册相关的协议常量。
///
/// 数值与上游 `include/WeaselIPC.h`、`WeaselTSF/Globals.h` 保持一致；
/// 一旦上游变更，以 `3rdparty/weasel/UPSTREAM.md` 记录的 commit 为准复核。
namespace WeaselProtocol {

/// 本仓的 IPC 管道名（不含 \\.\pipe\<user>\ 前缀）。
///
/// ⚠️ 与上游的 `WeaselNamedPipe` **不同**：机器上装着官方 Weasel 时，
/// 官方的 WeaselServer.exe 会占住同名管道，本仓的服务就 bind 不起来。
/// 两端（主程序 / TIP）都必须用这个名字，靠
/// `build_defines/weasel_fluent_defines.h` 的 /FI 强制包含统一覆盖
/// （见上游补丁 0002）。
inline constexpr const wchar_t* kPipeName = L"WeaselFluentNamedPipe";

/// 上游 IPC 服务窗口类名。
inline constexpr const wchar_t* kServerWindowClass = L"WeaselIPCWindow_1.0";

/// 本仓 TIP 使用的文本服务 CLSID —— **就是官方 Weasel 的那个**。
///
/// 原因：上游 `WeaselTSF::OnActivated` 只在 `clsid == c_clsidTextService`
/// 时接管，而 `c_clsidTextService` 恒为官方的 {A3F4CDED-…}。若换成本仓
/// 自己的 CLSID，TSF 会把 DLL 加载起来但 OnActivated 直接返回，TIP 变空壳。
///
/// 做法是 **HKCU 影子注册**：COM 解析 HKCU\Software\Classes\CLSID 优先于
/// HKLM，所以本仓的 DLL 顶替机器上官方 Weasel 的 TIP；而官方安装已经写进
/// 用户语言列表的 `0804:{A3F4CDED-…}{3D02CAB6-…}` 条目直接生效，
/// 不需要再往语言列表里加东西。卸载时删掉 HKCU 这几项即可恢复官方 Weasel。
inline constexpr const char* kClsidTextService = "{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}";

/// 文本服务的 profile GUID（与官方 Weasel 一致）。
inline constexpr const char* kProfileGuid = "{3D02CAB6-2B8E-4781-BA20-1C9267529467}";

/// 本仓 TSF 的语言配置 id（语言:CLSID:ProfileGUID）。
inline constexpr const char* kTipString =
    "0804:{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}{3D02CAB6-2B8E-4781-BA20-1C9267529467}";

} // namespace WeaselProtocol

#endif // WEASEL_PROTOCOL_H
