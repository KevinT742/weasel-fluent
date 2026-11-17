// WTL 的应用模块全局对象。
//
// 上游由 WeaselServer/WeaselServer.cpp 定义 `CAppModule _Module;`，
// 并由 WeaselIPCServer/WeaselServerImpl.cpp 以 `extern CAppModule _Module;` 引用
// （weasel::ServerImpl 继承自 WTL 的 CWindowImpl，需要它做消息循环宿主）。
//
// 本仓用 Qt 服务取代了 WeaselServer，因此在这里补上这个定义，
// 好让上游的 WeaselIPCServer 能正常链接。
//
// CAppModule 是 WTL 的（wtl/atlapp.h: `class CAppModule : public ATL::CComModule`），
// 不是 ATL 自带的那个 —— 所以这里要包含 wtl/atlapp.h。

#include <atlbase.h>
#include <wtl/atlapp.h>

WTL::CAppModule _Module;
