set_project("weasel-fluent-cpp-qt")
set_version("1.0.0")

set_languages("cxx17")
add_rules("mode.debug", "mode.release")

-- ---------------------------------------------------------------------------
-- 依赖：Boost（仅头文件）与 ATL
--
-- Boost：Boost 1.88 起 boost::thread 在 Windows 上不再产出 .lib（通用实现移到
-- libs/thread/src/pthread/），上游 Weasel 因此一个 Boost 库都不链接，只把
-- BOOST_ROOT 加进 include 路径。本仓沿用同一策略：只加头文件路径。
--
-- ATL：Weasel 的 WeaselIPCServer 用 ATL 的 CWindowImpl / DECLARE_WND_CLASS，
-- 需要 ATL 头文件与 atls.lib。
-- ---------------------------------------------------------------------------
add_includedirs("deps/boost_1_88_0")
add_linkdirs("deps/boost_1_88_0/stage/lib")

-- 定位 ATL 头文件与库（Weasel 的 WeaselIPCServer 用 ATL 的 CWindowImpl）。
--
-- xmake 3.1 的沙箱既不提供 io/pcall，也不提供 os.execv，因此用 os.files/os.isfile
-- 在常见安装布局里探测（构建期探测，不是运行期路径）。
--
-- ⚠️ Visual Studio 18 把 ATL/MFC 挪到了**工具集目录内**：
--    VC/Tools/MSVC/<ver>/atlmfc/{include,lib}
--    而不是 VS2022 的 VC/ATL/include。这也是 Boost 的 msvc.jam 反推
--    vcvarsall.bat 失败、msvc-setup 步骤崩掉的同一个根因（目录布局变了）。
-- 去掉路径最后一段（xmake 沙箱里没有 path.dirname，只能用字符串处理）。
local function strip_last_dir(p)
    return (p:match("^(.*)[/\\][^/\\]+$")) or p
end

local function find_atl()
    local roots = {}
    for _, env in ipairs({"ProgramFiles", "ProgramFiles(x86)"}) do
        local v = os.getenv(env)
        if v and v ~= "" then table.insert(roots, v) end
    end

    local function probe(include_dir, lib_dir)
        if os.isfile(path.join(include_dir, "atlbase.h")) then
            return include_dir, lib_dir
        end
        return nil
    end

    for _, r in ipairs(roots) do
        for _, ver in ipairs({"18", "2022"}) do
            for _, ed in ipairs({"Community", "Professional", "Enterprise", "BuildTools"}) do
                local vsroot = path.join(r, "Microsoft Visual Studio", ver, ed)
                -- 布局 A（VS 18+）：ATL 在工具集目录内
                local msvc_root = path.join(vsroot, "VC", "Tools", "MSVC")
                if os.isdir(msvc_root) then
                    local hits = os.files(path.join(msvc_root, "*/atlmfc/include/atlbase.h"))
                    if hits and #hits > 0 then
                        local inc = strip_last_dir(hits[1])
                        local lib = path.join(strip_last_dir(inc), "lib", "x64")
                        local ok_inc, ok_lib = probe(inc, lib)
                        if ok_inc then
                            return ok_inc, ok_lib
                        end
                    end
                end
                -- 布局 B（VS 2022 及更早）：ATL 在 VC/ATL 下
                local legacy_inc = path.join(vsroot, "VC", "ATL", "include")
                local legacy_lib = path.join(vsroot, "VC", "ATL", "lib", "x64")
                local inc, lib = probe(legacy_inc, legacy_lib)
                if inc then
                    return inc, lib
                end
            end
        end
    end
    return nil
end

local atl_include, atl_lib = find_atl()
if atl_include then
    add_includedirs(atl_include)
    add_linkdirs(atl_lib)
    print("ATL include: %s", atl_include)
    print("ATL lib dir: %s", atl_lib)
else
    print("WARNING: ATL include not found; WeaselIPCServer will not compile.")
end

-- Vendored Weasel（GPLv3）—— 详见 3rdparty/weasel/UPSTREAM.md
add_includedirs("3rdparty/weasel/include")
add_includedirs("3rdparty/weasel/include/wtl")
-- 本仓专属的编译期宏（通过 /FI 强制包含），见 build_defines/weasel_fluent_defines.h
add_includedirs("build_defines")

-- ---------------------------------------------------------------------------
-- Weasel 内核静态库
--
-- 三个模块都按 /MD 编译：本仓 GUI 链的是 Qt 官方二进制（/MD），若跟随上游的
-- /MT 会产生 LNK2038 运行库不匹配。Boost 亦为 /MD（runtime-link=shared）。
--
-- /FIguiddef.h /FIcoguid.h：Visual Studio 18 的 <Windows.h> 在
-- WIN32_LEAN_AND_MEAN 下不再顺带包含 guiddef.h，而 ATL 的 atlcomcli.h 会用到
-- GUID_NULL / IID_NULL 却不自己包含它们（它假定包含者已经带过了）。
-- guiddef.h 提供 GUID 的 typedef 与 DEFINE_GUID 宏，coguid.h 用该宏给出
-- GUID_NULL / IID_NULL / CLSID_NULL 的定义（需配合 INITGUID）。
-- 两者都是 SDK 自带头，且 guiddef.h 自带 GUID 的 typedef，提前包含是安全的。
-- 这属于构建层适配，不改动 vendor 源码。
-- ---------------------------------------------------------------------------
local weasel_defines = {"UNICODE", "_UNICODE", "WINDOWS", "MSVC", "BOOST_ALL_NO_LIB"}
local weasel_cxflags = {"/utf-8", "/GS", "/Gy", "/EHsc", "/Zc:wchar_t", "/Zc:forScope", "/Zc:inline", "/MD"}
-- /FI 必须 force=true，否则 xmake 会把它当未知参数「自动忽略」（只打印一行警告），
-- 于是 guiddef.h / coguid.h 根本没被强制包含，症状是 atlcomcli.h 里
-- 「GUID_NULL: 未声明的标识符」。
--
-- /FIweasel_fluent_defines.h：把 IPC 管道名从上游的 WeaselNamedPipe 改成
-- WeaselFluentNamedPipe。不改的话，机器上已装的官方 WeaselServer.exe 会占住
-- 同一个命名管道，本仓的服务 bind 失败（而且两端管道名必须一致，
-- 所以主程序与 TIP 都要强制包含同一个头）。
local weasel_force_include = {"/FIguiddef.h", "/FIcoguid.h", "/FIweasel_fluent_defines.h"}

target("WeaselIPC")
    set_kind("static")
    set_languages("cxx17")
    add_defines(weasel_defines)
    if is_plat("windows") then
        add_cxflags(weasel_cxflags, {tools = {"cl", "clang_cl"}})
        add_cxflags(weasel_force_include, {force = true, tools = {"cl", "clang_cl"}})
    end
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("3rdparty/weasel/include/wtl")
    add_files("3rdparty/weasel/WeaselIPC/*.cpp")
    add_syslinks("advapi32", "user32", "ole32")

target("WeaselIPCServer")
    set_kind("static")
    set_languages("cxx17")
    add_defines(weasel_defines)
    if is_plat("windows") then
        add_cxflags(weasel_cxflags, {tools = {"cl", "clang_cl"}})
        add_cxflags(weasel_force_include, {force = true, tools = {"cl", "clang_cl"}})
    end
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("3rdparty/weasel/include/wtl")
    add_files("3rdparty/weasel/WeaselIPCServer/*.cpp")
    add_syslinks("advapi32", "user32", "ole32", "atls")

target("RimeWithWeasel")
    set_kind("static")
    set_languages("cxx17")
    add_defines(weasel_defines)
    if is_plat("windows") then
        add_cxflags(weasel_cxflags, {tools = {"cl", "clang_cl"}})
        add_cxflags(weasel_force_include, {force = true, tools = {"cl", "clang_cl"}})
    end
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("3rdparty/weasel/include/wtl")
    add_includedirs("deps/rime/dist/include")
    add_files("3rdparty/weasel/RimeWithWeasel/*.cpp")
    add_syslinks("advapi32", "user32", "ole32")

-- Target: WeaselUI (static)
--
-- ⚠️ 虽然设计文档写的是「不引入 WeaselUI」，但 WeaselTSF 的 CCandidateList 持有
-- std::unique_ptr<weasel::UI>，而 weasel::UI 的实现（UIImpl / WeaselPanel / D2D）
-- 就在 WeaselUI/ 里 —— 没有它 WeaselTSF 根本编不过。
--
-- 所以这里引入 WeaselUI，但**只作为数据载体**：补丁 0001 让 TIP 不调用
-- _ui->Create()，D2D 面板窗口永不创建，候选窗由 Fluent 前端独占（§4.2 R4）。
target("WeaselUI")
    set_kind("static")
    set_languages("cxx17")
    add_defines(weasel_defines)
    add_defines("INITGUID")
    if is_plat("windows") then
        add_cxflags(weasel_cxflags, {tools = {"cl", "clang_cl"}})
        add_cxflags(weasel_force_include, {force = true, tools = {"cl", "clang_cl"}})
    end
    add_includedirs("3rdparty/weasel/include")
    add_files("3rdparty/weasel/WeaselUI/*.cpp")
    add_syslinks("user32", "shlwapi", "dwmapi", "shcore", "gdi32", "shell32",
                 "d2d1", "dwrite", "dxgi", "d3d11", "dcomp", "windowscodecs", "ole32")

-- Target: WeaselTSF (static) — 上游 TSF 实现
--
-- D2D 候选面板已由 3rdparty/weasel/patches/0001-disable-d2d-candidate-panel.patch
-- 编译掉；运行该脚本即可把补丁打上（幂等）。
target("WeaselTSF")
    set_kind("static")
    set_languages("cxx17")
    add_defines(weasel_defines)
    add_defines("WEASEL_TSF_EXPORTS", "INITGUID")
    if is_plat("windows") then
        add_cxflags(weasel_cxflags, {tools = {"cl", "clang_cl"}})
        add_cxflags(weasel_force_include, {force = true, tools = {"cl", "clang_cl"}})
    end
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("3rdparty/weasel/include/wtl")
    add_includedirs("3rdparty/weasel/WeaselTSF")
    add_deps("WeaselIPC", "WeaselUI")
    add_files("3rdparty/weasel/WeaselTSF/*.cpp")
    -- 上游的 Register.cpp 写 HKEY_CLASSES_ROOT（= HKLM），需要管理员权限；
    -- 本仓是「每用户、免提权」安装（设计文档 §6.6），故由
    -- tip/weasel_tsf_register.cpp 取代它（见该文件顶部说明）。
    remove_files("3rdparty/weasel/WeaselTSF/Register.cpp")
    add_syslinks("advapi32", "user32", "ole32", "oleaut32", "dwmapi")

-- Target: FluentQt (Static Library)
target("FluentQt")
    set_kind("static")
    add_rules("qt.static")
    add_frameworks("QtWidgets", "QtGui", "QtCore")

    add_defines("UNICODE", "_UNICODE", "_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING")
    if is_plat("windows") then
        add_cxflags("/utf-8", "/FS", "/MP", {tools = {"cl", "clang_cl"}})
    end

    add_includedirs("3rdparty/Fluent-Qt/include", {public = true})
    add_includedirs("3rdparty/Fluent-Qt/src", {public = true})

    add_files("3rdparty/Fluent-Qt/src/**.cpp")
    add_files("3rdparty/Fluent-Qt/resources.qrc")

    on_load(function (target)
        if not target:data("qt") then
            import("detect.sdks.find_qt")
            import("core.project.config")
            local sdkdir = config.get("qt")
            if not sdkdir and os.isdir("C:/Qt/6.8.2/msvc2022_64") then
                sdkdir = "C:/Qt/6.8.2/msvc2022_64"
            end
            local qt = find_qt(sdkdir)
            if qt then
                target:data_set("qt", qt)
            end
        end

        for _, file in ipairs(os.files("3rdparty/Fluent-Qt/src/**.h")) do
            local content = io.readfile(file)
            if content and (content:find("Q_OBJECT") or content:find("Q_GADGET") or content:find("Q_NAMESPACE")) then
                target:add("files", file)
            else
                target:add("headerfiles", file)
            end
        end
    end)

-- Target: weasel-fluent-cpp-qt (Executable)
target("weasel-fluent-cpp-qt")
    set_kind("binary")
    add_rules("qt.widgetapp")
    add_frameworks("QtWidgets", "QtGui", "QtCore", "QtSvg")

    add_deps("FluentQt")
    -- Weasel 内核（Round 2）：引擎 + IPC 客户端/服务端
    add_deps("WeaselIPC", "WeaselIPCServer", "RimeWithWeasel", "WeaselUI")

    add_defines("UNICODE", "_UNICODE")
    if is_plat("windows") then
        add_cxflags("/utf-8", "/FS", "/MP", {tools = {"cl", "clang_cl"}})
        add_cxflags(weasel_force_include, {force = true, tools = {"cl", "clang_cl"}})
        add_ldflags("/SUBSYSTEM:CONSOLE", {force = true})
        add_syslinks("User32", "Gdi32", "Dwmapi", "Winmm", "Psapi", "Advapi32")
        add_syslinks("atls", "ole32", "oleaut32", "d2d1", "dwrite", "dxgi", "d3d11",
                     "dcomp", "windowscodecs", "shcore", "shlwapi")
    end

    add_includedirs("src")
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("deps/rime/dist/include")
    -- librime 的导入库：上游 RimeWithWeasel 调用的是 rime_api.h 里
    -- RIME_API(dllimport) 声明的导出函数（rime_get_api 等），必须链接 rime.lib。
    -- 代价：rime.dll 成为启动期硬依赖（与上游 Weasel 相同）；after_build 会把
    -- deps/rime/dist/lib/rime.dll 拷到输出目录。
    add_linkdirs("deps/rime/dist/lib")
    add_links("rime")
    -- Boost：Weasel 的 IPC 层用到 boost::thread / boost::archive，需要链接
    -- （上游 Weasel 同样链接 Boost，只是它把 Boost 放在 BOOST_ROOT/stage/lib 下）。
    -- 库由 scripts/build_boost.ps1 + scripts/build_boost_thread.ps1 产出，
    -- 统一为简单名字以便 add_links。
    add_linkdirs("deps/boost_1_88_0/stage/lib")
    add_links("boost_thread", "boost_serialization", "boost_system", "boost_atomic")

    add_files("src/*.cpp")
    add_files("src/*.h")
    add_files("assets/emojis.qrc")

    on_load(function (target)
        if not target:data("qt") then
            import("detect.sdks.find_qt")
            import("core.project.config")
            local sdkdir = config.get("qt")
            if not sdkdir and os.isdir("C:/Qt/6.8.2/msvc2022_64") then
                sdkdir = "C:/Qt/6.8.2/msvc2022_64"
            end
            local qt = find_qt(sdkdir)
            if qt then
                target:data_set("qt", qt)
            end
        end
    end)

    after_build(function (target)
        local targetfile = target:targetfile()
        -- README documents build/Release as the canonical output location.
        local release_dir = path.join(os.projectdir(), "build", "Release")
        if os.isfile(targetfile) and not os.isdir(release_dir) then
            os.tryrm(release_dir)   -- drop a stale file left at that path
            os.mkdir(release_dir)
        end
        if os.isfile(targetfile) and os.isdir(release_dir) then
            os.cp(targetfile, path.join(release_dir, "weasel-fluent-cpp-qt.exe"))
        end
        -- Stage the librime runtime next to the executable so the local build
        -- behaves like the distribution (real engine instead of simulation).
        -- Fetched by scripts/fetch_rime.ps1 into deps/.
        local deps = path.join(os.projectdir(), "deps")
        local rime_dll = path.join(deps, "rime", "dist", "lib", "rime.dll")
        for _, stage_dir in ipairs({ target:targetdir(), release_dir }) do
            if os.isdir(stage_dir) then
                if os.isfile(rime_dll) then
                    os.cp(rime_dll, path.join(stage_dir, "rime.dll"))
                end
                -- 把 Qt 运行时 DLL 与平台插件也放到 exe 旁边。
                --
                -- 为什么必须这么做：exe 是用 Qt 6.8.2 编译的，而 DLL 搜索默认
                -- 会**先看 PATH**。本机 PATH 里就排着 STM32CubeProgrammer 的 bin，
                -- 里面有一套 Qt 6.6.1 的 Qt6Core/Qt6Gui/Qt6Widgets/Qt6Svg，
                -- 一旦从 PATH 里捡到它们，双击 exe 就会报
                -- 「无法定位程序输入点 ?setGeometry@QStackedLayout...」。
                -- 运行时另有一层保险：main() 开头用 SetDefaultDllDirectories
                -- 把搜索顺序固定为「exe 目录 + System32」，完全忽略 PATH。
                import("core.project.config")
                local qt_sdk = config.get("qt")
                if qt_sdk then
                    local qt_bin = path.join(qt_sdk, "bin")
                    for _, dll in ipairs({ "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Svg.dll" }) do
                        local src = path.join(qt_bin, dll)
                        if os.isfile(src) then
                            os.cp(src, path.join(stage_dir, dll))
                        end
                    end
                    -- 平台插件（qwindows）与样式插件。
                    -- 注意 Qt 的插件目录在 **SDK 根下**（msvc2022_64\plugins），
                    -- 不在 bin\ 里。
                    local plugins_root = path.join(stage_dir, "plugins")
                    if not os.isdir(plugins_root) then
                        os.mkdir(plugins_root)
                    end
                    for _, sub in ipairs({ "platforms", "styles" }) do
                        local src_dir = path.join(qt_sdk, "plugins", sub)
                        local dst_dir = path.join(plugins_root, sub)
                        if os.isdir(src_dir) then
                            if not os.isdir(dst_dir) then
                                os.mkdir(dst_dir)
                            end
                            for _, f in ipairs(os.files(path.join(src_dir, "*.dll"))) do
                                os.cp(f, path.join(dst_dir, path.filename(f)))
                            end
                        end
                    end
                end
                local data_dir = path.join(stage_dir, "data")
                if not os.isdir(data_dir) then
                    os.mkdir(data_dir)
                end
                for _, src in ipairs({
                    path.join(deps, "rime-prelude"),
                    path.join(deps, "rime-luna-pinyin"),
                }) do
                    if os.isdir(src) then
                        for _, f in ipairs(os.files(path.join(src, "*.yaml"))) do
                            os.cp(f, path.join(data_dir, path.filename(f)))
                        end
                    end
                end
            end
        end
        if os.isfile(targetfile) and os.isdir("dist/weasel-fluent") then
            local dst = "dist/weasel-fluent/weasel-fluent-cpp-qt.exe"
            local old = "dist/weasel-fluent/weasel-fluent-cpp-qt.exe.old"
            os.tryrm(old)
            os.trymv(dst, old)
            os.trycp(targetfile, dst)
        end
    end)

-- Target: weasel-fluent-tsf (Windows TSF COM TIP DLL)
--
-- Round 2 起：TIP 由**上游 WeaselTSF** 编译而成（本仓自研的精简 tsf/ 退役，
-- 见 Task 7）。导出的 4 个入口沿用上游的 WeaselTSF.def。
--
-- 注册逻辑不走上游的 Register.cpp（它写 HKLM、要管理员），改用本仓的
-- tip/weasel_tsf_register.cpp（HKCU 逐用户）。
target("weasel-fluent-tsf")
    set_kind("shared")
    set_languages("cxx17")
    add_defines(weasel_defines)
    add_defines("WEASEL_TSF_EXPORTS", "INITGUID")
    if is_plat("windows") then
        add_cxflags(weasel_cxflags, {tools = {"cl", "clang_cl"}})
        add_cxflags(weasel_force_include, {force = true, tools = {"cl", "clang_cl"}})
        add_syslinks("User32", "Gdi32", "Advapi32", "Ole32", "OleAut32", "Dwmapi")
        -- TSF 类别 GUID（GUID_TFCAT_*）由 SDK 的 uuid.lib 提供
        add_syslinks("uuid")
    end
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("3rdparty/weasel/include/wtl")
    add_includedirs("3rdparty/weasel/WeaselTSF")
    add_deps("WeaselIPC", "WeaselUI", "WeaselTSF")
    -- Boost：weasel::Client（WeaselIPC）用到 boost::archive / boost::thread
    add_linkdirs("deps/boost_1_88_0/stage/lib")
    add_links("boost_thread", "boost_serialization", "boost_system", "boost_atomic")
    add_files("tip/*.cpp")
    add_files("tip/weasel-fluent-tsf.def")
    -- 语言栏（输入法工具栏）的资源：菜单 + 中/英/全半角图标。
    -- 没有它，LoadMenuW / LoadImageW 全部失败 ⇒ 工具栏右键菜单是空的、图标也不出来。
    -- ⚠️ 资源必须编进 **DLL**（静态库里的 .rc 不会被链接器带上）。
    -- .rc 里 #include "afxres.h" ⇒ 需要 ATL/MFC 头文件目录。
    if is_plat("windows") then
        local atl_inc = find_atl()
        if atl_inc then
            add_includedirs(atl_inc)
        end
        add_files("tip/weasel-tsf.rc")
    end

    after_build(function (target)
        local targetfile = target:targetfile()
        local release_dir = path.join(os.projectdir(), "build", "Release")
        if os.isfile(targetfile) and not os.isdir(release_dir) then
            os.tryrm(release_dir)
            os.mkdir(release_dir)
        end
        if os.isfile(targetfile) and os.isdir(release_dir) then
            os.trycp(targetfile, path.join(release_dir, "weasel-fluent-tsf.dll"))
        end
        if os.isfile(targetfile) and os.isdir("dist/weasel-fluent") then
            local dst = "dist/weasel-fluent/weasel-fluent-tsf.dll"
            local old = "dist/weasel-fluent/weasel-fluent-tsf.dll.old"
            os.tryrm(old)
            os.trymv(dst, old)
            os.trycp(targetfile, dst)
        end
    end)

