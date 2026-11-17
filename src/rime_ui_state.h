#ifndef RIME_UI_STATE_H
#define RIME_UI_STATE_H

#include <QString>
#include <QVector>

/// 一个候选项。
struct CandidateItem {
    int index = 0;
    QString text;
    QString comment;
};

/// 候选窗状态。
///
/// 它既能从官方 Weasel 的
/// `weasel::Context`/`weasel::Status` 映射得到（见 `fluent_ui_adapter.h`），
/// 因此单独成头，UI 层不必知道状态是从哪来的。
struct RimeUiState {
    bool isComposing = false;
    QString preedit;
    int cursorPos = 0;
    QVector<CandidateItem> candidates;
    int highlightedIndex = 0;
    int pageNo = 0;
    bool isLastPage = false;
    QString commitText;
    /// 中/英文模式。Weasel 路径由 weasel::Status::ascii_mode 提供，
    /// 由 Weasel 桥接推送的 status.ascii_mode 驱动。
    bool asciiMode = false;
};

#endif // RIME_UI_STATE_H
