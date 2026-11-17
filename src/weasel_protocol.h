#ifndef WEASEL_PROTOCOL_H
#define WEASEL_PROTOCOL_H

/// 官方 Weasel IPC / TSF 注册相关的协议常量。
///
/// 数值与上游 `include/WeaselIPC.h`、`WeaselTSF/Globals.h` 保持一致；
/// 一旦上游变更，以 `3rdparty/weasel/UPSTREAM.md` 记录的 commit 为准复核。
namespace WeaselProtocol {

/// 上游 IPC 管道名（不含 \\.\pipe\<user>\ 前缀）。
/// 上游用 getUsername() 做每用户隔离，见 include/WeaselIPC.h 的 GetPipeName()。
inline constexpr const wchar_t* kPipeName = L"WeaselNamedPipe";

/// 上游 IPC 服务窗口类名。
inline constexpr const wchar_t* kServerWindowClass = L"WeaselIPCWindow_1.0";

/// 本仓 TSF 文本服务 CLSID。
inline constexpr const char* kClsidFluentTextService = "{D7B1C618-9F3E-4E7B-B96E-5C13D1A5B4C2}";

/// 官方 Weasel 文本服务 CLSID。
/// 安装器靠它做 HKCU 影子注册，从而免提权顶掉机器上已装的官方 Weasel
/// （COM 解析 HKCU\Software\Classes\CLSID 优先于 HKLM）。
inline constexpr const char* kClsidOfficialWeasel = "{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}";

/// 本仓 TSF 的语言配置 id（语言:CLSID:ProfileGUID）。
inline constexpr const char* kTipString =
    "0804:{D7B1C618-9F3E-4E7B-B96E-5C13D1A5B4C2}{E8934A01-C349-4F8A-8512-14AE0293BD01}";

/// 上游 Weasel 的 profile GUID。
inline constexpr const char* kProfileOfficialWeasel = "{3D02CAB6-2B8E-4781-BA20-1C9267529467}";

} // namespace WeaselProtocol

#endif // WEASEL_PROTOCOL_H
