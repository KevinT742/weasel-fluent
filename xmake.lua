set_project("weasel-fluent-cpp-qt")
set_version("1.0.0")

set_languages("cxx17")
add_rules("mode.debug", "mode.release")

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

    add_defines("UNICODE", "_UNICODE")
    if is_plat("windows") then
        add_cxflags("/utf-8", "/FS", "/MP", {tools = {"cl", "clang_cl"}})
        add_ldflags("/SUBSYSTEM:CONSOLE", {force = true})
        add_syslinks("User32", "Gdi32", "Dwmapi", "Winmm", "Psapi", "Advapi32")
    end

    add_includedirs("src")

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
target("weasel-fluent-tsf")
    set_kind("shared")
    set_languages("cxx17")
    add_defines("UNICODE", "_UNICODE", "WEASEL_TSF_EXPORTS")
    if is_plat("windows") then
        add_cxflags("/utf-8", "/FS", "/MP", {tools = {"cl", "clang_cl"}})
        add_syslinks("User32", "Gdi32", "Advapi32", "Ole32", "OleAut32", "Dwmapi")
    end
    add_files("tsf/*.cpp")
    add_files("tsf/weasel_tsf.def")

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

