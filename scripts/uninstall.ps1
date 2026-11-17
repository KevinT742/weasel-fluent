# scripts/uninstall.ps1
# 卸载 Weasel-Fluent（每用户，免管理员）。
#
# 做的事：
#   1. 停掉本仓的服务与官方 WeaselServer；
#   2. 反注册本仓的 TIP DLL（清掉 HKCU 的影子注册）；
#   3. 移除自启项 Run\WeaselFluent；
#   4. 删除安装目录 %LOCALAPPDATA%\Programs\WeaselFluent；
#   5. **保留**用户数据 %APPDATA%\Rime（加 -Purge 可一并清除）。
#
# 卸载后官方 Weasel 会自动恢复：HKCU 的影子注册被删掉，COM 解析又回到 HKLM
# 里官方那份注册。
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/uninstall.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/uninstall.ps1 -Purge

param(
    [switch]$Purge
)

$ErrorActionPreference = "Stop"

# 直接复用 install.ps1 的 -Uninstall 分支，保证两边逻辑一致、不会漂移。
$Installer = Join-Path $PSScriptRoot "install.ps1"
if (-not (Test-Path $Installer)) {
    Write-Host "找不到 install.ps1（$Installer）" -ForegroundColor Red
    exit 1
}

$args = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $Installer, "-Uninstall")
if ($Purge) { $args += "-Purge" }

& powershell @args
exit $LASTEXITCODE
