#ifndef WEASEL_PATHS_H
#define WEASEL_PATHS_H

#include <QString>

/// 运行期路径的唯一真源。
///
/// 禁止在其他文件里出现指向特定用户或特定安装位置的绝对路径字面量；
/// 一律通过本命名空间解析。scripts/verify-no-hardcoded-paths.ps1 会强制这一点。
namespace WeaselPaths {

/// librime 核心库 rime.dll 的绝对路径。
/// 探测顺序：应用目录 → 开机自启注册项所在目录 → 系统 PATH。
/// 找不到时返回空串（调用方应转入仿真模式）。
QString rimeDllPath();

/// librime 共享数据目录（含 default.yaml、build/ 等）。
/// 找不到时返回空串。
QString sharedDataDir();

/// 用户 Rime 配置目录（%APPDATA%\Rime），不存在则创建。
QString userDataDir();

/// 本仓配置文件目录（config.json 所在），不存在则创建。
QString configDir();

/// 皮肤文件目录（<configDir>/skins），不存在则创建。
QString skinDir();

/// 发行包所在目录（用于定位随包分发的 rime.dll 与 data/）。
QString installDir();

} // namespace WeaselPaths

#endif // WEASEL_PATHS_H
