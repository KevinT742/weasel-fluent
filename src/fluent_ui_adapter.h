#ifndef FLUENT_UI_ADAPTER_H
#define FLUENT_UI_ADAPTER_H

#include <WeaselIPCData.h>

#include "rime_ui_state.h"

class CandidateWindow;

/// 把官方 Weasel 的组合态映射为本仓的候选窗状态。
///
/// 映射关系（设计文档 §4.3）：
///
///   ctx.preedit.str         → RimeUiState::preedit
///   ctx.cinfo.candies[i]    → RimeUiState::candidates[i].text
///   ctx.cinfo.comments[i]   → RimeUiState::candidates[i].comment
///   ctx.cinfo.highlighted   → RimeUiState::highlightedIndex
///   ctx.cinfo.currentPage   → RimeUiState::pageNo
///   ctx.cinfo.is_last_page  → RimeUiState::isLastPage
///   status.ascii_mode       → RimeUiState::asciiMode
///
/// weasel::Context 本身就带着 preedit 与完整候选列表，而 weasel::Status 带着
/// ascii_mode 与 schema_id，因此这个映射几乎是一一对应的，不需要经过 IPC 往返。
///
/// 上游限制（实测，RimeWithWeasel.cpp `_GetContext`）：服务端的 weasel::Context
/// **不携带组合态光标**——它只填 `preedit.str` 与「选中区」属性，光标位置仅存在于
/// TIP 响应协议（`ctx.preedit.cursor=start,end,cursor`）里。因此本适配器的
/// `cursorPos` 只能按「光标在预编辑串末尾」推导（属性里若带 cursor 则优先采用）。
class FluentUiAdapter {
public:
    explicit FluentUiAdapter(CandidateWindow* window) : m_window(window) {}

    /// 把上游 Context/Status 映射为 RimeUiState 并推给候选窗。
    void apply(const weasel::Context& ctx, const weasel::Status& status);

    /// 纯函数形式，便于单元测试。
    static RimeUiState toUiState(const weasel::Context& ctx, const weasel::Status& status);

private:
    CandidateWindow* m_window = nullptr;
};

#endif // FLUENT_UI_ADAPTER_H
