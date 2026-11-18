// tip/weasel_fluent_dllmain.cpp
//
// 本仓 TIP DLL 的**入口点**。
//
// 为什么需要它：UCRT 的 `DllMainCRTStartup` 是用
// `GetProcAddress(hmod, "DllMain")` **动态查找**用户 DllMain 的 ——
// DllMain 不在 DLL 的导出表里就永远不会被调用。上游 WeaselTSF 的
// `dllmain.cpp` 里那个 DllMain 于是从没跑过，`Globals.cpp` 的 `g_hInst`
// 一直是空，后果有两个：
//   * 注册时 `GetModuleFileNameW(g_hInst, ...)` 拿到的是**宿主进程**
//     （regsvr32.exe）的路径 ⇒ 注册表里的 InprocServer32 指向一个错的 DLL；
//   * 语言栏的菜单与图标（`LoadMenuW` / `LoadImageW(g_hInst, ...)`）全部失效。
//
// 所以：
//   1. 上游的 DllMain 被 vendor 补丁 0007 改名为 `WeaselFluentDllMain`；
//   2. 本文件提供 C 链接的 `DllMain` 转发给它；
//   3. `tip/weasel-fluent-tsf.def` 把 `DllMain` 导出，UCRT 才能动态找到它。
//
// 两条机制（UCRT 的动态查找、链接期的直接调用）都指向这一个入口。

#include "stdafx.h"

#include "Globals.h"

/// 上游 WeaselTSF/dllmain.cpp 里的初始化逻辑（vendor 补丁 0007 改的名）。
BOOL WINAPI WeaselFluentDllMain(HINSTANCE hInstance, DWORD dwReason,
                                LPVOID pvReserved);

extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason,
                                LPVOID pvReserved) {
    return WeaselFluentDllMain(hInstance, dwReason, pvReserved);
}
