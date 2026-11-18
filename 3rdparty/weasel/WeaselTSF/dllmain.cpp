// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include "Globals.h"
#include <stdio.h>
#include <Windows.h>
#include <DbgHelp.h>
#include <tchar.h>
#include <time.h>
#pragma comment(lib, "DbgHelp.lib")

LONG WINAPI _UnhandledExceptionFilter(EXCEPTION_POINTERS* pExceptionPointers) {
  TCHAR szFileName[MAX_PATH];
  TCHAR szAppName[MAX_PATH];
  TCHAR szDllName[MAX_PATH];
  DWORD dwProcessId = GetCurrentProcessId();
  SYSTEMTIME st;
  TCHAR szDateTime[20];  // 用于存储日期时间字符串
  // 获取当前系统时间
  GetLocalTime(&st);
  // 格式化日期时间字符串
  _stprintf_s(szDateTime, 20, _T("%04d%02d%02d-%02d%02d%02d"), st.wYear,
              st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  // 获取应用程序名称
  GetModuleFileName(NULL, szAppName, MAX_PATH);
  // 从完整路径中提取应用程序名称
  TCHAR* pszBaseName = _tcsrchr(szAppName, '\\');
  if (pszBaseName) {
    pszBaseName++;  // 移过 '\\'
  } else {
    pszBaseName = szAppName;
  }
  // 获取当前dll模块的文件名
  GetModuleFileName(g_hInst, szDllName, MAX_PATH);
  TCHAR* pszDllBaseName = _tcsrchr(szDllName, '\\');
  if (pszDllBaseName) {
    pszDllBaseName++;  // 移过 '\\'
  } else {
    pszDllBaseName = szDllName;
  }
  WCHAR _path[MAX_PATH] = {0};
  // default location
  ExpandEnvironmentStringsW(L"%TEMP%\\rime.weasel", _path, _countof(_path));
  // 构造 dump 文件名：应用程序名称-DLL名称-时间.进程号.dmp
  _stprintf_s(szFileName, MAX_PATH, _T("%s\\%s-%s-%s.%lu.dmp"), _path,
              pszBaseName, pszDllBaseName, szDateTime, dwProcessId);
  HANDLE hDumpFile = CreateFile(szFileName, GENERIC_WRITE, 0, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hDumpFile != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
    dumpInfo.ExceptionPointers = pExceptionPointers;
    dumpInfo.ThreadId = GetCurrentThreadId();
    dumpInfo.ClientPointers = TRUE;
    MiniDumpWriteDump(GetCurrentProcess(), dwProcessId, hDumpFile,
                      MiniDumpNormal, &dumpInfo, NULL, NULL);
    CloseHandle(hDumpFile);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

// [weasel-fluent] 改名为 WeaselFluentDllMain：本文件不再提供名为 DllMain 的入口，
// 入口改由本仓的 tip/weasel_fluent_dllmain.cpp 提供并转发到这里。
//
// 原因：UCRT 的 DllMainCRTStartup 是用 GetProcAddress(hmod, "DllMain") **动态**
// 查找用户的 DllMain 的 —— DllMain 不在 DLL 的导出表里就永远不会被调用，
// 于是上游 Globals.cpp 里的 g_hInst 一直是空：
//   * 注册时 GetModuleFileNameW(g_hInst, ...) 拿到的是**宿主进程**（regsvr32.exe）
//     的路径，注册表里的 InprocServer32 会指向一个错的 DLL；
//   * 语言栏的菜单与图标（LoadMenuW / LoadImageW(g_hInst, ...)）也全部失效。
// 本仓的 tip/weasel-fluent-tsf.def 导出了 DllMain，入口因此能被打到。
BOOL WINAPI WeaselFluentDllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID pvReserved) {
  switch (dwReason) {
    case DLL_PROCESS_ATTACH:
      g_hInst = hInstance;
      SetUnhandledExceptionFilter(_UnhandledExceptionFilter);
      if (!InitializeCriticalSectionAndSpinCount(&g_cs, 0))
        return FALSE;
      break;
    case DLL_PROCESS_DETACH:
      DeleteCriticalSection(&g_cs);
      break;
  }
  return TRUE;
}
