#include "stdafx.h"
#include <filesystem>
#include <string>
#include <WeaselUtility.h>

fs::path WeaselUserDataPath() {
  WCHAR _path[MAX_PATH] = {0};
  // [weasel-fluent] 允许用环境变量 WEASEL_USER_DIR 覆盖用户 Rime 目录。
  // 用途：自检套件需要**隔离**的 Rime 目录 —— 机器上装着官方 Weasel 时，
  // 官方 WeaselServer.exe 正占着 %APPDATA%\Rime 里用户词库的 LOCK 文件，
  // 本仓的引擎就拿不到词库（只读），输入出不来候选。环境变量是进程级的，
  // 不会动用户的注册表配置。
  const DWORD envLen =
      ::GetEnvironmentVariableW(L"WEASEL_USER_DIR", _path, _countof(_path));
  if (envLen > 0 && envLen < _countof(_path)) {
    return fs::path(_path);
  }
  const WCHAR KEY[] = L"Software\\Rime\\Weasel";
  HKEY hKey;
  LSTATUS ret = RegOpenKey(HKEY_CURRENT_USER, KEY, &hKey);
  if (ret == ERROR_SUCCESS) {
    DWORD len = sizeof(_path);
    DWORD type = 0;
    DWORD data = 0;
    ret =
        RegQueryValueEx(hKey, L"RimeUserDir", NULL, &type, (LPBYTE)_path, &len);
    RegCloseKey(hKey);
    if (ret == ERROR_SUCCESS && type == REG_SZ && _path[0]) {
      return fs::path(_path);
    }
  }
  // default location
  ExpandEnvironmentStringsW(L"%AppData%\\Rime", _path, _countof(_path));
  return fs::path(_path);
}

fs::path WeaselSharedDataPath() {
  wchar_t _path[MAX_PATH] = {0};
  GetModuleFileNameW(NULL, _path, _countof(_path));
  return fs::path(_path).remove_filename().append("data");
}

std::string GetCustomResource(const char* name, const char* type) {
  const HINSTANCE module = 0;  // main executable
  HRSRC hRes = FindResourceA(module, name, type);
  if (hRes) {
    HGLOBAL hData = LoadResource(module, hRes);
    if (hData) {
      const char* data = (const char*)::LockResource(hData);
      size_t size = ::SizeofResource(module, hRes);

      if (data && size) {
        if (data[size - 1] == '\0')  // null-terminated string
          size--;
        return std::string(data, size);
      }
    }
  }

  return std::string();
}
