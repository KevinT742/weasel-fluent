// build_defines/weasel_fluent_defines.h
//
// 编译期强制包含（/FI）的本仓专属宏。用于在不改动 vendor 源码语义的前提下，
// 把「本仓构建」与「上游默认」区分开。
//
// 为什么需要它：上游把管道名硬编码成 `#define WEASEL_IPC_PIPE_NAME
// L"WeaselNamedPipe"`，而本仓要跑在「机器上已装官方 Weasel」的机器上 ——
// 官方的 WeaselServer.exe 会占住同一个命名管道，本仓的服务就 bind 失败。
// 0002 补丁把那行改成 `#ifndef` 守卫，这里用 /FI 在编译期覆盖它。
//
// 用 /FI 而不是 /D 命令行宏：值里有 L"..." 引号，走命令行要处理多层转义，
// 放在头文件里没有这个问题（与既有的 /FIguiddef.h 同一套路）。

#ifndef WEASEL_FLUENT_DEFINES_H
#define WEASEL_FLUENT_DEFINES_H

/// 本仓的 IPC 管道名，与官方 Weasel 隔离。
/// 完整管道路径由 WeaselIPC.h 的 GetPipeName() 组装：
///   \\.\pipe\<用户名>\<WEASEL_IPC_PIPE_NAME>
#define WEASEL_IPC_PIPE_NAME L"WeaselFluentNamedPipe"

/// 服务端单实例互斥量的名字前缀，与官方 Weasel 隔离。
/// 上游用 "(WEASEL)Furandōru-Sukāretto-<用户名>"：官方 WeaselServer.exe 在跑的时候，
/// 那个互斥量已经存在，weasel::Server::Start() 会直接返回 0（本仓的服务起不来）。
#define WEASEL_SERVER_INSTANCE_PREFIX L"(WeaselFluent)Instance-"

#endif // WEASEL_FLUENT_DEFINES_H
