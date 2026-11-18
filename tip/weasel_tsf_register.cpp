// src/weasel_tsf_register.cpp
//
// 本仓 TIP 的注册实现，**替代**上游 `3rdparty/weasel/WeaselTSF/Register.cpp`
// （上游那份不参与编译，排除规则见 xmake.lua 的 `del_files`）。
//
// ── 为什么不直接用上游的 ────────────────────────────────────────────────
// 上游的 RegisterServer() 写 **HKEY_CLASSES_ROOT**（即 HKLM\Software\Classes），
// 上游的 RegisterProfiles() 用 `ITfInputProcessorProfileMgr::RegisterProfile`，
// 两者都是**机器级**注册 → 需要管理员权限。而本仓的安装形态是
// 「每用户、免提权」（设计文档 §6.6）。所以注册策略改由本文件实现：
//   * COM 类：写 **HKCU\Software\Classes\CLSID\...**
//   * 语言配置文件：`ITfInputProcessorProfiles::AddLanguageProfile` +
//     `EnableLanguageProfile`（逐用户）
//
// ── 为什么用「官方 Weasel 的 CLSID」而不是本仓自己的 ────────────────────
// 上游 `WeaselTSF::OnActivated` 只在 `clsid == c_clsidTextService` 时接管，
// 而 c_clsidTextService 就是官方的 {A3F4CDED-…}。若拿本仓另一个 CLSID 去注册，
// TSF 会把本 DLL 加载起来，但 OnActivated 会直接 `return S_OK` —— 整个 TIP 变空壳。
//
// 用官方 CLSID + **HKCU 影子注册**正好实现设计文档 §6.6 的「免提权顶掉官方 Weasel」：
// COM 解析 HKCU\Software\Classes\CLSID 优先于 HKLM，所以本 DLL 会顶替机器上
// 官方 Weasel 的 TIP；而用户语言列表里已有的
// `0804:{A3F4CDED-…}{3D02CAB6-…}` 条目直接生效，不需要再往语言列表里加东西。
// 卸载时删掉 HKCU 这几项，官方 Weasel 立刻恢复。
//
// 本文件实现的 6 个函数与上游 `WeaselTSF/Register.h` 的声明一一对应，
// 上游 `WeaselTSF/Server.cpp`（DllRegisterServer / DllUnregisterServer）直接调用它们。

#include "stdafx.h"

#include "Globals.h"
#include "Register.h"

#include <strsafe.h>

namespace {

/// 安装期日志：写到 %TEMP%\weasel-fluent-register.log。
/// 注册失败时 regsvr32 只给一个 E_FAIL，看不出是哪一步挂了。
void regLog(const wchar_t* fmt, ...) {
    wchar_t line[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(line, _countof(line), _TRUNCATE, fmt, args);
    va_end(args);

    wchar_t tempDir[MAX_PATH] = {0};
    ::GetTempPathW(_countof(tempDir), tempDir);
    std::wstring path = std::wstring(tempDir) + L"weasel-fluent-register.log";

    HANDLE file = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    ::WriteFile(file, line, static_cast<DWORD>(wcslen(line) * sizeof(wchar_t)), &written, nullptr);
    ::WriteFile(file, L"\r\n", 2, &written, nullptr);
    ::CloseHandle(file);
}

/// 语言配置 id：与上游 WeaselTSF/Globals.h 一致。
constexpr LANGID kLangIds[] = {
    MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED),
    MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL),
    MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_HONGKONG),
    MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_MACAU),
    MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SINGAPORE),
};

/// 语言栏里显示的名字。官方 Weasel 用 `get_weasel_ime_name()`（返回「小狼毫」/「Weasel」），
/// 本仓强调是 Fluent 前端，故加后缀区分。
const wchar_t* kTextServiceDesc = L"小狼毫 (Fluent)";

void debugLog(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    OutputDebugStringW(buf);
    regLog(L"%s", buf);
}

/// 写一个 REG_SZ 值（valueName 为 nullptr 时写「默认值」）。
bool setRegString(HKEY root, const wchar_t* subKey, const wchar_t* valueName,
                  const wchar_t* data) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD bytes = static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t));
    const LONG rc = RegSetValueExW(key, valueName, 0, REG_SZ,
                                   reinterpret_cast<const BYTE*>(data), bytes);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

void deleteRegTree(HKEY root, const wchar_t* subKey) {
    RegDeleteTreeW(root, subKey);
}

/// 写一个 REG_DWORD 值。
bool setRegDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName,
                 DWORD data) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const LONG rc = RegSetValueExW(key, valueName, 0, REG_DWORD,
                                   reinterpret_cast<const BYTE*>(&data),
                                   sizeof(data));
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

/// 把 GUID 变成 "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}"。
std::wstring guidToString(REFGUID guid) {
    wchar_t buf[64] = {0};
    StringFromGUID2(guid, buf, static_cast<int>(_countof(buf)));
    return std::wstring(buf);
}

/// 把 TIP 加进**当前用户的语言列表**。
///
/// 用系统的 `InstallLayoutOrTip`（imm32.dll 导出）—— 这是微软文档里
/// 「把某个 TIP 加进用户语言列表」的正路，写的是 HKCU，不需要管理员。
bool installLanguageProfileTip(const std::wstring& tipString) {
    HMODULE imm32 = ::LoadLibraryW(L"imm32.dll");
    if (!imm32) {
        return false;
    }
    typedef BOOL(WINAPI* PfnInstallLayoutOrTip)(LPCWSTR);
    PfnInstallLayoutOrTip fn = reinterpret_cast<PfnInstallLayoutOrTip>(
        ::GetProcAddress(imm32, "InstallLayoutOrTip"));
    if (!fn) {
        ::FreeLibrary(imm32);
        return false;
    }
    const BOOL ok = fn(tipString.c_str());
    ::FreeLibrary(imm32);
    return ok != FALSE;
}

/// 逐用户写语言 profile + 加进用户语言列表。
///
/// `ITfInputProcessorProfiles::AddLanguageProfile` 走的是**机器级**注册路径，
/// 没有管理员权限时它必然 E_FAIL（实测：本机官方 Weasel 是 system32\weasel.dll，
/// profile 已经在机器级注册好了，AddLanguageProfile 照样 E_FAIL）。
///
/// 所以逐用户注册走两条**不需要管理员**的路：
///   1. 直接按 TSF 的注册表布局写 HKCU 那一份语言 profile
///      （HKCU\Software\Microsoft\CTF\TIP\{CLSID}\LanguageProfile\0x0000xxxx\{ProfileGUID}
///        的 Description / IconFile / IconIndex / Enable）；
///      TSF 读取时 HKCU 优先于 HKLM，规则和 COM 类一样。
///   2. 用系统的 `InstallLayoutOrTip` 把这个 TIP 加进**用户语言列表**。
BOOL writeUserLanguageProfiles(const wchar_t* dllPath) {
    const std::wstring clsidStr = guidToString(c_clsidTextService);
    const std::wstring profileStr = guidToString(c_guidProfile);

    for (LANGID langId : kLangIds) {
        wchar_t langKey[16] = {0};
        swprintf_s(langKey, _countof(langKey), L"0x%08X", static_cast<unsigned>(langId));
        const std::wstring key = std::wstring(L"Software\\Microsoft\\CTF\\TIP\\") +
                                 clsidStr + L"\\LanguageProfile\\" + langKey + L"\\" +
                                 profileStr;
        const bool ok =
            setRegString(HKEY_CURRENT_USER, key.c_str(), L"Description", kTextServiceDesc) &&
            setRegString(HKEY_CURRENT_USER, key.c_str(), L"IconFile", dllPath) &&
            setRegDword(HKEY_CURRENT_USER, key.c_str(), L"IconIndex", 0) &&
            setRegDword(HKEY_CURRENT_USER, key.c_str(), L"Enable", 1);
        debugLog(L"[weasel-fluent] RegisterProfiles: HKCU 语言 profile(0x%04x) %s", langId,
                 ok ? L"OK" : L"失败");
    }

    const std::wstring tipString = std::wstring(L"0804:") + clsidStr + profileStr;
    const bool installed = installLanguageProfileTip(tipString);
    debugLog(L"[weasel-fluent] RegisterProfiles: InstallLayoutOrTip(%s) %s",
             tipString.c_str(), installed ? L"OK" : L"未生效（可能已有同一条目）");
    return TRUE;
}

} // namespace

// ── COM 类注册（HKCU 影子） ─────────────────────────────────────────────
BOOL RegisterServer() {
    wchar_t dllPath[MAX_PATH] = {0};
    if (GetModuleFileNameW(g_hInst, dllPath, _countof(dllPath)) == 0) {
        return FALSE;
    }

    // HKCU\Software\Classes\CLSID\{官方 CLSID}
    //   (default)              = 文本服务显示名
    //   InprocServer32\(default)= 本 DLL 路径
    //   InprocServer32\ThreadingModel = Apartment
    const std::wstring clsidKey =
        L"Software\\Classes\\CLSID\\" + guidToString(c_clsidTextService);
    const std::wstring inprocKey = clsidKey + L"\\InprocServer32";

    if (!setRegString(HKEY_CURRENT_USER, clsidKey.c_str(), nullptr, kTextServiceDesc)) {
        debugLog(L"[weasel-fluent] RegisterServer: 写 CLSID 描述失败 (err=%lu)", GetLastError());
        return FALSE;
    }
    if (!setRegString(HKEY_CURRENT_USER, inprocKey.c_str(), nullptr, dllPath)) {
        debugLog(L"[weasel-fluent] RegisterServer: 写 InprocServer32 失败\n");
        return FALSE;
    }
    if (!setRegString(HKEY_CURRENT_USER, inprocKey.c_str(), L"ThreadingModel",
                      L"Apartment")) {
        debugLog(L"[weasel-fluent] RegisterServer: 写 ThreadingModel 失败\n");
        return FALSE;
    }
    return TRUE;
}

void UnregisterServer() {
    // 只删 HKCU 这一份影子；机器上官方 Weasel 的 HKLM 注册保持不动，
    // 卸载本仓后官方 Weasel 立即恢复可用。
    const std::wstring clsidKey =
        L"Software\\Classes\\CLSID\\" + guidToString(c_clsidTextService);
    deleteRegTree(HKEY_CURRENT_USER, clsidKey.c_str());
}

// ── 语言配置文件（逐用户） ──────────────────────────────────────────────
BOOL RegisterProfiles() {
    wchar_t dllPath[MAX_PATH] = {0};
    if (GetModuleFileNameW(g_hInst, dllPath, _countof(dllPath)) == 0) {
        return FALSE;
    }
    const ULONG descLen = static_cast<ULONG>(wcslen(kTextServiceDesc));
    const ULONG pathLen = static_cast<ULONG>(wcslen(dllPath));

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ITfInputProcessorProfiles* profiles = nullptr;
    const HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                        CLSCTX_INPROC_SERVER,
                                        IID_ITfInputProcessorProfiles,
                                        reinterpret_cast<void**>(&profiles));
    if (FAILED(hr) || !profiles) {
        debugLog(L"[weasel-fluent] RegisterProfiles: 取 ITfInputProcessorProfiles 失败 hr=0x%08X（非致命，走 HKCU）", hr);
        CoUninitialize();
        return writeUserLanguageProfiles(dllPath);
    }
    debugLog(L"[weasel-fluent] RegisterProfiles: 拿到 ITfInputProcessorProfiles");

    // 机器级注册（需要管理员；官方 Weasel 已装时这一步本来就在）。
    // 失败不致命 —— 逐用户注册才是本仓的主路径。
    const HRESULT regHr = profiles->Register(c_clsidTextService);
    debugLog(L"[weasel-fluent] RegisterProfiles: Register(clsid) hr=0x%08X", regHr);

    BOOL ok = TRUE;
    for (LANGID langId : kLangIds) {
        const HRESULT add = profiles->AddLanguageProfile(
            c_clsidTextService, langId, c_guidProfile, kTextServiceDesc, descLen,
            dllPath, pathLen, 0);
        if (FAILED(add)) {
            debugLog(L"[weasel-fluent] RegisterProfiles: AddLanguageProfile(0x%04x) hr=0x%08X（非致命，走 HKCU）",
                     langId, add);
            continue;
        }
        debugLog(L"[weasel-fluent] RegisterProfiles: AddLanguageProfile(0x%04x) OK", langId);
        profiles->EnableLanguageProfile(c_clsidTextService, langId, c_guidProfile,
                                        TRUE);
    }

    profiles->Release();
    CoUninitialize();
    return writeUserLanguageProfiles(dllPath);
}

void UnregisterProfiles() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ITfInputProcessorProfiles* profiles = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfiles,
                                   reinterpret_cast<void**>(&profiles))) &&
        profiles) {
        for (LANGID langId : kLangIds) {
            profiles->RemoveLanguageProfile(c_clsidTextService, langId,
                                           c_guidProfile);
        }
        profiles->Release();
    }

    CoUninitialize();
}

// ── TSF 类别 ────────────────────────────────────────────────────────────
// 与上游 Register.cpp 的 SupportCategories0 一致。
namespace {
const GUID kSupportCategories[] = {
    GUID_TFCAT_CATEGORY_OF_TIP,
    GUID_TFCAT_TIP_KEYBOARD,
    GUID_TFCAT_TIPCAP_SECUREMODE,
    GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    GUID_TFCAT_TIPCAP_COMLESS,
    GUID_TFCAT_TIPCAP_WOW16,
    GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
    GUID_TFCAT_PROP_AUDIODATA,
    GUID_TFCAT_PROP_INKDATA,
    GUID_TFCAT_PROPSTYLE_CUSTOM,
    GUID_TFCAT_PROPSTYLE_STATIC,
    GUID_TFCAT_PROPSTYLE_STATICCOMPACT,
    GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    GUID_TFCAT_DISPLAYATTRIBUTEPROPERTY,
};
} // namespace

BOOL RegisterCategories() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ITfCategoryMgr* catMgr = nullptr;
    if (FAILED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                reinterpret_cast<void**>(&catMgr))) ||
        !catMgr) {
        debugLog(L"[weasel-fluent] RegisterCategories: 取 ITfCategoryMgr 失败\n");
        CoUninitialize();
        // 类别注册失败不致命：TIP 本身仍能工作，只是语言栏里可能不出现。
        return TRUE;
    }

    for (const auto& guid : kSupportCategories) {
        if (FAILED(catMgr->RegisterCategory(c_clsidTextService, guid,
                                            c_clsidTextService))) {
            debugLog(L"[weasel-fluent] RegisterCategories: 注册类别失败（忽略）\n");
            break;
        }
    }

    catMgr->Release();
    CoUninitialize();
    return TRUE;
}

void UnregisterCategories() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ITfCategoryMgr* catMgr = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                   reinterpret_cast<void**>(&catMgr))) &&
        catMgr) {
        for (const auto& guid : kSupportCategories) {
            catMgr->UnregisterCategory(c_clsidTextService, guid, c_clsidTextService);
        }
        catMgr->Release();
    }

    CoUninitialize();
}
