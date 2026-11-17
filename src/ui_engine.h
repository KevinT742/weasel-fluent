#ifndef UI_ENGINE_H
#define UI_ENGINE_H

#include <QObject>
#include <QString>

#include "rime_ui_state.h"

/// Rime/ibus 的键码约定：小写 ASCII 就是字母本身（ibus::XK_c == 0x63），
/// 特殊键用 0xFFxx 段。UI 层与两个引擎实现共用这一套常量。
constexpr int RIME_KEY_BACKSPACE = 0xff08;
constexpr int RIME_KEY_TAB = 0xff09;
constexpr int RIME_KEY_RETURN = 0xff0d;
constexpr int RIME_KEY_ESCAPE = 0xff1b;
constexpr int RIME_KEY_SPACE = 0x0020;
constexpr int RIME_KEY_LEFT = 0xff51;
constexpr int RIME_KEY_UP = 0xff52;
constexpr int RIME_KEY_RIGHT = 0xff53;
constexpr int RIME_KEY_DOWN = 0xff54;
constexpr int RIME_KEY_PAGE_UP = 0xff55;
constexpr int RIME_KEY_PAGE_DOWN = 0xff56;

/// 候选窗 / 扩展面板 / 托盘所需的引擎能力 —— 设计文档 §4.1 的「切换点」。
///
/// Round 2 起有两个实现：
///   * `WeaselEngine` —— **服务进程用**，跑官方 Weasel 的 RimeWithWeaselHandler，
///     状态经 `FluentUiAdapter` 映射后喂给候选窗；
///   * `RimeEngine`   —— 过渡实现（设计文档 R6），目前只剩设置中心进程与
///     自检套件在用，随 T7 整体删除。
///
/// UI 层只认这个接口，因此切换引擎不需要改 UI 的调用方式。
class UiEngine : public QObject {
    Q_OBJECT
public:
    explicit UiEngine(QObject* parent = nullptr) : QObject(parent) {}
    ~UiEngine() override = default;

    /// 当前组合态。
    virtual RimeUiState uiState() const = 0;

    /// 处理一次按键（Windows 虚拟键码）。
    virtual bool processKey(int keyCode, int mask = 0) = 0;
    /// 处理一个字符。
    virtual bool processChar(QChar ch) = 0;
    /// 选中当前页的第 index 个候选。
    virtual bool selectCandidate(int index) = 0;
    /// 直接上屏一段文本（表情 / 短语）。
    virtual void commitDirectText(const QString& text) = 0;

    virtual bool isAsciiMode() const = 0;
    virtual void setAsciiMode(bool ascii) = 0;
    virtual bool isSimplified() const = 0;
    virtual void setSimplified(bool simp) = 0;
    virtual bool isFullShape() const = 0;
    virtual void setFullShape(bool full) = 0;

    /// 切换到指定方案并使其生效。
    virtual bool selectSchema(const QString& schemaId) = 0;
    /// 当前生效的方案 id。
    virtual QString currentSchema() const = 0;
    /// 重新部署（不重编译词库）。
    virtual bool deploy() = 0;

signals:
    /// 组合态发生变化。
    void stateChanged();
    /// 需要上屏的文本。
    void committed(const QString& text);
};

#endif // UI_ENGINE_H
