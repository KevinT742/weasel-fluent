#include "fluent_ui_adapter.h"

#include "candidate_window.h"

RimeUiState FluentUiAdapter::toUiState(const weasel::Context& ctx, const weasel::Status& status,
                                       const QRectF& inputBox) {
    RimeUiState st;
    st.isComposing = !ctx.preedit.str.empty();
    st.preedit = QString::fromStdWString(ctx.preedit.str);

    // 服务端 Context 不带组合态光标：属性里有 cursor 就用，否则按末尾处理。
    st.cursorPos = static_cast<int>(ctx.preedit.str.size());
    for (const auto& attr : ctx.preedit.attributes) {
        if (attr.range.cursor >= 0) {
            st.cursorPos = attr.range.cursor;
            break;
        }
    }

    st.highlightedIndex = ctx.cinfo.highlighted;
    st.pageNo = ctx.cinfo.currentPage;
    st.isLastPage = ctx.cinfo.is_last_page;
    st.asciiMode = status.ascii_mode;
    st.fullShape = status.full_shape;
    st.inputBoxRect = inputBox;

    // 上游的 candies/comments/labels 都是 std::vector<weasel::Text>，
    // 文本要取 Text::str 而不是直接当 wstring 用。
    const int n = static_cast<int>(ctx.cinfo.candies.size());
    st.candidates.reserve(n);
    for (int i = 0; i < n; ++i) {
        CandidateItem item;
        item.index = i;
        item.text = QString::fromStdWString(ctx.cinfo.candies[i].str);
        if (i < static_cast<int>(ctx.cinfo.comments.size())) {
            item.comment = QString::fromStdWString(ctx.cinfo.comments[i].str);
        }
        st.candidates.append(item);
    }
    return st;
}

void FluentUiAdapter::apply(const weasel::Context& ctx, const weasel::Status& status,
                            const QRectF& inputBox) {
    if (!m_window) {
        return;
    }
    m_window->updateFromWeasel(ctx, status);
    // Pass input box rect for follow behavior.
    m_window->setInputBoxRect(inputBox);
}
